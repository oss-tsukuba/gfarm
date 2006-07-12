/*
 * $Id$
 */

#include <assert.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <limits.h>
#include <errno.h>
#include <gfarm/gfarm.h>
#include "gfevent.h"
#include "host.h" /* gfarm_host_info_address_get() */
#include "auth.h"
#include "config.h"
#include "gfs_client.h"

char *program_name = "gfhost";

static char *
update_host(char *hostname, int nhostaliases, char **hostaliases,
	char *architecture, int ncpu,
	char *(*update_op)(char *, struct gfarm_host_info *))
{
	struct gfarm_host_info hi;

	hi.hostname = hostname;
	hi.nhostaliases = nhostaliases;
	hi.hostaliases = hostaliases;
	hi.architecture = architecture;
	hi.ncpu = ncpu;
	return ((*update_op)(hostname, &hi));
}

static char *
check_hostname(char *hostname)
{
	char *e, *n;

	e = gfarm_host_get_canonical_name(hostname, &n);
	if (e == NULL || e == GFARM_ERR_AMBIGUOUS_RESULT) {
		if (e == NULL)
			free(n);
		return ("the hostname is already registered");
	}
	return (NULL);
}

static char *
check_hostaliases(int nhostaliases, char **hostaliases)
{
	int i;

	for (i = 0; i < nhostaliases; i++) {
		if (check_hostname(hostaliases[i]) != NULL)
			return ("the hostalias is already registered");
	}
	return (NULL);
}

char *
add_host(char *hostname, char **hostaliases, char *architecture,
	int ncpu)
{
	int nhostaliases = gfarm_strarray_length(hostaliases);
	char *e;

	e = check_hostname(hostname);
	if (e != NULL)
		return (e);
	e = check_hostaliases(nhostaliases, hostaliases);
	if (e != NULL)
		return (e);

	return (update_host(hostname, nhostaliases, hostaliases,
	    architecture, ncpu, gfarm_host_info_set));
}

char *
gfarm_modify_host(char *hostname, char **hostaliases, char *architecture,
	int ncpu, int add_aliases)
{
	char *e;
	struct gfarm_host_info hi;
	int host_info_needs_free = 0;
	gfarm_stringlist aliases;

	if (*hostaliases == NULL || architecture == NULL || ncpu < 1 ||
	    add_aliases) {
		e = gfarm_host_info_get(hostname, &hi);
		if (e != NULL)
			return (e);
		host_info_needs_free = 1;
		if (!add_aliases) {
			/* XXX - do check_hostaliases() here, too. */
			hostaliases = hostaliases;
		} else {
			e = check_hostaliases(
			    gfarm_strarray_length(hostaliases), hostaliases);
			if (e != NULL)
				goto free_host_info;

			e = gfarm_stringlist_init(&aliases);
			if (e != NULL)
				goto free_host_info;
			if (hi.hostaliases != NULL) {
				e = gfarm_stringlist_cat(&aliases,
				    hi.hostaliases);
				if (e != NULL)
					goto free_aliases;
			}
			if (hostaliases != NULL) {
				e = gfarm_stringlist_cat(&aliases,
				    hostaliases);
				if (e != NULL)
					goto free_aliases;
			}
			e = gfarm_stringlist_add(&aliases, NULL);
			if (e != NULL)
				goto free_aliases;
			hostaliases = GFARM_STRINGLIST_STRARRAY(aliases);
		}
		if (architecture == NULL)
			architecture = hi.architecture;
		if (ncpu < 1)
			ncpu = hi.ncpu;
	}
	e = update_host(hostname,
	    gfarm_strarray_length(hostaliases), hostaliases,
	    architecture, ncpu,
	    gfarm_host_info_replace);
	if (e == NULL && !add_aliases && *hostaliases == NULL)
		e = gfarm_host_info_remove_hostaliases(hostname);
 free_aliases:
	if (add_aliases)
		gfarm_stringlist_free(&aliases);
 free_host_info:
	if (host_info_needs_free)
		gfarm_host_info_free(&hi);
	return (e);
}

char *
validate_architecture(char *architecture)
{
	unsigned char c, *s = (unsigned char *)architecture;

	while ((c = *s++) != '\0') {
		if (!isalnum(c) && c != '-' && c != '_' && c != '.')
			return ((char *)s - 1);
	}
	return (NULL);
}

char *
validate_hostname(char *hostname)
{
	unsigned char c, *s = (unsigned char *)hostname;

	while ((c = *s++) != '\0') {
		if (!isalnum(c) && c != '-' && c != '.')
			return ((char *)s - 1);
	}
	return (NULL);
}

char *
invalid_input(int lineno)
{
	fprintf(stderr, "line %d: invalid input format\n", lineno);
	fprintf(stderr, "%s: input must be "
	    "\"<architecture> <ncpu> <hostname> <hostalias>...\" format\n",
	    program_name);
	return (GFARM_ERR_INVALID_ARGUMENT);
}

#define LINE_BUFFER_SIZE 16384
#define MAX_HOSTALIASES 256

