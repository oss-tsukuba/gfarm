/*
 * $Id$
 */

#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#include <string.h>
#include <stdio.h>

#ifdef _OPENMP
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <omp.h>
#define LIBGFARM_NOT_MT_SAFE
#else
static void omp_set_num_threads(int n){ return; }
#endif

#include <gfarm/gfarm.h>
#include <openssl/evp.h>
#include "host.h"
#include "schedule.h"
#include "gfs_misc.h"
#include "gfarm_list.h"
#include "gfarm_foreach.h"
#include "gfarm_xinfo.h"

static char *program_name = "gfrm";

struct gfrm_arg {
	gfarm_stringlist files, dirs;
	int nhosts;
	char **hosts;
	char *section;
	int nrep;
	int force;
	int noexecute;
	int verbose;
};

static char *
add_file(char *file, struct gfs_stat *st, void *arg)
{
	struct gfrm_arg *a = arg;
	char *f;
	
	f = strdup(file);
	if (f == NULL)
		return (GFARM_ERR_NO_MEMORY);

	return (gfarm_stringlist_add(&a->files, f));
}

static char *
reject_dir(char *file, struct gfs_stat *st, void *arg)
{
	const char *f = gfarm_url_prefix_skip(file);
	
	if (f[0] == '.' && (f[1] == '\0' || (f[1] == '.' && f[2] == '\0')))
		return ("cannot remove \'.\' or \'..\'");
	return (NULL);
}

static char *
add_this_dir(char *file, struct gfs_stat *st, void *arg)
{
	struct gfrm_arg *a = arg;
	char *f, *e;
	
	f = strdup(file);
	if (f == NULL)
		return (GFARM_ERR_NO_MEMORY);
	
	e = gfarm_stringlist_add(&a->dirs, f);
	/* return error always to prevent further traverse */
	return (e != NULL ? e : GFARM_ERR_IS_A_DIRECTORY);
}

static char *
add_dir(char *file, struct gfs_stat *st, void *arg)
{
	struct gfrm_arg *a = arg;
	char *f;
	
	f = strdup(file);
	if (f == NULL)
		return (GFARM_ERR_NO_MEMORY);
	
	return (gfarm_stringlist_add(&a->dirs, f));
}

struct add_sec_arg {
	char *file;
	gfarm_list *slist;
	gfarm_stringlist copylist;
	struct gfrm_arg *a;
};

static char *
add_copy(struct gfarm_file_section_copy_info *info, void *arg)
{
	struct add_sec_arg *a = arg;
	int i;
	
	/* XXX - linear search */
	for (i = 0; i < a->a->nhosts; ++i) {
		if (strcmp(a->a->hosts[i], info->hostname) == 0) {
			gfarm_stringlist_add(
				&a->copylist, strdup(info->hostname));
			break;
		}
	}
	return (NULL);
}

