#include <assert.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <time.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"
#include "xxx_proto.h"
#include "io_fd.h"
#include "host.h"
#include "param.h"
#include "auth.h"
#include "config.h"
#include "gfs_proto.h"
#include "gfs_client.h"

#define GFRCMD_URL	"gfarm:/bin/gfrcmd"

static char *gfrcmd_path = "gfrcmd"; /* i.e. search this from $PATH */
static char *gfrep_backend_client_url = "gfarm:/libexec/gfrepbe_client";

/*
 **********************************************************************
 * Really experimental rate limiter.
 * XXX This doesn't really work well.
 * XXX RTT is hardcoded.
 * XXX MTU is hardcoded.
 * XXX This doesn't work, if any packet is dropepd.
 * XXX slowstart estimation is too optimistic.
 * XXX use Web100 to see number of pending packets in SO_SNDBUF???
 **********************************************************************
 */

#define RATECTL_TICK		100		/* i.e. 1/100seconds */
#define RTT			25		/* XXX - hardcoded 250ms RTT */
#define USEC_RATECTL_TICK	(GFARM_SECOND_BY_MICROSEC / RATECTL_TICK)
#define DEFAULT_MTU		1500

struct gfs_client_rep_rate_info {
	long octets_per_tick;
	long flow_limit;
	long flow;
	struct timeval period;
	int tick;
};

struct gfs_client_rep_rate_info *
gfs_client_rep_rate_info_alloc(long rate)
{
	struct gfs_client_rep_rate_info *rinfo;

	GFARM_MALLOC(rinfo);
	if (rinfo == NULL)
		return (NULL);
	rinfo->octets_per_tick = rate / 8 / RATECTL_TICK;
	rinfo->flow_limit = DEFAULT_MTU;
	rinfo->flow = 0;
	gettimeofday(&rinfo->period, NULL);
	gfarm_timeval_add_microsec(&rinfo->period, USEC_RATECTL_TICK);
	rinfo->tick++;
	return (rinfo);
}

void
gfs_client_rep_rate_control(struct gfs_client_rep_rate_info *rinfo,
	long size)
{
	rinfo->flow += size;
	/* XXX - it's inefficient to call usleep() multiple times. */
	while (rinfo->flow >= rinfo->flow_limit) {
		struct timeval now, interval;

		gettimeofday(&now, NULL);
		if (gfarm_timeval_cmp(&now, &rinfo->period) < 0) {
			interval = rinfo->period;
			gfarm_timeval_sub(&interval, &now);
			usleep(interval.tv_sec * GFARM_SECOND_BY_MICROSEC
			    + interval.tv_usec);
#if 1
			/* make this slow while slow starting */
			rinfo->period = now;
#endif
		}

#if 0
		/*
		 * We do update rinfo->period by current time here,
		 * because our intention is to limit network flow.
		 */
		rinfo->period = now;
#endif

		gfarm_timeval_add_microsec(&rinfo->period, USEC_RATECTL_TICK);
		rinfo->flow -= rinfo->flow_limit;

		/* slow start */
		if (rinfo->flow_limit < rinfo->octets_per_tick) {
			if (++rinfo->tick >= RTT) {
				rinfo->tick = 0;
				rinfo->flow_limit <<= 1;
				if (rinfo->flow_limit > rinfo->octets_per_tick)
					rinfo->flow_limit =
					    rinfo->octets_per_tick;
			}
		}
	}
}

void
gfs_client_rep_rate_info_free(struct gfs_client_rep_rate_info *rinfo)
{
	free(rinfo);
}

/*
 **********************************************************************
 * Implementation of new replication protocol which uses gfrepbe-client/server
 **********************************************************************
 */

/*
 * common routines for gfrep and gfrepbe_client:
 *	gfrep uses this to invoke gfrepbe_client.
 *	gfrepbe_client uses this to invoke gfrepbe_server.
 * other programs/libraries shouldn't use this function.
 */

struct gfs_client_rep_backend_state {
	int pid;
	struct xxx_connection *in;
	struct xxx_connection *out;

	int n;
};

