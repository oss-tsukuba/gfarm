/*
 * $Id$
 */

#include <gfarm/gfarm_config.h>

#include <pthread.h>

#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h> /* TCP_NODELAY */
#include <netdb.h> /* getprotobyname() */
#include <sys/resource.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <setjmp.h>
#include <syslog.h>
#include <stdarg.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#include <pwd.h>
#include <grp.h>

#include <gfarm/error.h>
#include <gfarm/gflog.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

#include "liberror.h"
#include "gfutil.h"
#include "gfp_xdr.h"
#include "io_fd.h"
#include "hostspec.h"
#include "sockopt.h"
#include "auth.h"
#include "config.h"
#include "gfm_proto.h"
#include "gfj_client.h"
#include "gfpath.h"

#include "../gfsl/gfarm_auth.h"

#include "thrpool.h"
#include "subr.h"
#include "db_access.h"
#include "host.h"
#include "user.h"
#include "group.h"
#include "peer.h"
#include "inode.h"
#include "process.h"
#include "fs.h"
#include "job.h"
#include "back_channel.h"
#include "xattr.h"

#include "protocol_state.h"

#ifdef SOMAXCONN
#define LISTEN_BACKLOG	SOMAXCONN
#else
#define LISTEN_BACKLOG	5
#endif

#ifndef GFMD_CONFIG
#define GFMD_CONFIG		"/etc/gfmd.conf"
#endif

/* limit maximum connections, when system limit is very high */
#ifndef GFMD_CONNECTION_LIMIT
#define GFMD_CONNECTION_LIMIT	65536
#endif

char *program_name = "gfmd";
static struct protoent *tcp_proto;
static char *pid_file;