static char *
add_sec(struct gfarm_file_section_info *info, void *arg)
{
	struct add_sec_arg *a = arg;
	struct gfarm_section_xinfo *i;
	char *e;

	/*
	 * If number of file replicas stored on the specified set of
	 * hosts is less than 'nrep', do not add the file section.
	 */
	e = gfarm_stringlist_init(&a->copylist);
	if (e != NULL)
		return (e);
	gfarm_foreach_copy(add_copy,
		info->pathname, info->section, arg, NULL);
	if (gfarm_stringlist_length(&a->copylist) <= a->a->nrep) {
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
create_file_section_list(gfarm_stringlist *list, struct gfrm_arg *gfrm_arg,
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
	sec_arg.a = gfrm_arg;

	for (i = 0; i < gfarm_stringlist_length(list); i++) {
		file = gfarm_stringlist_elem(list, i);
		e = gfarm_url_make_path(file, &path);
		if (e != NULL)
			goto free_list;

		sec_arg.file = file;
		if (gfrm_arg->section == NULL)
			e = gfarm_foreach_section(
				add_sec, path, &sec_arg, NULL);
		else
			e = do_section(
				add_sec, path, gfrm_arg->section, &sec_arg);
		free(path);
		if (e == GFARM_ERR_NO_FRAGMENT_INFORMATION)
			e = gfs_unlink(file);
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

static void
print_gfrm_arg(struct gfrm_arg *arg)
{
	int i;

	printf("nhosts = %d\n", arg->nhosts);
	for (i = 0; i < arg->nhosts; ++i)
		printf("%s\n", arg->hosts[i]);
	printf("section = %s\n",
	       arg->section == NULL ? "(null)" : arg->section);
	printf("nrep = %d\n", arg->nrep);
	printf("force    : %d\n", arg->force);
	printf("noexecute: %d\n", arg->noexecute);
	printf("verbose  : %d\n", arg->verbose);
}

static char *
remove_files(int nthreads, struct gfrm_arg *arg)
{
	int i, nsinfo, nerr = 0;
	char *e;
	struct gfarm_section_xinfo **sinfo;
	int nhosts = arg->nhosts;

	if (nhosts <= 0)
		return "no host";
	if (arg->nrep < 0)
		arg->nrep = 0;

	e = create_file_section_list(&arg->files, arg, &nsinfo, &sinfo);
	if (e != NULL)
		return(e);
	if (nsinfo <= 0)
		return (NULL); /* no file */

	if (nthreads <= 0) {
		nthreads = nsinfo;
		if (nhosts < nthreads)
			nthreads = nhosts;
	}
	if (arg->verbose) {
		print_gfrm_arg(arg);
		printf("files: %d\n", nsinfo);
#ifdef _OPENMP
		printf("parallel remove using %d streams\n", nthreads);
#endif
	}
	omp_set_num_threads(nthreads);

#pragma omp parallel for schedule(dynamic) reduction(+:nerr)
	for (i = 0; i < nsinfo; ++i) {
		struct gfarm_section_xinfo *si = sinfo[i];
#ifdef LIBGFARM_NOT_MT_SAFE
		pid_t pid;
		int s, rv;

		/* XXX - libgfarm is not thread-safe... */
		pid = fork();
		if (pid == 0) {
#endif
			si->ncopy -= arg->nrep;
			if (arg->verbose || arg->noexecute) {
				gfarm_section_xinfo_print(si);
				e = NULL;
			}
			if (!arg->noexecute)
				e = gfs_unlink_section_replica(si->file,
					si->i.section,
					si->ncopy, si->copy, arg->force);
			if (e != NULL) {
#ifndef LIBGFARM_NOT_MT_SAFE
				++nerr;
#endif
				fprintf(stderr, "%s (%s): %s\n",
					si->file, si->i.section, e);
			}
#ifdef LIBGFARM_NOT_MT_SAFE
			_exit(e == NULL ? 0 : 1);
		}
		while ((rv = waitpid(pid, &s, 0)) == -1 && errno == EINTR);
		if (rv == -1 || (WIFEXITED(s) && WEXITSTATUS(s) != 0))
			++nerr;
#endif
	}

	for (i = 0; i < gfarm_stringlist_length(&arg->dirs); ++i) {
		char *dir = gfarm_stringlist_elem(&arg->dirs, i);

		if (!arg->noexecute)
			e = gfs_rmdir(dir);
		else {
			printf("%s\n", dir);
			e = NULL;
		}
		if (arg->force && e != NULL) {
			++nerr;
			fprintf(stderr, "%s: %s\n", dir, e);
		}
	}
	gfarm_array_free_deeply(nsinfo, sinfo,
		(void (*)(void *))gfarm_section_xinfo_free);
	return (nerr == 0 ? NULL : "error happens during removal");
}

static int
usage()
{
	fprintf(stderr,	"Usage: %s [-frRnqv] [-I <section>] [-h <host>]"
		" [-D <domain>]\n", program_name);
	fprintf(stderr,	"\t[-H <hostfile>] [-N <#replica>]");
#ifdef _OPENMP
	fprintf(stderr, " [-P <#thread>]");
#endif
	fprintf(stderr,	" <gfarm_url>...\n");
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

int
main(int argc, char *argv[])
{
	char *domain = "", *hostfile = NULL, *section = NULL;
	gfarm_stringlist paths;
	gfs_glob_t types;
	int mode_ch = 0, num_replicas = 0, parallel = -1, recursive = 0;
	int force = 0, noexecute = 0, quiet = 0, verbose = 0;
	int i, nhosts, error_line = -1;
	char **hosts, ch, *e;
	struct gfrm_arg gfrm_arg;
	char *(*op_dir_before)(char *, struct gfs_stat *, void *);

	if (argc >= 1)
		program_name = basename(argv[0]);

	e = gfarm_initialize(&argc, &argv);
	error_check(e);

#ifdef _OPENMP
	while ((ch = getopt(argc, argv, "a:fh:nqrvD:H:I:N:P:R?")) != -1) {
#else
	while ((ch = getopt(argc, argv, "a:fh:nqrvD:H:I:N:R?")) != -1) {
#endif
		switch (ch) {
		case 'a':
		case 'I':
			section = optarg;
			break;
		case 'f':
			force = 1;
			break;
		case 'n':
			noexecute = 1;
			break;
		case 'q':
			quiet = 1;
			break;
		case 'r':
		case 'R':
			recursive = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'h':
		case 'D':
			domain = optarg;
			conflict_check(&mode_ch, ch);
			break;
		case 'H':
			hostfile = optarg;
			conflict_check(&mode_ch, ch);
			break;
		case 'N':
			num_replicas = strtol(optarg, NULL, 0);
			break;
#ifdef _OPENMP
		case 'P':
			parallel = strtol(optarg, NULL, 0);
			break;
#endif
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

	if (recursive)
		op_dir_before = reject_dir;
	else
		op_dir_before = add_this_dir;

	e = gfarm_stringlist_init(&gfrm_arg.files);
	error_check(e);
	e = gfarm_stringlist_init(&gfrm_arg.dirs);
	error_check(e);
	for (i = 0; i < gfarm_stringlist_length(&paths); i++) {
		char *file = gfarm_stringlist_elem(&paths, i);
		e = gfarm_foreach_directory_hierarchy(
			add_file, op_dir_before, add_dir, file, &gfrm_arg);
		/*
		 * GFARM_ERR_IS_A_DIRECTORY may be returned to prevent
		 * further traverse.
		 */
		if (e != NULL && e != GFARM_ERR_IS_A_DIRECTORY)
			fprintf(stderr, "%s: %s\n", file, e);
	}
	gfarm_stringlist_free_deeply(&paths);
	if (gfarm_stringlist_length(&gfrm_arg.files)
		+ gfarm_stringlist_length(&gfrm_arg.dirs) <= 0)
		exit(0); /* no file */

	if (!quiet) {
		printf("investigating hosts...");
		fflush(stdout);
	}
	if (hostfile != NULL) {
		e = gfarm_hostlist_read(
			hostfile, &nhosts, &hosts, &error_line);
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
		e = gfarm_hosts_in_domain(&nhosts, &hosts, domain);
	}
	if (e == NULL)
		e = gfarm_schedule_search_idle_acyclic_hosts(
			nhosts, hosts, &nhosts, hosts);
	error_check(e);
	if (!quiet)
		printf(" done\n");

	gfrm_arg.nhosts = nhosts;
	gfrm_arg.hosts = hosts;
	gfrm_arg.section = section;
	gfrm_arg.nrep = num_replicas;
	gfrm_arg.force = force ||
		(domain[0] == '\0' && hostfile == NULL && num_replicas == 0);
	gfrm_arg.noexecute = noexecute;
	gfrm_arg.verbose = verbose;

	e = remove_files(parallel, &gfrm_arg);
	error_check(e);

	gfarm_strings_free_deeply(nhosts, hosts);
	gfarm_stringlist_free_deeply(&gfrm_arg.files);
	gfarm_stringlist_free_deeply(&gfrm_arg.dirs);
	e = gfarm_terminate();
	error_check(e);

	return (0);
}
