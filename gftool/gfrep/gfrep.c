/*
 * $Id$
 */

#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>

#ifdef _OPENMP
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <omp.h>
#define LIBGFARM_NOT_MT_SAFE
#else
static void omp_set_num_threads(int n){ return; }
static int omp_get_num_threads(void){ return (1); }
static int omp_get_thread_num(void){ return (0); }
#endif

#include <gfarm/gfarm.h>
#include <openssl/evp.h>
#include "host.h"
#include "schedule.h"
#include "gfs_client.h"
#include "gfs_misc.h"
#include "gfarm_list.h"
#include "gfarm_foreach.h"
#include "gfarm_xinfo.h"

static char *program_name = "gfrep";

struct action {
	char *action;
	char *(*transfer_to)(const char *, char *, char *);
};

struct action replicate_mode = {
	"replicate",
	gfarm_url_section_replicate_to
};

struct action migrate_mode = {
	"migrate",
	gfarm_url_section_migrate_to
};

struct action *act = &replicate_mode;

static char *
add_file(char *file, struct gfs_stat *st, void *arg)
{
	gfarm_stringlist *list = arg;
	char *f;
	
	f = strdup(file);
	if (f == NULL)
		return (GFARM_ERR_NO_MEMORY);

	return (gfarm_stringlist_add(list, f));
}

struct gfrep_arg {
	int nsrc, ndst;
	char **src, **dst;
	char *section;
	int nrep;
	int noexecute;
	int verbose;
	int quiet;
};

struct add_sec_arg {
	char *file;
	gfarm_list *slist;
	int nsrcrep, ndstrep;
	gfarm_stringlist copylist;
	struct gfrep_arg *a;
};

static char *
count_src_dst(struct gfarm_file_section_copy_info *info, void *arg)
{
	struct add_sec_arg *a = arg;
	int i;
	
	/* XXX - linear search */
	for (i = 0; i < a->a->nsrc; ++i) {
		if (strcmp(a->a->src[i], info->hostname) == 0) {
			++a->nsrcrep;
			break;
		}
	}
	for (i = 0; i < a->a->ndst; ++i) {
		if (strcmp(a->a->dst[i], info->hostname) == 0) {
			++a->ndstrep;
			break;
		}
	}
	gfarm_stringlist_add(&a->copylist, strdup(info->hostname));
	return (NULL);
}

static char *
add_sec(struct gfarm_file_section_info *info, void *arg)
{
	struct add_sec_arg *a = arg;
	struct gfarm_section_xinfo *i;
	char *e;

	a->nsrcrep = a->ndstrep = 0;
	e = gfarm_stringlist_init(&a->copylist);
	if (e != NULL)
		return (e);
	gfarm_foreach_copy(count_src_dst,
		info->pathname, info->section, arg, NULL);
	if (a->nsrcrep == 0 || a->ndstrep >= a->a->nrep) {
		gfarm_stringlist_free_deeply(&a->copylist);
		return (NULL);
	}

	i = malloc(sizeof(*i));
	if (i == NULL)
		return (GFARM_ERR_NO_MEMORY);

	i->file = strdup(a->file);
	i->ncopy = gfarm_stringlist_length(&a->copylist);
	i->copy = gfarm_strings_alloc_from_stringlist(&a->copylist);
	i->i = *info;
	i->i.pathname = strdup(info->pathname);
	i->i.section = strdup(info->section);
	i->i.checksum_type = NULL;
	i->i.checksum = NULL;

	if (i->file == NULL || i->copy == NULL ||
	    i->i.pathname == NULL || i->i.section == NULL) {
		gfarm_stringlist_free_deeply(&a->copylist);
		gfarm_section_xinfo_free(i);
		return (GFARM_ERR_NO_MEMORY);
	}
	else
		gfarm_stringlist_free(&a->copylist);

	gfarm_list_add(a->slist, i);
	return (NULL);
}

static char *
do_section(char *(*op)(struct gfarm_file_section_info *, void *),
	const char *gfarm_file, const char *section, void *arg)
{
	char *e;
	struct gfarm_file_section_info info;

	e = gfarm_file_section_info_get(gfarm_file, section, &info);
	if (e == NULL) {
		e = op(&info, arg);
		gfarm_file_section_info_free(&info);
	}
	return (e);
}

