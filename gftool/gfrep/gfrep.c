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
#include "hash.h"

#define HOSTHASH_SIZE	101

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

struct gfrep_arg {
	char *src_domain;
	struct gfarm_hash_table *src_hosthash;
	int nsrc, ndst;
	char **src, **dst;
	char *section;
	int ncopy;
	int noexecute;
	int verbose;
	int quiet;
};

struct host_copy {
	struct gfrep_arg *a;
	struct gfarm_hash_table *hosthash;
	gfarm_list *slist;
	char *file;
	int nsrccopy;
	gfarm_stringlist copylist;
};

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

	if ((a->a->src_hosthash && gfarm_hash_lookup(
			a->a->src_hosthash, s, strlen(s) + 1))
	    || (!a->a->src_hosthash && gfarm_host_is_in_domain(
			s, a->a->src_domain))) {
		/* add info->hostname to a->hash */
		gfarm_hash_enter(a->hosthash, s, strlen(s) + 1, 0, NULL);
		++a->nsrccopy;
	}
	gfarm_stringlist_add(&a->copylist, strdup(s));
	return (NULL);
}

static char *
add_sec(struct gfarm_file_section_info *info, void *arg)
{
	struct host_copy *a = arg;
	int ncopy;
	char **copy, *e;

	a->nsrccopy = 0;
	e = gfarm_stringlist_init(&a->copylist);
	if (e != NULL)
		return (e);
	e = gfarm_foreach_copy(add_host_and_copy,
		info->pathname, info->section, arg, NULL);
	/* if there is no replica in specified domain, do not add. */
	if (e != NULL || a->nsrccopy == 0)
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
	gfarm_stringlist *list, struct gfrep_arg *gfrep_arg,
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
	host_copy.a = gfrep_arg;

	for (i = 0; i < gfarm_stringlist_length(list); i++) {
		file = gfarm_stringlist_elem(list, i);
		e = gfarm_url_make_path(file, &path);
		if (e != NULL)
			goto free_hash;

		host_copy.file = file;
		if (gfrep_arg->section == NULL)
			e = gfarm_foreach_section(
				add_sec, path, &host_copy, NULL);
		else
			e = do_section(
				add_sec, path, gfrep_arg->section, &host_copy);
		free(path);
		if (e == GFARM_ERR_NO_FRAGMENT_INFORMATION) {
			e = NULL;
			continue;
		}
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

static int
count_copy(struct gfarm_section_xinfo *xinfo, struct gfarm_hash_table *hash)
{
	int i, count = 0;
	char *s;

	for (i = 0; i < xinfo->ncopy; ++i) {
		s = xinfo->copy[i];
		if (gfarm_hash_lookup(hash, s, strlen(s) + 1))
			++count;
	}
	return (count);
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
	struct gfrep_arg *arg)
{
	int i, j, nsinfo = *nsinfop, src_ncopy, dst_ncopy;
	struct gfarm_section_xinfo **sinfo = *sinfop;
	struct gfarm_hash_table *src_hosthash, *dst_hosthash;
	gfarm_list slist;
	char *e;

	e = create_hash_table_from_string_list(arg->nsrc, arg->src,
		HOSTHASH_SIZE, &src_hosthash);
	if (e != NULL)
		return (e);
	e = create_hash_table_from_string_list(arg->ndst, arg->dst,
		HOSTHASH_SIZE, &dst_hosthash);
	if (e != NULL)
		goto free_src_hosthash;
	e = gfarm_list_init(&slist);
	if (e != NULL)
		goto free_dst_hosthash;

	for (i = 0; i < nsinfo; gfarm_section_xinfo_free(sinfo[i++])) {
		src_ncopy = count_copy(sinfo[i], src_hosthash);
		dst_ncopy = count_copy(sinfo[i], dst_hosthash);
		if (src_ncopy == 0 || dst_ncopy >= arg->ncopy)
			continue;
		for (j = 0; j < arg->ncopy - dst_ncopy; ++j) {
			e = gfarm_list_add_xinfo2(sinfo[i], &slist);
			if (e != NULL)
				goto free_list;
		}
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
 free_dst_hosthash:
	gfarm_hash_table_free(dst_hosthash);
 free_src_hosthash:
	gfarm_hash_table_free(src_hosthash);
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
	printf("ncopy = %d\n", arg->ncopy);
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

static int
is_enough_space(char *host, file_offset_t size)
{
	gfarm_int32_t bsize;
	file_offset_t blocks, bfree, bavail, files, ffree, favail;
	char *e;

	e = gfs_statfsnode(host, &bsize,
	    &blocks, &bfree, &bavail, &files, &ffree, &favail);
	return (e == NULL && bavail * bsize >= size);
}

static char *
replicate(int nsinfo, struct gfarm_section_xinfo **sinfo,
	  int nthreads, struct gfrep_arg *arg)
{
	int i, pi, tnum, nth, nerr = 0;
	int max_niter;
	char *e;
	int ndst = arg->ndst, nsrc = arg->nsrc;
	char **dst = arg->dst;

	if (ndst <= 0)
		return "no destination node";
	if (nsrc <= 0)
		return "no source node";
	if (ndst < arg->ncopy)
		return "not enough number of destination nodes";
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
		struct gfarm_section_xinfo *si = sinfo[i];
#ifdef LIBGFARM_NOT_MT_SAFE
		pid_t pid;
		int s, rv;
#endif
		if (!arg->quiet)
			printf("%s (%s)\n", si->file, si->i.section);
		if (arg->verbose)
			gfarm_section_xinfo_print(si);
		if (tnum + pi * nth > ndst)
			pi = 0;

#ifdef LIBGFARM_NOT_MT_SAFE
		pid = fork();
		if (pid == 0) {
#endif
			di = (tnum + pi * nth) % ndst;
			/*
			 * check whether the destination node already
			 * has the file replica, or whether the
			 * destination node has enough disk space.
			 *
			 * XXX - the destination may conflict
			 */
			max_niter = ndst;
			while (gfarm_file_section_copy_info_does_exist(
				si->i.pathname, si->i.section, dst[di])
			       || ! is_enough_space(dst[di], si->i.filesize)) {
				if (arg->verbose)
					printf("%02d: warning: the destination"
					       " may conflict: %s -> %s\n",
					       tnum, dst[di],
					       dst[(di + 1) % ndst]);
				di = (di + 1) % ndst;
				if (--max_niter == 0)
					break;
			}
			if (max_niter == 0) {
				e = "not enough free disk space";
				goto skip_replication;
			}
			if (arg->verbose) {
				printf("%02d(%03d): %s (%s) --> %s\n",
				       tnum, pi, si->file,
				       si->i.section, dst[di]);
				gettimeofday(&t1, NULL);
			}
			if (!arg->noexecute)
				e = act->transfer_to(si->file, si->i.section,
					dst[di]);
			else {
				printf("%s (%s): %s to %s\n", act->action,
				       si->file, si->i.section, dst[di]);
				e = NULL;
			}
			if (arg->verbose) {
				gettimeofday(&t2, NULL);
				t = gfarm_timerval_sub(&t2, &t1);
				printf("%s (%s): %f sec  %f MB/sec\n",
				       si->file, si->i.section, t,
				       si->i.filesize / t / 1024 / 1024);
			}
		skip_replication:
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
		++pi;
	}
	}
	return (nerr == 0 ? NULL : "error happens during replication");
}

static int
usage()
{
	fprintf(stderr, "Usage: %s [-mnqv] [-I <section>] [-S <src_domain>]"
		" [-D <dst_domain>]\n", program_name);
	fprintf(stderr, "\t[-h <src_hostlist>] [-H <dst_hostlist>]"
		" [-N <#replica>]");
#ifdef _OPENMP
	fprintf(stderr, " [-j <#thread>]");
#endif
	fprintf(stderr, "\n\t<gfarm_url>...\n");
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
	struct gfarm_hash_table *src_hosthash = NULL;
	gfarm_stringlist paths, allpaths;
	gfs_glob_t types;
	int mode_src_ch = 0, mode_dst_ch = 0, num_replicas = 1, parallel = -1;
	int noexecute = 0, quiet = 0, verbose = 0;
	int i, nsrchosts, ndsthosts, nsinfo;
	char **srchosts, **dsthosts, ch, *e;
	struct gfrep_arg gfrep_arg;
	struct gfarm_section_xinfo **sinfo;

#ifdef __GNUC__ /* workaround gcc warning: may be used uninitialized */
	nsinfo = 0;
	sinfo = NULL;
#endif

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

	/* make writing-to-stderr atomic, for GfarmFS-FUSE log output */
	setvbuf(stderr, NULL, _IOLBF, 0);

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

	if (src_hostfile != NULL) {
		e = create_hostlist(src_hostfile, NULL, &nsrchosts, &srchosts);
		error_check(e);
		e = create_hash_table_from_string_list(nsrchosts, srchosts,
			HOSTHASH_SIZE, &src_hosthash);
		error_check(e);
		gfarm_strings_free_deeply(nsrchosts, srchosts);
	}
	gfrep_arg.src_domain = src_domain;
	gfrep_arg.src_hosthash = src_hosthash;
	gfrep_arg.section = section;
	e = create_host_and_file_section_list(&allpaths, &gfrep_arg,
		&nsrchosts, &srchosts, &nsinfo, &sinfo);
	if (src_hosthash != NULL)
		gfarm_hash_table_free(src_hosthash);
	if (e == NULL)
		e = gfarm_schedule_search_idle_acyclic_hosts(
			nsrchosts, srchosts, &nsrchosts, srchosts);
	error_check(e);

	e = create_hostlist(dst_hostfile, dst_domain, &ndsthosts, &dsthosts);
	if (e == NULL)
		e = gfarm_schedule_search_idle_acyclic_hosts_to_write(
			ndsthosts, dsthosts, &ndsthosts, dsthosts);
	error_check(e);

	gfrep_arg.nsrc = nsrchosts;
	gfrep_arg.ndst = ndsthosts;
	gfrep_arg.src = srchosts;
	gfrep_arg.dst = dsthosts;
	gfrep_arg.ncopy = num_replicas;
	e = refine_file_section_list(&nsinfo, &sinfo, &gfrep_arg);
	error_check(e);
	if (!quiet)
		printf(" done\n");

	gfrep_arg.noexecute = noexecute;
	gfrep_arg.verbose = verbose;
	gfrep_arg.quiet = quiet;

	e = replicate(nsinfo, sinfo, parallel, &gfrep_arg);
	error_check(e);

	gfarm_array_free_deeply(nsinfo, sinfo,
		(void (*)(void *))gfarm_section_xinfo_free);
	gfarm_strings_free_deeply(nsrchosts, srchosts);
	gfarm_strings_free_deeply(ndsthosts, dsthosts);
	gfarm_stringlist_free_deeply(&allpaths);
	e = gfarm_terminate();
	error_check(e);

	return (0);
}