char *
gfs_client_rep_backend_invoke(char *canonical_hostname,
	char *gfrcmd, char *program, char *arg,
	int algorithm_version,
	int ndivisions, int interleave_factor,
	int file_sync_stripe, int send_stripe_sync, int recv_stripe_sync,
	char *message_prefix,
	struct xxx_connection **from_server_p,
	struct xxx_connection **to_server_p,
	struct gfs_client_rep_backend_state **statep)
{
	/*
	 * XXX FIXME
	 * This should be done by gfs_client_command() instead of
	 * calling gfrcmd, but it requires a wrapping layer which
	 * converts xxx_connection() presentation to the input
	 * for gfs_client_command_send_stdin(), and the output
	 * of gfs_client_command_recv_stdout() to xxx_connection()
	 * presentation.
	 */
	char *e;
	int i, stdin_pipe[2], stdout_pipe[2];
	char interleave[GFARM_INT32STRLEN + 1];
	char divisions[GFARM_INT32STRLEN + 1];
	char fs_stripe[GFARM_INT32STRLEN + 1];
	char version[GFARM_INT32STRLEN + 1];

	/* "gfrcmd <dst> gfrepbe_client -i <I> -n <N> -s -r -S 1 <srchost>" */
	/* "gfrcmd <src> gfrepbe_server -i <I> -n <N> -s -a <version>" */
	char *args[17], *if_hostname;
	struct sockaddr peer_addr;
	struct gfs_client_rep_backend_state *state;

	if (algorithm_version >= 0 &&
	    (arg != NULL || file_sync_stripe > 0 || recv_stripe_sync))
		return ("gfs_client_rep_backend_invoke: argument invalid");
	GFARM_MALLOC(state);
	if (state == NULL)
		return (GFARM_ERR_NO_MEMORY);

	e = gfarm_host_address_get(canonical_hostname, gfarm_spool_server_port,
	    &peer_addr, &if_hostname);
	if (e != NULL) {
		free(state);
		return (e);
	}

	i = 0;
	args[i++] = "gfrcmd";
	args[i++] = "-N";
	args[i++] = canonical_hostname;
	args[i++] = if_hostname;
	args[i++] = program;
	if (interleave_factor != 0) {
		sprintf(interleave, "%d", interleave_factor);
		args[i++] = "-i";
		args[i++] = interleave;
	}
	if (ndivisions > 1) {
		sprintf(divisions, "%d", ndivisions);
		args[i++] = "-n";
		args[i++] = divisions;
	}
	if (file_sync_stripe > 0) {
		sprintf(fs_stripe, "%d", file_sync_stripe);
		args[i++] = "-S";
		args[i++] = fs_stripe;
	}
	if (send_stripe_sync)
		args[i++] = "-s";
	if (recv_stripe_sync)
		args[i++] = "-r";
	if (algorithm_version >= 0) {
		sprintf(version, "%d", algorithm_version);
		args[i++] = "-a";
		args[i++] = version;
	}
	if (arg != NULL)
		args[i++] = arg;
	args[i++] = NULL;

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, stdin_pipe) == -1) {
		e = gfarm_errno_to_error(errno);
		fprintf(stderr, "%s: creating a pipe for stdin to %s: %s\n",
		    message_prefix, canonical_hostname, e);
		free(if_hostname);
		free(state);
		return (e);
	}
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, stdout_pipe) == -1) {
		e = gfarm_errno_to_error(errno);
		fprintf(stderr, "%s: creating a pipe for stdout to %s: %s\n",
		    message_prefix, canonical_hostname, e);
		close(stdin_pipe[0]);
		close(stdin_pipe[1]);
		free(if_hostname);
		free(state);
		return (e);
	}
	if ((state->pid = fork()) == -1) {
		e = gfarm_errno_to_error(errno);
		fprintf(stderr, "%s: fork(2) for %s %s: %s\n",
		    message_prefix, gfrcmd, canonical_hostname, e);
		close(stdin_pipe[0]);
		close(stdin_pipe[1]);
		close(stdout_pipe[0]);
		close(stdout_pipe[1]);
		free(if_hostname);
		free(state);
		return (e);
	} else if (state->pid == 0) {
		/* child */
		dup2(stdin_pipe[0], STDIN_FILENO);
		dup2(stdout_pipe[1], STDOUT_FILENO);
		close(stdin_pipe[0]);
		close(stdin_pipe[1]);
		close(stdout_pipe[0]);
		close(stdout_pipe[1]);
		/* close other streams, if any */
		for (i = 3; i < stdout_pipe[1]; i++)
			close(i);
		execvp(gfrcmd, args);
		e = gfarm_errno_to_error(errno);
		fprintf(stderr, "%s: invoking %s %s: %s\n",
		    message_prefix, gfrcmd, canonical_hostname, e);
		_exit(EXIT_FAILURE);
	}
	/* parent */
	close(stdin_pipe[0]);
	close(stdout_pipe[1]);
	free(if_hostname);

	e = xxx_socket_connection_new(stdin_pipe[1], &state->out);
	if (e != NULL) {
		fprintf(stderr, "%s: allocating memory for pipe to %s: %s\n",
		    message_prefix, canonical_hostname, e);
		close(stdin_pipe[1]);
		close(stdout_pipe[0]);
		kill(SIGTERM, state->pid);
		free(state);
		return (e);
	}
	e = xxx_socket_connection_new(stdout_pipe[0], &state->in);
	if (e != NULL) {
		fprintf(stderr, "%s: allocating memory for pipe from %s: %s\n",
		    message_prefix, canonical_hostname, e);
		xxx_connection_free(state->out);
		close(stdout_pipe[0]);
		kill(SIGTERM, state->pid);
		free(state);
		return (e);
	}
	*from_server_p = state->in;
	*to_server_p = state->out;
	*statep = state;
	return (NULL);
}