static char *
create_file_section_list(gfarm_stringlist *list, struct gfrep_arg *gfrep_arg,
	int *nsinfop, struct gfarm_section_xinfo ***sinfop)
{
	gfarm_list slist;
	char *file, *path, *e;
	struct add_sec_arg sec_arg;
	int i;

	e = gfarm_list_init(&slist);
	if (e != NULL)
		goto free_list;
	sec_arg.slist = &slist;
	sec_arg.a = gfrep_arg;

	for (i = 0; i < gfarm_stringlist_length(list); i++) {
		file = gfarm_stringlist_elem(list, i);
		e = gfarm_url_make_path(file, &path);
		if (e != NULL)
			goto free_list;

		sec_arg.file = file;
		if (gfrep_arg->section == NULL)
			e = gfarm_foreach_section(
				add_sec, path, &sec_arg, NULL);
		else
			e = do_section(
				add_sec, path, gfrep_arg->section, &sec_arg);
		free(path);
		if (e == GFARM_ERR_NO_FRAGMENT_INFORMATION)
			continue;
		if (e != NULL)
			goto free_list;
	}			
	*sinfop = gfarm_array_alloc_from_list(&slist);
	*nsinfop = gfarm_list_length(&slist);
	/* do not call list_free_deeply() */
	gfarm_list_free(&slist);
	return (NULL);

free_list:
	gfarm_list_free_deeply(&slist,
		(void (*)(void *))gfarm_section_xinfo_free);
	return (e);
}

#include <sys/time.h>
#define gfarm_timerval_sub(t1, t2) \
	(((double)(t1)->tv_sec - (double)(t2)->tv_sec)	\
	+ ((double)(t1)->tv_usec - (double)(t2)->tv_usec) * .000001)

static void
print_gfrep_arg(struct gfrep_arg *arg)
{
	int i;

	printf("nsrc = %d\n", arg->nsrc);
	for (i = 0; i < arg->nsrc; ++i)
		printf("%s\n", arg->src[i]);
	printf("ndst = %d\n", arg->ndst);
	for (i = 0; i < arg->ndst; ++i)
		printf("%s\n", arg->dst[i]);
	printf("nrep = %d\n", arg->nrep);
}

static void
print_stringlist(struct gfarm_stringlist *list)
{
	int i;

	for (i= 0; i < gfarm_stringlist_length(list); ++i)
		printf("%s\n", gfarm_stringlist_elem(list, i));
}

static int
filesizecmp_inv(const void *a, const void *b)
{
	struct gfarm_section_xinfo * const *aa = a, * const *bb = b;
	file_offset_t aaa = (*aa)->i.filesize, bbb = (*bb)->i.filesize;

	if (aaa < bbb)
		return (1);
	if (aaa > bbb)
		return (-1);
	return (0);
}