char *
add_line(char *line, int lineno)
{
	long ncpu;
	int len, nhostaliases;
	char *e, *hostname, *architecture;
	char *hostaliases[MAX_HOSTALIASES + 1];
	static char space[] = " \t";

	/* parse architecture */
	line += strspn(line, space); /* skip space */
	len = strcspn(line, space);
	if (len == 0 || line[len] == '\0')
		return (invalid_input(lineno));
	line[len] = '\0';
	architecture = line;
	line += len + 1;
	e = validate_architecture(architecture);
	if (e != NULL) {
		fprintf(stderr,
		    "line %d: invalid character '%c' in architecture \"%s\"\n",
		    lineno, *e, architecture);
		return (GFARM_ERR_INVALID_ARGUMENT);
	}

	/* parse ncpu */
	line += strspn(line, space); /* skip space */
	len = strcspn(line, space);
	if (len == 0 || line[len] == '\0')
		return (invalid_input(lineno));
	line[len] = '\0';
	errno = 0;
	ncpu = strtol(line, &e, 0);
	if (e == line) {
		return (invalid_input(lineno));
	} else if (*e != '\0') {
		fprintf(stderr, "line %d: garbage \"%s\" in ncpu \"%s\"\n",
		    lineno, e, line);
		return (GFARM_ERR_INVALID_ARGUMENT);
	} else if (errno != 0 && (ncpu == LONG_MIN || ncpu == LONG_MAX)) {
		fprintf(stderr, "line %d: %s on \"%s\"\n",
		    lineno, strerror(errno), line);
		return (GFARM_ERR_INVALID_ARGUMENT);
	}
	line += len + 1;

	/* parse hostname */
	line += strspn(line, space); /* skip space */
	len = strcspn(line, space);
	if (len == 0)
		return (invalid_input(lineno));
	hostname = line;
	if (line[len] == '\0') {
		line += len;
	} else {
		line[len] = '\0';
		line += len + 1;
	}
	e = validate_hostname(hostname);
	if (e != NULL) {
		fprintf(stderr,
		    "line %d: invalid character '%c' in hostname \"%s\"\n",
		    lineno, *e, hostname);
		return (GFARM_ERR_INVALID_ARGUMENT);
	}

	/* parse hostaliases */
	for (nhostaliases = 0;; nhostaliases++) {
		line += strspn(line, space); /* skip space */
		if (*line == '\0')
			break;
		len = strcspn(line, space);
		/* assert(len > 0); */
		if (nhostaliases >= MAX_HOSTALIASES) {
			fprintf(stderr, "line %d: "
			    "number of hostaliases exceeds %d\n",
			    lineno, nhostaliases);
			return (GFARM_ERR_INVALID_ARGUMENT);
		}
		hostaliases[nhostaliases] = line;
		if (line[len] == '\0') {
			line += len;
		} else {
			line[len] = '\0';
			line += len + 1;
		}
		e = validate_hostname(hostaliases[nhostaliases]);
		if (e != NULL) {
			fprintf(stderr, "line %d: "
			    "invalid character '%c' in hostalias \"%s\"\n",
			    lineno, *e, hostaliases[nhostaliases]);
			return (GFARM_ERR_INVALID_ARGUMENT);
		}
	}
	hostaliases[nhostaliases] = NULL;

	e = add_host(hostname, hostaliases, architecture, ncpu);
	if (e != NULL)
		fprintf(stderr, "line %d: %s\n", lineno, e);
	return (e);
}

char *
register_db(void)
{
	char *e, *e_save = NULL;
	int len, lineno;
	char line[LINE_BUFFER_SIZE];

	if (fgets(line, sizeof line, stdin) == NULL)
		return (NULL);
	len = strlen(line);
	for (lineno = 1;; lineno++) {
		if (len > 0 && line[len - 1] == '\n') {
			line[len - 1] = '\0';
		} else {
			fprintf(stderr, "line %d: too long line\n", lineno);
			if (e_save == NULL)
				e_save = GFARM_ERR_INVALID_ARGUMENT;
			do {
				if (fgets(line, sizeof line, stdin) == NULL)
					return (e_save);
				len = strlen(line);
			} while (len == 0 || line[len - 1] != '\n');
			continue;
		}
		e = add_line(line, lineno);
		if (e_save == NULL)
			e_save = e;
		if (fgets(line, sizeof line, stdin) == NULL)
			break;
		len = strlen(line);
	}
	return (e_save);
}

/*
 * Handle the case where opt_use_metadb == 0.
 * In that case, the host_info is faked, and all members in the info structure
 * except info->hostname are not valid. (see list_gfsd_info())
 */
char *
resolv_addr_without_metadb(
	const char *hostname, int port, struct gfarm_host_info *info,
	struct sockaddr *addr, char **if_hostnamep)
{
	/* sizeof(struct sockaddr_in) == sizeof(struct sockaddr) */
	struct sockaddr_in *addr_in = (struct sockaddr_in *)addr;
	struct hostent *hp;

