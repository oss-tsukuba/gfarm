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

#include "liberror.h"

#include "host.h" /* gfm_host_info_address_get() */
#include "auth.h"
#include "gfm_client.h"
#include "gfs_client.h"
#include "lookup.h"
#include "filesystem.h"
#include "config.h"
#include "gfarm_path.h"
#include "context.h"

char *program_name = "gfhost";

struct gfm_connection *gfm_server = NULL;

/**********************************************************************/

/* register application specific error number */

static const char *app_error_map[] = {
#define APP_ERR_IMPLEMENTATION_INDEX		0
#define APP_ERR_IMPLEMENTATION			app_error(0)
	"implementation error, invalid error number",
#define APP_ERR_HOSTNAME_IS_ALREADY_REGISERED	app_error(1)
	"the hostname is already registered",
#define APP_ERR_HOSTALIAS_IS_ALREADY_REGISERED	app_error(2)
	"the hostalias is already registered",
};

const char *
app_error_code_to_message(void *cookie, int code)
{
	if (code < 0 || code >= GFARM_ARRAY_LENGTH(app_error_map))
		return (app_error_map[APP_ERR_IMPLEMENTATION_INDEX]);
	else
		return (app_error_map[code]);
}

gfarm_error_t
app_error(int code)
{
	static struct gfarm_error_domain *app_error_domain = NULL;

	if (app_error_domain == NULL) {
		gfarm_error_t e = gfarm_error_domain_alloc(
		    0, GFARM_ARRAY_LENGTH(app_error_map) - 1,
		    app_error_code_to_message, NULL,
		    &app_error_domain);

		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "%s: gfarm_error_domain_alloc: %s\n",
			    program_name, gfarm_error_string(e));
			exit(EXIT_FAILURE);
		}
	}

	return (gfarm_error_domain_map(app_error_domain, code));
}

/**********************************************************************/

static gfarm_error_t
update_host(const char *hostname, int port,
	int nhostaliases, char **hostaliases,
	char *architecture, int ncpu, int flags,
	gfarm_error_t (*update_op)(struct gfm_connection *,
		const struct gfarm_host_info *))
{
	struct gfarm_host_info hi;

	hi.hostname = (char *)hostname; /* UNCONST */
	hi.port = port;
	hi.nhostaliases = nhostaliases;
	hi.hostaliases = hostaliases;
	hi.architecture = architecture;
	hi.ncpu = ncpu;
	hi.flags = flags;
	return ((*update_op)(gfm_server, &hi));
}