char *
gfs_client_rep_backend_wait(struct gfs_client_rep_backend_state *state)
{
	while (waitpid(state->pid, NULL, 0) == -1 && errno == EINTR)
		;
	xxx_connection_free(state->in);
	xxx_connection_free(state->out);
	free(state);
	return (NULL);
}

char *
gfs_client_rep_backend_kill(struct gfs_client_rep_backend_state *state)
{
	kill(SIGTERM, state->pid);
	xxx_connection_free(state->in);
	xxx_connection_free(state->out);
	free(state);
	return (NULL);
}

/*
 * common routine for gfrep and gfrepbe_client.
 * other programs/libraries shouldn't use this function.
 */
char *
gfs_client_rep_filelist_send(
	char *server_name, struct xxx_connection *to_server,
	char *message_prefix,
	int n, gfarm_stringlist *files, gfarm_stringlist *sections)
{
	char *e;
	int i;

	/*
	 * XXX FIXME: This should be done in parallel during replication.
	 */
	for (i = 0; i < n; i++) {
		char *gfarm_file = gfarm_stringlist_elem(files, i);
		char *section = gfarm_stringlist_elem(sections, i);

		if (section == NULL)
			section = "";
		e = xxx_proto_send(to_server, "ss", gfarm_file, section);
		if (e != NULL) {
			fprintf(stderr, "%s: "
			    "error while sending requests to %s on %s: %s\n",
			    message_prefix, server_name,
			    gfarm_host_get_self_name(), e);
			return (e);
		}
	}
	/* end of requests mark */
	e = xxx_proto_send(to_server, "ss", "", "");
	if (e != NULL) {
		fprintf(stderr, "%s: "
		    "error while sending end of requests to %s on %s: %s\n",
		    message_prefix, server_name, gfarm_host_get_self_name(),
		    e);
		return (e);
	}
	e = xxx_proto_flush(to_server);
	if (e != NULL) {
		fprintf(stderr, "%s: "
		    "error while flushing requests to %s on %s: %s\n",
		    message_prefix, server_name, gfarm_host_get_self_name(),
		    e);
		return (e);
	}
	return (NULL);
}

/*
 * common routine for gfrepbe_client and gfrepbe_server.
 * other programs/libraries shouldn't use this function.
 */