static char *
replicate(gfarm_stringlist *list, int nthreads, struct gfrep_arg *arg)
{
	int i, nsinfo, pi, tnum, nth, nerr = 0;
	char *e;
	struct gfarm_section_xinfo **sinfo;
	int ndst = arg->ndst, nsrc = arg->nsrc;
	char **dst = arg->dst;

	if (ndst <= 0)
		return "no destination node";
	if (nsrc <= 0)
		return "no source node";
	if (ndst < arg->nrep)
		return "not enough number of destination nodes";

	e = create_file_section_list(list, arg, &nsinfo, &sinfo);
	if (e != NULL)
		return(e);
	if (nsinfo <= 0)
		return (NULL); /* no file */
	/* sort 'sinfo' in descending order wrt file size */
	qsort(sinfo, nsinfo, sizeof(*sinfo), filesizecmp_inv);

	if (nthreads <= 0) {
		nthreads = nsinfo;
		if (ndst < nthreads)
			nthreads = ndst;
		if (nsrc < nthreads)
			nthreads = nsrc;
	}
	if (arg->verbose) {
		print_gfrep_arg(arg);
		printf("files: %d\n", nsinfo);
#ifdef _OPENMP
		printf("parallel replication using %d streams\n", nthreads);
#endif
	}
	omp_set_num_threads(nthreads);

#ifdef LIBGFARM_NOT_MT_SAFE
	/*
	 * XXX - libgfarm is not thread-safe...
	 * purge the connection cache for gfsd since the connection to
	 * gfsd cannot be shared among child processes.
	 */
	gfs_client_terminate();
#endif
#pragma omp parallel reduction(+:nerr) private(pi,tnum,nth)
	{
	pi = 0;
	tnum = omp_get_thread_num();
	nth = omp_get_num_threads();

#pragma omp for schedule(dynamic)
	for (i = 0; i < nsinfo; ++i) {
		int di;
		struct timeval t1, t2;
		double t;
#ifdef LIBGFARM_NOT_MT_SAFE
		pid_t pid;
		int s, rv;
#endif
		if (!arg->quiet)
			printf("%s (%s)\n",
			       sinfo[i]->file, sinfo[i]->i.section);
		if (arg->verbose)
			gfarm_section_xinfo_print(sinfo[i]);
		if (tnum + pi * nth > ndst)
			pi = 0;

#ifdef LIBGFARM_NOT_MT_SAFE
		pid = fork();
		if (pid == 0) {
#endif
			di = (tnum + pi * nth) % ndst;
			/* XXX - the destination may conflict */
			while (gfarm_file_section_copy_info_does_exist(
				sinfo[i]->i.pathname, sinfo[i]->i.section,
				dst[di])) {
				if (arg->verbose)
					printf("%02d: warning: the destination"
					       " may conflict: %s -> %s\n",
					       tnum, dst[di],
					       dst[(di + 1) % ndst]);
				di = (di + 1) % ndst;
			}
			if (arg->verbose) {
				printf("%02d(%03d): %s (%s) --> %s\n",
		 		       tnum, pi, sinfo[i]->file,
				       sinfo[i]->i.section, dst[di]);
				gettimeofday(&t1, NULL);
			}
			if (!arg->noexecute)
				e = act->transfer_to(sinfo[i]->file,
					sinfo[i]->i.section, dst[di]);
			else {
				printf("%s (%s): %s to %s\n", act->action,
				       sinfo[i]->file, sinfo[i]->i.section,
				       dst[di]);
				e = NULL;
			}
			if (arg->verbose) {
				gettimeofday(&t2, NULL);
				t = gfarm_timerval_sub(&t2, &t1);
				printf("%s (%s): %f sec  %f MB/sec\n",
				       sinfo[i]->file, sinfo[i]->i.section, t,
				       sinfo[i]->i.filesize / t / 1024 / 1024);
			}
			if (e != NULL) {
#ifndef LIBGFARM_NOT_MT_SAFE
				++nerr;
#endif
				fprintf(stderr, "%s (%s): %s\n",
					sinfo[i]->file, sinfo[i]->i.section, e);
			}
#ifdef LIBGFARM_NOT_MT_SAFE
			_exit(e == NULL ? 0 : 1);
		}
		while ((rv = waitpid(pid, &s, 0)) == -1 && errno == EINTR);
		if (rv == -1 || (WIFEXITED(s) && WEXITSTATUS(s) != 0))
			++nerr;
#endif
		++pi;
	}
	}

	gfarm_array_free_deeply(nsinfo, sinfo,
		(void (*)(void *))gfarm_section_xinfo_free);
	return (nerr == 0 ? NULL : "error happens during replication");
}

static int
usage()
{
	fprintf(stderr,	"Usage: %s [-mnqv] [-I <section>] [-S <src_domain>]"
		" [-D <dst_domain>]\n", program_name);
	fprintf(stderr,	"\t[-h <src_hostlist>] [-H <dst_hostlist>]"
		" [-N <#replica>]");
#ifdef _OPENMP
	fprintf(stderr, " [-j <#thread>]");
#endif
	fprintf(stderr,	"\n\t<gfarm_url>...\n");
	exit(EXIT_FAILURE);
}

