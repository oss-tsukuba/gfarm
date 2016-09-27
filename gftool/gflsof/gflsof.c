#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>

#include <gfarm/gfarm.h>

#include "hash.h"

#include "gfm_proto.h"
#include "gfm_client.h"
#include "lookup.h"
#include "gfarm_path.h"

#define HOSTNAME_HASHTAB_SIZE	3079	/* prime number */
#define BUF_LEN			1024

enum operation_mode {
	OP_CLIENT_LIST = 'C',
	OP_GFSD_LIST = 'G',
	OP_DEFAULT = '\0'
};

static const char OPT_USER_ALL[] = "";
static const char *program_name = "gflsof";

static struct gfarm_hash_table *hostname_hashtab;

static void
hostname_hashtab_init(void)
{
	hostname_hashtab =
	    gfarm_hash_table_alloc(HOSTNAME_HASHTAB_SIZE,
		gfarm_hash_casefold, gfarm_hash_key_equal_casefold);
	if (hostname_hashtab == NULL) {
		fprintf(stderr, "%s: no memory for %d entry hash table\n",
		    program_name, HOSTNAME_HASHTAB_SIZE);
		exit(EXIT_FAILURE);
	}
}

static int
hostname_hashtab_enter(const char *str)
{
	int created;

	if (gfarm_hash_enter(hostname_hashtab, str, strlen(str) + 1, 1,
	    &created) == NULL) {
		fprintf(stderr, "%s: no memory to record `%s'\n",
		    program_name, str);
		exit(EXIT_FAILURE);
	}
	return (created);
}

static void
print_client_list(int nfds, struct gfarm_process_fd_info *fd_info)
{
	int i;
	struct gfarm_process_fd_info *fdi;

	hostname_hashtab_init();

	for (i = 0; i < nfds; i++) {
		fdi = &fd_info[i];
		if (hostname_hashtab_enter(fdi->fd_client_host))
			printf("%s\n", fdi->fd_client_host);
	}
}

static void
print_gfsd_list(int nfds, struct gfarm_process_fd_info *fd_info)
{
	int i;
	struct gfarm_process_fd_info *fdi;

	hostname_hashtab_init();

	for (i = 0; i < nfds; i++) {
		fdi = &fd_info[i];
		if (hostname_hashtab_enter(fdi->fd_gfsd_host))
			printf("%s\n", fdi->fd_gfsd_host);
	}
}

static void
set_lenmax(int *lenmax, const char *s)
{
	int len = strlen(s);

	if (*lenmax < len)
		*lenmax = len;
}