char *
gfs_client_rep_filelist_receive(struct xxx_connection *from_client,
	int *np, gfarm_stringlist *files, gfarm_stringlist *sections,
	char *message_prefix)
{
	int eof, n;
	char *e, *file, *section;

	/*
	 * XXX FIXME: This should be done in parallel during replication.
	 */
	e = gfarm_stringlist_init(files);
	if (e != NULL) {
		fprintf(stderr, "%s: allocating memory for filenames: %s\n",
		    message_prefix, e);
		return (e);
	}
	e = gfarm_stringlist_init(sections);
	if (e != NULL) {
		fprintf(stderr, "%s: allocating memory for sections: %s\n",
		    message_prefix, e);
		return (e);
	}
	n = 0;
	for (;;) {
		e = xxx_proto_recv(from_client, 0, &eof, "ss",
		    &file, &section);
		if (e != NULL) {
			fprintf(stderr, "%s: error while receiving requests: "
			    "%s\n",
			    message_prefix, e);
			return (e);
		}
		if (eof) {
			fprintf(stderr, "%s: unexpected EOF "
			    "while receiving file:section lists\n",
			    message_prefix);
			return (GFARM_ERR_PROTOCOL);
		}
		if (*file == '\0') { /* end of list */
			if (*section != '\0') {
				fprintf(stderr, "%s: "
				    "protocol error, unexpected section = <%s>"
				    " at the end of replication list\n",
				    message_prefix, section);
				return (GFARM_ERR_PROTOCOL);
			}
			free(file);
			free(section);
			break;
		}
		e = gfarm_stringlist_add(files, file);
		if (e != NULL) {
			fprintf(stderr, "%s: %s for file %s:%s\n",
			    message_prefix, e, file, section);
			return (e);
		}
		e = gfarm_stringlist_add(sections, section);
		if (e != NULL) {
			fprintf(stderr, "%s: %s for section %s:%s\n",
			    message_prefix, e, file, section);
			return (e);
		}
		n++;
	}
	*np = n;
	return (NULL);
}

char *
gfarm_file_section_replicate_multiple_request(
	gfarm_stringlist *gfarm_file_list,
	gfarm_stringlist *section_list,
	char *src_canonical, char *dst_canonical,
	struct gfs_client_rep_backend_state **statep)
{
	char *e, **gfrep_backend_client_paths;
	struct sockaddr peer_addr;
	long parallel_streams, stripe_unit_size;
	long file_sync_stripe, send_stripe_sync, recv_stripe_sync;
	struct gfs_client_rep_backend_state *state;
	struct xxx_connection *from_server, *to_server;
	int replication_method_save;
	static char *gfrcmd = NULL; /* cache */

	if (gfarm_stringlist_length(gfarm_file_list) !=
	    gfarm_stringlist_length(section_list))
		return ("number of files isn't equal to number of sections");

	/*
	 * netparam is evaluated here rather than in gfsd,
	 * so, settings in user's .gfarmrc can be reflected.
	 *
	 * XXX - but this also means that settings in frontend host
	 *	is used, rather than settings in the host which does
	 *	actual transfer.
	 */

	e = gfarm_host_address_get(src_canonical, gfarm_spool_server_port,
	    &peer_addr, NULL);
	if (e != NULL)
		return (e);

	e = gfarm_netparam_config_get_long(
	    &gfarm_netparam_parallel_streams,
	    src_canonical, (struct sockaddr *)&peer_addr,
	    &parallel_streams);
	if (e != NULL) /* shouldn't happen */
		return (e);

	e = gfarm_netparam_config_get_long(
	    &gfarm_netparam_stripe_unit_size,
	    src_canonical, (struct sockaddr *)&peer_addr,
	    &stripe_unit_size);
	if (e != NULL) /* shouldn't happen */
		return (e);

	e = gfarm_netparam_config_get_long(
	    &gfarm_netparam_file_sync_stripe,
	    src_canonical, (struct sockaddr *)&peer_addr,
	    &file_sync_stripe);
	if (e != NULL) /* shouldn't happen */
		return (e);

