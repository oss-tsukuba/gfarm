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
static int omp_get_num_threads(void){ return (1); }
static int omp_get_thread_num(void){ return (0); }
#endif

#include <gfarm/gfarm.h>
#include "hash.h"
#include "config.h"
#include "gfm_client.h"
#include "gfarm_foreach.h"
#include "gfarm_list.h"

#define HOSTHASH_SIZE	101

static char *program_name = "gfrep";

struct action {
	char *action;
	gfarm_error_t (*transfer_to)(char *, char *, int);
};

struct action replicate_mode = {
	"replicate",
	gfs_replicate_to
};

struct action migrate_mode = {
	"migrate",
	gfs_migrate_to
};

struct action *act = &replicate_mode;

struct gfrep_arg {
	int ndst, nsrc;
	char **dst, **src;
	int *dst_port;
	int num_replicas;
	int noexecute, verbose, quiet;
};

struct file_info {
	char *pathname;
	gfarm_off_t filesize;
	int ncopy;
	char **copy;
};

struct flist_arg {
	char *src_domain, *dst_domain;

	/* hash table from the input host file */
	struct gfarm_hash_table *src_hosthash, *dst_hosthash;

	/* hash table to count the num of source nodes */
	struct gfarm_hash_table *srchash;

	int num_replicas;

	/* file_info list */
	gfarm_list *slist;
};

static void
file_info_free(struct file_info *info)
{
	if (info->pathname != NULL)
		free(info->pathname);
	if (info->copy != NULL)
		gfarm_strings_free_deeply(info->ncopy, info->copy);
	free(info);
}

static struct file_info *
file_info_alloc(void)
{
	struct file_info *i;

	GFARM_MALLOC(i);
	if (i == NULL)
		return (NULL);

	i->pathname = NULL;
	i->copy = NULL;
	return (i);
}

static gfarm_error_t
gfarm_list_add_file_info(char *pathname, gfarm_off_t filesize,
	int ncopy, char **copy, gfarm_list *list)
{
	struct file_info *info;
	gfarm_error_t e;
	int i;

	info = file_info_alloc();
	if (info == NULL)
		return (GFARM_ERR_NO_MEMORY);

	info->pathname = strdup(pathname);
	if (info->pathname == NULL) {
		file_info_free(info);
		return (GFARM_ERR_NO_MEMORY);
	}
	GFARM_MALLOC_ARRAY(info->copy, ncopy);
	if (info->copy == NULL) {
		file_info_free(info);
		return (GFARM_ERR_NO_MEMORY);
	}
	for (i = 0; i < ncopy; ++i) {
		info->copy[i] = strdup(copy[i]);
		if (info->copy[i] == NULL) {
			gfarm_strings_free_deeply(i, info->copy);
			info->copy = NULL;
			file_info_free(info);
			return (GFARM_ERR_NO_ERROR);
		}
	}
	info->ncopy = ncopy;
	info->filesize = filesize;
	e = gfarm_list_add(list, info);
	if (e != GFARM_ERR_NO_ERROR)
		file_info_free(info);
	return (e);
}