static void
print_fds(int nfds, struct gfarm_process_fd_info *fd_info)
{
	int i;
	struct gfarm_process_fd_info *fdi;
	int user_width = 4; /* == strlen("USER"), and so on... */
	int gpid_width = 4;
	int fd_width = 2;
	int inode_width = 5;
	int gen_width = 3;
	int off_width = 8; /* == strlen("SIZE/OFF") */
	int client_width = 6;
	int gfsd_width = 4;
	char buf[BUF_LEN];

	for (i = 0; i < nfds; i++) {
		fdi = &fd_info[i];
		snprintf(buf, sizeof buf, "%s", fdi->fd_user);
		set_lenmax(&user_width, buf);
		snprintf(buf, sizeof buf, "%llu", (long long)fdi->fd_pid);
		set_lenmax(&gpid_width, buf);
		snprintf(buf, sizeof buf, "%u", (int)fdi->fd_fd);
		set_lenmax(&fd_width, buf);
		snprintf(buf, sizeof buf, "%llu", (long long)fdi->fd_ino);
		set_lenmax(&inode_width, buf);
		snprintf(buf, sizeof buf, "%llu", (long long)fdi->fd_gen);
		set_lenmax(&gen_width, buf);
		snprintf(buf, sizeof buf, "%llu", (long long)fdi->fd_off);
		set_lenmax(&off_width, buf);
		snprintf(buf, sizeof buf, "%s:%u",
		    fdi->fd_client_host, (int)fdi->fd_client_port);
		set_lenmax(&client_width, buf);
#if 0
		snprintf(buf, sizeof buf, "%s:%u:%u",
		    fdi->fd_gfsd_host, (int)fdi->fd_gfsd_port,
		    (int)fdi->fd_gfsd_peer_port);
#else
		snprintf(buf, sizeof buf, "%s:%u",
		    fdi->fd_gfsd_host, (int)fdi->fd_gfsd_peer_port);
#endif
		set_lenmax(&gfsd_width, buf);
	}

	printf("%*s %*s %*s %4s %*s %*s %*s %*s %*s\n",
	    -user_width, "USER",
	    gpid_width, "GPID",
	    fd_width, "FD",
	    "TYPE",
	    inode_width, "INODE",
	    gen_width, "GEN",
	    off_width, "SIZE/OFF",
	    -client_width, "CLIENT",
	    -gfsd_width, "GFSD");
	for (i = 0; i < nfds; i++) {
		fdi = &fd_info[i];
		printf("%*s %*llu %*u",
		       -user_width, fdi->fd_user,
		       gpid_width, (long long)fdi->fd_pid,
		       fd_width, (int)fdi->fd_fd);
		switch (fdi->fd_open_flags & GFARM_FILE_ACCMODE) {
		case GFARM_FILE_RDONLY:
			putchar('r'); break;
		case GFARM_FILE_WRONLY:
			putchar('w'); break;
		case GFARM_FILE_RDWR:
			putchar('u'); break;
		default: /* LOOKUP */
			putchar(' '); break;
		}
		switch (fdi->fd_mode & GFARM_S_IFMT) {
		case GFARM_S_IFDIR:
			printf(" DIR"); break;
		case GFARM_S_IFREG:
			printf(" REG"); break;
		case GFARM_S_IFLNK:
			printf(" LNK"); break;
		default:
			printf(" %03o", (fdi->fd_mode & GFARM_S_IFMT) >> 12);
			break;
		}
		printf(" %*llu %*llu %*llu",
		    inode_width, (long long)fdi->fd_ino,
		    gen_width, (long long)fdi->fd_gen,
		    off_width, (long long)fdi->fd_off);

		if (fdi->fd_client_host[0] != '\0')
			snprintf(buf, sizeof buf, "%s:%u",
			    fdi->fd_client_host, (int)fdi->fd_client_port);
		else
			snprintf(buf, sizeof buf, "-");
		printf(" %*s", -client_width, buf);

		if (fdi->fd_gfsd_host[0] != '\0')
#if 0
			snprintf(buf, sizeof buf, "%s:%u:%u",
			    fdi->fd_gfsd_host, (int)fdi->fd_gfsd_port,
			    (int)fdi->fd_gfsd_peer_port);
#else
			snprintf(buf, sizeof buf, "%s:%u",
			    fdi->fd_gfsd_host, (int)fdi->fd_gfsd_peer_port);
#endif
		else
			snprintf(buf, sizeof buf, "-");
		printf(" %*s\n", -gfsd_width, buf);
	}
}

static void
usage(void)
{
	fprintf(stderr,
	    "Usage: %s [-ACGW] [-D <gfsd_domain>] [-U <domain_of_user_host>] "
	    "[-P <path>] [-u <user>]\n",
	    program_name);
	exit(2);
}

static void
check_and_set_op(enum operation_mode *op_modep, enum operation_mode op)
{
	if (*op_modep != OP_DEFAULT) {
		fprintf(stderr, "%s: -%c option conflicts with -%c\n",
		    program_name, op, *op_modep);
		usage();
		/*NOTREACHED*/
	}
	*op_modep = op;
}