	e = gfarm_netparam_config_get_long(
	    &gfarm_netparam_send_stripe_sync,
	    src_canonical, (struct sockaddr *)&peer_addr,
	    &send_stripe_sync);
	if (e != NULL) /* shouldn't happen */
		return (e);

	e = gfarm_netparam_config_get_long(
	    &gfarm_netparam_recv_stripe_sync,
	    src_canonical, (struct sockaddr *)&peer_addr,
	    &recv_stripe_sync);
	if (e != NULL) /* shouldn't happen */
		return (e);

	if (gfrcmd == NULL) { /* if `gfrcmd' is not cached */
		char *self_canonical, **gfrcmd_real_paths;

		if (gfarm_host_get_canonical_self_name(&self_canonical)!= NULL)
			gfrcmd = gfrcmd_path; /* not a filesystem node */
		else {
			replication_method_save =
			    gfarm_replication_get_method();
			gfarm_replication_set_method(
			    GFARM_REPLICATION_BOOTSTRAP_METHOD);
			e = gfarm_url_program_deliver(GFRCMD_URL,
			    1, &self_canonical, &gfrcmd_real_paths);
			gfarm_replication_set_method(
			    replication_method_save);
			if (e != NULL)
				return ("cannot replicate " GFRCMD_URL
				    " to this host");
			gfrcmd = strdup(gfrcmd_real_paths[0]);
			gfarm_strings_free_deeply(1, gfrcmd_real_paths);
			if (gfrcmd == NULL)
				return (GFARM_ERR_NO_MEMORY);
		}
	}

	replication_method_save = gfarm_replication_get_method();
	gfarm_replication_set_method(GFARM_REPLICATION_BOOTSTRAP_METHOD);
	e = gfarm_url_program_deliver(gfrep_backend_client_url,
	    1, &dst_canonical, &gfrep_backend_client_paths);
	gfarm_replication_set_method(replication_method_save);
	if (e != NULL)
		return (e);

	e = gfs_client_rep_backend_invoke(dst_canonical,
	    gfrcmd, gfrep_backend_client_paths[0],
	    src_canonical, -1,
	    parallel_streams, stripe_unit_size,
	    file_sync_stripe, send_stripe_sync, recv_stripe_sync,
	    "gfarm_file_section_replicate_multiple_request()",
	    &from_server, &to_server, &state);
	gfarm_strings_free_deeply(1, gfrep_backend_client_paths);
	if (e != NULL)
		return (e);
	state->n = gfarm_stringlist_length(gfarm_file_list);

	e = gfs_client_rep_filelist_send(src_canonical, state->out,
	    "gfarm_file_section_replicate_multiple_request()",
	    state->n, gfarm_file_list, section_list);
	if (e != NULL) {
		gfs_client_rep_backend_kill(state);
		return (e);
	}
	*statep = state;
	return (NULL);
}

char *
gfarm_file_section_replicate_multiple_result(
	struct gfs_client_rep_backend_state *state,
	char **errors)
{
	char *e = NULL;
	int i, eof;
	gfarm_int32_t result;

	for (i = 0; i < state->n; i++) {
		e = xxx_proto_recv(state->in, 0, &eof, "i", &result);
		if (e != NULL || eof) {
			if (e == NULL)
				e = "unexpected EOF";
			fprintf(stderr,
			    "reply from gfrepbe_client is too short: "
			    "%d expected, %d received: %s\n",
			    state->n, i, e);
			if (i == 0)
				break;
			for (; i < state->n; i++)
				errors[i] = GFARM_ERR_PROTOCOL;
			e = NULL;
			break;
		}
		errors[i] = result == GFS_ERROR_NOERROR ? NULL :
		    gfs_proto_error_string(result);
	}
	gfs_client_rep_backend_wait(state);
	return (e);
}	

