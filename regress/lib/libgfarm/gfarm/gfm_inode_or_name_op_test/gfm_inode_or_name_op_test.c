#undef NDEBUG

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <unistd.h>
#include <memory.h>
#include <limits.h>

#ifndef NAME_MAX /* Solaris 5.9 doesn't define this */
#define NAME_MAX	1024
#endif

#define GFARM_INTERNAL_USE
#include <gfarm/gfarm.h>

#include "context.h"
#include "gfm_client.h"
#include "lookup.h"

#define EXIT_OK			0
#define EXIT_ERR		1
#define EXIT_PATH_ROOT		2
#define EXIT_NO_SUCH_FILE	3
#define EXIT_CROSS_DEVICE	4
#define EXIT_MANY_LVL_OF_SYMLNK	5
#define EXIT_OPE_NOT_PERM	6
#define EXIT_USAGE		250

char *program_name = "gfm_inode_or_name_op";

#define OP_INODE_OP	'I'
#define OP_INODE_OP_NF	'J'
#define OP_NAME_OP	'N'
#define OP_NAME2_OP	'P'
#define OP_NAME2_OP_OL	'Q'
#define OP_REALPATH	'R'
#define OP_SHOW_SVR	'S'

struct file_info {
	char name[NAME_MAX];
	gfarm_ino_t ino;
	gfarm_int32_t fd;
};

struct op_closure {
	int request, result, success, cleanup;
	int is_stat_or_open;
	struct file_info f1, f2;
};

#define MAX_OPS	1024

int open_parent = 0;

void
usage(void)
{
	fprintf(stderr,
	    "Usage:\n"
	    "  %s [-IJNS] <filename>\n"
	    "  %s [-PQ] <source filename> <destination filename>\n"
	    "  %s [-S]\n",
	    program_name, program_name, program_name);
	exit(EXIT_USAGE);
}

static gfarm_error_t
get_inonum_request(struct gfm_connection *conn, struct file_info *f,
	int stat_or_open, int open_parent)
{
	gfarm_error_t e;
	const char *errf;
	const char *name;

	assert(strlen(f->name) > 0 || stat_or_open);
	name = open_parent ? "." : f->name;

	if (stat_or_open) {
		if ((e = gfm_client_fstat_request(conn))
		    != GFARM_ERR_NO_ERROR)
			errf = "gfm_client_fstat_request";
	} else if ((e = gfm_client_open_request(conn, name,
		strlen(name), GFARM_FILE_RDONLY)) != GFARM_ERR_NO_ERROR) {
		errf = "gfm_client_open_request";
	}
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "get_inonum_request : %s : %s\n",
		    errf, gfarm_error_string(e));
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
get_inonum_result(struct gfm_connection *conn, struct file_info *f,
	int stat_or_open, int open_parent)
{
	gfarm_error_t e;
	const char *errf;
	gfarm_ino_t ino;
	gfarm_mode_t mode;
	gfarm_uint64_t gen;
	struct gfs_stat st;

	if (stat_or_open) {
		if ((e = gfm_client_fstat_result(conn, &st))
		    != GFARM_ERR_NO_ERROR)
			errf = "gfm_client_fstat_result";
		else
			f->ino = st.st_ino;
	} else if ((e = gfm_client_open_result(
		    conn, &ino, &gen, &mode)) != GFARM_ERR_NO_ERROR) {
		errf = "gfm_client_open_result";
	} else
		f->ino = ino;

	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "get_inonum_result : %s : %s\n",
		    errf, gfarm_error_string(e));
		return (e);
	}

	return (e);
}

static gfarm_error_t
inode_op_request(struct gfm_connection *conn, void *closure)
{
	gfarm_error_t e;
	struct op_closure *c = (struct op_closure *)closure;

	assert(conn);
	assert(closure);
	((struct op_closure *)closure)->request = 1;
	if ((e = gfm_client_get_fd_request(conn)) != GFARM_ERR_NO_ERROR)
		return (e);
	if ((e = get_inonum_request(conn, &c->f1, 1, open_parent))
	    != GFARM_ERR_NO_ERROR)
		return (e);
	return (gfm_client_close_request(conn));
}

static gfarm_error_t
inode_op_result(struct gfm_connection *conn, void *closure)
{
	gfarm_error_t e;
	struct op_closure *c = (struct op_closure *)closure;

	assert(conn);
	assert(closure);
	c->result = 1;
	if ((e = (gfm_client_get_fd_result(conn, &c->f1.fd)))
	    != GFARM_ERR_NO_ERROR)
		return (e);
	if ((e = get_inonum_result(conn, &c->f1, 1, open_parent))
	    != GFARM_ERR_NO_ERROR)
		return (e);

	return (gfm_client_close_result(conn));
}