	hp = gethostbyname(hostname);
	if (hp == NULL || hp->h_addrtype != AF_INET)
		return (GFARM_ERR_UNKNOWN_HOST);
	memset(addr_in, 0, sizeof(*addr_in));
	memcpy(&addr_in->sin_addr, hp->h_addr,
	    sizeof(addr_in->sin_addr));
	addr_in->sin_family = hp->h_addrtype;
	addr_in->sin_port = htons(port);
	if (if_hostnamep != NULL) {
		*if_hostnamep = strdup(hostname);
		if (*if_hostnamep == NULL)
			return (GFARM_ERR_NO_MEMORY);
	}
	return (NULL);
}

/*
 * handle option "-i" (ignore "address_use" directive in gfarm.conf(5))
 */
char *
resolv_addr_without_address_use(
	const char *hostname, int port, struct gfarm_host_info *info,
	struct sockaddr *addr, char **if_hostnamep)
{
	char *e = resolv_addr_without_metadb(hostname, port, NULL,
	    addr, if_hostnamep);
	int i;

	if (e == NULL)
		return (NULL);
	for (i = 0; i < info->nhostaliases; i++) {
		e = resolv_addr_without_metadb(
		    info->hostaliases[i], port, NULL, addr, if_hostnamep);
		if (e == NULL)
			return (NULL);
	}
	return (e);
}

char *(*opt_resolv_addr)(const char *, int, struct gfarm_host_info *,
    struct sockaddr *, char **) = gfarm_host_info_address_get;

/*
 * extend the result of gfarm_auth_method_mnemonic()
 */

#define MNEMONIC_AUTH_FAIL	'x'
#define MNEMONIC_NOT_TRIED	'-'

char
mnemonic(struct gfs_connection *gfs_server, struct gfs_client_load *load)
{
	if (gfs_server != NULL)
		return (gfarm_auth_method_mnemonic(
		    gfs_client_connection_auth_method(gfs_server)));
	if (load != NULL)
		return (MNEMONIC_AUTH_FAIL); /* c.f. LOADAVG_AVAIL() */
	return (MNEMONIC_NOT_TRIED);
}

/*
 * sort output
 */

struct output {
	void *closure;
	char *canonical_hostname;
	struct sockaddr peer_addr;
	struct gfs_client_load load;
	char auth_mnemonic;
	char *error;
};

#define OUTPUT_INITIAL_SPACE	130

int output_number = 0, output_space;
struct output *output_buffer = NULL;

void (*output_callback)(struct output *);
int output_record;
int output_sort_reverse = 0;

#define LOADAVG_AVAIL(o)	((o)->auth_mnemonic != MNEMONIC_NOT_TRIED)

int
output_cmp_by_name(const void *a, const void *b)
{
	const struct output *o1, *o2;

	if (output_sort_reverse) {
		o1 = b;
		o2 = a;
	} else {
		o1 = a;
		o2 = b;
	}
	return (strcmp(o1->canonical_hostname, o2->canonical_hostname));
}

int
output_cmp_by_loadavg(const void *a, const void *b)
{
	const struct output *o1, *o2;

	if (output_sort_reverse) {
		o1 = b;
		o2 = a;
	} else {
		o1 = a;
		o2 = b;
	}
	if (!LOADAVG_AVAIL(o1) && !LOADAVG_AVAIL(o2))
		return (0);
	else if (!LOADAVG_AVAIL(o2)) /* treat NULL as infinity */
		return (-1);
	else if (!LOADAVG_AVAIL(o1))
		return (1);
	else if (o1->load.loadavg_1min < o2->load.loadavg_1min)
		return (-1);
	else if (o1->load.loadavg_1min > o2->load.loadavg_1min)
		return (1);
	else
		return (0);
}

void
output_sort(int (*cmp)(const void *, const void *))
{
	int i;

	qsort(output_buffer, output_number, sizeof(*output_buffer), cmp);
	for (i = 0; i < output_number; i++)
		(*output_callback)(&output_buffer[i]);
}

void
output_add(struct output *o)
{
	if (output_buffer == NULL || output_number >= output_space) {
		if (output_buffer == NULL)
			output_space = OUTPUT_INITIAL_SPACE;
		else
			output_space += output_space;
		GFARM_REALLOC_ARRAY(output_buffer,
				output_buffer, output_space);
		if (output_buffer == NULL) {
			fprintf(stderr, "no memory to record %d hosts\n",
			    output_number);
			exit(EXIT_FAILURE);
		}
	}
	output_buffer[output_number++] = *o;
}

void
output_process(void *closure,
	char *canonical_hostname, struct sockaddr *peer_addr,
	struct gfs_client_load *load, struct gfs_connection *gfs_server,
	char *error)
{
	struct output o;

	o.closure = closure;
	o.canonical_hostname = canonical_hostname;
	if (peer_addr != NULL)
		o.peer_addr = *peer_addr;
	if (load != NULL)
		o.load = *load;
	o.auth_mnemonic = mnemonic(gfs_server, load);
	o.error = error;

	assert((load != NULL) == LOADAVG_AVAIL(&o));

	if (output_record)
		output_add(&o);
	else /* i.e. don't sort */
		(*output_callback)(&o);
}