gfarm_error_t
protocol_switch(struct peer *peer, int from_client, int skip, int level,
	gfarm_int32_t *requestp, gfarm_error_t *on_errorp)
{
	gfarm_error_t e, e2;
	int eof;
	gfarm_int32_t request;

	e = gfp_xdr_recv(peer_get_conn(peer), 0, &eof, "i", &request);
	if (eof) {
		/* actually, this is not an error, but completion on eof */
		peer_record_protocol_error(peer);
		return (GFARM_ERR_NO_ERROR);
	}
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_warning("receiving request number: %s",
		    gfarm_error_string(e));
		peer_record_protocol_error(peer);
		return (e); /* finish on error */
	}
	switch (request) {
	case GFM_PROTO_HOST_INFO_GET_ALL:
		e = gfm_server_host_info_get_all(peer, from_client, skip);
		break;
	case GFM_PROTO_HOST_INFO_GET_BY_ARCHITECTURE:
		e = gfm_server_host_info_get_by_architecture(peer,
		    from_client, skip);
		break;
	case GFM_PROTO_HOST_INFO_GET_BY_NAMES:
		e = gfm_server_host_info_get_by_names(peer, from_client, skip);
		break;
	case GFM_PROTO_HOST_INFO_GET_BY_NAMEALIASES:
		e = gfm_server_host_info_get_by_namealiases(peer,
		    from_client, skip);
		break;
	case GFM_PROTO_HOST_INFO_SET:
		e = gfm_server_host_info_set(peer, from_client, skip);
		break;
	case GFM_PROTO_HOST_INFO_MODIFY:
		e = gfm_server_host_info_modify(peer, from_client, skip);
		break;
	case GFM_PROTO_HOST_INFO_REMOVE:
		e = gfm_server_host_info_remove(peer, from_client, skip);
		break;
	case GFM_PROTO_USER_INFO_GET_ALL:
		e = gfm_server_user_info_get_all(peer, from_client, skip);
		break;
	case GFM_PROTO_USER_INFO_GET_BY_NAMES:
		e = gfm_server_user_info_get_by_names(peer, from_client, skip);
		break;
	case GFM_PROTO_USER_INFO_SET:
		e = gfm_server_user_info_set(peer, from_client, skip);
		break;
	case GFM_PROTO_USER_INFO_MODIFY:
		e = gfm_server_user_info_modify(peer, from_client, skip);
		break;
	case GFM_PROTO_USER_INFO_REMOVE:
		e = gfm_server_user_info_remove(peer, from_client, skip);
		break;
	case GFM_PROTO_USER_INFO_GET_BY_GSI_DN:
		e = gfm_server_user_info_get_by_gsi_dn(
			peer, from_client, skip);
		break;
	case GFM_PROTO_GROUP_INFO_GET_ALL:
		e = gfm_server_group_info_get_all(peer, from_client, skip);
		break;
	case GFM_PROTO_GROUP_INFO_GET_BY_NAMES:
		e = gfm_server_group_info_get_by_names(peer,
		    from_client, skip);
		break;
	case GFM_PROTO_GROUP_INFO_SET:
		e = gfm_server_group_info_set(peer, from_client, skip);
		break;
	case GFM_PROTO_GROUP_INFO_MODIFY:
		e = gfm_server_group_info_modify(peer, from_client, skip);
		break;
	case GFM_PROTO_GROUP_INFO_REMOVE:
		e = gfm_server_group_info_remove(peer, from_client, skip);
		break;
	case GFM_PROTO_GROUP_INFO_ADD_USERS:
		e = gfm_server_group_info_add_users(peer, from_client, skip);
		break;
	case GFM_PROTO_GROUP_INFO_REMOVE_USERS:
		e = gfm_server_group_info_remove_users(peer,
		    from_client, skip);
		break;
	case GFM_PROTO_GROUP_NAMES_GET_BY_USERS:
		e = gfm_server_group_names_get_by_users(peer,
		    from_client, skip);
		break;
	case GFM_PROTO_COMPOUND_BEGIN:
		e = gfm_server_compound_begin(peer, from_client, skip, level);
		break;
	case GFM_PROTO_COMPOUND_END:
		e = gfm_server_compound_end(peer, from_client, skip, level);
		break;
	case GFM_PROTO_COMPOUND_ON_ERROR:
		e = gfm_server_compound_on_error(peer, from_client, skip,
		    level, on_errorp);
		break;
	case GFM_PROTO_GET_FD:
		e = gfm_server_get_fd(peer, from_client, skip);
		break;
	case GFM_PROTO_PUT_FD:
		e = gfm_server_put_fd(peer, from_client, skip);
		break;
	case GFM_PROTO_SAVE_FD:
		e = gfm_server_save_fd(peer, from_client, skip);
		break;
	case GFM_PROTO_RESTORE_FD:
		e = gfm_server_restore_fd(peer, from_client, skip);
		break;
	case GFM_PROTO_CREATE:
		e = gfm_server_create(peer, from_client, skip);
		break;
	case GFM_PROTO_OPEN:
		e = gfm_server_open(peer, from_client, skip);
		break;
	case GFM_PROTO_OPEN_ROOT:
		e = gfm_server_open_root(peer, from_client, skip);
		break;
	case GFM_PROTO_OPEN_PARENT:
		e = gfm_server_open_parent(peer, from_client, skip);
		break;
	case GFM_PROTO_CLOSE:
		e = gfm_server_close(peer, from_client, skip);
		break;
	case GFM_PROTO_VERIFY_TYPE:
		e = gfm_server_verify_type(peer, from_client, skip);
		break;
	case GFM_PROTO_VERIFY_TYPE_NOT:
		e = gfm_server_verify_type_not(peer, from_client, skip);
		break;
	case GFM_PROTO_BEQUEATH_FD:
		e = gfm_server_bequeath_fd(peer, from_client, skip);
		break;
	case GFM_PROTO_INHERIT_FD:
		e = gfm_server_inherit_fd(peer, from_client, skip);
		break;
	case GFM_PROTO_FSTAT:
		e = gfm_server_fstat(peer, from_client, skip);
		break;
	case GFM_PROTO_FUTIMES:
		e = gfm_server_futimes(peer, from_client, skip);
		break;
	case GFM_PROTO_FCHMOD:
		e = gfm_server_fchmod(peer, from_client, skip);
		break;
	case GFM_PROTO_FCHOWN:
		e = gfm_server_fchown(peer, from_client, skip);
		break;
	case GFM_PROTO_CKSUM_GET:
		e = gfm_server_cksum_get(peer, from_client, skip);
		break;
	case GFM_PROTO_CKSUM_SET:
		e = gfm_server_cksum_set(peer, from_client, skip);
		break;
	case GFM_PROTO_SCHEDULE_FILE:
		e = gfm_server_schedule_file(peer, from_client, skip);
		break;
	case GFM_PROTO_SCHEDULE_FILE_WITH_PROGRAM:
		e = gfm_server_schedule_file_with_program(peer,
		    from_client, skip);
		break;
	case GFM_PROTO_REMOVE:
		e = gfm_server_remove(peer, from_client, skip);
		break;
	case GFM_PROTO_RENAME:
		e = gfm_server_rename(peer, from_client, skip);
		break;
	case GFM_PROTO_FLINK:
		e = gfm_server_flink(peer, from_client, skip);
		break;
	case GFM_PROTO_MKDIR:
		e = gfm_server_mkdir(peer, from_client, skip);
		break;
	case GFM_PROTO_SYMLINK:
		e = gfm_server_symlink(peer, from_client, skip);
		break;
	case GFM_PROTO_READLINK:
		e = gfm_server_readlink(peer, from_client, skip);
		break;
	case GFM_PROTO_GETDIRPATH:
		e = gfm_server_getdirpath(peer, from_client, skip);
		break;
	case GFM_PROTO_GETDIRENTS:
		e = gfm_server_getdirents(peer, from_client, skip);
		break;
	case GFM_PROTO_SEEK:
		e = gfm_server_seek(peer, from_client, skip);
		break;
	case GFM_PROTO_GETDIRENTSPLUS:
		e = gfm_server_getdirentsplus(peer, from_client, skip);
		break;
	case GFM_PROTO_REOPEN:
		e = gfm_server_reopen(peer, from_client, skip);
		break;
	case GFM_PROTO_CLOSE_READ:
		e = gfm_server_close_read(peer, from_client, skip);
		break;
	case GFM_PROTO_CLOSE_WRITE:
		e = gfm_server_close_write(peer, from_client, skip);
		break;
	case GFM_PROTO_LOCK:
		e = gfm_server_lock(peer, from_client, skip);
		break;
	case GFM_PROTO_TRYLOCK:
		e = gfm_server_trylock(peer, from_client, skip);
		break;
	case GFM_PROTO_UNLOCK:
		e = gfm_server_unlock(peer, from_client, skip);
		break;
	case GFM_PROTO_LOCK_INFO:
		e = gfm_server_lock_info(peer, from_client, skip);
		break;
	case GFM_PROTO_SWITCH_BACK_CHANNEL:
		e = gfm_server_switch_back_channel(peer, from_client, skip);
		/* should not call gfp_xdr_flush() due to race */
		*requestp = request;
		return (e);
	case GFM_PROTO_GLOB:
		e = gfm_server_glob(peer, from_client, skip);
		break;
	case GFM_PROTO_SCHEDULE:
		e = gfm_server_schedule(peer, from_client, skip);
		break;
	case GFM_PROTO_PIO_OPEN:
		e = gfm_server_pio_open(peer, from_client, skip);
		break;
	case GFM_PROTO_PIO_SET_PATHS:
		e = gfm_server_pio_set_paths(peer, from_client, skip);
		break;
	case GFM_PROTO_PIO_CLOSE:
		e = gfm_server_pio_close(peer, from_client, skip);
		break;
	case GFM_PROTO_PIO_VISIT:
		e = gfm_server_pio_visit(peer, from_client, skip);
		break;
	case GFM_PROTO_HOSTNAME_SET:
		e = gfm_server_hostname_set(peer, from_client, skip);
		break;
	case GFM_PROTO_SCHEDULE_HOST_DOMAIN:
		e = gfm_server_schedule_host_domain(peer, from_client, skip);
		break;
	case GFM_PROTO_STATFS:
		e = gfm_server_statfs(peer, from_client, skip);
		break;
	case GFM_PROTO_REPLICA_LIST_BY_NAME:
		e = gfm_server_replica_list_by_name(peer, from_client, skip);
		break;
	case GFM_PROTO_REPLICA_LIST_BY_HOST:
		e = gfm_server_replica_list_by_host(peer, from_client, skip);
		break;
	case GFM_PROTO_REPLICA_REMOVE_BY_HOST:
		e = gfm_server_replica_remove_by_host(peer, from_client, skip);
		break;
	case GFM_PROTO_REPLICA_REMOVE_BY_FILE:
		e = gfm_server_replica_remove_by_file(peer, from_client, skip);
		break;
	case GFM_PROTO_REPLICA_ADDING:
		e = gfm_server_replica_adding(peer, from_client, skip);
		break;
	case GFM_PROTO_REPLICA_ADDED: /* obsolete protocol */
		e = gfm_server_replica_added(peer, from_client, skip);
		break;
	case GFM_PROTO_REPLICA_ADDED2:
		e = gfm_server_replica_added2(peer, from_client, skip);
		break;
	case GFM_PROTO_REPLICA_REMOVE:
		e = gfm_server_replica_remove(peer, from_client, skip);
		break;
	case GFM_PROTO_REPLICA_ADD:
		e = gfm_server_replica_add(peer, from_client, skip);
		break;
	case GFM_PROTO_PROCESS_ALLOC:
		e = gfm_server_process_alloc(peer, from_client, skip);
		break;
	case GFM_PROTO_PROCESS_ALLOC_CHILD:
		e = gfm_server_process_alloc_child(peer, from_client, skip);
		break;
	case GFM_PROTO_PROCESS_FREE:
		e = gfm_server_process_free(peer, from_client, skip);
		break;
	case GFM_PROTO_PROCESS_SET:
		e = gfm_server_process_set(peer, from_client, skip);
		break;
	case GFJ_PROTO_LOCK_REGISTER:
		e = gfj_server_lock_register(peer, from_client, skip); break;
	case GFJ_PROTO_UNLOCK_REGISTER:
		e = gfj_server_unlock_register(peer, from_client, skip); break;
	case GFJ_PROTO_REGISTER:
		e = gfj_server_register(peer, from_client, skip);
		break;
	case GFJ_PROTO_UNREGISTER:
		e = gfj_server_unregister(peer, from_client, skip);
		break;
	case GFJ_PROTO_REGISTER_NODE:
		e = gfj_server_register_node(peer, from_client, skip); break;
	case GFJ_PROTO_LIST:
		e = gfj_server_list(peer, from_client, skip); break;
	case GFJ_PROTO_INFO:
		e = gfj_server_info(peer, from_client, skip); break;
	case GFJ_PROTO_HOSTINFO:
		e = gfj_server_hostinfo(peer, from_client, skip); break;
	case GFM_PROTO_XATTR_SET:
		e = gfm_server_setxattr(peer, from_client, skip, 0);
		break;
	case GFM_PROTO_XMLATTR_SET:
		e = gfm_server_setxattr(peer, from_client, skip, 1);
		break;
	case GFM_PROTO_XATTR_GET:
		e = gfm_server_getxattr(peer, from_client, skip, 0);
		break;
	case GFM_PROTO_XMLATTR_GET:
		e = gfm_server_getxattr(peer, from_client, skip, 1);
		break;
	case GFM_PROTO_XATTR_REMOVE:
		e = gfm_server_removexattr(peer, from_client, skip, 0);
		break;
	case GFM_PROTO_XMLATTR_REMOVE:
		e = gfm_server_removexattr(peer, from_client, skip, 1);
		break;
	case GFM_PROTO_XATTR_LIST:
		e = gfm_server_listxattr(peer, from_client, skip, 0);
		break;
	case GFM_PROTO_XMLATTR_LIST:
		e = gfm_server_listxattr(peer, from_client, skip, 1);
		break;
	case GFM_PROTO_XMLATTR_FIND:
		e = gfm_server_findxmlattr(peer, from_client, skip);
		break;
	default:
		gflog_warning("unknown request: %d", request);
		e = GFARM_ERR_PROTOCOL;
	}
	if ((level == 0 && request != GFM_PROTO_COMPOUND_BEGIN)
	    || request == GFM_PROTO_COMPOUND_END) {
		/* flush only when a COMPOUND loop is done */
		if (debug_mode)
			gflog_debug("gfp_xdr_flush");
		e2 = gfp_xdr_flush(peer_get_conn(peer));
		if (e2 != GFARM_ERR_NO_ERROR)
			gflog_warning("protocol flush: %s",
			    gfarm_error_string(e2));
		if (e == GFARM_ERR_NO_ERROR)
			e = e2;
	}

	*requestp = request;
	/* continue unless protocol error happens */
	return (e);
}