static gfarm_error_t
inode_op_success(struct gfm_connection *conn, void *closure, int type,
	const char *path, gfarm_ino_t ino, gfarm_uint64_t igen)
{
	struct op_closure *c = (struct op_closure *)closure;

	assert(conn);
	assert(closure);
	assert(path);
	assert(type == GFS_DT_DIR || type == GFS_DT_LNK ||
		type == GFS_DT_REG || type == GFS_DT_UNKNOWN);
	c->success = 1;
	gfm_client_connection_free(conn);
	return (GFARM_ERR_NO_ERROR);
}

static void
inode_op_cleanup(struct gfm_connection *conn, void *closure)
{
	struct op_closure *c = (struct op_closure *)closure;

	assert(conn);
	assert(closure);
	c->cleanup++;
}

static gfarm_error_t
name_op_request(struct gfm_connection *conn, void *closure,
	const char *name)
{
	gfarm_error_t e;
	struct op_closure *c = (struct op_closure *)closure;

	assert(conn);
	assert(closure);
	assert(name);
	c->request++;
	strcpy(c->f1.name, name);
	if ((e = (gfm_client_get_fd_request(conn))) != GFARM_ERR_NO_ERROR)
		return (e);
	if ((e = get_inonum_request(conn, &c->f1, 0, open_parent))
	    != GFARM_ERR_NO_ERROR)
		return (e);
	return (gfm_client_close_request(conn));
}

static gfarm_error_t
name_op_result(struct gfm_connection *conn, void *closure)
{
	gfarm_error_t e;
	struct op_closure *c = (struct op_closure *)closure;

	assert(conn);
	assert(closure);
	c->result++;
	if ((e = (gfm_client_get_fd_result(conn, &c->f1.fd)))
	    != GFARM_ERR_NO_ERROR)
		return (e);
	if ((e = get_inonum_result(conn, &c->f1, 0, open_parent))
	    != GFARM_ERR_NO_ERROR)
		return (e);
	return (gfm_client_close_result(conn));
}