/*
 * parallel access wrapper
 */

struct gfarm_paraccess {
	struct gfarm_eventqueue *q;
	int try_auth;

	struct gfarm_access {
		void *closure;
		char *canonical_hostname;
		struct sockaddr peer_addr;
		struct gfs_client_load load;

		void *protocol_state;
		struct gfarm_paraccess *pa;

		struct gfarm_access *next;
	} *access_state;
	struct gfarm_access *free_list;
	int concurrency, nfree;
};

char *
gfarm_paraccess_alloc(
	int concurrency, int try_auth,
	struct gfarm_paraccess **pap)
{
	struct gfarm_paraccess *pa;
	int i;

	GFARM_MALLOC(pa);
	if (pa == NULL)
		return (GFARM_ERR_NO_MEMORY);

	pa->q = gfarm_eventqueue_alloc();
	if (pa->q == NULL) {
		free(pa);
		return (GFARM_ERR_NO_MEMORY);
	}

	GFARM_MALLOC_ARRAY(pa->access_state, concurrency);
	if (pa->access_state == NULL) {
		gfarm_eventqueue_free(pa->q);
		free(pa);
		return (GFARM_ERR_NO_MEMORY);
        }

	/* construct free slot list */
	i = concurrency - 1;
	pa->access_state[i].next = NULL;
	while (--i >= 0)
		pa->access_state[i].next = &pa->access_state[i + 1];
	pa->free_list = &pa->access_state[0];
	pa->concurrency = pa->nfree = concurrency;

	pa->try_auth = try_auth;
	*pap = pa;
	return (NULL);
}

static void
gfarm_paraccess_callback(struct gfarm_paraccess *pa, struct gfarm_access *a,
	struct gfs_client_load *load, struct gfs_connection *gfs_server,
	char *e)
{
	output_process(a->closure, a->canonical_hostname, &a->peer_addr,
	    load, gfs_server, e);

	/* bring this back to the free slot list */
	a->next = pa->free_list;
	pa->free_list = a;
	pa->nfree++;
}

static void
gfarm_paraccess_load_finish(void *closure)
{
	struct gfarm_access *a = closure;
	char *e;

	e = gfs_client_get_load_result_multiplexed(a->protocol_state,
	    &a->load);
	gfarm_paraccess_callback(a->pa, a, e == NULL ? &a->load : NULL, NULL,
	    e);
}

static void
gfarm_paraccess_connect_finish(void *closure)
{
	struct gfarm_access *a = closure;
	char *e;
	struct gfs_connection *gfs_server;

	e = gfs_client_connect_result_multiplexed(a->protocol_state,
	    &gfs_server);
	if (e != NULL) {
		gfarm_paraccess_callback(a->pa, a, &a->load, NULL, e);
		return;
	}
	gfarm_paraccess_callback(a->pa, a, &a->load, gfs_server, e);
	gfs_client_disconnect(gfs_server);
}

static void
gfarm_paraccess_connect_request(void *closure)
{
	struct gfarm_access *a = closure;
	char *e;
	struct gfs_client_connect_state *cs;

	e = gfs_client_get_load_result_multiplexed(a->protocol_state,
	    &a->load);
	if (e != NULL) {
		gfarm_paraccess_callback(a->pa, a, NULL, NULL, e);
		return;
	}
	e = gfs_client_connect_request_multiplexed(a->pa->q,
	    a->canonical_hostname, &a->peer_addr,
	    gfarm_paraccess_connect_finish, a,
	    &cs);
	if (e != NULL) {
		gfarm_paraccess_callback(a->pa, a, &a->load, NULL, e);
		return;
	}
	a->protocol_state = cs;
}

char *
gfarm_paraccess_request(struct gfarm_paraccess *pa,
	void *closure, char *canonical_hostname, struct sockaddr *peer_addr)
{
	int rv;
	char *e;
	struct gfarm_access *a;
	struct gfs_client_get_load_state *gls;

	/*
	 * Wait until at least one slot becomes available.
	 * We limit concurrency here.
	 */
	while (pa->free_list == NULL) {
		rv = gfarm_eventqueue_turn(pa->q, NULL);
		if (rv == EAGAIN || rv == EINTR) {
			continue; /* not really an error */
		} else if (rv != 0) {
			return (gfarm_errno_to_error(rv));
		}
	}

	/* acquire free slot */
	a = pa->free_list;
	pa->free_list = a->next;
	--pa->nfree;

	a->closure = closure;
	a->canonical_hostname = canonical_hostname;
	a->peer_addr = *peer_addr;

	e = gfs_client_get_load_request_multiplexed(pa->q, &a->peer_addr,
	    pa->try_auth ?
	    gfarm_paraccess_connect_request :
	    gfarm_paraccess_load_finish,
	    a,
	    &gls);
	if (e != NULL) {
		gfarm_paraccess_callback(pa, a, NULL, NULL, e);
		return (e);
	}
	a->protocol_state = gls;
	a->pa = pa;
	return (NULL);
}