static gfarm_error_t
check_hostname(char *hostname)
{
	gfarm_error_t e;
	char *n;
	int p;

	e = gfm_host_get_canonical_name(gfm_server, hostname,
	    &n, &p);
	if (e == GFARM_ERR_NO_ERROR) {
		free(n);
		return (APP_ERR_HOSTNAME_IS_ALREADY_REGISERED);
	}
	/* XXX TODO: e == GFARM_ERR_AMBIGUOUS_RESULT? */
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
check_hostaliases(int nhostaliases, char **hostaliases)
{
	int i;

	for (i = 0; i < nhostaliases; i++) {
		if (check_hostname(hostaliases[i]) != GFARM_ERR_NO_ERROR)
			return (APP_ERR_HOSTALIAS_IS_ALREADY_REGISERED);
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
add_host(char *hostname, int port, char **hostaliases, char *architecture,
	int ncpu, int flags)
{
	int nhostaliases = gfarm_strarray_length(hostaliases);
	gfarm_error_t e;

	e = check_hostname(hostname);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	e = check_hostaliases(nhostaliases, hostaliases);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	return (update_host(hostname, port, nhostaliases, hostaliases,
	    architecture, ncpu, flags, gfm_client_host_info_set));
}

gfarm_error_t
gfarm_modify_host(const char *hostname, int port,
	char **hostaliases, char *architecture,
	int ncpu, int flags, int add_aliases)
{
	gfarm_error_t e, e2;
	struct gfarm_host_info hi;
	int host_info_needs_free = 0;
	gfarm_stringlist aliases;

	if (port == 0 || *hostaliases == NULL || architecture == NULL ||
	    ncpu < 1 || flags == -1 || add_aliases) {
		e = gfm_client_host_info_get_by_names(gfm_server,
		    1, &hostname, &e2, &hi);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
		if (e2 != GFARM_ERR_NO_ERROR)
			return (e2);
		host_info_needs_free = 1;
		if (!add_aliases) {
			/* XXX - do check_hostaliases() against hostaliases */
		} else {
			e = check_hostaliases(
			    gfarm_strarray_length(hostaliases), hostaliases);
			if (e != GFARM_ERR_NO_ERROR)
				goto free_host_info;

			e = gfarm_stringlist_init(&aliases);
			if (e != GFARM_ERR_NO_ERROR)
				goto free_host_info;
			if (hi.hostaliases != NULL) {
				e = gfarm_stringlist_cat(&aliases,
				    hi.hostaliases);
				if (e != GFARM_ERR_NO_ERROR)
					goto free_aliases;
			}
			if (hostaliases != NULL) {
				e = gfarm_stringlist_cat(&aliases,
				    hostaliases);
				if (e != GFARM_ERR_NO_ERROR)
					goto free_aliases;
			}
			e = gfarm_stringlist_add(&aliases, NULL);
			if (e != GFARM_ERR_NO_ERROR)
				goto free_aliases;
			hostaliases = GFARM_STRINGLIST_STRARRAY(aliases);
		}
		if (port == 0)
			port = hi.port;
		if (architecture == NULL)
			architecture = hi.architecture;
		if (ncpu < 1)
			ncpu = hi.ncpu;
		if (flags == -1)
			flags = hi.flags;
	}
	e = update_host(hostname, port,
	    gfarm_strarray_length(hostaliases), hostaliases,
	    architecture, ncpu, flags,
	    gfm_client_host_info_modify);
#if 0 /* XXX FIXME not yet in v2 */
	if (e == GFARM_ERR_NO_ERROR && !add_aliases && *hostaliases == NULL)
		e = gfarm_host_info_remove_hostaliases(hostname);
#endif
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

gfarm_error_t
invalid_input(int lineno)
{
	fprintf(stderr, "line %d: invalid input format\n", lineno);
	fprintf(stderr, "%s: input must be \""
	    "<architecture> <ncpu> <hostname> <port> <flags> <hostalias>..."
	    "\" format\n",
	    program_name);
	return (GFARM_ERR_INVALID_ARGUMENT);
}

static char space[] = " \t";

gfarm_error_t
parse_string_long(char **linep, int lineno, const char *diag, long *retvalp)
{
	char *line = *linep, *s;
	int len;
	long retval;

	line += strspn(line, space); /* skip space */
	len = strcspn(line, space);
	if (len == 0)
		return (invalid_input(lineno));
	else if (line[len] != '\0') {
		line[len] = '\0';
		++len;
	}
	errno = 0;
	retval = strtol(line, &s, 0);
	if (s == line) {
		return (invalid_input(lineno));
	} else if (*s != '\0') {
		fprintf(stderr, "line %d: garbage \"%s\" in %s \"%s\"\n",
		    lineno, s, diag, line);
		return (GFARM_ERR_INVALID_ARGUMENT);
	} else if (errno != 0 && (retval == LONG_MIN || retval == LONG_MAX)) {
		fprintf(stderr, "line %d: %s on \"%s\"\n",
		    lineno, strerror(errno), line);
		return (GFARM_ERR_INVALID_ARGUMENT);
	}
	line += len;
	*linep = line;
	*retvalp = retval;
	return (GFARM_ERR_NO_ERROR);
}

#define LINE_BUFFER_SIZE 16384
#define MAX_HOSTALIASES 256

gfarm_error_t
add_line(char *line, int lineno)
{
	gfarm_error_t e;
	long port, ncpu, flags;
	int len, nhostaliases;
	char *s, *hostname, *architecture;
	char *hostaliases[MAX_HOSTALIASES + 1];

	/* parse architecture */
	line += strspn(line, space); /* skip space */
	len = strcspn(line, space);
	if (len == 0 || line[len] == '\0')
		return (invalid_input(lineno));
	line[len] = '\0';
	architecture = line;
	line += len + 1;
	s = validate_architecture(architecture);
	if (s != NULL) {
		fprintf(stderr,
		    "line %d: invalid character '%c' in architecture \"%s\"\n",
		    lineno, *s, architecture);
		return (GFARM_ERR_INVALID_ARGUMENT);
	}

	e = parse_string_long(&line, lineno, "ncpu", &ncpu);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

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
	s = validate_hostname(hostname);
	if (s != NULL) {
		fprintf(stderr,
		    "line %d: invalid character '%c' in hostname \"%s\"\n",
		    lineno, *s, hostname);
		return (GFARM_ERR_INVALID_ARGUMENT);
	}

	e = parse_string_long(&line, lineno, "port", &port);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	e = parse_string_long(&line, lineno, "flags", &flags);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

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
		s = validate_hostname(hostaliases[nhostaliases]);
		if (s != NULL) {
			fprintf(stderr, "line %d: "
			    "invalid character '%c' in hostalias \"%s\"\n",
			    lineno, *s, hostaliases[nhostaliases]);
			return (GFARM_ERR_INVALID_ARGUMENT);
		}
	}
	hostaliases[nhostaliases] = NULL;

	e = add_host(hostname, port, hostaliases, architecture, ncpu, flags);
	if (e != GFARM_ERR_NO_ERROR)
		fprintf(stderr, "line %d: %s\n",
		    lineno, gfarm_error_string(e));
	return (e);
}

gfarm_error_t
register_db(void)
{
	gfarm_error_t e, e_save = GFARM_ERR_NO_ERROR;
	int len, lineno;
	char line[LINE_BUFFER_SIZE];

	if (fgets(line, sizeof line, stdin) == NULL)
		return (GFARM_ERR_NO_ERROR);
	len = strlen(line);
	for (lineno = 1;; lineno++) {
		if (len > 0 && line[len - 1] == '\n') {
			line[len - 1] = '\0';
		} else {
			fprintf(stderr, "line %d: too long line\n", lineno);
			if (e_save == GFARM_ERR_NO_ERROR)
				e_save = GFARM_ERR_INVALID_ARGUMENT;
			do {
				if (fgets(line, sizeof line, stdin) == NULL)
					return (e_save);
				len = strlen(line);
			} while (len == 0 || line[len - 1] != '\n');
			continue;
		}
		e = add_line(line, lineno);
		if (e_save == GFARM_ERR_NO_ERROR)
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
gfarm_error_t
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
	return (GFARM_ERR_NO_ERROR);
}

/*
 * handle option "-i" (ignore "address_use" directive in gfarm.conf(5))
 */
gfarm_error_t
resolv_addr_without_address_use(
	const char *hostname, int port, struct gfarm_host_info *info,
	struct sockaddr *addr, char **if_hostnamep)
{
	gfarm_error_t e = resolv_addr_without_metadb(hostname, port, NULL,
	    addr, if_hostnamep);
	int i;

	if (e == GFARM_ERR_NO_ERROR)
		return (GFARM_ERR_NO_ERROR);
	for (i = 0; i < info->nhostaliases; i++) {
		e = resolv_addr_without_metadb(
		    info->hostaliases[i], port, NULL, addr, if_hostnamep);
		if (e == GFARM_ERR_NO_ERROR)
			return (GFARM_ERR_NO_ERROR);
	}
	return (e);
}

gfarm_error_t (*opt_resolv_addr)(const char *, int, struct gfarm_host_info *,
    struct sockaddr *, char **) =
#if 0 /* XXX FIXME not yet in v2 */
	gfarm_host_info_address_get;
#else
	resolv_addr_without_address_use;
#endif

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
	gfarm_error_t error;
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
	gfarm_error_t error)
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
		int port;
		struct sockaddr peer_addr;
		struct gfs_client_load load;

		void *protocol_state;
		struct gfarm_paraccess *pa;

		struct gfarm_access *next;
	} *access_state;
	struct gfarm_access *free_list;
	int concurrency, nfree;
};

gfarm_error_t
gfarm_paraccess_alloc(
	int concurrency, int try_auth,
	struct gfarm_paraccess **pap)
{
	int syserr;
	struct gfarm_paraccess *pa;
	int i;

	GFARM_MALLOC(pa);
	if (pa == NULL)
		return (GFARM_ERR_NO_MEMORY);

	syserr = gfarm_eventqueue_alloc(concurrency, &pa->q);
	if (syserr !=0) {
		free(pa);
		return (gfarm_errno_to_error(syserr));
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
	return (GFARM_ERR_NO_ERROR);
}

static void
gfarm_paraccess_callback(struct gfarm_paraccess *pa, struct gfarm_access *a,
	struct gfs_client_load *load, struct gfs_connection *gfs_server,
	gfarm_error_t e)
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
	gfarm_error_t e;

	e = gfs_client_get_load_result_multiplexed(a->protocol_state,
	    &a->load);
	gfarm_paraccess_callback(a->pa, a,
	    e == GFARM_ERR_NO_ERROR ? &a->load : NULL, NULL, e);
}

static void
gfarm_paraccess_connect_finish(void *closure)
{
	struct gfarm_access *a = closure;
	gfarm_error_t e;
	struct gfs_connection *gfs_server;

	e = gfs_client_connect_result_multiplexed(a->protocol_state,
	    &gfs_server);
	if (e != GFARM_ERR_NO_ERROR) {
		gfarm_paraccess_callback(a->pa, a, &a->load, NULL, e);
		return;
	}
	gfarm_paraccess_callback(a->pa, a, &a->load, gfs_server, e);
	gfs_client_connection_free(gfs_server);
}

static void
gfarm_paraccess_connect_request(void *closure)
{
	struct gfarm_access *a = closure;
	gfarm_error_t e;
	struct gfs_client_connect_state *cs;
	struct gfarm_filesystem *fs =
	    gfarm_filesystem_get_by_connection(gfm_server);

	e = gfs_client_get_load_result_multiplexed(a->protocol_state,
	    &a->load);
	if (e != GFARM_ERR_NO_ERROR) {
		gfarm_paraccess_callback(a->pa, a, NULL, NULL, e);
		return;
	}
	e = gfs_client_connect_request_multiplexed(a->pa->q,
	    a->canonical_hostname, a->port, gfm_client_username(gfm_server),
	    &a->peer_addr, fs, gfarm_paraccess_connect_finish, a, &cs);
	if (e != GFARM_ERR_NO_ERROR) {
		gfarm_paraccess_callback(a->pa, a, &a->load, NULL, e);
		return;
	}
	a->protocol_state = cs;
}

gfarm_error_t
gfarm_paraccess_request(struct gfarm_paraccess *pa,
	void *closure, char *canonical_hostname, int port,
	struct sockaddr *peer_addr)
{
	int rv;
	gfarm_error_t e;
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
	a->port = port;
	a->peer_addr = *peer_addr;

	e = gfs_client_get_load_request_multiplexed(pa->q, &a->peer_addr,
	    pa->try_auth ?
	    gfarm_paraccess_connect_request :
	    gfarm_paraccess_load_finish,
	    a,
	    &gls, 1);
	if (e != GFARM_ERR_NO_ERROR) {
		gfarm_paraccess_callback(pa, a, NULL, NULL, e);
		return (e);
	}
	a->protocol_state = gls;
	a->pa = pa;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfarm_paraccess_free(struct gfarm_paraccess *pa)
{
	int rv = gfarm_eventqueue_loop(pa->q, NULL);
	gfarm_error_t e;

	free(pa->access_state);
	gfarm_eventqueue_free(pa->q);
	free(pa);
	if (rv == 0)
		return (GFARM_ERR_NO_ERROR);
	e = gfarm_errno_to_error(rv);
	fprintf(stderr, "%s: %s\n", program_name, gfarm_error_string(e));
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
	if (opt_verbose && o->error != GFARM_ERR_NO_ERROR)
		fprintf(stderr, "%s: %s\n", if_hostname,
		    gfarm_error_string(o->error));
	free(o->canonical_hostname);
	free(if_hostname);
}

/*
 * Note that this may be called when opt_use_metadb == 0.
 * In that case, the host_info is faked, and all members in the info structure
 * except info->hostname are not valid. (see list_gfsd_info())
 */
gfarm_error_t
request_gfsd_info(struct gfarm_host_info *info,
	struct gfarm_paraccess *pa)
{
	gfarm_error_t e;
	struct sockaddr addr;
	char *canonical_hostname, *if_hostname;

	canonical_hostname = strdup(info->hostname);
	if (canonical_hostname == NULL) {
		e = GFARM_ERR_NO_MEMORY;
	} else {
		e = (*opt_resolv_addr)(
		    canonical_hostname, info->port, info,
		    &addr, &if_hostname);
	}
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", info->hostname,
		    gfarm_error_string(e));
		if (canonical_hostname != NULL)
			free(canonical_hostname);
		return (e);
	}
	return (gfarm_paraccess_request(pa,
	    if_hostname, canonical_hostname, info->port, &addr));
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
	printf("%s %d %s %d %d",
	    info->architecture, info->ncpu,
	    o->canonical_hostname, info->port, info->flags);
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
	if (opt_verbose && o->error != GFARM_ERR_NO_ERROR)
		fprintf(stderr, "%s: %s\n", o->canonical_hostname,
		    gfarm_error_string(o->error));
	gfarm_host_info_free(info);
	if (if_hostname != NULL)
		free(if_hostname);
	free(param);
}