void
compound_state_init(struct compound_state *cs)
{
	cs->current_part = GFARM_ERR_NO_ERROR;
	cs->cause = GFARM_ERR_NO_ERROR;
	cs->skip = 0;
}

void
protocol_state_init(struct protocol_state *ps)
{
	ps->nesting_level = 0;
}

int
protocol_service(struct peer *peer)
{
	struct protocol_state *ps = peer_get_protocol_state(peer);
	struct compound_state *cs = &ps->cs;
	gfarm_error_t e, dummy;
	gfarm_int32_t request;
	int from_client;
	int transaction = 0;
	const char msg[] = "protocol_service";

	from_client = peer_get_auth_id_type(peer) == GFARM_AUTH_ID_TYPE_USER;
	if (ps->nesting_level == 0) { /* top level */
		e = protocol_switch(peer, from_client, 0, 0,
		    &request, &dummy);
		giant_lock();
		peer_fdpair_clear(peer);
		if (peer_had_protocol_error(peer)) {
			if (db_begin(msg) == GFARM_ERR_NO_ERROR)
				transaction = 1;
			/*
			 * the following internally calls inode_close*() and
			 * closing must be done regardless of the result of
			 * db_begin().  because not closing may cause
			 * descriptor leak.
			 */
			peer_free(peer);
			if (transaction)
				db_end(msg);
			giant_unlock();
			return (1); /* finish */
		}
		giant_unlock();
	} else { /* inside of a COMPOUND block */
		e = protocol_switch(peer, from_client, cs->skip, 1,
		    &request, &cs->current_part);
		if (peer_had_protocol_error(peer)) {
			giant_lock();
			peer_fdpair_clear(peer);
			if (db_begin(msg) == GFARM_ERR_NO_ERROR)
				transaction = 1;
			/*
			 * the following internally calls inode_close*() and
			 * closing must be done regardless of the result of
			 * db_begin().  because not closing may cause
			 * descriptor leak.
			 */
			peer_free(peer);
			if (transaction)
				db_end(msg);
			giant_unlock();
			return (1); /* finish */
		}
		if (e != GFARM_ERR_NO_ERROR) {
			/*
			 * set cs->cause, if it's first error at a main part
			 * of a COMPOUND block
			 */
			if (cs->cause == GFARM_ERR_NO_ERROR && !cs->skip)
				cs->cause = e;
			cs->skip = 1;
		} else if (request == GFM_PROTO_COMPOUND_END) {
			giant_lock();
			peer_fdpair_clear(peer);
			giant_unlock();
			ps->nesting_level--;
		} else if (request == GFM_PROTO_COMPOUND_ON_ERROR) {
			cs->skip = cs->current_part != cs->cause;
		}
	}
	if (request == GFM_PROTO_SWITCH_BACK_CHANNEL) {
		if (e != GFARM_ERR_NO_ERROR) {
			giant_lock();
			if (db_begin(msg) == GFARM_ERR_NO_ERROR)
				transaction = 1;
			/*
			 * the following internally calls inode_close*() and
			 * closing must be done regardless of the result of
			 * db_begin().  because not closing may cause
			 * descriptor leak.
			 */
			peer_free(peer);
			if (transaction)
				db_end(msg);
			giant_unlock();
		}
		return (1); /* finish */
	}
	if (e == GFARM_ERR_NO_ERROR && request == GFM_PROTO_COMPOUND_BEGIN) {
		ps->nesting_level++;
		compound_state_init(&ps->cs);
	}

	return (0); /* still doing */
}