char *
gfarm_paraccess_free(struct gfarm_paraccess *pa)
{
	int rv = gfarm_eventqueue_loop(pa->q, NULL);
	char *e;

	free(pa->access_state);
	gfarm_eventqueue_free(pa->q);
	free(pa);
	if (rv == 0)
		return (NULL);
	e = gfarm_errno_to_error(rv);
	fprintf(stderr, "%s: %s\n", program_name, e);
	return (e);
}

/*
 * listing options.
 */

int opt_verbose = 0;
int opt_udp_only = 0;

#define round_loadavg(l) ((l) < 0.0 ? 0.0 : (l) > 9.99 ? 9.99 : (l))

void
print_loadavg_authinfo(struct output *o)
{
	if (LOADAVG_AVAIL(o))
		printf("%4.2f/%4.2f/%4.2f ",
		    round_loadavg(o->load.loadavg_1min),
		    round_loadavg(o->load.loadavg_5min),
		    round_loadavg(o->load.loadavg_15min));
	else if (o->error == GFARM_ERR_CONNECTION_REFUSED) /* machine is up */
		printf("-.--/-.--/-.-- ");
	else
		printf("x.xx/x.xx/x.xx ");
	if (!opt_udp_only)
		printf("%c ", o->auth_mnemonic);
}

void
callback_gfsd_info(struct output *o)
{
	char *if_hostname = o->closure;
	struct sockaddr_in *addr_in = (struct sockaddr_in *)&o->peer_addr;

	print_loadavg_authinfo(o);
	printf("%s(%s)\n", if_hostname, inet_ntoa(addr_in->sin_addr));
	if (opt_verbose && o->error != NULL)
		fprintf(stderr, "%s: %s\n", if_hostname, o->error);
	free(o->canonical_hostname);
	free(if_hostname);
}

/*
 * Note that this may be called when opt_use_metadb == 0.
 * In that case, the host_info is faked, and all members in the info structure
 * except info->hostname are not valid. (see list_gfsd_info())
 */
char *
request_gfsd_info(struct gfarm_host_info *info,
	struct gfarm_paraccess *pa)
{
	char *e;
	struct sockaddr addr;
	char *canonical_hostname, *if_hostname;

	canonical_hostname = strdup(info->hostname);
	if (canonical_hostname == NULL) {
		e = GFARM_ERR_NO_MEMORY;
	} else {
		e = (*opt_resolv_addr)(
		    canonical_hostname, gfarm_spool_server_port, info,
		    &addr, &if_hostname);
	}
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", info->hostname, e);
		if (canonical_hostname != NULL)
			free(canonical_hostname);
		return (e);
	}
	return (gfarm_paraccess_request(pa,
	    if_hostname, canonical_hostname, &addr));
}

struct long_format_parameter {
	char *if_hostname;
	struct gfarm_host_info info;
};

void
callback_long_format(struct output *o)
{
	struct long_format_parameter *param = o->closure;
	char *if_hostname = param->if_hostname;
	struct gfarm_host_info *info = &param->info;
	/* sizeof(struct sockaddr_in) == sizeof(struct sockaddr) */
	struct sockaddr_in *addr_in = (struct sockaddr_in *)&o->peer_addr;
	int i, print_ifaddr = if_hostname != NULL;

	print_loadavg_authinfo(o);
	printf("%s %d %s",
	    info->architecture, info->ncpu, o->canonical_hostname);
	if (print_ifaddr &&
	    strcasecmp(o->canonical_hostname, if_hostname) == 0) {
		print_ifaddr = 0;
		printf("(%s)", inet_ntoa(addr_in->sin_addr));
	}
	for (i = 0; i < info->nhostaliases; i++) {
		printf(" %s", info->hostaliases[i]);
		if (print_ifaddr &&
		    strcasecmp(info->hostaliases[i], if_hostname) == 0) {
			print_ifaddr = 0;
			printf("(%s)", inet_ntoa(addr_in->sin_addr));
		}
	}
	if (print_ifaddr) {
		printf(" [%s(%s)]", if_hostname, inet_ntoa(addr_in->sin_addr));
	}
	putchar('\n');
	if (opt_verbose && o->error != NULL)
		fprintf(stderr, "%s: %s\n", o->canonical_hostname, o->error);
	gfarm_host_info_free(info);
	if (if_hostname != NULL)
		free(if_hostname);
	free(param);
}

char *
request_long_format(struct gfarm_host_info *host_info,
	struct gfarm_paraccess *pa)
{
	char *e;
	struct sockaddr addr;
	struct long_format_parameter *param;
	struct gfarm_host_info *info;

	GFARM_MALLOC(param);
	if (param == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		fprintf(stderr, "%s: %s\n", program_name, e);
		return (e);
	}
	info = &param->info;

	/* dup `*host_info' -> `*info' */
	info->hostname = strdup(host_info->hostname);
	info->nhostaliases = host_info->nhostaliases;
	if (host_info->nhostaliases == 0) {
		info->hostaliases = NULL;
	} else {
		info->hostaliases = gfarm_strarray_dup(host_info->hostaliases);
		if (info->hostaliases == NULL)
			info->nhostaliases = 0;
	}
	info->architecture = strdup(host_info->architecture);
	info->ncpu = host_info->ncpu;
	if (info->hostname == NULL || info->architecture == NULL) {
		gfarm_host_info_free(info);
		free(param);
		e = GFARM_ERR_NO_MEMORY;
		fprintf(stderr, "%s: %s\n", program_name, e);
		return (e);
	}