gfarm_error_t
request_long_format(struct gfarm_host_info *host_info,
	struct gfarm_paraccess *pa)
{
	gfarm_error_t e;
	struct sockaddr addr;
	struct long_format_parameter *param;
	struct gfarm_host_info *info;

	GFARM_MALLOC(param);
	if (param == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		return (e);
	}
	info = &param->info;

	/* dup `*host_info' -> `*info' */
	info->hostname = strdup(host_info->hostname);
	info->port = host_info->port;
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
	info->flags = host_info->flags;
	if (info->hostname == NULL || info->architecture == NULL) {
		gfarm_host_info_free(info);
		free(param);
		e = GFARM_ERR_NO_MEMORY;
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		return (e);
	}

	param->if_hostname = NULL;
	e = (*opt_resolv_addr)(info->hostname, info->port, info,
	    &addr, &param->if_hostname);
	if (e != GFARM_ERR_NO_ERROR) {
		output_process(param, info->hostname, NULL, NULL, NULL, e);
		return (e);
	}

	return (gfarm_paraccess_request(pa, param, info->hostname, info->port,
	    &addr));
}

void
callback_nodename(struct output *o)
{
	if (o->error == GFARM_ERR_NO_ERROR)
		puts(o->canonical_hostname);
	else if (opt_verbose)
		fprintf(stderr, "%s: %s\n",
		    o->canonical_hostname, gfarm_error_string(o->error));
	free(o->canonical_hostname);
}