static void
check_and_set_user(const char **userp, const char *arg)
{
	if (*userp != NULL) {
		if (strcmp(*userp, OPT_USER_ALL) == 0)
			fprintf(stderr, "%s: -u %s option conflicts "
			    "with -A\n", program_name, arg);
		else
			fprintf(stderr, "%s: -u %s option conflicts "
			    "with -u %s\n", program_name, arg, *userp);
		usage();
		/*NOTREACHED*/
	}
	*userp = arg;
}

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	int exit_code = EXIT_SUCCESS;
	struct gfm_connection *gfm_server = NULL;
	enum operation_mode op_mode = OP_DEFAULT;
	char *opt_gfsd_domain = "";
	char *opt_user_host_domain = "";
	const char *opt_user = NULL;
	gfarm_uint64_t opt_flags = 0;

	static const char OPT_PATH_DEFAULT[] = ".";
	const char *opt_path = OPT_PATH_DEFAULT;
	char *realpath = NULL;

	int c, nfds;
	struct gfarm_process_fd_info *fd_info;

	if (argc > 0)
		program_name = basename(argv[0]);

	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}

	while ((c = getopt(argc, argv, "CGAD:P:U:Wu:?")) != -1) {
		switch (c) {
		case OP_CLIENT_LIST:
			check_and_set_op(&op_mode, c);
			opt_flags |=
			    GFM_PROTO_PROCESS_FD_FLAG_EXCLUDE_CLIENT_DETACH;
			break;
		case OP_GFSD_LIST:
			check_and_set_op(&op_mode, c);
			opt_flags |=
			    GFM_PROTO_PROCESS_FD_FLAG_EXCLUDE_GFSD_DETACH |
			    (GFM_PROTO_PROCESS_FD_FLAG_EXCLUDE_INODE_ALLMASK ^
			     GFM_PROTO_PROCESS_FD_FLAG_EXCLUDE_INODE_REG);
			break;
		case 'A':
			check_and_set_user(&opt_user, OPT_USER_ALL);
			break;
		case 'D':
			opt_gfsd_domain = optarg;
			opt_flags |=
			    GFM_PROTO_PROCESS_FD_FLAG_EXCLUDE_GFSD_DETACH |
			    (GFM_PROTO_PROCESS_FD_FLAG_EXCLUDE_INODE_ALLMASK ^
			     GFM_PROTO_PROCESS_FD_FLAG_EXCLUDE_INODE_REG);
			break;
		case 'P':
			opt_path = optarg;
			break;
		case 'U':
			opt_user_host_domain = optarg;
			opt_flags |=
			    GFM_PROTO_PROCESS_FD_FLAG_EXCLUDE_CLIENT_DETACH;
			break;
		case 'W':
			opt_flags |=
			    GFM_PROTO_PROCESS_FD_FLAG_EXCLUDE_WRITE_NO_OPEN;
			break;
		case 'u':
			check_and_set_user(&opt_user, optarg);
			break;
		case '?':
		default:
			usage();
			/*NOTREACHED*/
		}
	}
	argc -= optind;
	argv += optind;
	if (argc > 0) {
		fprintf(stderr, "%s: extra operand `%s'\n",
		    program_name, argv[0]);
		usage();
		/*NOTREACHED*/
	}

	if (gfarm_realpath_by_gfarm2fs(opt_path, &realpath)
	    == GFARM_ERR_NO_ERROR)
		opt_path = realpath;
	if ((e = gfm_client_connection_and_process_acquire_by_path(
	    opt_path, &gfm_server)) != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: metadata server for \"%s\": %s\n",
		    program_name, opt_path, gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}
	free(realpath);

	if (opt_user == NULL)
		opt_user = gfm_client_username(gfm_server);

	e = gfm_client_process_fd_info(gfm_server,
	    opt_gfsd_domain, opt_user_host_domain, opt_user, opt_flags,
	    &nfds, &fd_info);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n",
		    program_name, gfarm_error_string(e));
		exit_code = EXIT_FAILURE;
	} else {
		switch (op_mode) {
		case OP_CLIENT_LIST:
			print_client_list(nfds, fd_info);
			break;
		case OP_GFSD_LIST:
			print_gfsd_list(nfds, fd_info);
			break;
		default:
			print_fds(nfds, fd_info);
			break;
		}
		gfarm_process_fd_info_free(nfds, fd_info);
	}

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}
	return (exit_code);
}