	param->if_hostname = NULL;
	e = (*opt_resolv_addr)(info->hostname, gfarm_spool_server_port, info,
	    &addr, &param->if_hostname);
	if (e != NULL) {
		output_process(param, info->hostname, NULL, NULL, NULL, e);
		return (e);
	}

	return (gfarm_paraccess_request(pa, param, info->hostname, &addr));
}

void
callback_nodename(struct output *o)
{
	if (o->error == NULL)
		puts(o->canonical_hostname);
	else if (opt_verbose)
		fprintf(stderr, "%s: %s\n", o->canonical_hostname, o->error);
	free(o->canonical_hostname);
}

char *
request_nodename(struct gfarm_host_info *host_info,
	struct gfarm_paraccess *pa)
{
	char *e, *canonical_hostname;
	struct sockaddr addr;

	/* dup `host_info->hostname' -> `hostname' */
	canonical_hostname = strdup(host_info->hostname);
	if (canonical_hostname == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		fprintf(stderr, "%s: %s\n", program_name, e);
		return (e);
	}

	e = (*opt_resolv_addr)(
	    canonical_hostname, gfarm_spool_server_port, host_info,
	    &addr, NULL);
	if (e != NULL) {
		output_process(NULL, canonical_hostname, NULL, NULL, NULL, e);
		return (e);
	}

	return (gfarm_paraccess_request(pa, NULL, canonical_hostname, &addr));
}

char *
print_host_info(struct gfarm_host_info *info,
	struct gfarm_paraccess *pa)
{
	int i;

	printf("%s %d %s", info->architecture, info->ncpu, info->hostname);
	for (i = 0; i < info->nhostaliases; i++)
		printf(" %s", info->hostaliases[i]);
	putchar('\n');
	return (NULL);
}

char *
list_all(const char *architecture, const char *domainname,
	char *(*request_op)(struct gfarm_host_info *,
	    struct gfarm_paraccess *),
	struct gfarm_paraccess *pa)
{
	char *e, *e_save = NULL;
	int i, nhosts;
	struct gfarm_host_info *hosts;

	if (architecture != NULL)
		e = gfarm_host_info_get_allhost_by_architecture(
			architecture, &nhosts, &hosts);
	else
		e = gfarm_host_info_get_all(&nhosts, &hosts);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		return (e);
	}
	for (i = 0; i < nhosts; i++) {
		if (domainname == NULL ||
	 	    gfarm_host_is_in_domain(hosts[i].hostname, domainname)) {
			e = (*request_op)(&hosts[i], pa);
			if (e_save == NULL)
				e_save = e;
		}
	}
	gfarm_host_info_free_all(nhosts, hosts);
	return (e_save);
}

char *
list(int nhosts, char **hosts,
	char *(*request_op)(struct gfarm_host_info *,
	    struct gfarm_paraccess *),
	struct gfarm_paraccess *pa)
{
	char *e, *e_save = NULL;
	int i;
	struct gfarm_host_info hi;

	for (i = 0; i < nhosts; i++) {
		e = gfarm_host_info_get_by_if_hostname(hosts[i], &hi);
		if (e != NULL) {
			fprintf(stderr, "%s: %s\n", hosts[i], e);
			if (e_save == NULL)
				e_save = e;
		} else {
			e = (*request_op)(&hi, pa);
			if (e_save == NULL)
				e_save = e;
			gfarm_host_info_free(&hi);
		}
	}
	return (e_save);
}

/*
 * This function is a special case to avoid Meta DB access,
 * and only called if opt_use_metadb == 0.
 */
char *
list_without_metadb(int nhosts, char **hosts,
	char *(*request_op)(struct gfarm_host_info *,
	    struct gfarm_paraccess *),
	struct gfarm_paraccess *pa)
{
	char *e, *e_save = NULL;
	int i;
	struct gfarm_host_info host;

	for (i = 0; i < nhosts; i++) {
		host.hostname = hosts[i]; /* host_info is faked */
		/*
		 * Because request_op is always request_gfsd_info for now,
		 * the following fields aren't actually used.
		 */
		host.nhostaliases = 0;
		host.hostaliases = NULL;
		host.architecture = NULL;
		host.ncpu = 0;

		e = (*request_op)(&host, pa);
		if (e_save == NULL)
			e_save = e;
	}
	return (e_save);
}

