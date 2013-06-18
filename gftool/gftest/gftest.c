/*
 * $Id$
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <strings.h>
#include <gfarm/gfarm.h>
#include "gfm_client.h"
#include "lookup.h"
#include "host.h"
#include "gfarm_path.h"

static char *program_name = "gftest";

static int
usage()
{
#if 0
	fprintf(stderr,	"Usage: %s [-defhrswxLR] file\n", program_name);
#else
	fprintf(stderr,	"Usage: %s [-defhsLR] file\n", program_name);
#endif
	exit(EXIT_FAILURE);
}

static void
error_check(char *msg, gfarm_error_t e)
{
	if (e == GFARM_ERR_NO_ERROR)
		return;

	fprintf(stderr, "%s: %s\n", msg, gfarm_error_string(e));
	exit(1);
}

static void
conflict_check(int *mode_ch_p, int ch)
{
	if (*mode_ch_p) {
		fprintf(stderr, "%s: -%c option conflicts with -%c\n",
			program_name, ch, *mode_ch_p);
		usage();
	}
	*mode_ch_p = ch;
}

static gfarm_error_t
is_local_file(char *file, struct gfs_stat *st)
{
	struct gfm_connection *gfm_server;
	int port, ncopy, i;
	char *host, **copy;
	gfarm_error_t e;

	if (!GFARM_S_ISREG(st->st_mode))
		return (GFARM_ERR_NOT_A_REGULAR_FILE);

	e = gfm_client_connection_and_process_acquire_by_path(
		file, &gfm_server);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	e = gfs_replica_list_by_name(file, &ncopy, &copy);
	if (e != GFARM_ERR_NO_ERROR)
		goto free_gfm_server;

	e = gfm_host_get_canonical_self_name(gfm_server, &host, &port);
	if (e != GFARM_ERR_NO_ERROR)
		goto free_copy;

	for (i = 0; i < ncopy; ++i) {
		if (strcasecmp(host, copy[i]) == 0) {
			e = GFARM_ERR_NO_ERROR;
			break;
		}
	}
	if (i == ncopy)
		e = GFARM_ERR_NO_SUCH_OBJECT;
free_copy:
	gfarm_strings_free_deeply(ncopy, copy);
free_gfm_server:
	gfm_client_connection_free(gfm_server);
	return (e);
}

int
main(int argc, char *argv[])
{
	gfarm_error_t e;
	char *file = NULL, *realpath = NULL;
	struct gfs_stat st;
	int ch, mode_ch = 0, ret = 1;

#if 0
	while ((ch = getopt(argc, argv, "d:e:f:h:r:s:w:x:L:R:?")) != -1) {
#else
	while ((ch = getopt(argc, argv, "d:e:f:h:s:L:R:?")) != -1) {
#endif
		switch (ch) {
		case 'd': conflict_check(&mode_ch, ch);	file = optarg; break;
		case 'e': conflict_check(&mode_ch, ch);	file = optarg; break;
		case 'f': conflict_check(&mode_ch, ch);	file = optarg; break;
		case 'h': conflict_check(&mode_ch, ch);	file = optarg; break;
#if 0
		case 'r': conflict_check(&mode_ch, ch);	file = optarg; break;
#endif
		case 's': conflict_check(&mode_ch, ch);	file = optarg; break;
#if 0
		case 'w': conflict_check(&mode_ch, ch);	file = optarg; break;
		case 'x': conflict_check(&mode_ch, ch);	file = optarg; break;
#endif
		case 'L': conflict_check(&mode_ch, ch);	file = optarg; break;
		case 'R': conflict_check(&mode_ch, ch);	file = optarg; break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (file == NULL || mode_ch == 0 || argc > 0)
		usage();

	e = gfarm_initialize(&argc, &argv);
	error_check("gfarm_initialize", e);

	e = gfarm_realpath_by_gfarm2fs(file, &realpath);
	if (e == GFARM_ERR_NO_ERROR)
		file = realpath;

	e = gfs_lstat(file, &st);
	if (e != GFARM_ERR_NO_ERROR) {
		free(realpath);
		return (1); /* not exist */
	}
	switch (mode_ch) {
	case 'd': ret = GFARM_S_ISDIR(st.st_mode); break;
	case 'e': ret = 1;			   break;
	case 'f': ret = GFARM_S_ISREG(st.st_mode); break;
	case 'h': ret = GFARM_S_ISLNK(st.st_mode); break;
#if 0
	case 'r': ret = gfs_access(file, R_OK) == GFARM_ERR_NO_ERROR; break;
#endif
	case 's': ret = st.st_size > 0;		   break;
#if 0
	case 'w': ret = gfs_access(file, W_OK) == GFARM_ERR_NO_ERROR; break;
	case 'x': ret = gfs_access(file, X_OK) == GFARM_ERR_NO_ERROR; break;
#endif
	case 'L':
		ret = GFARM_S_ISREG(st.st_mode) &&
		    (is_local_file(file, &st) == GFARM_ERR_NO_ERROR); break;
	case 'R':
		ret = GFARM_S_ISREG(st.st_mode) &&
		    (is_local_file(file, &st) != GFARM_ERR_NO_ERROR); break;
	default:
		break;
	}
	gfs_stat_free(&st);
	free(realpath);

	e = gfarm_terminate();
	error_check("gfarm_terminate", e);

	return (!ret);
}