static gfarm_error_t
name_op_success(struct gfm_connection *conn, void *closure, int type,
	const char *path, gfarm_ino_t ino, gfarm_uint64_t igen)
{
	struct op_closure *c = (struct op_closure *)closure;

	assert(conn);
	assert(closure);
	assert(path);
	c->success++;
	gfm_client_connection_free(conn);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
name2_op_request(struct gfm_connection *conn, void *closure,
	const char *sname, const char *dname)
{
	gfarm_error_t e;
	struct op_closure *c = (struct op_closure *)closure;

	assert(conn);
	assert(closure);
	assert(sname);
	assert(dname);
	c->request++;
	strcpy(c->f1.name, sname);
	strcpy(c->f2.name, dname);
	if ((e = gfm_client_get_fd_request(conn))
	    != GFARM_ERR_NO_ERROR)
		return (e);
	if ((e = get_inonum_request(conn, &c->f2, 0, 1))
	    != GFARM_ERR_NO_ERROR)
		return (e);
	if ((e = gfm_client_restore_fd_request(conn))
	    != GFARM_ERR_NO_ERROR)
		return (e);
	if ((e = gfm_client_get_fd_request(conn))
	    != GFARM_ERR_NO_ERROR)
		return (e);
	if ((e = get_inonum_request(conn, &c->f1, c->is_stat_or_open, 0))
	    != GFARM_ERR_NO_ERROR)
		return (e);
	if ((e = gfm_client_save_fd_request(conn))
	    != GFARM_ERR_NO_ERROR)
		return (e);
	return (gfm_client_open_root_request(conn, GFARM_FILE_RDONLY));
}

static gfarm_error_t
name2_op_ol_request(struct gfm_connection *conn, void *closure,
	const char *dname)
{
	gfarm_error_t e;
	struct op_closure *c = (struct op_closure *)closure;

	assert(conn);
	assert(closure);
	assert(dname);
	c->request++;
	strcpy(c->f1.name, ".");
	strcpy(c->f2.name, dname);
	if ((e = gfm_client_get_fd_request(conn))
	    != GFARM_ERR_NO_ERROR)
		return (e);
	if ((e = get_inonum_request(conn, &c->f2, 0, 1))
	    != GFARM_ERR_NO_ERROR)
		return (e);
	if ((e = gfm_client_restore_fd_request(conn))
	    != GFARM_ERR_NO_ERROR)
		return (e);
	if ((e = (gfm_client_get_fd_request(conn)))
	    != GFARM_ERR_NO_ERROR)
		return (e);
	if ((e = get_inonum_request(conn, &c->f1, c->is_stat_or_open, 0))
	    != GFARM_ERR_NO_ERROR)
		return (e);
	if ((e = gfm_client_save_fd_request(conn))
	    != GFARM_ERR_NO_ERROR)
		return (e);
	return (gfm_client_open_root_request(conn, GFARM_FILE_RDONLY));
}

static gfarm_error_t
name2_op_result(struct gfm_connection *conn, void *closure)
{
	gfarm_error_t e;
	/*
	gfarm_ino_t ino;
	gfarm_uint64_t gen;
	gfarm_mode_t mode;
	*/
	struct op_closure *c = (struct op_closure *)closure;

	assert(conn);
	assert(closure);
	c->result++;
	if ((e = gfm_client_get_fd_result(conn, &c->f2.fd))
	    != GFARM_ERR_NO_ERROR)
		return (e);
	if ((e = get_inonum_result(conn, &c->f2, 0, 1))
	    != GFARM_ERR_NO_ERROR)
		return (e);
	if ((e = gfm_client_restore_fd_result(conn))
	    != GFARM_ERR_NO_ERROR)
		return (e);
	if ((e != gfm_client_get_fd_result(conn, &c->f1.fd))
	    != GFARM_ERR_NO_ERROR)
		return (e);
	if ((e = get_inonum_result(conn, &c->f1, c->is_stat_or_open, 0))
	    != GFARM_ERR_NO_ERROR)
		return (e);
	if ((e = gfm_client_save_fd_result(conn))
	    != GFARM_ERR_NO_ERROR)
		return (e);
	return (gfm_client_open_root_result(conn));
	/*return (gfm_client_open_result(conn, &ino, &gen, &mode));*/
}

static gfarm_error_t
name2_op_success(struct gfm_connection *conn, void *closure)
{
	struct op_closure *c = (struct op_closure *)closure;

	assert(conn);
	assert(closure);
	c->success++;
	gfm_client_connection_free(conn);
	return (GFARM_ERR_NO_ERROR);
}

static void
name2_op_cleanup(struct gfm_connection *conn, void *closure)
{
	struct op_closure *c = (struct op_closure *)closure;
	assert(conn);
	assert(closure);
	c->cleanup++;
}

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	int c, use_conn, flags, op = 0;
	const char *filename, *filename2 = NULL;
	char *path;
	struct op_closure closure;
	struct gfm_connection *conn;
	gfarm_error_t (*inode_request_op)(struct gfm_connection *, void *,
		const char *) = NULL;
	gfarm_error_t (*name_request_op)(struct gfm_connection *, void *,
		const char *, const char *) = NULL;

	if (argc > 0)
		program_name = basename(argv[0]);

	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfarm_initialize: %s\n",
		    gfarm_error_string(e));
		return (EXIT_ERR);
	}

	while ((c = getopt(argc, argv, "IJNPQRSp")) != -1) {
		switch (c) {
		case OP_INODE_OP:
		case OP_INODE_OP_NF:
		case OP_NAME_OP:
		case OP_NAME2_OP:
		case OP_NAME2_OP_OL:
		case OP_REALPATH:
		case OP_SHOW_SVR:
			if (op != 0) {
				fprintf(stderr,
				    "%s : too many options", program_name);
				usage();
			}
			op = c;
			break;
		case 'p': /* for OP_NAME_OP */
			open_parent = 1;
			break;
		default:
			fprintf(stderr, "%s: unknown option -%c\n",
			    program_name, c);
			usage();
		}
	}
	if (optind == 1)
		usage();
	argc -= optind;
	argv += optind;
	switch (op) {
	case OP_NAME2_OP:
	case OP_NAME2_OP_OL:
		use_conn = 1;
		if (argc != 2)
			usage();
		break;
	case OP_SHOW_SVR:
		if (argc != 0)
			usage();
		break;
	default:
		if (argc != 1)
			usage();
		break;
	}
	filename = argv[0];
	if (argc > 0)
		filename2 = argv[1];
	memset(&closure, 0, sizeof(closure));

	if (use_conn &&
	    (e = gfm_client_connection_and_process_acquire_by_path(
		    "/", &conn)) != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s",
		    program_name, gfarm_error_string(e));
		exit(1);
	}

	switch (op) {
	case OP_INODE_OP:
		if ((e = gfm_inode_op_modifiable(filename,
		    GFARM_FILE_RDONLY,
		    inode_op_request, inode_op_result,
		    inode_op_success, inode_op_cleanup, NULL, &closure))
		    != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "gfm_inode_op : %s\n",
			    gfarm_error_string(e));
			break;
		}
		assert(closure.request);
		assert(closure.result);
		assert(closure.success);
		assert(closure.cleanup == 0);
		printf("%ld\n", (long)closure.f1.ino);
		break;
	case OP_INODE_OP_NF:
		if ((e = gfm_inode_op_no_follow_modifiable(filename,
		    GFARM_FILE_RDONLY,
		    inode_op_request, inode_op_result,
		    inode_op_success, inode_op_cleanup,
		    NULL, &closure))
		    != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "gfm_inode_op_no_follow : %s\n",
			    gfarm_error_string(e));
			break;
		}
		assert(closure.request);
		assert(closure.result);
		assert(closure.success);
		assert(closure.cleanup == 0);
		printf("%ld\n", (long)closure.f1.ino);
		break;
	case OP_NAME_OP:
		if ((e = gfm_name_op_modifiable(filename,
		    GFARM_ERR_OPERATION_NOT_PERMITTED,
		    name_op_request, name_op_result,
		    name_op_success, NULL, &closure))
		    != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "gfm_name_op : %s\n",
			    gfarm_error_string(e));
			break;
		}
		assert(closure.request);
		assert(closure.result);
		assert(closure.success);
		assert(closure.cleanup == 0);
		assert(strlen(closure.f1.name) > 0);
		assert(closure.f1.ino > 0);
		printf("%ld %s\n", (long)closure.f1.ino,
		    closure.f1.name);
		break;
	case OP_NAME2_OP:
	case OP_NAME2_OP_OL:
		if (op == OP_NAME2_OP) {
			flags = 0;
			name_request_op = name2_op_request;
		} else {
			flags = GFARM_FILE_SYMLINK_NO_FOLLOW |
			    GFARM_FILE_OPEN_LAST_COMPONENT;
			closure.is_stat_or_open = 1;
			inode_request_op = name2_op_ol_request;
		}
		if ((e = gfm_name2_op_modifiable(filename, filename2, flags,
		    inode_request_op, name_request_op,
		    name2_op_result, name2_op_success,
		    name2_op_cleanup, NULL, &closure))
		    != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "gfm_name2_op : %s\n",
			    gfarm_error_string(e));
			break;
		}

		assert(closure.request);
		assert(closure.result);
		assert(closure.success);
		assert(closure.cleanup == 0);
		assert(strlen(closure.f1.name) > 0);
		assert(closure.f1.ino > 0);
		printf("%ld %s %ld %s\n",
		    (long)closure.f1.ino, closure.f1.name,
		    (long)closure.f2.ino, closure.f2.name);
		break;
	case OP_REALPATH:
		if ((e = gfs_realpath(filename, &path))
		    != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "gfs_realpath : %s\n",
			    gfarm_error_string(e));
			break;
		}
		printf("%s\n", path);
		free(path);
		break;
	case OP_SHOW_SVR:
		printf("%s:%d\n", gfarm_ctxp->metadb_server_name,
		    gfarm_ctxp->metadb_server_port);
		e = GFARM_ERR_NO_ERROR;
		break;
	default:
		assert(0);
	}

	if (e != GFARM_ERR_NO_ERROR) {
		switch (e) {
		case GFARM_ERR_PATH_IS_ROOT:
			return (EXIT_PATH_ROOT);
		case GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY:
			return (EXIT_NO_SUCH_FILE);
		case GFARM_ERR_CROSS_DEVICE_LINK:
			return (EXIT_CROSS_DEVICE);
		case GFARM_ERR_TOO_MANY_LEVELS_OF_SYMBOLIC_LINK:
			return (EXIT_MANY_LVL_OF_SYMLNK);
		case GFARM_ERR_OPERATION_NOT_PERMITTED:
			return (EXIT_OPE_NOT_PERM);
		default:
			return (EXIT_ERR);
		}
	}
	if ((e = gfarm_terminate()) != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfarm_terminate: %s\n",
		    gfarm_error_string(e));
		return (EXIT_ERR);
	}
	return (EXIT_OK);
}
