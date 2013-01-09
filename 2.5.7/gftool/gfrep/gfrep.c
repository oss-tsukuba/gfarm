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
static void omp_set_num_threads(int n) { return; }
static int omp_get_num_threads(void){ return (1); }
static int omp_get_thread_num(void){ return (0); }
#endif

#include <gfarm/gfarm.h>
#include "hash.h"
#include "config.h"
#include "gfm_client.h"
#include "gfarm_foreach.h"
#include "gfarm_list.h"
#include "lookup.h"

#define HOSTHASH_SIZE	101

static char *program_name = "gfrep";

/* options */
static int opt_quiet;		/* -q */
static int opt_noexec;		/* -n */
static int opt_verbose;		/* -v */
static int opt_nrep = 1;	/* -N */
static int opt_remove;		/* -x */

struct gfrep_arg {
	int ndst, nsrc;
	char **dst, **src;
	int *dst_port;
};

struct file_info {
	char *pathname;
	gfarm_off_t filesize;
	int ncopy;
	char **copy;
	int surplus_ncopy;
};

/* for create_filelist */
struct flist {
	/* domain name */
	char *src_domain, *dst_domain;
	/* hash table from the input host file */
	struct gfarm_hash_table *src_hosthash, *dst_hosthash;
	/* hash table to count the num of source nodes */
	struct gfarm_hash_table *srchash;
	/* file_info list */
	gfarm_list slist, dlist;
};

static gfarm_error_t
gfrep_replicate_to(struct file_info *fi, int di, struct gfrep_arg *a)
{
	return (gfs_replicate_to(fi->pathname, a->dst[di], a->dst_port[di]));
}

static int remove_replicas(struct file_info *, int, int, char **);

static gfarm_error_t
gfrep_migrate_to(struct file_info *fi, int di, struct gfrep_arg *a)
{
	gfarm_error_t e;

	e = gfs_replicate_to(fi->pathname, a->dst[di], a->dst_port[di]);
	if (e == GFARM_ERR_NO_ERROR)
		e = remove_replicas(fi, 1, a->nsrc, a->src);
	return (e);
}

static gfarm_error_t
gfrep_remove_replica(struct file_info *fi, int di, struct gfrep_arg *a)
{
	return (remove_replicas(fi, fi->surplus_ncopy, a->ndst, a->dst));
}

struct action {
	char *msg;
	gfarm_error_t (*action)(struct file_info *, int, struct gfrep_arg *);
};

struct action replicate_mode = {
	"replicate",
	gfrep_replicate_to
};

struct action migrate_mode = {
	"migrate",
	gfrep_migrate_to
};

struct action remove_mode = {
	"remove",
	gfrep_remove_replica
};

struct action *act = &replicate_mode;

static void
file_info_free(struct file_info *info)
{
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
	int ncopy, char **copy, int surplus, gfarm_list *list)
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
	info->surplus_ncopy = surplus;
	e = gfarm_list_add(list, info);
	if (e != GFARM_ERR_NO_ERROR)
		file_info_free(info);
	return (e);
}