void *
protocol_main(void *arg)
{
	struct peer *peer = arg;

	do {
		if (protocol_service(peer))
			return (NULL); /* end of gfmd protocol session */
	} while (gfp_xdr_recv_is_ready(peer_get_conn(peer)));

	/*
	 * NOTE:
	 * We should use do...while loop for the above gfp_xdr_recv_is_ready()
	 * case, instead of thrpool_add_job() like peer_authorized().
	 * Because this thread is executed under thrpool_worker(),
	 * and such thread should not use thrpool_add_job().
	 * Think about the following scenario:
	 * (1) massive number of new connections filled thrpool.jobq.
	 * (2) at the same time, all threads under thrpool_worker()
	 *   were protocol_main().
	 * (3) The gfp_xdr_recv_is_ready() condition became true with
	 *   all the protocol_main() threads.
	 * With this scenario, if protocol_main() tried thrpool_add_job(), then
	 * they would wait forever, because thrpool.jobq were already filled,
	 * and there was no chance that the jobq became available.
	 */

	peer_watch_access(peer);

	/* this return value won't be used, because this thread is detached */
	return (NULL);
}

/* only called in case of gfarm_auth_id_type == GFARM_AUTH_ID_TYPE_USER */
gfarm_error_t
auth_uid_to_global_username(void *closure,
	enum gfarm_auth_method auth_method,
	const char *auth_user_id,
	char **global_usernamep)
{
	char *global_username;
	struct user *u;

	giant_lock();
	if (GFARM_IS_AUTH_GSI(auth_method)) { /* auth_user_id is a DN */
		u = user_lookup_gsi_dn(auth_user_id);
	} else { /* auth_user_id is a gfarm global user name */
		u = user_lookup(auth_user_id);
	}
	giant_unlock();

	if (u == NULL || user_is_invalidated(u)) {
		/*
		 * do not return GFARM_ERR_NO_SUCH_USER
		 * to prevent information leak
		 */
		return (GFARM_ERR_AUTHENTICATION);
	}
	if (global_usernamep == NULL)
		return (GFARM_ERR_NO_ERROR);
	global_username = strdup(user_name(u));
	if (global_username == NULL)
		return (GFARM_ERR_NO_MEMORY);
	*global_usernamep = global_username;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
peer_authorize(struct peer *peer)
{
	gfarm_error_t e;
	int rv, saved_errno;
	enum gfarm_auth_id_type id_type;
	char *username = NULL, *hostname;
	enum gfarm_auth_method auth_method;
	struct sockaddr addr;
	socklen_t addrlen = sizeof(addr);
	char addr_string[GFARM_SOCKADDR_STRLEN];

	/* without TCP_NODELAY, gfmd is too slow at least on NetBSD-3.0 */
	rv = 1;
	setsockopt(gfp_xdr_fd(peer_get_conn(peer)), tcp_proto->p_proto,
	    TCP_NODELAY, &rv, sizeof(rv));

	rv = getpeername(gfp_xdr_fd(peer_get_conn(peer)), &addr, &addrlen);
	if (rv == -1) {
		saved_errno = errno;
		gflog_error("authorize: getpeername: %s", strerror(errno));
		return (gfarm_errno_to_error(saved_errno));
	}
	e = gfarm_sockaddr_to_name(&addr, &hostname);
	if (e != GFARM_ERR_NO_ERROR) {
		gfarm_sockaddr_to_string(&addr,
		    addr_string, GFARM_SOCKADDR_STRLEN);
		gflog_warning("%s: %s", gfarm_error_string(e), addr_string);
		hostname = strdup(addr_string);
		if (hostname == NULL) {
			gflog_warning("%s: %s", addr_string,
			    gfarm_error_string(GFARM_ERR_NO_MEMORY));
			return (GFARM_ERR_NO_MEMORY);
		}
	}
	e = gfarm_authorize(peer_get_conn(peer), 0, GFM_SERVICE_TAG,
	    hostname, &addr, auth_uid_to_global_username, NULL,
	    &id_type, &username, &auth_method);
	if (e == GFARM_ERR_NO_ERROR) {
		protocol_state_init(peer_get_protocol_state(peer));

		giant_lock();
		peer_authorized(peer,
		    id_type, username, hostname, &addr, auth_method);
		giant_unlock();
	} else {
		gflog_warning("authorize: %s", gfarm_error_string(e));
	}
	return (e);
}

void *
try_auth(void *arg)
{
	struct peer *peer = arg;
	gfarm_error_t e;

	if ((e = peer_authorize(peer)) != GFARM_ERR_NO_ERROR) {
		gflog_warning("peer_authorize: %s", gfarm_error_string(e));
		giant_lock();
		/* db_begin()/db_end() is not necessary in this case */
		peer_free(peer);
		giant_unlock();
	}

	/* this return value won't be used, because this thread is detached */
	return (NULL);
}

void
accepting_loop(int accepting_socket)
{
	gfarm_error_t e;
	int client_socket;
	struct sockaddr_in client_addr;
	socklen_t client_addr_size;
	struct peer *peer;

	for (;;) {
		client_addr_size = sizeof(client_addr);
		client_socket = accept(accepting_socket,
		   (struct sockaddr *)&client_addr, &client_addr_size);
		if (client_socket < 0) {
			if (errno != EINTR)
				gflog_warning_errno("accept");
		} else if ((e = peer_alloc(client_socket, &peer)) !=
		    GFARM_ERR_NO_ERROR) {
			gflog_warning("peer_alloc: %s", gfarm_error_string(e));
			close(client_socket);
		} else {
			thrpool_add_job(try_auth, peer);
		}
	}
}

int
open_accepting_socket(int port)
{
	gfarm_error_t e;
	struct sockaddr_in self_addr;
	socklen_t self_addr_size;
	int sock, sockopt;

	memset(&self_addr, 0, sizeof(self_addr));
	self_addr.sin_family = AF_INET;
	self_addr.sin_addr.s_addr = INADDR_ANY;
	self_addr.sin_port = htons(port);
	self_addr_size = sizeof(self_addr);
	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock < 0)
		gflog_fatal_errno("accepting socket");
	sockopt = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
	    &sockopt, sizeof(sockopt)) == -1)
		gflog_warning_errno("SO_REUSEADDR");
	if (bind(sock, (struct sockaddr *)&self_addr, self_addr_size) < 0)
		gflog_fatal_errno("bind accepting socket");
	e = gfarm_sockopt_apply_listener(sock);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_warning("setsockopt: %s", gfarm_error_string(e));
	if (listen(sock, LISTEN_BACKLOG) < 0)
		gflog_fatal_errno("listen");
	return (sock);
}