gfarm_error_t
request_nodename(struct gfarm_host_info *host_info,
	struct gfarm_paraccess *pa)
{
	gfarm_error_t e;
	char *canonical_hostname;
	struct sockaddr addr;

	/* dup `host_info->hostname' -> `hostname' */
	canonical_hostname = strdup(host_info->hostname);
	if (canonical_hostname == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		return (e);
	}

	e = (*opt_resolv_addr)(
	    canonical_hostname, host_info->port, host_info,
	    &addr, NULL);
	if (e != GFARM_ERR_NO_ERROR) {
		output_process(NULL, canonical_hostname, NULL, NULL, NULL, e);
		return (e);
	}

	return (gfarm_paraccess_request(pa, NULL,
	    canonical_hostname, host_info->port, &addr));
}

gfarm_error_t
print_host_info(struct gfarm_host_info *info,
	struct gfarm_paraccess *pa)
{
	int i;

	printf("%s %d %s %d %d", info->architecture, info->ncpu,
	    info->hostname, info->port, info->flags);
	for (i = 0; i < info->nhostaliases; i++)
		printf(" %s", info->hostaliases[i]);
	putchar('\n');
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
list_all(const char *architecture, const char *domainname,
	gfarm_error_t (*request_op)(struct gfarm_host_info *,
	    struct gfarm_paraccess *),
	struct gfarm_paraccess *pa)
{
	gfarm_error_t e, e_save = GFARM_ERR_NO_ERROR;
	int i, nhosts;
	struct gfarm_host_info *hosts;

