/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"
#include "timer.h"
#include "gfs_profile.h"
#include "metadb_server.h" /* gfarm_host_info_get_by_name_alias */

/* INTERNAL FUNCTION */
gfarm_error_t gfs_pio_internal_set_view_section(
	GFS_File, char *, gfarm_int32_t);

char *program_name = "gfreg";

gfarm_error_t
gfimport(FILE *ifp, GFS_File ogf)
{
	int c;

	while ((c = getc(ifp)) != EOF)
		gfs_pio_putc(ogf, c);
	return (gfs_pio_error(ogf));
}

gfarm_error_t
gfimport_to(FILE *ifp, char *gfarm_url, int mode,
	char *host, gfarm_int32_t port)
{
	gfarm_error_t e, e2;
	GFS_File gf;
	gfarm_timerval_t t1, t2, t3, t4, t5;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t2);
	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t3);
	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t4);
	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t5);

	gfs_profile(gfarm_gettimerval(&t1));
	e = gfs_pio_create(
		gfarm_url, GFARM_FILE_WRONLY|GFARM_FILE_TRUNC, mode, &gf);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", gfarm_url, gfarm_error_string(e));
		return (e);
	}
	gfs_profile(gfarm_gettimerval(&t2));
	e = gfs_pio_internal_set_view_section(gf, host, port);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", gfarm_url, gfarm_error_string(e));
		goto close;
	}
	gfs_profile(gfarm_gettimerval(&t3));

	e = gfimport(ifp, gf);
	if (e != GFARM_ERR_NO_ERROR)
		fprintf(stderr, "writing to %s: %s\n", gfarm_url,
		    gfarm_error_string(e));
	gfs_profile(gfarm_gettimerval(&t4));
 close:
	e2 = gfs_pio_close(gf);
	if (e2 != GFARM_ERR_NO_ERROR)
		fprintf(stderr, "closing %s: %s\n", gfarm_url,
		    gfarm_error_string(e2));
	gfs_profile(gfarm_gettimerval(&t5));
	gfs_profile(fprintf(stderr,
			    "create %g, view %g, import %g, close %g\n",
			    gfarm_timerval_sub(&t2, &t1),
			    gfarm_timerval_sub(&t3, &t2),
			    gfarm_timerval_sub(&t4, &t3),
			    gfarm_timerval_sub(&t5, &t4)));

	return (e != GFARM_ERR_NO_ERROR ? e : e2);
}

gfarm_error_t
gfimport_from_to(const char *ifile, char *gfarm_url,
	char *host, gfarm_int32_t port)
{
	gfarm_error_t e;
	FILE *ifp = fopen(ifile, "r");
	struct stat st;
	int rv;

	if (ifp == NULL) {
		perror(ifile);
		return (GFARM_ERR_CANT_OPEN);
	}
	rv = stat(ifile, &st);
	if (rv == -1) {
		perror("stat");
		return (gfarm_errno_to_error(errno));
	}
	e = gfimport_to(ifp, gfarm_url, st.st_mode & 0777, host, port);
	fclose(ifp);
	return (e);
}

static gfarm_error_t
get_port(char *host, gfarm_int32_t *portp)
{
	gfarm_error_t e;
	struct gfarm_host_info hinfo;

	e = gfarm_host_info_get_by_name_alias(host, &hinfo);
	if (e == GFARM_ERR_NO_ERROR) {
		*portp = hinfo.port;
		gfarm_host_info_free(&hinfo);
	}
	return (e);
}

static void
usage(void)
{
	fprintf(stderr, "Usage: %s [option] <src_file> <dst_gfarm_file>\n",
	    program_name);
	fprintf(stderr, "option:\n");
	fprintf(stderr, "\t%s\n", "-h <hostname>");
	fprintf(stderr, "\t%s\t%s\n", "-p", "turn on profiling");
	fprintf(stderr, "\t%s\t%s\n", "-v", "verbose output");
	exit(1);
}

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	int c, status = 0;
	char *host = NULL;
	gfarm_int32_t port = 0;
	extern int optind;

	if (argc > 0)
		program_name = basename(argv[0]);
	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(1);
	}

	while ((c = getopt(argc, argv, "h:pv?")) != -1) {
		switch (c) {
		case 'p':
			gfs_profile_set();
			break;
		case 'h':
			host = optarg;
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
	if (argc != 2)
		usage();

	if (host != NULL) {
		e = get_port(host, &port);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "%s: %s\n", host,
				gfarm_error_string(e));
			exit(EXIT_FAILURE);
		}
	}
	e = gfimport_from_to(argv[0], argv[1], host, port);
	if (e != GFARM_ERR_NO_ERROR)
		status = 1;

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		status = 1;
	}
	return (status);
}