static void
write_pid()
{
	FILE *pid_fp;

	if (pid_file == NULL)
		return;

	pid_fp = fopen(pid_file, "w");
	if (pid_fp == NULL)
		gflog_fatal_errno(pid_file);

	fprintf(pid_fp, "%ld\n", (long)getpid());
	fclose(pid_fp);
}

void
sigs_set(sigset_t *sigs)
{
	sigemptyset(sigs);
	sigaddset(sigs, SIGHUP);
	sigaddset(sigs, SIGINT);
	sigaddset(sigs, SIGTERM);
#ifdef SIGINFO
	sigaddset(sigs, SIGINFO);
#endif
	sigaddset(sigs, SIGUSR2);
}

void *
sigs_handler(void *p)
{
	sigset_t *sigs = p;
	int sig;
	int transaction = 0;
	const char msg[] = "sigs_handler";

#ifdef __linux__
	/* A Linux Thread is a process having its own process id. */
	write_pid(pid_file);
#endif
	sigs_set(sigs);

	for (;;) {
		if (sigwait(sigs, &sig) == -1)
			gflog_warning("sigs_handler: %s", strerror(errno));
#ifdef __linux__
		/*
		 * On linux-2.6.11 on Fedora Core 4,
		 * spurious signal sig=8195840 arrives.
		 * On debian-etch 4.0, signal 0 arrives.
		 */
		if (sig == 0 ||
		    (sig >= 16
#ifdef SIGINFO
		     && sig != SIGINFO
#endif
		     && sig != SIGUSR2)) {
			gflog_info("spurious signal %d received: ignoring...",
			    sig);
			continue;
		}
#endif
		switch (sig) {
		case SIGHUP: /* reload the grid-mapfile */
#ifdef HAVE_GSI
			giant_lock();
			gfarmAuthFinalize();
			(void)gfarmAuthInitialize(GRID_MAPFILE);
			giant_unlock();
#endif
			continue;

#ifdef SIGINFO
		case SIGINFO:
			/*FALLTHRU*/
#endif
		case SIGUSR2:
			thrpool_info();
			continue;

		default:
			break;
		}
		break;
	}

	gflog_info("signal %d received: terminating...", sig);

	/* we never release the giant lock until exit */
	/* so, it's safe to modify the state of all peers */
	giant_lock();

	gflog_info("dumping dead file copies");
	host_remove_replica_dump_all();

	gflog_info("shutting down peers");
	if (db_begin(msg) == GFARM_ERR_NO_ERROR)
		transaction = 1;
	/*
	 * the following internally calls inode_close*() and
	 * closing must be done regardless of the result of db_begin().
	 * because not closing may cause descriptor leak.
	 */
	peer_shutdown_all();
	if (transaction)
		db_end(msg);

	/* save all pending transactions */
	/* db_terminate() needs giant_lock(), see comment in dbq_enter() */
	db_terminate();

	gflog_info("bye");
	exit(0);

	/*NOTREACHED*/
	return (0); /* to shut up warning */
}