char *
paraccess_list(int opt_concurrency, int opt_udp_only,
	char *opt_architecture, char *opt_domainname,
	int opt_plain_order, int opt_sort_by_loadavg,
	int opt_use_metadb, int nhosts, char **hosts,
	char *(*request_op)(struct gfarm_host_info *,
	    struct gfarm_paraccess *),
	void (*callback_op)(struct output *))
{
	char *e, *e_save;
	struct gfarm_paraccess *pa;

	if (opt_plain_order) /* i.e. don't sort */
		output_record = 0;
	else
		output_record = 1;

	output_callback = callback_op;

	e = gfarm_paraccess_alloc(opt_concurrency, !opt_udp_only, &pa);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}

	if (nhosts == 0) {
		e_save = list_all(opt_architecture, opt_domainname,
			request_op, pa);
	} else if (opt_use_metadb) {
		e_save = list(nhosts, hosts, request_op, pa);
	} else {
		e_save = list_without_metadb(nhosts, hosts, request_op, pa);
	}
	e = gfarm_paraccess_free(pa);
	if (e_save == NULL)
		e_save = e;

	if (!opt_plain_order)
		output_sort(opt_sort_by_loadavg ? output_cmp_by_loadavg :
		    output_cmp_by_name);

	return (e_save);
}

void
usage(void)
{
	fprintf(stderr, "Usage:" 
	    "\t%s %s\n" "\t%s %s\n" "\t%s %s\n" "\t%s %s\n" "\t%s %s\n",
	    program_name,
	    "[-lMH] [-a <architecture>] [-D <domainname>] [-j <concurrency>] [-iprv]",
	    program_name,
	    "-c  -a <architecture>  [-n <ncpu>] <hostname> [<hostalias>...]",
	    program_name,
	    "-m [-a <architecture>] [-n <ncpu>] [-A] <hostname> [<hostalias>...]",
	    program_name, "-d <hostname>...",
	    program_name, "-R");
	exit(EXIT_FAILURE);
}

#define OP_NODENAME		'\0'	/* '\0' means default operation */
#define OP_LIST_GFSD_INFO	'H'
#define OP_LIST_LONG		'l'
#define OP_DUMP_METADB		'M'
#define OP_REGISTER_DB		'R'	/* or, restore db */
#define OP_CREATE_ENTRY		'c'
#define OP_DELETE_ENTRY		'd'
#define OP_MODIFY_ENTRY		'm'

void
inconsistent_option(int c1, int c2)
{
	fprintf(stderr, "%s: inconsistent option -%c and -%c\n",
	    program_name, c1, c2);
	usage();
}

void
invalid_option(int c)
{
	fprintf(stderr, "%s: option -%c is only available with -%c or -%c\n",
	    program_name, c, OP_CREATE_ENTRY, OP_MODIFY_ENTRY);
	usage();
}

long
parse_opt_long(char *option, int option_char, char *argument_name)
{
	long value;
	char *s;

	errno = 0;
	value = strtol(option, &s, 0);
	if (s == option) {
		fprintf(stderr, "%s: missing %s after -%c\n",
		    program_name, argument_name, option_char);
		usage();
	} else if (*s != '\0') {
		fprintf(stderr, "%s: garbage in -%c %s\n",
		    program_name, option_char, option);
		usage();
	} else if (errno != 0 && (value == LONG_MIN || value == LONG_MAX)) {
		fprintf(stderr, "%s: %s with -%c %s\n",
		    program_name, strerror(errno), option_char, option);
		usage();
	}
	return (value);
}


#define DEFAULT_CONCURRENCY 10 /* reflect this value to gfhost.1.docbook */