char *
gfarm_file_section_replicate_multiple(
	gfarm_stringlist *gfarm_file_list,
	gfarm_stringlist *section_list,
	char *srchost, char *dsthost,
	char **errors)
{
	char *e;
	struct gfs_client_rep_backend_state *state;

	e = gfarm_file_section_replicate_multiple_request(
	    gfarm_file_list, section_list, srchost, dsthost, &state);
	if (e != NULL)
		return (e);
	return (gfarm_file_section_replicate_multiple_result(state, errors));
}	

/* these constants shoudn't be changed without changing algorithm_version */
#define GFS_CLIENT_IOSIZE_ALIGNMENT		4096
#define GFS_CLIENT_IOSIZE_MINIMUM_DIVISION	65536

int
gfs_client_rep_limit_division(int algorithm_version, int ndivisions,
	file_offset_t file_size)
{
	if (algorithm_version != GFS_CLIENT_REP_ALGORITHM_LATEST)
		return (-1);

	/* do not divide too much */
	if (ndivisions > file_size / GFS_CLIENT_IOSIZE_MINIMUM_DIVISION) {
		ndivisions = file_size / GFS_CLIENT_IOSIZE_MINIMUM_DIVISION;
		if (ndivisions == 0)
			ndivisions = 1;
	}
	return (ndivisions);
}

struct gfs_client_rep_transfer_state {
	file_offset_t size;
	int interleave_factor;		/* 0, if it's simple_division case */
	file_offset_t full_stripe_size;	/* only used for striping case */
	file_offset_t offset;
	file_offset_t chunk_offset;

	file_offset_t transfered;
	file_offset_t chunk_residual;
};

static void
simple_division(file_offset_t file_size, int ndivisions,
	struct gfs_client_rep_transfer_state **transfers)
{
	file_offset_t offset = 0, residual = file_size;
	file_offset_t size_per_division =
	    file_offset_floor(file_size / ndivisions);
	int i;

	if (file_offset_floor(size_per_division/GFS_CLIENT_IOSIZE_ALIGNMENT) *
	    GFS_CLIENT_IOSIZE_ALIGNMENT != size_per_division) {
		size_per_division = (file_offset_floor(
		    size_per_division / GFS_CLIENT_IOSIZE_ALIGNMENT) + 1) *
		    GFS_CLIENT_IOSIZE_ALIGNMENT;
	}

	for (i = 0; i < ndivisions; i++) {
		file_offset_t size;

		/* NOTE: ``residual'' may be 0 here */
		size = residual <= size_per_division ?
		    residual : size_per_division;
		transfers[i]->size = size;
		transfers[i]->interleave_factor = 0;
		transfers[i]->full_stripe_size = 0;
		transfers[i]->offset = offset;
		transfers[i]->chunk_offset = offset;
		offset += size_per_division;
		residual -= size;

		transfers[i]->transfered = 0;
		transfers[i]->chunk_residual = size;
	}
}

static void
striping(file_offset_t file_size, int ndivisions, int interleave_factor,
	struct gfs_client_rep_transfer_state **transfers)
{
	file_offset_t full_stripe_size =
	    (file_offset_t)interleave_factor * ndivisions;
	file_offset_t stripe_number = file_offset_floor(file_size /
	    full_stripe_size);
	file_offset_t size_per_division = interleave_factor * stripe_number;
	file_offset_t residual = file_size - full_stripe_size * stripe_number;
	file_offset_t chunk_number_on_last_stripe;
	file_offset_t last_chunk_size;
	file_offset_t offset = 0;
	int i;

	if (residual == 0) {
		chunk_number_on_last_stripe = 0;
		last_chunk_size = 0;
	} else {
		chunk_number_on_last_stripe = file_offset_floor(
		    residual / interleave_factor);
		last_chunk_size = residual - 
		    interleave_factor * chunk_number_on_last_stripe;
	}

