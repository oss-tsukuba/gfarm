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
#include "hash.h"

#define HOSTHASH_SIZE	101

static char *program_name = "gfrm";

struct gfrm_arg {
	char *domain;
	struct gfarm_hash_table *hosthash;
	int nhosts;
	char **hosts;
	char *section;
	int ncopy;
	int force;
	int noexecute;
	int verbose;
};

struct files {
	gfarm_stringlist files, dirs;
};

struct host_copy {
	struct gfrm_arg *a;
	struct gfarm_hash_table *hosthash;
	gfarm_list *slist;
	char *file;
	int nsrccopy;
	gfarm_stringlist copylist;
};

static char *
add_file(char *file, struct gfs_stat *st, void *arg)
{
	struct files *a = arg;
	char *f;

	f = strdup(file);
	if (f == NULL)
		return (GFARM_ERR_NO_MEMORY);

	return (gfarm_stringlist_add(&a->files, f));
}

static char *
is_valid_dir(char *file, struct gfs_stat *st, void *arg)
{
	const char *f = gfarm_url_prefix_skip(file);

	if (f[0] == '.' && (f[1] == '\0' || (f[1] == '.' && f[2] == '\0')))
		return ("cannot remove \'.\' or \'..\'");
	return (NULL);
}

static char *
do_not_add_dir(char *file, struct gfs_stat *st, void *arg)
{
	const char *f = gfarm_url_prefix_skip(file);
	char *e = GFARM_ERR_IS_A_DIRECTORY;

	fprintf(stderr, "%s: '%s' %s\n", program_name, f, e);
	/* return error always to prevent further traverse */
	return (e);
}

static char *
add_dir(char *file, struct gfs_stat *st, void *arg)
{
	struct files *a = arg;
	char *f;

	f = strdup(file);
	if (f == NULL)
		return (GFARM_ERR_NO_MEMORY);

	return (gfarm_stringlist_add(&a->dirs, f));
}

static char *
gfarm_list_add_xinfo(char *file, int ncopy, char **copy,
	struct gfarm_file_section_info *info, gfarm_list *list)
{
	struct gfarm_section_xinfo *i;
	char **newcopy, *e;

	GFARM_MALLOC(i);
	if (i == NULL)
		return (GFARM_ERR_NO_MEMORY);

	GFARM_MALLOC_ARRAY(newcopy, ncopy);
	if (newcopy == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto free_i;
	}
	e = gfarm_fixedstrings_dup(ncopy, newcopy, copy);
	if (e != NULL)
		goto free_newcopy;

	i->file = strdup(file);
	i->ncopy = ncopy;
	i->copy = newcopy;
	i->i = *info;
	i->i.pathname = strdup(info->pathname);
	i->i.section = strdup(info->section);
	i->i.checksum_type = NULL;
	i->i.checksum = NULL;

	if (i->file == NULL || i->i.pathname == NULL || i->i.section == NULL) {
		gfarm_section_xinfo_free(i);
		return (GFARM_ERR_NO_MEMORY);
	}

	e = gfarm_list_add(list, i);
	if (e != NULL)
		gfarm_section_xinfo_free(i);
	return (e);

 free_newcopy:
	free(newcopy);
 free_i:
	free(i);
	return (e);
}

static char *
gfarm_list_add_xinfo2(struct gfarm_section_xinfo *xinfo, gfarm_list *list)
{
	return (gfarm_list_add_xinfo(xinfo->file, xinfo->ncopy, xinfo->copy,
			&xinfo->i, list));
}

static char *
add_host_and_copy(struct gfarm_file_section_copy_info *info, void *arg)
{
	struct host_copy *a = arg;
	char *s = info->hostname;

	if ((a->a->hosthash && gfarm_hash_lookup(
			a->a->hosthash, s, strlen(s) + 1))
	    || (!a->a->hosthash && gfarm_host_is_in_domain(s, a->a->domain))) {
		/* add info->hostname to a->hash */
		gfarm_hash_enter(a->hosthash, s, strlen(s) + 1, 0, NULL);
		gfarm_stringlist_add(&a->copylist, strdup(s));
	}
	return (NULL);
}

