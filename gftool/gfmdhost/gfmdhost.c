/*
 * $Id$
 */

#include <assert.h>
#include <ctype.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>

#include <gfarm/gfarm.h>

#include "metadb_common.h"
#include "metadb_server.h"
#include "gfm_client.h"
#include "gfm_proto.h"
#include "lookup.h"
#include "config.h"
#include "gfarm_path.h"

char *program_name = "gfmdhost";

struct gfm_connection *gfm_conn = NULL;

#define OP_LIST			'\0'	/* '\0' means default operation */
#define OP_LIST_DETAIL		'l'
#define OP_CREATE_ENTRY		'c'
#define OP_MODIFY_ENTRY		'm'
#define OP_DELETE_ENTRY		'd'


static void
usage(void)
{
	fprintf(stderr, "Usage:"
	    "\t%s %s\n" "\t%s %s\n" "\t%s %s\n" "\t%s %s\n",
	    program_name,
	    "[-l] [-P <path>]",
	    program_name,
	    "-c   [-P <path>] "
	    "[-p <port>] [-C <clustername>] [-t <m|c|s>] <hostname>",
	    program_name,
	    "-m   [-P <path>] "
	    "[-p <port>] [-C <clustername>] [-t <m|c|s>] <hostname>",
	    program_name,
	    "-d   [-P <path>] <hostname> ...");
	exit(EXIT_FAILURE);
}

static gfarm_error_t
do_set_or_modify(int op, const char *hostname, int port,
	const char *clustername, int cname_is_set, int is_def_master,
	int is_master_candidate, struct gfarm_metadb_server *ms,
	const char *diag)
{
	gfarm_error_t e;
	gfarm_error_t (*rpc_op)(struct gfm_connection *,
		struct gfarm_metadb_server *);

	if (ms->name == NULL) {
		ms->name = strdup(hostname);
		if (ms->name == NULL)
			return (GFARM_ERR_NO_MEMORY);
	}
	if (port >= 0)
		ms->port = port;
	if (cname_is_set) {
		if (clustername == NULL)
			clustername = "";
		free(ms->clustername);
		ms->clustername = strdup(clustername);
		if (ms->clustername == NULL)
			return (GFARM_ERR_NO_MEMORY);
	}
	if (is_def_master >= 0)
		gfarm_metadb_server_set_is_default_master(ms, is_def_master);
	if (is_master_candidate >= 0)
		gfarm_metadb_server_set_is_master_candidate(ms,
			is_master_candidate);
	rpc_op = op == OP_CREATE_ENTRY ?
	    gfm_client_metadb_server_set :
	    gfm_client_metadb_server_modify;
	if ((e = rpc_op(gfm_conn, ms)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: rpc failed: %s", diag, gfarm_error_string(e));
	}
	return (e);
}

static gfarm_error_t
do_set(const char *hostname, int port, const char *clustername,
	int is_def_master, int is_master_candidate)
{
	gfarm_error_t e;
	struct gfarm_metadb_server ms;

	memset(&ms, 0, sizeof(ms));
	if (port == -1)
		port = GFMD_DEFAULT_PORT;
	if (is_master_candidate == -1)
		is_master_candidate = 1;

	e = do_set_or_modify(OP_CREATE_ENTRY, hostname, port, clustername,
	    1, is_def_master, is_master_candidate, &ms, "do_set");
	gfarm_metadb_server_free(&ms);

	return (e);
}

static gfarm_error_t
do_modify(const char *hostname, int port, const char *clustername,
	int cname_is_set, int is_def_master, int is_master_candidate)
{
	gfarm_error_t e;
	struct gfarm_metadb_server ms;

	if ((e = gfm_client_metadb_server_get(gfm_conn, hostname, &ms))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s", gfarm_error_string(e));
		return (e);
	}
	e = do_set_or_modify(OP_MODIFY_ENTRY, hostname, port, clustername,
	    cname_is_set, is_def_master, is_master_candidate, &ms,
	    "do_modify");
	gfarm_metadb_server_free(&ms);

	return (e);
}

static gfarm_error_t
do_remove(int n, const char **hostnames)
{
	int i;
	const char *hostname;
	gfarm_error_t e, e2 = GFARM_ERR_NO_ERROR;

	for (i = 0; i < n; ++i) {
		hostname = hostnames[i];
		if ((e = gfm_client_metadb_server_remove(gfm_conn,
		    hostname)) != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "%s: %s: %s\n",
			    program_name, hostname, gfarm_error_string(e));
			if (e2 == GFARM_ERR_NO_ERROR)
				e2 = e;
		}
	}

	return (e2);
}

static int
compare_metadb_server(const void *a, const void *b)
{
	const struct gfarm_metadb_server *ma =
		*(const struct gfarm_metadb_server **)a;
	const struct gfarm_metadb_server *mb =
		*(const struct gfarm_metadb_server **)b;

	return (strcmp(ma->name, mb->name));
}