	if (architecture != NULL)
		e = gfm_client_host_info_get_by_architecture(
		    gfm_server, architecture, &nhosts, &hosts);
	else
		e = gfm_client_host_info_get_all(
		    gfm_server, &nhosts, &hosts);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		return (e);
	}
	for (i = 0; i < nhosts; i++) {
		if (domainname == NULL ||
	 	    gfarm_host_is_in_domain(hosts[i].hostname, domainname)) {
			e = (*request_op)(&hosts[i], pa);
			if (e_save == GFARM_ERR_NO_ERROR)
				e_save = e;
		}
	}
	gfarm_host_info_free_all(nhosts, hosts);
	return (e_save);
}

gfarm_error_t
list(int nhosts, char **hosts,
	gfarm_error_t (*request_op)(struct gfarm_host_info *,
	    struct gfarm_paraccess *),
	struct gfarm_paraccess *pa)
{
	gfarm_error_t e, e_save = GFARM_ERR_NO_ERROR;
	int i;
	struct gfarm_host_info hi;

	for (i = 0; i < nhosts; i++) {
		e = gfm_host_info_get_by_name_alias(gfm_server,
		    hosts[i], &hi);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "%s: %s\n", hosts[i],
		    	    gfarm_error_string(e));
			if (e_save == GFARM_ERR_NO_ERROR)
				e_save = e;
		} else {
			e = (*request_op)(&hi, pa);
			if (e_save == GFARM_ERR_NO_ERROR)
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
gfarm_error_t
list_without_metadb(int nhosts, char **hosts, int port,
	gfarm_error_t (*request_op)(struct gfarm_host_info *,
	    struct gfarm_paraccess *),
	struct gfarm_paraccess *pa)
{
	gfarm_error_t e, e_save = GFARM_ERR_NO_ERROR;
	int i;
	struct gfarm_host_info host;

	for (i = 0; i < nhosts; i++) {
		host.hostname = hosts[i]; /* host_info is faked */
		host.port = port;
		/*
		 * Because request_op is always request_gfsd_info for now,
		 * the following fields aren't actually used.
		 */
		host.nhostaliases = 0;
		host.hostaliases = NULL;
		host.architecture = NULL;
		host.ncpu = 0;
		host.flags = -1;

		e = (*request_op)(&host, pa);
		if (e_save == GFARM_ERR_NO_ERROR)
			e_save = e;
	}
	return (e_save);
}

gfarm_error_t
paraccess_list(int opt_concurrency, int opt_udp_only,
	char *opt_architecture, char *opt_domainname, int opt_port,
	int opt_plain_order, int opt_sort_by_loadavg,
	int opt_use_metadb, int nhosts, char **hosts,
	gfarm_error_t (*request_op)(struct gfarm_host_info *,
	    struct gfarm_paraccess *),
	void (*callback_op)(struct output *))
{
	gfarm_error_t e, e_save;
	struct gfarm_paraccess *pa;

	if (opt_plain_order) /* i.e. don't sort */
		output_record = 0;
	else
		output_record = 1;

	output_callback = callback_op;

	e = gfarm_paraccess_alloc(opt_concurrency, !opt_udp_only, &pa);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n",
		    program_name, gfarm_error_string(e));
		exit(1);
	}

	if (nhosts == 0) {
		e_save = list_all(opt_architecture, opt_domainname,
			request_op, pa);
	} else if (opt_use_metadb) {
		e_save = list(nhosts, hosts, request_op, pa);
	} else {
		e_save = list_without_metadb(nhosts, hosts, opt_port,
		    request_op, pa);
	}
	e = gfarm_paraccess_free(pa);
	if (e_save == GFARM_ERR_NO_ERROR)
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
	    "[-lMH] [-P <path>] [-a <architecture>] [-D <domainname>] [-j <concurrency>] [-iruv]",
	    program_name,
	    "-c  -a <architecture>  [-P <path>] [-n <ncpu>] [-p <port>] [-f <flags>] <hostname> [<hostalias>...]",
	    program_name,
	    "-m [-a <architecture>] [-P <path>] [-n <ncpu>] [-p <port>] [-f <flags>] [-A] <hostname> [<hostalias>...]",
	    program_name, "-d [-P <path>] <hostname>...",
	    program_name, "-R [-P <path>]");
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
#define DEFAULT_OPT_PATH "."