static int
error_check(char *e)
{
	if (e == NULL)
		return (0);
	fprintf(stderr, "%s: %s\n", program_name, e);
	exit(EXIT_FAILURE);
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

static char *
create_hostlist(char *hostfile, char *domain, int *nhosts, char ***hosts)
{
	int error_line = -1;
	char *e;

	if (hostfile != NULL) {
		e = gfarm_hostlist_read(
			hostfile, nhosts, hosts, &error_line);
		if (e != NULL) {
			if (error_line != -1)
				fprintf(stderr, "%s: line %d: %s\n",
					hostfile, error_line, e);
			else
				fprintf(stderr, "%s: %s\n", hostfile, e);
			exit(EXIT_FAILURE);
		}
	}
	else {
		e = gfarm_hosts_in_domain(nhosts, hosts, domain);
	}
	return (e);
}

int
main(int argc, char *argv[])
{
	char *src_domain = "", *dst_domain = "", *section = NULL;
	char *src_hostfile = NULL, *dst_hostfile = NULL;
	gfarm_stringlist paths, allpaths;
	gfs_glob_t types;
	int mode_src_ch = 0, mode_dst_ch = 0, num_replicas = 1, parallel = -1;
	int noexecute = 0, quiet = 0, verbose = 0;
	int i, nsrchosts, ndsthosts;
	char **srchosts, **dsthosts, ch, *e;
	struct gfrep_arg gfrep_arg;

	if (argc >= 1)
		program_name = basename(argv[0]);

	e = gfarm_initialize(&argc, &argv);
	error_check(e);

#ifdef _OPENMP
	while ((ch = getopt(argc, argv, "a:h:j:mnqvS:D:H:I:N:?")) != -1) {
#else
	while ((ch = getopt(argc, argv, "a:h:mnqvS:D:H:I:N:?")) != -1) {
#endif
		switch (ch) {
		case 'a':
		case 'I':
			section = optarg;
			break;
		case 'h':
			src_hostfile = optarg;
			conflict_check(&mode_src_ch, ch);
			break;
#ifdef _OPENMP
		case 'j':
			parallel = strtol(optarg, NULL, 0);
			break;
#endif
		case 'm':
			act = &migrate_mode;
			break;
		case 'n':
			noexecute = 1;
			break;
		case 'q':
			quiet = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'S':
			src_domain = optarg;
			conflict_check(&mode_src_ch, ch);
			break;
		case 'D':
			dst_domain = optarg;
			conflict_check(&mode_dst_ch, ch);
			break;
		case 'H':
			dst_hostfile = optarg;
			conflict_check(&mode_dst_ch, ch);
			break;
		case 'N':
			num_replicas = strtol(optarg, NULL, 0);
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	e = gfarm_stringlist_init(&paths);
	if (e == NULL) {
		e = gfs_glob_init(&types);
		if (e == NULL) {
			for (i = 0; i < argc; i++)
				gfs_glob(argv[i], &paths, &types);
			gfs_glob_free(&types);
		}
	}
	error_check(e);

	e = gfarm_stringlist_init(&allpaths);
	error_check(e);
	if (!quiet) {
		printf("constructing file list...");
		fflush(stdout);
	}
	for (i = 0; i < gfarm_stringlist_length(&paths); i++) {
		char *file = gfarm_stringlist_elem(&paths, i);
		gfarm_foreach_directory_hierarchy(
			add_file, NULL, NULL, file, &allpaths);
	}
	gfarm_stringlist_free_deeply(&paths);
	if (!quiet)
		printf(" done\n");
	if (verbose)
		print_stringlist(&allpaths);
	if (gfarm_stringlist_length(&allpaths) <= 0)
		exit(0); /* no file */

	if (!quiet) {
		printf("investigating hosts...");
		fflush(stdout);
	}
	e = create_hostlist(src_hostfile, src_domain, &nsrchosts, &srchosts);
	if (e == NULL)
		e = gfarm_schedule_search_idle_acyclic_hosts(
			nsrchosts, srchosts, &nsrchosts, srchosts);
	error_check(e);

	e = create_hostlist(dst_hostfile, dst_domain, &ndsthosts, &dsthosts);
	if (e == NULL)
		e = gfarm_schedule_search_idle_acyclic_hosts_to_write(
			ndsthosts, dsthosts, &ndsthosts, dsthosts);
	error_check(e);
	if (!quiet)
		printf(" done\n");

	gfrep_arg.nsrc = nsrchosts;
	gfrep_arg.ndst = ndsthosts;
	gfrep_arg.src = srchosts;
	gfrep_arg.dst = dsthosts;
	gfrep_arg.section = section;
	gfrep_arg.nrep = num_replicas;
	gfrep_arg.noexecute = noexecute;
	gfrep_arg.verbose = verbose;
	gfrep_arg.quiet = quiet;

	e = replicate(&allpaths, parallel, &gfrep_arg);
	error_check(e);

	gfarm_strings_free_deeply(nsrchosts, srchosts);
	gfarm_strings_free_deeply(ndsthosts, dsthosts);
	gfarm_stringlist_free_deeply(&allpaths);
	e = gfarm_terminate();
	error_check(e);

	return (0);
}