int
main(int argc, char **argv)
{
	int argc_save = argc;
	char **argv_save = argv;
	char *e, *e_save = NULL;
	char opt_operation = '\0'; /* default operation */
	int opt_concurrency = DEFAULT_CONCURRENCY;
	int opt_alter_aliases = 0;
	char *opt_architecture = NULL;
	char *opt_domainname = NULL;
	long opt_ncpu = 0;
	int opt_plain_order = 0; /* i.e. do not sort */
	int opt_sort_by_loadavg = 0;
	int i, c, opt_use_metadb = 1;

	if (argc > 0)
		program_name = basename(argv[0]);
	while ((c = getopt(argc, argv, "AD:HLMRUa:cdij:lmn:prv?")) != -1) {
		switch (c) {
		case 'A':
			opt_alter_aliases = 1;
			break;
		case 'L':
			opt_sort_by_loadavg = 1;
			break;
		case 'M':
		case 'H':
		case 'R':
		case 'c':
		case 'd':
		case 'l':
		case 'm':
			if (opt_operation != '\0' && opt_operation != c)
				inconsistent_option(opt_operation, c);
			opt_operation = c;
			break;
		case 'a':
			opt_architecture = optarg;
			e = validate_architecture(opt_architecture);
			if (e != NULL) {
				fprintf(stderr, "%s: "
				    "invalid character '%c' in \"-a %s\"\n",
				    program_name, *e, opt_architecture);
				exit(1);
			}
			break;
		case 'D':
			opt_domainname = optarg;
			e = validate_hostname(opt_domainname);
			if (e != NULL) {
				fprintf(stderr, "%s: "
				    "invalid character '%c' in \"-a %s\"\n",
				    program_name, *e, opt_domainname);
				exit(1);
			}
			break;
		case 'i':
			opt_resolv_addr = resolv_addr_without_address_use;
			break;
		case 'j':
			opt_concurrency = parse_opt_long(optarg,
			    c, "<concurrency>");
			if (opt_concurrency <= 0) {
				fprintf(stderr, "%s: invalid value: -%c %d\n",
				    program_name, c, opt_concurrency);
				usage();
			}
			break;
		case 'n':
			opt_ncpu = parse_opt_long(optarg, c, "<ncpu>");
			break;
		case 'p':
			opt_plain_order = 1;
			break;
		case 'r':
			output_sort_reverse = 1;
			break;
		case 'U':
			opt_udp_only = 1;
			break;
		case 'v':
			opt_verbose = 1;
			break;
		case '?':
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	switch (opt_operation) {
	case OP_CREATE_ENTRY:
		if (opt_architecture == NULL) {
			fprintf(stderr, "%s: missing -a <architecture>\n",
			    program_name);
			usage();
		}
		if (opt_ncpu == 0) {
			opt_ncpu = 1;
		}
		/* opt_alter_aliases is meaningless, but allowed */
		break;
	case OP_REGISTER_DB:
	case OP_DELETE_ENTRY:
		if (opt_architecture != NULL)
			invalid_option('a');
		if (opt_domainname != NULL)
			invalid_option('D');
		/* fall through */
	case OP_NODENAME:
	case OP_LIST_GFSD_INFO:
	case OP_LIST_LONG:
	case OP_DUMP_METADB:
		if (opt_ncpu != 0)
			invalid_option('n');
		if (opt_alter_aliases)
			invalid_option('A');
		break;
	case OP_MODIFY_ENTRY:
		if (opt_domainname != NULL)
			invalid_option('D');
		break;
	default:
		;
	}

	for (i = 0; i < argc; i++) {
		e = validate_hostname(argv[i]);
		if (e != NULL) {
			fprintf(stderr, "%s: "
			    "invalid character '%c' in hostname \"%s\"\n",
			    program_name, *e, argv[i]);
			exit(1);
		}
	}

	e = gfarm_initialize(&argc_save, &argv_save);
	if (opt_operation == OP_LIST_GFSD_INFO && argc > 0 &&
	    opt_resolv_addr == resolv_addr_without_address_use) {
		/*
		 * An implicit feature to access gfsd directly
		 * without having working gfmd.
		 * e.g. gfhost -Hi <hostname>
		 *
		 * XXX	should describe this in the manual?
		 *	or use explicit and different option?
		 *
		 * NOTE: We have to call gfarm_initialize() anyway
		 *	to initialize gfarm_spool_server_port.
		 */
		opt_use_metadb = 0;
		opt_resolv_addr = resolv_addr_without_metadb;
	} else if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}

	switch (opt_operation) {
	case OP_CREATE_ENTRY:
		if (argc > 0) {
			e_save = add_host(argv[0], &argv[1],
			    opt_architecture, opt_ncpu);
			if (e_save != NULL)
				fprintf(stderr, "%s: %s\n", argv[0], e_save);
		}
		break;
	case OP_MODIFY_ENTRY:
		if (argc > 0) {
			e_save = gfarm_modify_host(argv[0], &argv[1],
			    opt_architecture, opt_ncpu, !opt_alter_aliases);
			if (e_save != NULL)
				fprintf(stderr, "%s: %s\n", argv[0], e_save);
		}
		break;
	case OP_DELETE_ENTRY:
		for (i = 0; i < argc; i++) {
			e = gfarm_host_info_remove(argv[i]);
			if (e != NULL) {
				fprintf(stderr, "%s: %s\n", argv[i], e);
				if (e_save == NULL)
					e_save = e;
			}
		}
		break;
	case OP_REGISTER_DB:
		if (argc > 0) {
			fprintf(stderr, "%s: too many argument: %s\n",
			    program_name, argv[0]);
			exit(1);
		}
		e_save = register_db();
		break;
	case OP_LIST_GFSD_INFO:
		e = paraccess_list(opt_concurrency, opt_udp_only,
		    opt_architecture, opt_domainname,
		    opt_plain_order, opt_sort_by_loadavg,
		    opt_use_metadb, argc, argv,
		    request_gfsd_info, callback_gfsd_info);
		break;
	case OP_NODENAME:
		e = paraccess_list(opt_concurrency, opt_udp_only,
		    opt_architecture, opt_domainname,
		    opt_plain_order, opt_sort_by_loadavg,
		    opt_use_metadb, argc, argv,
		    request_nodename, callback_nodename);
		break;
	case OP_LIST_LONG:
		e = paraccess_list(opt_concurrency, opt_udp_only,
		    opt_architecture, opt_domainname,
		    opt_plain_order, opt_sort_by_loadavg,
		    opt_use_metadb, argc, argv,
		    request_long_format, callback_long_format);
		break;
	case OP_DUMP_METADB:
		if (argc == 0) {
			e_save = list_all(opt_architecture, opt_domainname,
				print_host_info, NULL);
		} else {
			e_save = list(argc, argv, print_host_info, NULL);
		}
		break;
	}
	e = gfarm_terminate();
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}
	exit(e_save == NULL ? 0 : 1);
}