int
main(int argc, char **argv)
{
	int argc_save = argc;
	char **argv_save = argv;
	gfarm_error_t e, e_save = GFARM_ERR_NO_ERROR;
	char opt_operation = '\0'; /* default operation */
	int opt_concurrency = DEFAULT_CONCURRENCY;
	int opt_alter_aliases = 0;
	const char *opt_path = DEFAULT_OPT_PATH;
	char *realpath = NULL;
	char *opt_architecture = NULL, *opt_domainname = NULL;
	long opt_ncpu = 0;
	int opt_port = 0, opt_flags = -1;
	int opt_plain_order = 0; /* i.e. do not sort */
	int opt_sort_by_loadavg = 0;
	int i, c, opt_use_metadb = 1;
	char *s;

	if (argc > 0)
		program_name = basename(argv[0]);
	while ((c = getopt(argc, argv, "AD:HLMP:RUa:cdf:ij:lmn:p:ruv?"))
	    != -1) {
		switch (c) {
		case 'A':
			opt_alter_aliases = 1;
			break;
		case 'L':
			opt_sort_by_loadavg = 1;
			break;
		case 'P':
			opt_path = optarg;
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
			s = validate_architecture(opt_architecture);
			if (s != NULL) {
				fprintf(stderr, "%s: "
				    "invalid character '%c' in \"-a %s\"\n",
				    program_name, *s, opt_architecture);
				exit(1);
			}
			break;
		case 'D':
			opt_domainname = optarg;
			s = validate_hostname(opt_domainname);
			if (s != NULL) {
				fprintf(stderr, "%s: "
				    "invalid character '%c' in \"-a %s\"\n",
				    program_name, *s, opt_domainname);
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
		case 'f':
			opt_flags = parse_opt_long(optarg, c, "<flags>");
			break;
		case 'n':
			opt_ncpu = parse_opt_long(optarg, c, "<ncpu>");
			break;
		case 'p':
			opt_port = parse_opt_long(optarg, c, "<port>");
			break;
		case 'r':
			output_sort_reverse = 1;
			break;
		case 'U':
			opt_udp_only = 1;
			break;
		case 'u':
			opt_plain_order = 1;
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
		if (opt_ncpu == 0)
			opt_ncpu = 1;
		if (opt_flags == -1)
			opt_flags = 0;
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
		s = validate_hostname(argv[i]);
		if (s != NULL) {
			fprintf(stderr, "%s: "
			    "invalid character '%c' in hostname \"%s\"\n",
			    program_name, *s, argv[i]);
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
		 */
		opt_use_metadb = 0;
		opt_resolv_addr = resolv_addr_without_metadb;
	} else if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(1);
	}

	e = gfarm_realpath_by_gfarm2fs(opt_path, &realpath);
	if (e == GFARM_ERR_NO_ERROR)
		opt_path = realpath;

	if (!opt_use_metadb)
		; /* nothing to do */
	else if (strcmp(opt_path, DEFAULT_OPT_PATH) == 0) {
		char *user = NULL;

		e = gfarm_get_global_username_by_host_for_connection_cache(
		    gfarm_ctxp->metadb_server_name,
		    gfarm_ctxp->metadb_server_port, &user);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "gfarm_get_global_username_by_host_"
			    "for_connection_cache: %s", gfarm_error_string(e));
			exit(1);
		}
		if ((e = gfm_client_connection_acquire(
		    gfarm_ctxp->metadb_server_name,
		    gfarm_ctxp->metadb_server_port, user, &gfm_server))
		    != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "metadata server `%s', port %d: %s\n",
			    gfarm_ctxp->metadb_server_name,
			    gfarm_ctxp->metadb_server_port,
			    gfarm_error_string(e));
			free(user);
			exit(1);
		}
		free(user);
	} else {
		if ((e = gfm_client_connection_and_process_acquire_by_path(
		    opt_path, &gfm_server)) != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "%s: metadata server for \"%s\": %s\n",
			    program_name, opt_path, gfarm_error_string(e));
			exit(1);
		}
	}

	free(realpath);

	switch (opt_operation) {
	case OP_CREATE_ENTRY:
		if (argc <= 0)
			usage();
		if (opt_port == 0) {
			fprintf(stderr, "%s: option -p <port> is "
			    "mandatory with -c\n", program_name);
			usage();
		}
		e_save = add_host(argv[0], opt_port, &argv[1],
		    opt_architecture, opt_ncpu, opt_flags);
		if (e_save != GFARM_ERR_NO_ERROR)
			fprintf(stderr, "%s: %s: %s\n", program_name,
			    argv[0], gfarm_error_string(e_save));
		break;
	case OP_MODIFY_ENTRY:
		if (argc <= 0)
			usage();
		e_save = gfarm_modify_host(argv[0], opt_port, &argv[1],
		    opt_architecture, opt_ncpu, opt_flags,
		    !opt_alter_aliases);
		if (e_save != GFARM_ERR_NO_ERROR)
			fprintf(stderr, "%s: %s: %s\n", program_name,
			    argv[0], gfarm_error_string(e_save));
		break;
	case OP_DELETE_ENTRY:
		if (argc <= 0)
			usage();
		for (i = 0; i < argc; i++) {
			e = gfm_client_host_info_remove(gfm_server,
			    argv[i]);
			if (e != GFARM_ERR_NO_ERROR) {
				fprintf(stderr, "%s: %s\n", argv[i],
				    gfarm_error_string(e));
				if (e_save == GFARM_ERR_NO_ERROR)
					e_save = e;
			}
		}
		break;
	case OP_REGISTER_DB:
		if (argc > 0) {
			fprintf(stderr, "%s: too many argument: %s\n",
			    program_name, argv[0]);
			usage();
		}
		e_save = register_db();
		break;
	case OP_LIST_GFSD_INFO:
		e_save = paraccess_list(opt_concurrency, opt_udp_only,
		    opt_architecture, opt_domainname, opt_port,
		    opt_plain_order, opt_sort_by_loadavg,
		    opt_use_metadb, argc, argv,
		    request_gfsd_info, callback_gfsd_info);
		break;
	case OP_NODENAME:
		e_save = paraccess_list(opt_concurrency, opt_udp_only,
		    opt_architecture, opt_domainname, opt_port,
		    opt_plain_order, opt_sort_by_loadavg,
		    opt_use_metadb, argc, argv,
		    request_nodename, callback_nodename);
		break;
	case OP_LIST_LONG:
		e_save = paraccess_list(opt_concurrency, opt_udp_only,
		    opt_architecture, opt_domainname, opt_port,
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

	if (opt_use_metadb)
		gfm_client_connection_free(gfm_server);

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(1);
	}
	exit(e_save == GFARM_ERR_NO_ERROR ? 0 : 1);
}