static int
compare_metadb_server_detail(const void *a, const void *b)
{
	const struct gfarm_metadb_server *ma =
		*(const struct gfarm_metadb_server **)a;
	const struct gfarm_metadb_server *mb =
		*(const struct gfarm_metadb_server **)b;
	int cla = ma->clustername ? strlen(ma->clustername) : 0;
	int clb = mb->clustername ? strlen(mb->clustername) : 0;
	int c;

	if (cla == 0 && clb > 0)
		return (-1);
	if (cla > 0 && clb == 0)
		return (1);
	if (cla > 0 && clb > 0 &&
	    (c = strcmp(ma->clustername, mb->clustername)) != 0)
		return (c);
	return (strcmp(ma->name, mb->name));
}

static int
gfarm_metadb_server_get_state_symbol(struct gfarm_metadb_server *ms)
{
	if (gfarm_metadb_server_seqnum_is_out_of_sync(ms))
		return ('x'); /* ignore gfarm_metadb_server_is_active() */
	else if (gfarm_metadb_server_seqnum_is_error(ms))
		return ('e'); /* ignore gfarm_metadb_server_is_active() */
	else if (!gfarm_metadb_server_is_active(ms))
		return ('-');
	else if (gfarm_metadb_server_seqnum_is_ok(ms))
		return ('+');
	else /* if (gfarm_metadb_server_seqnum_is_unknown(ms)) */
		return ('?');
}

static gfarm_error_t
do_list(int detail)
{
	gfarm_error_t e;
	int i, n;
	struct gfarm_metadb_server *ms, *mss, **pmss;

	if ((e = gfm_client_metadb_server_get_all(gfm_conn, &n, &mss))
	    != GFARM_ERR_NO_ERROR) {
		return (e);
	}
	if (n == 0)
		return (GFARM_ERR_NO_ERROR);

	GFARM_MALLOC_ARRAY(pmss, n);

	for (i = 0; i < n; ++i)
		pmss[i] = &mss[i];
	qsort(pmss, n, sizeof(*pmss), detail ?
		compare_metadb_server_detail : compare_metadb_server);

	for (i = 0; i < n; ++i) {
		ms = pmss[i];
		if (detail) {
			printf("%c %-6s %-5s %c %-12s %s %d\n",
			    gfarm_metadb_server_get_state_symbol(ms),
			    gfarm_metadb_server_is_master(ms) ?
				"master" : "slave",
			    gfarm_metadb_server_is_master(ms) ? "-" :
			    (gfarm_metadb_server_is_sync_replication(ms) ?
				"sync" : "async"),
			    gfarm_metadb_server_is_default_master(ms) ? 'm' :
			    (gfarm_metadb_server_is_master_candidate(ms) ?
				'c' : 's'),
			    strlen(ms->clustername) == 0 ?
				"(default)" : ms->clustername,
			    ms->name, ms->port);
		} else {
		    printf("%s\n", ms->name);
		}
	}
	free(pmss);
	gfarm_metadb_server_free_all(n, mss);

	return (GFARM_ERR_NO_ERROR);
}

static void
inconsistent_option(int c1, int c2)
{
	fprintf(stderr, "%s: inconsistent option -%c and -%c\n",
	    program_name, c1, c2);
	usage();
}

static void
validate_hostname_or_exit(char *hostname)
{
	unsigned char c;
	unsigned char *p = NULL, *s = (unsigned char *)hostname;

	if (hostname == NULL) {
		fprintf(stderr, "%s: missing hostname\n", program_name);
		exit(EXIT_FAILURE);
	}

	while ((c = *s++) != '\0') {
		if (!isalnum(c) && c != '-' && c != '.') {
			p = s - 1;
			break;
		}
	}
	if (p) {
		fprintf(stderr, "%s: "
		    "invalid character '%c' in \"-a %s\"\n",
		    program_name, *p, hostname);
		exit(EXIT_FAILURE);
	}
}

static void
validate_clustername_or_exit(char *clustername)
{
	unsigned char c;
	unsigned char *p = NULL, *s = (unsigned char *)clustername;

	while ((c = *s++) != '\0') {
		if (!isalnum(c) && c != '-' && c != '_' && c != '.') {
			p = s - 1;
			break;
		}
	}
	if (p) {
		fprintf(stderr, "%s: "
		    "invalid character '%c' in \"-a %s\"\n",
		    program_name, *p, clustername);
		exit(EXIT_FAILURE);
	}
}

static long
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