	for (i = 0; i < ndivisions; i++) {
		file_offset_t size = size_per_division;

		if (i < chunk_number_on_last_stripe)
			size += interleave_factor;
		else if (i == chunk_number_on_last_stripe)
			size += last_chunk_size;
		/* NOTE: ``size'' may be 0 here */
		transfers[i]->size = size;
		transfers[i]->interleave_factor = interleave_factor;
		transfers[i]->full_stripe_size = full_stripe_size;
		transfers[i]->offset = offset;
		transfers[i]->chunk_offset = offset;
		offset += interleave_factor;

		transfers[i]->transfered = 0;
		if (stripe_number > 0 || i < chunk_number_on_last_stripe)
			transfers[i]->chunk_residual = interleave_factor;
		else if (i == chunk_number_on_last_stripe)
			transfers[i]->chunk_residual = last_chunk_size;
		else
			transfers[i]->chunk_residual = 0;
	}
}

char *
gfs_client_rep_transfer_state_alloc(file_offset_t file_size,
	int algorithm_version,
	int ndivisions, int interleave_factor,
	struct gfs_client_rep_transfer_state ***transfersp)
{
	int i;
	struct gfs_client_rep_transfer_state **transfers;

	if (algorithm_version != GFS_CLIENT_REP_ALGORITHM_LATEST)
		return ("unknown gfrep algorithm");

	GFARM_MALLOC_ARRAY(transfers, ndivisions);
	if (transfers == NULL)
		return (GFARM_ERR_NO_MEMORY);
	for (i = 0; i < ndivisions; i++) {
		GFARM_MALLOC(transfers[i]);
		if (transfers[i] == NULL) {
			while (--i >= 0)
				free(transfers[i]);
			free(transfers);
			return (GFARM_ERR_NO_MEMORY);
		}
	}
	if (interleave_factor == 0) {
		simple_division(file_size, ndivisions, transfers);
	} else {
		striping(file_size, ndivisions, interleave_factor, transfers);
	}
	*transfersp = transfers;
	return (NULL);
}

void
gfs_client_rep_transfer_state_free(int ndivisions,
	struct gfs_client_rep_transfer_state **transfers)
{
	int i;

	for (i = 0; i < ndivisions; i++)
		free(transfers[i]);
	free(transfers);
}

int
gfs_client_rep_transfer_finished(
	struct gfs_client_rep_transfer_state *transfer)
{
	return (transfer->transfered >= transfer->size);
}

int
gfs_client_rep_transfer_stripe_finished(
	struct gfs_client_rep_transfer_state *transfer)
{
	return (transfer->chunk_residual == 0);
}

size_t
gfs_client_rep_transfer_length(
	struct gfs_client_rep_transfer_state *transfer,	size_t limit)
{
	return (transfer->chunk_residual < limit ?
	    (size_t)transfer->chunk_residual : limit);
}

file_offset_t
gfs_client_rep_transfer_offset(struct gfs_client_rep_transfer_state *transfer)
{
	return (transfer->offset);
}

int
gfs_client_rep_transfer_stripe_offset(
	struct gfs_client_rep_transfer_state *transfer)
{
	return (transfer->offset - transfer->chunk_offset);
}

void
gfs_client_rep_transfer_stripe_progress(
	struct gfs_client_rep_transfer_state *transfer, size_t length)
{
	/* assert(length <= transfer->chunk_residual); */
	transfer->offset += length;
	transfer->transfered += length;
	transfer->chunk_residual -= length;
}

void
gfs_client_rep_transfer_stripe_next(
	struct gfs_client_rep_transfer_state *transfer)
{
	if (transfer->chunk_residual == 0) { /* stripe_finished ? */
		if (transfer->transfered < transfer->size) {
			/* must be striping mode */
			/* assert(transfer->interleave_factor != 0) */
			transfer->offset +=
			    transfer->full_stripe_size -
			    transfer->interleave_factor;
			transfer->chunk_offset +=
			    transfer->full_stripe_size;
			transfer->chunk_residual =
			    transfer->size - transfer->transfered;
			if (transfer->chunk_residual >
			    transfer->interleave_factor)
				transfer->chunk_residual =
				    transfer->interleave_factor;
		}
	}
}

void
gfs_client_rep_transfer_progress(
	struct gfs_client_rep_transfer_state *transfer, size_t length)
{
	gfs_client_rep_transfer_stripe_progress(transfer, length);
	gfs_client_rep_transfer_stripe_next(transfer);
}