void
usage(void)
{
	fprintf(stderr, "Usage: %s [option]\n", program_name);
	fprintf(stderr, "option:\n");
	fprintf(stderr, "\t-L <syslog-priority-level>\n");
	fprintf(stderr, "\t-P <pid-file>\n");
	fprintf(stderr, "\t-d\t\t\t\t... debug mode\n");
	fprintf(stderr, "\t-f <gfarm-configuration-file>\n");
	fprintf(stderr, "\t-p <port>\n");
	fprintf(stderr, "\t-s <syslog-facility>\n");
	fprintf(stderr, "\t-v\t\t\t\t... make authentication log verbose\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	extern char *optarg;
	extern int optind;
	gfarm_error_t e;
	char *config_file = NULL, *port_number = NULL;
	int syslog_level = -1;
	int syslog_facility = GFARM_DEFAULT_FACILITY;
	int ch, sock, table_size;
	sigset_t sigs;

	if (argc >= 1)
		program_name = basename(argv[0]);
	gflog_set_identifier(program_name);

	while ((ch = getopt(argc, argv, "L:P:df:p:s:v")) != -1) {
		switch (ch) {
		case 'L':
			syslog_level = gflog_syslog_name_to_priority(optarg);
			if (syslog_level == -1)
				gflog_fatal("-L %s: invalid syslog priority",
				    optarg);
			break;
		case 'P':
			pid_file = optarg;
			break;
		case 'd':
			debug_mode = 1;
			if (syslog_level == -1)
				syslog_level = LOG_DEBUG;
			break;
		case 'f':
			config_file = optarg;
			break;
		case 'p':
			port_number = optarg;
			break;
		case 's':
			syslog_facility =
			    gflog_syslog_name_to_facility(optarg);
			if (syslog_facility == -1)
				gflog_fatal("%s: unknown syslog facility",
				    optarg);
			break;
		case 'v':
			gflog_auth_set_verbose(1);
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	tcp_proto = getprotobyname("tcp");
	if (tcp_proto == NULL)
		gflog_fatal("getprotobyname(\"tcp\") failed");

	if (config_file != NULL)
		gfarm_config_set_filename(config_file);
	else
		gfarm_config_set_filename(GFMD_CONFIG);
	e = gfarm_server_initialize();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfarm_server_initialize: %s\n",
		    gfarm_error_string(e));
		exit(1);
	}
	if (syslog_level != -1)
		gflog_set_priority_level(syslog_level);
	if (port_number != NULL)
		gfarm_metadb_server_port = strtol(port_number, NULL, 0);
	sock = open_accepting_socket(gfarm_metadb_server_port);

	/*
	 * We do this before calling gfarm_daemon()
	 * to print the error message to stderr.
	 */
	write_pid(pid_file);

	thrpool_init();
	giant_init();

	table_size = GFMD_CONNECTION_LIMIT;
	gfarm_unlimit_nofiles(&table_size);
	if (table_size > GFMD_CONNECTION_LIMIT)
		table_size = GFMD_CONNECTION_LIMIT;

	/*
	 * We do this before calling gfarm_daemon()
	 * to print the error message to stderr.
	 */
	switch (gfarm_backend_db_type) {
	case GFARM_BACKEND_DB_TYPE_LDAP:
#ifdef HAVE_LDAP
		db_use(&db_ldap_ops);
#else
		gflog_fatal("LDAP DB is specified, but it's not built in");
#endif
		break;
	case GFARM_BACKEND_DB_TYPE_POSTGRESQL:
#ifdef HAVE_POSTGRESQL
		db_use(&db_pgsql_ops);
#else
		gflog_fatal("PostgreSQL is specified, but it's not built in");
#endif
		break;
	default:
		gflog_fatal("neither LDAP or PostgreSQL is specified "
		    "in configuration");
		break;
	}
	e = db_initialize();
	if (e != GFARM_ERR_NO_ERROR) {
		/* XXX FIXME need to wait and try to reconnect */
		gflog_fatal("database initialization failed: %s",
		    gfarm_error_string(e));
	}

	if (!debug_mode) {
		gflog_syslog_open(LOG_PID, syslog_facility);
		gfarm_daemon(0, 0);
	}
	/*
	 * We do this after calling gfarm_daemon(),
	 * because it changes pid.
	 */
	write_pid(pid_file);

	host_init();
	user_init();
	group_init();
	inode_init();
	dir_entry_init();
	file_copy_init();
	dead_file_copy_init();
	symlink_init();
	xattr_init();

	peer_init(table_size, protocol_main);
	job_table_init(table_size);

	/*
	 * We don't want SIGPIPE, but want EPIPE on write(2)/close(2).
	 */
	gfarm_sigpipe_ignore();

	sigs_set(&sigs);
	pthread_sigmask(SIG_BLOCK, &sigs, NULL);
	e = create_detached_thread(sigs_handler, &sigs);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_fatal("create_detached_thread(sigs_handler): %s",
			    gfarm_error_string(e));

	e = create_detached_thread(db_thread, NULL);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_fatal("create_detached_thread(db_thread): %s",
			    gfarm_error_string(e));

	accepting_loop(sock);

	/*NOTREACHED*/
	return (0); /* to shut up warning */
}