static gfarm_error_t
create_filelist(char *file, struct gfs_stat *st, void *arg)
{
	struct flist_arg *a = arg;
	int i, j, ncopy, src_ncopy = 0, dst_ncopy = 0;
	char **copy;
	gfarm_error_t e;

	e = gfs_replica_list_by_name(file, &ncopy, &copy);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	for (i = 0; i < ncopy; ++i) {
		if ((a->src_hosthash == NULL || gfarm_hash_lookup(
			a->src_hosthash, copy[i], strlen(copy[i]) + 1)) &&
		    gfarm_host_is_in_domain(copy[i], a->src_domain)) {
			++src_ncopy;
		}
		if ((a->dst_hosthash == NULL || gfarm_hash_lookup(
			a->dst_hosthash, copy[i], strlen(copy[i]) + 1)) &&
		    gfarm_host_is_in_domain(copy[i], a->dst_domain)) {
			++dst_ncopy;
		}
	}
	/*
	 * if there is no replica in a set of source nodes or enough
	 * number of replicas in a set of destination nodes, do not add.
	 */
	if (src_ncopy == 0 || dst_ncopy >= a->num_replicas) {
		e = GFARM_ERR_NO_ERROR;
		goto free_copy;
	}

	/* add source nodes to srchash to count the number of source nodes */
	for (i = 0; i < ncopy; ++i) {
		char *s = copy[i];

		if ((a->src_hosthash == NULL || gfarm_hash_lookup(
			a->src_hosthash, s, strlen(s) + 1)) &&
		    gfarm_host_is_in_domain(s, a->src_domain))
			gfarm_hash_enter(a->srchash, s, strlen(s)+1, 0, NULL);
	}

	/* add a file info to slist */
	for (j = 0; j < a->num_replicas - dst_ncopy; ++j) {
		e = gfarm_list_add_file_info(file, st->st_size, ncopy, copy,
			a->slist);
		if (e != GFARM_ERR_NO_ERROR)
			break;
	}
 free_copy:
	gfarm_strings_free_deeply(ncopy, copy);

	return (e);
}

static gfarm_error_t
gfarm_hash_to_string_array(struct gfarm_hash_table *hash,
	int *array_lengthp, char ***arrayp)
{
	struct gfarm_hash_iterator iter;
	struct gfarm_hash_entry *entry;
	gfarm_stringlist ls;
	char *ent, **array;
	gfarm_error_t e;

	e = gfarm_stringlist_init(&ls);
	if (e != GFARM_ERR_NO_ERROR)
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
		if (e != GFARM_ERR_NO_ERROR)
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
	if (e == GFARM_ERR_NO_ERROR)
		gfarm_stringlist_free(&ls);
	else
		gfarm_stringlist_free_deeply(&ls);
	return (e);
}

static gfarm_error_t
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
	return (GFARM_ERR_NO_ERROR);
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
		printf("%s:%d\n", arg->dst[i], arg->dst_port[i]);
	printf("num_replicas = %d\n", arg->num_replicas);
}

static void
print_file_info(struct file_info *info)
{
	int i;

	printf("file: %s, size: %" GFARM_PRId64 "\n",
	       info->pathname, info->filesize);
	printf("ncopy = %d\n", info->ncopy);
	for (i = 0; i < info->ncopy; ++i)
		printf("%s\n", info->copy[i]);
	fflush(stdout);
}

static void
print_file_list(struct gfarm_list *list)
{
	int i;

	for (i= 0; i < gfarm_list_length(list); ++i)
		print_file_info(gfarm_stringlist_elem(list, i));
}

static int
filesizecmp_inv(const void *a, const void *b)
{
	struct file_info * const *aa = a, * const *bb = b;
	gfarm_off_t aaa = (*aa)->filesize, bbb = (*bb)->filesize;

	if (aaa < bbb)
		return (1);
	if (aaa > bbb)
		return (-1);
	return (0);
}

static int
is_enough_space(char *host, gfarm_off_t size)
{
#if 0 /* not yet in gfarm v2 */
	gfarm_int32_t bsize;
	gfarm_off_t blocks, bfree, bavail, files, ffree, favail;
	gfarm_error_t e;

	e = gfs_statfsnode(host, &bsize,
	    &blocks, &bfree, &bavail, &files, &ffree, &favail);
	return (e == GFARM_ERR_NO_ERROR && bavail * bsize >= size);
#endif
	return (1);
}

static int
file_copy_does_exist(struct file_info *info, char *host)
{
	int i;

	for (i = 0; i < info->ncopy; ++i)
		if (strcmp(info->copy[i], host) == 0)
			return (1);
	return (0);
}