static gfarm_error_t
create_filelist(char *file, struct gfs_stat *st, void *arg)
{
	struct flist *a = arg;
	int i, j, ncopy, src_ncopy = 0, dst_ncopy = 0;
	char **copy;
	gfarm_error_t e;

	if (!GFARM_S_ISREG(st->st_mode)) {
		if (opt_verbose)
			printf("%s: not a regular file, skipped\n", file);
		return (GFARM_ERR_NO_ERROR);
	}
	e = gfs_replica_list_by_name(file, &ncopy, &copy);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	/* if there is no available file replica, display error message */
	if (ncopy == 0 && st->st_size > 0) {
		fprintf(stderr, "%s: no available file repilca\n", file);
		e = GFARM_ERR_NO_ERROR;
		goto free_copy;
	}
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
	 * if there is no replica in a set of source nodes or there
	 * are already specified number of replicas in a set of
	 * destination nodes, do not add.
	 */
	if (src_ncopy == 0 || dst_ncopy == opt_nrep) {
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
	for (j = 0; j < opt_nrep - dst_ncopy; ++j) {
		e = gfarm_list_add_file_info(file, st->st_size, ncopy, copy,
			0, &a->slist);
		if (e != GFARM_ERR_NO_ERROR)
			goto free_copy;
	}

	/* add a file info to dlist if too many file replicas exist */
	if (dst_ncopy > opt_nrep) {
		e = gfarm_list_add_file_info(file, st->st_size, ncopy, copy,
			dst_ncopy - opt_nrep, &a->dlist);
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
	printf("num_replicas = %d\n", opt_nrep);
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

	for (i = 0; i < gfarm_list_length(list); ++i)
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
is_enough_space(char *host, int port, gfarm_off_t size)
{
	gfarm_int32_t bsize;
	gfarm_off_t blocks, bfree, bavail, files, ffree, favail;
	gfarm_error_t e;

	e = gfs_statfsnode(host, port, &bsize,
	    &blocks, &bfree, &bavail, &files, &ffree, &favail);
	if (e != GFARM_ERR_NO_ERROR)
		fprintf(stderr, "%s: %s\n", host, gfarm_error_string(e));
	if (size < gfarm_get_minimum_free_disk_space())
		size = gfarm_get_minimum_free_disk_space();
	return (e == GFARM_ERR_NO_ERROR && bavail * bsize >= size);
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

static int
remove_replicas(struct file_info *fi, int ncopy, int nhost, char **host)
{
	int i, j, k;
	gfarm_error_t e, e_save = GFARM_ERR_NO_ERROR;

	i = 0;
	/* XXX - linear search */
	for (j = 0; j < fi->ncopy && i < ncopy; ++j)
		for (k = 0; k < nhost && i < ncopy; ++k) {
			if (strcmp(fi->copy[j], host[k]) == 0) {
				if (opt_verbose) {
					printf("remove %s on %s\n",
					       fi->pathname, host[k]);
					fflush(stdout);
				}
				e = gfs_replica_remove_by_file(
					fi->pathname, host[k]);
				if (e == GFARM_ERR_NO_ERROR)
					++i;
				else {
					/* error is always overwritten */
					e_save = e;
					fprintf(stderr, "%s: %s\n",
						fi->pathname,
						gfarm_error_string(e));
				}
			}
		}

	return (i >= ncopy ? GFARM_ERR_NO_ERROR : e_save);
}

static gfarm_error_t
action(struct action *act, int tnum, int nth, int pi, struct file_info *fi,
	struct gfrep_arg *arg)
{
	int di, max_niter;
	int ndst = arg->ndst;
	char **dst = arg->dst;
	int *dst_port = arg->dst_port;
	gfarm_error_t e;

	di = (tnum + pi * nth) % ndst;
	/*
	 * check whether the destination node already
	 * has the file replica, or whether the
	 * destination node has enough disk space.
	 *
	 * XXX - the destination may conflict
	 *
	 * fi->surplus_ncopy == 0 means to create file replicas.
	 */
	max_niter = ndst;
	while (1) {
		while (fi->surplus_ncopy == 0
		       && (file_copy_does_exist(fi, dst[di])
			   || !is_enough_space(
				   dst[di], dst_port[di], fi->filesize))
		       && max_niter > 0) {
			if (opt_verbose)
				printf("%02d: warning: the destination may "
				       "conflict: %s -> %s\n", tnum, dst[di],
				       dst[(di + 1) % ndst]);
			di = (di + 1) % ndst;
			--max_niter;
		}
		if (max_niter == 0)
			return (GFARM_ERR_NO_SPACE);

		if (fi->surplus_ncopy == 0 && opt_verbose) {
			printf("%02d(%03d): %s --> %s\n",
			       tnum, pi, fi->pathname, dst[di]);
			fflush(stdout);
		}
		e = act->action(fi, di, arg);
		/* this may happen when this gfrep creates the replica */
		if (e == GFARM_ERR_ALREADY_EXISTS
		    || e == GFARM_ERR_OPERATION_ALREADY_IN_PROGRESS) {
			if (opt_verbose) {
				printf("%s: %s\n", fi->pathname,
				       gfarm_error_string(e));
				printf("%02d: warning: the destination may "
				       "conflict: %s -> %s\n", tnum, dst[di],
				       dst[(di + 1) % ndst]);
			}
			di = (di + 1) % ndst;
			--max_niter;
			continue;
		}
		break;
	}
	return (e);
}

static const char *
pfor(struct action *act, int nfinfo, struct file_info **finfo,
	  int nthreads, struct gfrep_arg *arg)
{
	int i, pi, tnum, nth, nerr = 0;
	gfarm_error_t e;
	int ndst = arg->ndst, nsrc = arg->nsrc;
	const char *errmsg;

	if (ndst <= 0)
		return ("no destination node");
	if (nsrc <= 0)
		return ("no source node");
	if (ndst < opt_nrep)
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
	if (opt_verbose) {
		print_gfrep_arg(arg);
		printf("files: %d\n", nfinfo);
#ifdef _OPENMP
		printf("parallel %s in %d threads\n", act->msg, nthreads);
#endif
	}
	omp_set_num_threads(nthreads);

#pragma omp parallel reduction(+:nerr) private(pi, tnum, nth)
	{
	pi = 0;
	tnum = omp_get_thread_num();
	nth = omp_get_num_threads();

#pragma omp for schedule(dynamic)
	for (i = 0; i < nfinfo; ++i) {
		struct timeval t1, t2;
		double t;
		struct file_info *fi = finfo[i];
#ifdef LIBGFARM_NOT_MT_SAFE
		pid_t pid;
		int s, rv;
#endif
		if (opt_noexec || !opt_quiet)
			printf("%s\n", fi->pathname);
		if (opt_verbose)
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
			if (opt_verbose)
				gettimeofday(&t1, NULL);
			if (!opt_noexec) {
				e = action(act, tnum, nth, pi, fi, arg);
				errmsg = gfarm_error_string(e);
			} else {
				e = GFARM_ERR_NO_ERROR;
				errmsg = gfarm_error_string(e);
			}
			if (opt_verbose && e == GFARM_ERR_NO_ERROR) {
				gettimeofday(&t2, NULL);
				t = gfarm_timerval_sub(&t2, &t1);
				printf("%s: %f sec  %f MB/sec\n",
				       fi->pathname, t,
				       fi->filesize / t / 1024 / 1024);
			}
#ifdef LIBGFARM_NOT_MT_SAFE
 skip_replication:
#endif
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
		while ((rv = waitpid(pid, &s, 0)) == -1 && errno == EINTR)
			;
		if (rv == -1 || (WIFEXITED(s) && WEXITSTATUS(s) != 0))
			++nerr;
#endif
		++pi;
	}
	}
	return (nerr == 0 ? NULL : "error happens during operations");
}

static int
usage()
{
	fprintf(stderr, "Usage: %s [-mnqvx] [-S <src_domain>]"
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

static int
compare_available_capacity(const void *s1, const void *s2)
{
	const struct gfarm_host_sched_info *h1 = s1;
	const struct gfarm_host_sched_info *h2 = s2;
	gfarm_uint64_t a1, a2;

	a1 = h1->disk_avail;
	a2 = h2->disk_avail;

	if (a1 < a2)
		return (-1);
	else if (a1 > a2)
		return (1);
	else
		return (0);
}

static int
compare_available_capacity_r(const void *s1, const void *s2)
{
	return (-compare_available_capacity(s1, s2));
}

/*
 * XXX FIXME
 * - should use appropriate metadata server for the file,
 * - maybe(?) should use gfm_client_schedule_file instead.
 */
gfarm_error_t
schedule_host_domain(const char *domain,
	int *nhostsp, struct gfarm_host_sched_info **hostsp)
{
	gfarm_error_t e;
	struct gfm_connection *gfm_server;
	const char *path = GFARM_PATH_ROOT;

	if ((e = gfarm_url_parse_metadb(&path, &gfm_server))
	    != GFARM_ERR_NO_ERROR)
		return (e);

	e = gfm_client_schedule_host_domain(gfm_server, domain,
	    nhostsp, hostsp);
	gfm_client_connection_free(gfm_server);
	return (e);
}

static gfarm_error_t
create_hostlist_by_domain_and_hash(char *domain,
	struct gfarm_hash_table *hosthash,
	int *nhostsp, char ***hostsp, int **portsp)
{
	int ninfo, i, j, *ports, nhosts;
	struct gfarm_host_sched_info *infos;
	char **hosts;
	gfarm_error_t e;

	e = schedule_host_domain(domain, &ninfo, &infos);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	/* sort 'infos' in descending order wrt available capacity */
	qsort(infos, ninfo, sizeof(*infos), compare_available_capacity_r);

	/* eliminate file system nodes that do not have enough space */
	for (i = 0; i < ninfo; ++i)
		/* note that disk_avail is the number of 1K blocks */
		if (infos[i].disk_avail * 1024
		    < gfarm_get_minimum_free_disk_space())
			break;
	nhosts = i;

	/* XXX - abandon CPU load and available capacity */
	GFARM_MALLOC_ARRAY(hosts, nhosts);
	if (hosts == NULL) {
		gfarm_host_sched_info_free(ninfo, infos);
		return (GFARM_ERR_NO_MEMORY);
	}
	GFARM_MALLOC_ARRAY(ports, nhosts);
	if (ports == NULL) {
		free(hosts);
		gfarm_host_sched_info_free(ninfo, infos);
		return (GFARM_ERR_NO_MEMORY);
	}
	for (i = 0, j = 0; i < nhosts; ++i) {
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

static const char *
pfor_list(struct action *act, gfarm_list *list, int parallel,
	  struct gfrep_arg *gfrep_arg)
{
	struct file_info **finfo;
	int nfinfo;
	const char *errmsg;

	finfo = gfarm_array_alloc_from_list(list);
	nfinfo = gfarm_list_length(list);

	errmsg = pfor(act, nfinfo, finfo, parallel, gfrep_arg);

	gfarm_array_free_deeply(nfinfo, finfo,
		(void (*)(void *))file_info_free);

	return (errmsg);
}

int
main(int argc, char *argv[])
{
	char *src_hostfile = NULL, *dst_hostfile = NULL;
	gfarm_stringlist paths;
	gfs_glob_t types;
	int mode_src_ch = 0, mode_dst_ch = 0, parallel = -1;
	int i, ch;
	gfarm_error_t e;
	const char *errmsg, *errmsg2 = NULL;
	struct gfrep_arg gfrep_arg;
	struct flist flist;

	if (argc >= 1)
		program_name = basename(argv[0]);
	memset(&gfrep_arg, 0, sizeof(gfrep_arg));
	memset(&flist, 0, sizeof(flist));
	flist.src_domain = "";
	flist.dst_domain = "";

	e = gfarm_initialize(&argc, &argv);
	error_check(e);

#ifdef _OPENMP
	while ((ch = getopt(argc, argv, "h:j:mnqvxS:D:H:N:?")) != -1) {
#else
	while ((ch = getopt(argc, argv, "h:mnqvxS:D:H:N:?")) != -1) {
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
			opt_noexec = 1;
			break;
		case 'q':
			opt_quiet = 1;
			break;
		case 'v':
			opt_verbose = 1;
			break;
		case 'x':
			opt_remove = 1;
			break;
		case 'S':
			flist.src_domain = optarg;
			conflict_check(&mode_src_ch, ch);
			break;
		case 'D':
			flist.dst_domain = optarg;
			conflict_check(&mode_dst_ch, ch);
			break;
		case 'H':
			dst_hostfile = optarg;
			conflict_check(&mode_dst_ch, ch);
			break;
		case 'N':
			opt_nrep = strtol(optarg, NULL, 0);
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

	if (!opt_quiet) {
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

	e = gfarm_list_init(&flist.slist);
	error_check(e);
	e = gfarm_list_init(&flist.dlist);
	error_check(e);
	flist.srchash = gfarm_hash_table_alloc(HOSTHASH_SIZE,
		gfarm_hash_casefold, gfarm_hash_key_equal_casefold);
	if (flist.srchash == NULL)
		error_check(GFARM_ERR_NO_MEMORY);

	e = create_hosthash_from_file(src_hostfile,
		HOSTHASH_SIZE, &flist.src_hosthash);
	error_check(e);
	e = create_hosthash_from_file(dst_hostfile,
		HOSTHASH_SIZE, &flist.dst_hosthash);
	error_check(e);

	for (i = 0; i < gfarm_stringlist_length(&paths); i++) {
		char *file = gfarm_stringlist_elem(&paths, i);

		e = gfarm_foreach_directory_hierarchy(
			create_filelist, NULL, NULL, file, &flist);
		if (e != GFARM_ERR_NO_ERROR)
			break;
	}
	gfarm_stringlist_free_deeply(&paths);
	error_check(e);

	if (!opt_quiet)
		printf(" done\n");
	if (opt_verbose) {
		printf("files to be replicated\n");
		print_file_list(&flist.slist);
	}
	if (opt_verbose && opt_remove) {
		printf("files having too many replicas\n");
		print_file_list(&flist.dlist);
	}
	if (gfarm_list_length(&flist.slist) <= 0
	    && (!opt_remove || gfarm_list_length(&flist.dlist) <= 0))
		exit(0); /* no file */

	/* replicate files */
	e = gfarm_hash_to_string_array(
		flist.srchash, &gfrep_arg.nsrc, &gfrep_arg.src);
	error_check(e);
	gfarm_hash_table_free(flist.srchash);

	if (!opt_quiet) {
		printf("investigating hosts...");
		fflush(stdout);
	}
	e = create_hostlist_by_domain_and_hash(
		flist.dst_domain, flist.dst_hosthash,
		&gfrep_arg.ndst, &gfrep_arg.dst, &gfrep_arg.dst_port);
	error_check(e);
	if (!opt_quiet)
		printf(" done\n");

	errmsg = pfor_list(act, &flist.slist, parallel, &gfrep_arg);
	gfarm_list_free(&flist.slist);

	/* remove file replicas */
	if (opt_remove)
		errmsg2 = pfor_list(
			&remove_mode, &flist.dlist, parallel, &gfrep_arg);
	gfarm_list_free(&flist.dlist);
	if (errmsg == NULL)
		errmsg = errmsg2;
	if (errmsg != NULL)
		fprintf(stderr, "%s\n", errmsg), exit(EXIT_FAILURE);

	gfarm_strings_free_deeply(gfrep_arg.nsrc, gfrep_arg.src);
	gfarm_strings_free_deeply(gfrep_arg.ndst, gfrep_arg.dst);
	free(gfrep_arg.dst_port);
	e = gfarm_terminate();
	error_check(e);

	return (0);
}