int
main(int argc, char **argv)
{
	int argc_save = argc;
	char **argv_save = argv;
	gfarm_error_t e, e2;
	char opt_operation = '\0'; /* default operation */
	const char *opt_path = ".";
	char *realpath = NULL, *opt_clustername = NULL;
	int cname_is_set = 0, opt_port = -1, opt_def_master = -1;
	int opt_master_candidate = -1;
	int i, c;

	if (argc > 0)
		program_name = basename(argv[0]);
	while ((c = getopt(argc, argv, "C:P:cdlmp:t:?"))
	    != -1) {
		switch (c) {
		case 'C':
			opt_clustername = optarg;
			cname_is_set = 1;
			validate_clustername_or_exit(opt_clustername);
			break;
		case 't':
			if (strcmp(optarg, "m") == 0)
				opt_def_master = 1;
			else if (strcmp(optarg, "c") == 0)
				opt_master_candidate = 1;
			else if (strcmp(optarg, "s") == 0)
				opt_master_candidate = 0;
			else {
				fprintf(stderr,
				    "%s: invalid argument after option -%c.",
				    program_name, c);
				exit(EXIT_FAILURE);
			}
			break;
		case 'P':
			opt_path = optarg;
			break;
		case 'c':
		case 'd':
		case 'l':
		case 'm':
			if (opt_operation != '\0' && opt_operation != c)
				inconsistent_option(opt_operation, c);
			opt_operation = c;
			break;
		case 'p':
			opt_port = parse_opt_long(optarg, c, "<port>");
			break;
		case '?':
			usage();
		}
	}

	if (opt_operation != OP_CREATE_ENTRY &&
	    opt_operation != OP_MODIFY_ENTRY &&
	    (opt_clustername || opt_port != -1 || opt_def_master != -1)) {
		fprintf(stderr, "%s: option -t is only available with "
		    "-%c or -%c\n", program_name,
		    OP_CREATE_ENTRY, OP_MODIFY_ENTRY);
		usage();
	}

	argc -= optind;
	argv += optind;

	switch (opt_operation) {
	case OP_CREATE_ENTRY:
	case OP_MODIFY_ENTRY:
		if (argc != 1) {
			fprintf(stderr, "%s: too many host name specified\n",
			    program_name);
			exit(EXIT_FAILURE);
		}
		validate_hostname_or_exit(argv[0]);
		break;
	case OP_DELETE_ENTRY:
		if (argc == 0) {
			fprintf(stderr, "%s: no host name specified\n",
			    program_name);
			exit(EXIT_FAILURE);
		}
		for (i = 0; i < argc; i++)
			validate_hostname_or_exit(argv[i]);
		break;
	case OP_LIST:
	case OP_LIST_DETAIL:
		if (argc > 0) {
			fprintf(stderr, "%s: too many arguments specified\n",
			    program_name);
			exit(EXIT_FAILURE);
		}
		break;
	}

	if ((e2 = gfarm_initialize(&argc_save, &argv_save)) !=
	    GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: failed to initialize: %s\n",
		    program_name, gfarm_error_string(e2));
		exit(EXIT_FAILURE);
	}
	if (gfarm_realpath_by_gfarm2fs(opt_path, &realpath)
	    == GFARM_ERR_NO_ERROR)
		opt_path = realpath;
	if ((e2 = gfm_client_connection_and_process_acquire_by_path(
	    opt_path, &gfm_conn)) != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: metadata server for \"%s\": %s\n",
		    program_name, opt_path, gfarm_error_string(e2));
		exit(EXIT_FAILURE);
	}
	free(realpath);

	switch (opt_operation) {
	case OP_CREATE_ENTRY:
		if ((e = do_set(argv[0], opt_port, opt_clustername,
		    opt_def_master, opt_master_candidate))
		    != GFARM_ERR_NO_ERROR)
			fprintf(stderr, "%s: %s: %s\n", program_name,
			    argv[0], gfarm_error_string(e));
		break;
	case OP_MODIFY_ENTRY:
		if ((e = do_modify(argv[0], opt_port, opt_clustername,
		    cname_is_set, opt_def_master, opt_master_candidate))
		    != GFARM_ERR_NO_ERROR)
			fprintf(stderr, "%s: %s: %s\n", program_name,
			    argv[0], gfarm_error_string(e));
		break;
	case OP_DELETE_ENTRY:
		e = do_remove(argc, (const char **)argv);
		break;
	case OP_LIST:
	case OP_LIST_DETAIL:
		if ((e = do_list(opt_operation == OP_LIST_DETAIL))
		    != GFARM_ERR_NO_ERROR)
			fprintf(stderr, "%s: %s\n", program_name,
			    gfarm_error_string(e));
		break;
	default:
		abort();
	}

	gfm_client_connection_free(gfm_conn);
	e2 = gfarm_terminate();
	if (e2 != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e2));
		exit(EXIT_FAILURE);
	}
	exit(e == GFARM_ERR_NO_ERROR ? 0 : 1);
}