static const char *
replicate(int nfinfo, struct file_info **finfo,
	  int nthreads, struct gfrep_arg *arg)
{
	int i, pi, tnum, nth, nerr = 0;
	int max_niter;
	gfarm_error_t e;
	int ndst = arg->ndst, nsrc = arg->nsrc;
	char **dst = arg->dst;
	const char *errmsg;
	int *dst_port = arg->dst_port;

	if (ndst <= 0)
		return ("no destination node");
	if (nsrc <= 0)
		return ("no source node");
	if (ndst < arg->num_replicas)
		return ("not enough number of destination nodes");
	if (nfinfo <= 0)
		return (NULL); /* no file */
	/* sort 'sinfo' in descending order wrt file size */
	qsort(finfo, nfinfo, sizeof(*finfo), filesizecmp_inv);

	if (nthreads <= 0) {
		nthreads = nfinfo;
		if (ndst < nthreads)
			nthreads = ndst;
		if (nsrc < nthreads)
			nthreads = nsrc;
	}
	if (arg->verbose) {
		print_gfrep_arg(arg);
		printf("files: %d\n", nfinfo);
#ifdef _OPENMP
		printf("parallel replication using %d streams\n", nthreads);
#endif
	}
	omp_set_num_threads(nthreads);

#pragma omp parallel reduction(+:nerr) private(pi,tnum,nth)
	{
	pi = 0;
	tnum = omp_get_thread_num();
	nth = omp_get_num_threads();

#pragma omp for schedule(dynamic)
	for (i = 0; i < nfinfo; ++i) {
		int di;
		struct timeval t1, t2;
		double t;
		struct file_info *fi = finfo[i];
#ifdef LIBGFARM_NOT_MT_SAFE
		pid_t pid;
		int s, rv;
#endif
		if (!arg->quiet)
			printf("%s\n", fi->pathname);
		if (arg->verbose)
			print_file_info(fi);
		if (tnum + pi * nth >= ndst)
			pi = 0;

#ifdef LIBGFARM_NOT_MT_SAFE
		pid = fork();
		if (pid == 0) {
			e = gfarm_terminate();
			if (e == GFARM_ERR_NO_ERROR)
				e = gfarm_initialize(NULL, NULL);
			if (e != GFARM_ERR_NO_ERROR) {
				errmsg = gfarm_error_string(e);
				goto skip_replication;
			}
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
			while (file_copy_does_exist(fi, dst[di])
			       || ! is_enough_space(dst[di], fi->filesize)) {
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
				e = GFARM_ERR_NO_SPACE;
				errmsg = "not enough free disk space";
				goto skip_replication;
			}
			if (arg->verbose) {
				printf("%02d(%03d): %s --> %s\n",
				       tnum, pi, fi->pathname, dst[di]);
				fflush(stdout);
				gettimeofday(&t1, NULL);
			}
			if (!arg->noexecute) {
				e = act->transfer_to(fi->pathname,
					dst[di], dst_port[di]);
				errmsg = gfarm_error_string(e);
			}
			else {
				printf("%s (%s) to %s\n", act->action,
				       fi->pathname, dst[di]);
				e = GFARM_ERR_NO_ERROR;
				errmsg = gfarm_error_string(e);
			}
			if (arg->verbose && e == GFARM_ERR_NO_ERROR) {
				gettimeofday(&t2, NULL);
				t = gfarm_timerval_sub(&t2, &t1);
				printf("%s: %f sec  %f MB/sec\n",
				       fi->pathname, t,
				       fi->filesize / t / 1024 / 1024);
			}
		skip_replication:
			if (e != GFARM_ERR_NO_ERROR) {
#ifndef LIBGFARM_NOT_MT_SAFE
				++nerr;
#endif
				fprintf(stderr, "%s: %s\n",
					fi->pathname, errmsg);
			}
#ifdef LIBGFARM_NOT_MT_SAFE
			fflush(stdout);
			sync();
			_exit(e == GFARM_ERR_NO_ERROR ? 0 : 1);
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
	fprintf(stderr, "Usage: %s [-mnqv] [-S <src_domain>]"
		" [-D <dst_domain>]\n", program_name);
	fprintf(stderr, "\t[-h <src_hostlist>] [-H <dst_hostlist>]"
		" [-N <#replica>]");
#ifdef _OPENMP
	fprintf(stderr, " [-j <#thread>]");
#endif
	fprintf(stderr, "\n\t<gfarm_path>...\n");
	exit(EXIT_FAILURE);
}

static int
error_check(gfarm_error_t e)
{
	if (e == GFARM_ERR_NO_ERROR)
		return (0);
	fprintf(stderr, "%s: %s\n", program_name, gfarm_error_string(e));
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

static gfarm_error_t
create_hosthash_from_file(char *hostfile,
	int hashsize, struct gfarm_hash_table **hashp)
{
	int error_line = -1, nhosts;
	gfarm_error_t e;
	char **hosts;

	if (hostfile == NULL) {
		*hashp = NULL;
		return (GFARM_ERR_NO_ERROR);
	}
	e = gfarm_hostlist_read(hostfile, &nhosts, &hosts, &error_line);
	if (e != GFARM_ERR_NO_ERROR) {
		if (error_line != -1)
			fprintf(stderr, "%s: line %d: %s\n", hostfile,
				error_line, gfarm_error_string(e));
		else
			fprintf(stderr, "%s: %s\n", hostfile,
				gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}
	e = create_hash_table_from_string_list(nhosts, hosts,
			HOSTHASH_SIZE, hashp);
	gfarm_strings_free_deeply(nhosts, hosts);
	return (e);
}

static gfarm_error_t
create_hostlist_by_domain_and_hash(char *domain,
	struct gfarm_hash_table *hosthash,
	int *nhostsp, char ***hostsp, int **portsp)
{
	int ninfo, i, j, *ports;
	struct gfarm_host_sched_info *infos;
	char **hosts;
	gfarm_error_t e;

	e = gfm_client_schedule_host_domain(gfarm_metadb_server, domain,
		&ninfo, &infos);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	/* XXX - abandon CPU load and available capacity */
	GFARM_MALLOC_ARRAY(hosts, ninfo);
	if (hosts == NULL) {
		gfarm_host_sched_info_free(ninfo, infos);
		return (GFARM_ERR_NO_MEMORY);
	}
	GFARM_MALLOC_ARRAY(ports, ninfo);
	if (ports == NULL) {
		free(hosts);
		gfarm_host_sched_info_free(ninfo, infos);
		return (GFARM_ERR_NO_MEMORY);
	}
	for (i = 0, j = 0; i < ninfo; ++i) {
		char *host = infos[i].host;

		if (hosthash == NULL ||
		    gfarm_hash_lookup(hosthash, host, strlen(host) + 1)) {
			hosts[j] = strdup(host);
			ports[j] = infos[i].port;
			if (hosts[j] == NULL) {
				gfarm_strings_free_deeply(j, hosts);
				return (GFARM_ERR_NO_MEMORY);
			}
			++j;
		}
	}
	gfarm_host_sched_info_free(ninfo, infos);
	*hostsp = hosts;
	*portsp = ports;
	*nhostsp = j;

	return (e);
}

int
main(int argc, char *argv[])
{
	char *src_domain = "", *dst_domain = "";
	char *src_hostfile = NULL, *dst_hostfile = NULL;
	struct gfarm_hash_table *src_hosthash = NULL, *dst_hosthash = NULL;
	gfarm_stringlist paths;
	gfs_glob_t types;
	int mode_src_ch = 0, mode_dst_ch = 0, num_replicas = 1, parallel = -1;
	int noexecute = 0, quiet = 0, verbose = 0;
	int i, src_nhosts, dst_nhosts, nfinfo, *dst_ports;
	char **src_hosts, **dst_hosts, ch;
	gfarm_error_t e;
	const char *errmsg;
	struct gfrep_arg gfrep_arg;
	struct flist_arg flist_arg;
	struct file_info **finfo;
	gfarm_list slist;
	struct gfarm_hash_table *srchash;

#ifdef __GNUC__ /* workaround gcc warning: may be used uninitialized */
	src_nhosts = dst_nhosts = 0;
	src_hosts = dst_hosts = NULL;
	dst_ports = NULL;
#endif

	if (argc >= 1)
		program_name = basename(argv[0]);

	e = gfarm_initialize(&argc, &argv);
	error_check(e);

#ifdef _OPENMP
	while ((ch = getopt(argc, argv, "h:j:mnqvS:D:H:N:?")) != -1) {
#else
	while ((ch = getopt(argc, argv, "h:mnqvS:D:H:N:?")) != -1) {
#endif
		switch (ch) {
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

	if (!quiet) {
		printf("constructing file list...");
		fflush(stdout);
	}

	e = gfarm_stringlist_init(&paths);
	if (e == GFARM_ERR_NO_ERROR) {
		e = gfs_glob_init(&types);
		if (e == GFARM_ERR_NO_ERROR) {
			for (i = 0; i < argc; i++)
				gfs_glob(argv[i], &paths, &types);
			gfs_glob_free(&types);
		}
	}
	error_check(e);

	e = gfarm_list_init(&slist);
	error_check(e);
	srchash = gfarm_hash_table_alloc(HOSTHASH_SIZE,
		gfarm_hash_casefold, gfarm_hash_key_equal_casefold);
	if (srchash == NULL)
		error_check(GFARM_ERR_NO_MEMORY);

	e = create_hosthash_from_file(src_hostfile,
		HOSTHASH_SIZE, &src_hosthash);
	error_check(e);
	e = create_hosthash_from_file(dst_hostfile,
		HOSTHASH_SIZE, &dst_hosthash);
	error_check(e);
	flist_arg.src_domain = src_domain;
	flist_arg.src_hosthash = src_hosthash;
	flist_arg.dst_domain = dst_domain;
	flist_arg.dst_hosthash = dst_hosthash;
	flist_arg.num_replicas = num_replicas;
	flist_arg.slist = &slist;
	flist_arg.srchash = srchash;

	for (i = 0; i < gfarm_stringlist_length(&paths); i++) {
		char *file = gfarm_stringlist_elem(&paths, i);

		e = gfarm_foreach_directory_hierarchy(
			create_filelist, NULL, NULL, file, &flist_arg);
		if (e != GFARM_ERR_NO_ERROR)
			break;
	}
	gfarm_stringlist_free_deeply(&paths);
	error_check(e);

	if (!quiet)
		printf(" done\n");
	if (verbose)
		print_file_list(flist_arg.slist);
	if (gfarm_list_length(flist_arg.slist) <= 0)
		exit(0); /* no file */

	finfo = gfarm_array_alloc_from_list(flist_arg.slist);
	nfinfo = gfarm_list_length(flist_arg.slist);
	gfarm_list_free(flist_arg.slist);
	e = gfarm_hash_to_string_array(
		flist_arg.srchash, &src_nhosts, &src_hosts);
	error_check(e);
	gfarm_hash_table_free(flist_arg.srchash);

	if (!quiet) {
		printf("investigating hosts...");
		fflush(stdout);
	}
	e = create_hostlist_by_domain_and_hash(dst_domain, dst_hosthash,
		&dst_nhosts, &dst_hosts, &dst_ports);
	error_check(e);
	if (!quiet)
		printf(" done\n");

	gfrep_arg.nsrc = src_nhosts;
	gfrep_arg.src = src_hosts;
	gfrep_arg.ndst = dst_nhosts;
	gfrep_arg.dst = dst_hosts;
	gfrep_arg.dst_port = dst_ports;
	gfrep_arg.num_replicas = num_replicas;
	gfrep_arg.noexecute = noexecute;
	gfrep_arg.verbose = verbose;
	gfrep_arg.quiet = quiet;

	errmsg = replicate(nfinfo, finfo, parallel, &gfrep_arg);
	if (errmsg != NULL)
		fprintf(stderr, "%s\n", errmsg), exit(EXIT_FAILURE);

	gfarm_array_free_deeply(nfinfo, finfo,
		(void (*)(void *))file_info_free);
	gfarm_strings_free_deeply(src_nhosts, src_hosts);
	gfarm_strings_free_deeply(dst_nhosts, dst_hosts);
	free(dst_ports);
	e = gfarm_terminate();
	error_check(e);

	return (0);
}