static char *
add_sec(struct gfarm_file_section_info *info, void *arg)
{
	struct host_copy *a = arg;
	int ncopy;
	char **copy, *e;

	e = gfarm_stringlist_init(&a->copylist);
	if (e != NULL)
		return (e);
	e = gfarm_foreach_copy(add_host_and_copy,
		info->pathname, info->section, arg, NULL);
	/* if there is no replica in specified domain, do not add. */
	if (e != NULL || gfarm_stringlist_length(&a->copylist) == 0)
		goto stringlist_free;

	/* add a file section */
	ncopy = gfarm_stringlist_length(&a->copylist);
	copy = a->copylist.array;
	e = gfarm_list_add_xinfo(a->file, ncopy, copy, info, a->slist);
 stringlist_free:
	gfarm_stringlist_free_deeply(&a->copylist);
	return (e);
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
gfarm_hash_to_string_array(struct gfarm_hash_table *hash,
	int *array_lengthp, char ***arrayp)
{
	struct gfarm_hash_iterator iter;
	struct gfarm_hash_entry *entry;
	gfarm_stringlist ls;
	char *ent, **array, *e;

	e = gfarm_stringlist_init(&ls);
	if (e != NULL)
		return (e);

	for (gfarm_hash_iterator_begin(hash, &iter);
	     !gfarm_hash_iterator_is_end(&iter);
	     gfarm_hash_iterator_next(&iter)) {
		entry = gfarm_hash_iterator_access(&iter);
		if (entry != NULL) {
			ent = strdup(gfarm_hash_entry_key(entry));
			if (ent == NULL)
				e = GFARM_ERR_NO_MEMORY;
			else
				e = gfarm_stringlist_add(&ls, ent);
		}
		if (e != NULL)
			goto stringlist_free;
	}
	array = gfarm_strings_alloc_from_stringlist(&ls);
	if (array == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto stringlist_free;
	}
	*array_lengthp = gfarm_stringlist_length(&ls);
	*arrayp = array;
 stringlist_free:
	if (e == NULL)
		gfarm_stringlist_free(&ls);
	else
		gfarm_stringlist_free_deeply(&ls);
	return (e);
}

static char *
create_host_and_file_section_list(
	gfarm_stringlist *list, struct gfrm_arg *gfrm_arg,
	int *nhosts, char ***hosts,
	int *nsinfop, struct gfarm_section_xinfo ***sinfop)
{
	gfarm_list slist;
	struct gfarm_hash_table *hosthash;
	char *file, *path, *e;
	struct host_copy host_copy;
	int i;

	e = gfarm_list_init(&slist);
	if (e != NULL)
		return (e);

	hosthash = gfarm_hash_table_alloc(HOSTHASH_SIZE,
		gfarm_hash_casefold, gfarm_hash_key_equal_casefold);
	if (hosthash == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto free_list;
	}
	host_copy.slist = &slist;
	host_copy.hosthash = hosthash;
	host_copy.a = gfrm_arg;

	for (i = 0; i < gfarm_stringlist_length(list); i++) {
		file = gfarm_stringlist_elem(list, i);
		e = gfarm_url_make_path(file, &path);
		if (e != NULL)
			goto free_hash;

		host_copy.file = file;
		if (gfrm_arg->section == NULL)
			e = gfarm_foreach_section(
				add_sec, path, &host_copy, NULL);
		else
			e = do_section(
				add_sec, path, gfrm_arg->section, &host_copy);
		free(path);
		if (e == GFARM_ERR_NO_FRAGMENT_INFORMATION
		    || e == GFARM_ERR_NO_SUCH_OBJECT)
			e = gfs_unlink(file);
		if (e != NULL)
			goto free_hash;
	}
	*sinfop = gfarm_array_alloc_from_list(&slist);
	*nsinfop = gfarm_list_length(&slist);

	/* hash to array */
	gfarm_hash_to_string_array(hosthash, nhosts, hosts);
 free_hash:
	gfarm_hash_table_free(hosthash);
 free_list:
	if (e == NULL)
		gfarm_list_free(&slist);
	else
		gfarm_list_free_deeply(&slist,
			(void (*)(void *))gfarm_section_xinfo_free);
	return (e);
}

static char *
refine_copy(struct gfarm_section_xinfo *xinfo, struct gfarm_hash_table *hash)
{
	gfarm_stringlist copylist;
	char *s, **copy, *e;
	int i;

	e = gfarm_stringlist_init(&copylist);
	if (e != NULL)
		return (e);

	for (i = 0; i < xinfo->ncopy; ++i) {
		s = xinfo->copy[i];
		if (gfarm_hash_lookup(hash, s, strlen(s) + 1)) {
			e = gfarm_stringlist_add(&copylist, strdup(s));
			if (e != NULL)
				goto free_copylist;
		}
	}
	gfarm_strings_free_deeply(xinfo->ncopy, xinfo->copy);
	copy = gfarm_strings_alloc_from_stringlist(&copylist);
	if (copy == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto free_copylist;
	}
	xinfo->ncopy = gfarm_stringlist_length(&copylist);
	xinfo->copy = copy;
 free_copylist:
	if (e == NULL)
		gfarm_stringlist_free(&copylist);
	else
		gfarm_stringlist_free_deeply(&copylist);
	return (e);
}

static char *
create_hash_table_from_string_list(int array_length, char **array,
	int hashsize, struct gfarm_hash_table **hashp)
{
	struct gfarm_hash_table *hash;
	int i;

	hash = gfarm_hash_table_alloc(hashsize,
		gfarm_hash_casefold, gfarm_hash_key_equal_casefold);
	if (hash == NULL)
		return (GFARM_ERR_NO_MEMORY);

	for (i = 0; i < array_length; ++i)
		gfarm_hash_enter(hash, array[i], strlen(array[i])+1, 0, NULL);
	*hashp = hash;
	return (NULL);
}

static char *
refine_file_section_list(int *nsinfop, struct gfarm_section_xinfo ***sinfop,
	struct gfrm_arg *arg)
{
	int i, nsinfo = *nsinfop;
	struct gfarm_section_xinfo **sinfo = *sinfop;
	struct gfarm_hash_table *hosthash;
	gfarm_list slist;
	char *e;

	e = create_hash_table_from_string_list(arg->nhosts, arg->hosts,
		HOSTHASH_SIZE, &hosthash);
	if (e != NULL)
		return (e);
	e = gfarm_list_init(&slist);
	if (e != NULL)
		goto free_hosthash;

	/*
	 * If number of file replicas stored on the specified set of
	 * hosts is less than 'num_replica', do not add the file section.
	 */
	for (i = 0; i < nsinfo; gfarm_section_xinfo_free(sinfo[i++])) {
		e = refine_copy(sinfo[i], hosthash);
		if (e != NULL)
			goto free_list;
		if (sinfo[i]->ncopy <= arg->ncopy)
			continue;
		e = gfarm_list_add_xinfo2(sinfo[i], &slist);
		if (e != NULL)
			goto free_list;
	}
	free(sinfo);
	*sinfop = gfarm_array_alloc_from_list(&slist);
	*nsinfop = gfarm_list_length(&slist);
 free_list:
	if (e == NULL)
		gfarm_list_free(&slist);
	else
		gfarm_list_free_deeply(&slist,
			(void (*)(void *))gfarm_section_xinfo_free);
 free_hosthash:
	gfarm_hash_table_free(hosthash);
	return (e);
}

static void
print_gfrm_arg(struct gfrm_arg *arg)
{
	int i;

	printf("nhosts = %d\n", arg->nhosts);
	for (i = 0; i < arg->nhosts; ++i)
		printf("%s\n", arg->hosts[i]);
	if (arg->section != NULL)
		printf("section = %s\n", arg->section);
	printf("ncopy = %d\n", arg->ncopy);
	printf("force    : %d\n", arg->force);
	printf("noexecute: %d\n", arg->noexecute);
	printf("verbose  : %d\n", arg->verbose);
}

static char *
remove_files(int nsinfo, struct gfarm_section_xinfo **sinfo,
	     gfarm_stringlist *dirs, int nthreads, struct gfrm_arg *arg)
{
	int i, nerr = 0;
	char *e = NULL;
	int nhosts = arg->nhosts;

	if (nhosts <= 0)
		return "no host";
	if (arg->ncopy < 0)
		arg->ncopy = 0;
	if (nsinfo + gfarm_stringlist_length(dirs) <= 0)
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

#ifdef LIBGFARM_NOT_MT_SAFE
	/*
	 * XXX - libgfarm is not thread-safe...
	 * purge the connection cache for gfsd since the connection to
	 * gfsd cannot be shared among child processes.
	 */
	gfs_client_terminate();
#endif
#pragma omp parallel for schedule(dynamic) reduction(+:nerr)
	for (i = 0; i < nsinfo; ++i) {
		struct gfarm_section_xinfo *si = sinfo[i];
#ifdef LIBGFARM_NOT_MT_SAFE
		pid_t pid;
		int s, rv;

		pid = fork();
		if (pid == 0) {
#endif
			if (arg->verbose || arg->noexecute) {
				si->ncopy -= arg->ncopy;
				gfarm_section_xinfo_print(si);
				si->ncopy += arg->ncopy;
				e = NULL;
			}
			if (!arg->noexecute)
				e = gfs_unlink_section_replica(si->file,
					si->i.section, si->ncopy - arg->ncopy,
					si->copy, arg->force);
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

	for (i = 0; i < gfarm_stringlist_length(dirs); ++i) {
		char *dir = gfarm_stringlist_elem(dirs, i);

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
	return (nerr == 0 ? NULL : "error happens during removal");
}

static int
usage()
{
	fprintf(stderr, "Usage: %s [-frRnqv] [-I <section>] [-h <host>]"
		" [-D <domain>]\n", program_name);
	fprintf(stderr, "\t[-H <hostfile>] [-N <#replica>]");
#ifdef _OPENMP
	fprintf(stderr, " [-j <#thread>]");
#endif
	fprintf(stderr, " <gfarm_url>...\n");
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
create_hostlist(char *hostfile, int *nhosts, char ***hosts)
{
	int error_line = -1;
	char *e;

	e = gfarm_hostlist_read(hostfile, nhosts, hosts, &error_line);
	if (e != NULL) {
		if (error_line != -1)
			fprintf(stderr, "%s: line %d: %s\n",
				hostfile, error_line, e);
		else
			fprintf(stderr, "%s: %s\n", hostfile, e);
		exit(EXIT_FAILURE);
	}
	return (e);
}

int
main(int argc, char *argv[])
{
	char *domain = "", *hostfile = NULL, *section = NULL;
	struct gfarm_hash_table *hosthash = NULL;
	gfarm_stringlist paths;
	gfs_glob_t types;
	int mode_ch = 0, num_replicas = 0, parallel = -1, recursive = 0;
	int force = 0, noexecute = 0, quiet = 0, verbose = 0;
	int i, nhosts, nsinfo;
	char **hosts, ch, *e;
	struct gfrm_arg gfrm_arg;
	struct files files;
	struct gfarm_section_xinfo **sinfo;
	char *(*op_dir_before)(char *, struct gfs_stat *, void *);

#ifdef __GNUC__ /* workaround gcc warning: may be used uninitialized */
	nsinfo = 0;
	sinfo = NULL;
#endif

	if (argc >= 1)
		program_name = basename(argv[0]);

	e = gfarm_initialize(&argc, &argv);
	error_check(e);

#ifdef _OPENMP
	while ((ch = getopt(argc, argv, "a:fh:j:nqrvD:H:I:N:R?")) != -1) {
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
#ifdef _OPENMP
		case 'j':
			parallel = strtol(optarg, NULL, 0);
			break;
#endif
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
		op_dir_before = is_valid_dir;
	else
		op_dir_before = do_not_add_dir;

	e = gfarm_stringlist_init(&files.files);
	error_check(e);
	e = gfarm_stringlist_init(&files.dirs);
	error_check(e);
	for (i = 0; i < gfarm_stringlist_length(&paths); i++) {
		char *file = gfarm_stringlist_elem(&paths, i);
		e = gfarm_foreach_directory_hierarchy(
			add_file, op_dir_before, add_dir, file, &files);
		/*
		 * GFARM_ERR_IS_A_DIRECTORY may be returned to prevent
		 * further traverse.
		 */
		if (e != NULL && e != GFARM_ERR_IS_A_DIRECTORY)
			fprintf(stderr, "%s: %s\n", file, e);
	}
	gfarm_stringlist_free_deeply(&paths);
	if (gfarm_stringlist_length(&files.files)
		+ gfarm_stringlist_length(&files.dirs) <= 0)
		exit(0); /* no file */

	if (!quiet) {
		printf("investigating hosts...");
		fflush(stdout);
	}
	if (hostfile != NULL) {
		e = create_hostlist(hostfile, &nhosts, &hosts);
		error_check(e);
		e = create_hash_table_from_string_list(nhosts, hosts,
			HOSTHASH_SIZE, &hosthash);
		error_check(e);
		gfarm_strings_free_deeply(nhosts, hosts);
	}
	gfrm_arg.domain = domain;
	gfrm_arg.hosthash = hosthash;
	gfrm_arg.section = section;
	e = create_host_and_file_section_list(&files.files, &gfrm_arg,
		&nhosts, &hosts, &nsinfo, &sinfo);
	gfarm_stringlist_free_deeply(&files.files);
	if (hosthash != NULL)
		gfarm_hash_table_free(hosthash);
	if (e == NULL)
		e = gfarm_schedule_search_idle_acyclic_hosts(
			nhosts, hosts, &nhosts, hosts);
	error_check(e);
	gfrm_arg.nhosts = nhosts;
	gfrm_arg.hosts = hosts;
	gfrm_arg.ncopy = num_replicas;
	e = refine_file_section_list(&nsinfo, &sinfo, &gfrm_arg);
	error_check(e);
	if (!quiet)
		printf(" done\n");

	gfrm_arg.force = force ||
		(domain[0] == '\0' && hostfile == NULL && num_replicas == 0);
	gfrm_arg.noexecute = noexecute;
	gfrm_arg.verbose = verbose;

	e = remove_files(nsinfo, sinfo, &files.dirs, parallel, &gfrm_arg);
	error_check(e);

	gfarm_array_free_deeply(nsinfo, sinfo,
		(void (*)(void *))gfarm_section_xinfo_free);
	gfarm_strings_free_deeply(nhosts, hosts);
	gfarm_stringlist_free_deeply(&files.dirs);
	e = gfarm_terminate();
	error_check(e);

	return (0);
}
