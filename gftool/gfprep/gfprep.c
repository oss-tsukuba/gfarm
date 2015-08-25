/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <libgen.h>
#include <limits.h>
#include <assert.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <errno.h>
#include <regex.h>

#include <gfarm/gfarm.h>

#include "hash.h"
#include "nanosec.h"
#include "thrsubr.h"
#include "gfutil.h"
#include "queue.h"

#include "config.h"
#include "context.h"
#include "gfarm_path.h"
#include "gfm_client.h"
#include "host.h"
#include "lookup.h"
#include "humanize_number.h"

#include "gfarm_list.h"

#include "gfurl.h"
#include "gfmsg.h"

#include "gfarm_parallel.h"
#include "gfarm_dirtree.h"
#include "gfarm_pfunc.h"

#define GFPREP_PARALLEL_DIRTREE 8

#define HOSTHASH_SIZE 101

static const char name_gfpcopy[] = "gfpcopy";
static char *program_name;

/* options */
struct gfprep_option {
	int quiet;	/* -q */
	int verbose;	/* -v */
	int debug;	/* -d */
	int performance;/* -p */
	int performance_each;/* -P */
	int max_rw;	/* -c */
	int check_disk_avail;	/* not -U */
	int openfile_cost;	/* -C */
};

static struct gfprep_option opt = { .check_disk_avail = 1 };

/* protection against callback from child process */
static pthread_mutex_t cb_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cb_cond = PTHREAD_COND_INITIALIZER;
static const char CB_MUTEX_DIAG[] = "cb_mutex";
static const char CB_COND_DIAG[] = "cb_cond";

/* locked by cb_mutex */
static gfarm_uint64_t total_ok_filesize = 0;
static gfarm_uint64_t total_ok_filenum = 0;
static gfarm_uint64_t total_skip_filesize = 0;
static gfarm_uint64_t total_skip_filenum = 0;
static gfarm_uint64_t total_ng_filesize = 0;
static gfarm_uint64_t total_ng_filenum = 0;
static gfarm_uint64_t removed_replica_ok_num = 0;
static gfarm_uint64_t removed_replica_ng_num = 0;
static gfarm_uint64_t removed_replica_ok_filesize = 0;
static gfarm_uint64_t removed_replica_ng_filesize = 0;

/* -------------------------------------------------------------- */

static void
gfprep_usage()
{
	fprintf(stderr,
"\t[-N <#replica>] [-x (remove surplus replicas)] [-m (migrate)]\n"
"\t<gfarm_url(gfarm:...) or local-path>\n");
}

static void
gfpcopy_usage()
{
	fprintf(stderr,
"\t[-f (force copy)(overwrite)] [-b <#bufsize to copy>]\n"
"\t[-e (skip existing files\n"
"\t     in order to execute multiple gfpcopy simultaneously)]\n"
"\t<src_url(gfarm:... or file:...) or local-path>\n"
"\t<dst_directory(gfarm:... or file:... or hpss:...) or local-path>\n");
}

static void
gfprep_usage_common(int error)
{
	fprintf(stderr,
"Usage: %s [-?] [-q (quiet)] [-v (verbose)] [-d (debug)]\n"
"\t[-X <regexp to exclude a source path>] [-X <regexp>] ...\n"
"\t[-S <source domainname to select a replica>]\n"
"\t[-h <source hostfile to select a replica>]\n"
"\t[-L (select a src_host within specified source)(limited scope)]\n"
"\t[-D <destination domainname\n"
"\t     ('' means all nodes even if write_target_domain is set)>]\n"
"\t[-H <destination hostfile>]\n"
"\t[-j <#parallel(to copy files)(connections)(default: %d)>]\n"
"\t[-J <#parallel(to read directories)(default: %d)>]\n"
"\t[-w <scheduling method (noplan,greedy)(default: noplan)>]\n"
"\t[-W <#KB> (threshold size to flat connections cost)(for -w greedy)]\n"
"\t[-p (report total performance) | -P (report each and total performance)]\n"
"\t[-n (not execute)] [-s <#KB/s(simulate)>]\n"
"\t[-U (disable checking disk_avail)(fast)]\n"
"\t[-M <#byte(K|M|G|T)(total copied size)(default: unlimited)>]\n"
"\t[-z <#byte(K|M|G|T)(minimum file size)(default: unlimited)>]\n"
"\t[-Z <#byte(K|M|G|T)(maximum file size)(default: unlimited)>]\n"
/* "\t[-R <#ratio (throughput: local=remote*ratio)(for -w greedy)>]\n" */
"\t[-F <#dirents(readahead)>]\n",
		program_name,
		gfarm_ctxp->client_parallel_copy,
		GFPREP_PARALLEL_DIRTREE
		);
	if (strcmp(program_name, name_gfpcopy) == 0)
		gfpcopy_usage();
	else
		gfprep_usage();
	if (error)
		exit(EXIT_FAILURE);
}

struct gfprep_host_info {
	char *hostname;
	int port;
	int ncpu;
	int max_rw;
	gfarm_int64_t disk_avail;

	/* locked by cb_mutex */
	int n_using; /* src:n_reading, dst:n_writing */
	gfarm_int64_t failed_size; /* for dst */
};

static void
gfprep_update_n_using(struct gfprep_host_info *info, int add_using)
{
	static const char diag[] = "gfprep_update_n_using";

	if (info == NULL)
		return;
	gfarm_mutex_lock(&cb_mutex, diag, CB_MUTEX_DIAG);
	info->n_using += add_using;
	gfarm_mutex_unlock(&cb_mutex, diag, CB_MUTEX_DIAG);
}

static void
gfprep_get_n_using(struct gfprep_host_info *info, int *n_using_p)
{
	static const char diag[] = "gfprep_get_n_using";

	gfarm_mutex_lock(&cb_mutex, diag, CB_MUTEX_DIAG);
	*n_using_p = info->n_using;
	gfarm_mutex_unlock(&cb_mutex, diag, CB_MUTEX_DIAG);
}

static void
gfprep_get_and_reset_failed_size(
	struct gfprep_host_info *info, gfarm_int64_t *failed_size_p)
{
	static const char diag[] = "gfprep_get_failed_size";

	gfarm_mutex_lock(&cb_mutex, diag, CB_MUTEX_DIAG);
	*failed_size_p = info->failed_size;
	info->failed_size = 0;
	gfarm_mutex_unlock(&cb_mutex, diag, CB_MUTEX_DIAG);
}

static int
gfprep_in_hostnamehash(struct gfarm_hash_table *hash, const char *hostname)
{
	struct gfarm_hash_entry *he;

	he = gfarm_hash_lookup(hash, hostname, strlen(hostname) + 1);
	return (he != NULL ? 1 : 0);
}

static struct gfprep_host_info *
gfprep_from_hostinfohash(struct gfarm_hash_table *hash, const char *hostname)
{
	struct gfarm_hash_entry *he;
	struct gfprep_host_info **hip;

	he = gfarm_hash_lookup(hash, &hostname, sizeof(char *));
	if (he) {
		hip = gfarm_hash_entry_data(he);
		return (*hip);
	}
	return (NULL);
}

static int
gfprep_in_hostinfohash(struct gfarm_hash_table *hash, const char *hostname)
{
	struct gfprep_host_info *hi = gfprep_from_hostinfohash(hash, hostname);

	return (hi != NULL ? 1 : 0);
}

static gfarm_error_t
gfprep_create_hostinfohash_all(const char *path, int *nhost_infos_p,
			       struct gfarm_hash_table **hash_all_p)
{
	gfarm_error_t e;
	int nhsis, created, i;
	struct gfarm_host_sched_info *hsis;
	struct gfarm_hash_entry *he;
	struct gfarm_hash_iterator iter;

	*hash_all_p = gfarm_hash_table_alloc(
		HOSTHASH_SIZE, gfarm_hash_casefold_strptr,
		gfarm_hash_key_equal_casefold_strptr);
	if (*hash_all_p == NULL)
		return (GFARM_ERR_NO_MEMORY);

	e = gfarm_schedule_hosts_domain_all(path, "", &nhsis, &hsis);
	if (e != GFARM_ERR_NO_ERROR) {
		gfarm_hash_table_free(*hash_all_p);
		return (e);
	}
	for (i = 0; i < nhsis; i++) {
		struct gfprep_host_info *hi, **hip;
		char *hostname = strdup(hsis[i].host);

		if (hostname == NULL)
			return (GFARM_ERR_NO_MEMORY);
		he = gfarm_hash_enter(*hash_all_p,
				      &hostname, sizeof(char *),
				      sizeof(struct gfprep_host_info *),
				      &created);
		if (he == NULL)
			goto nomem;
		hip = gfarm_hash_entry_data(he);
		if (!created)
			gfmsg_fatal(
				"unexpected: duplicaate hostname(%s) in hash",
				hostname);
		GFARM_MALLOC(hi);
		if (hi == NULL)
			return (GFARM_ERR_NO_MEMORY);
		hi->hostname = hostname;
		hi->port = hsis[i].port;
		hi->ncpu = hsis[i].ncpu;
		hi->max_rw = opt.max_rw > 0 ? opt.max_rw : hi->ncpu;
		hi->disk_avail = hsis[i].disk_avail * 1024; /* KB -> Byte */
		hi->n_using = 0;
		hi->failed_size = 0;
		*hip = hi;
	}
	gfarm_host_sched_info_free(nhsis, hsis);
	*nhost_infos_p = nhsis;
	return (GFARM_ERR_NO_ERROR);
nomem:
	for (gfarm_hash_iterator_begin(*hash_all_p, &iter);
	     !gfarm_hash_iterator_is_end(&iter);
	     gfarm_hash_iterator_next(&iter)) {
		he = gfarm_hash_iterator_access(&iter);
		if (he) {
			struct gfprep_host_info *hi, **hip;
			hip = gfarm_hash_entry_data(he);
			hi = *hip;
			free(hi->hostname);
			free(hi);
		}
	}
	gfarm_hash_table_free(*hash_all_p);
	gfarm_host_sched_info_free(nhsis, hsis);
	return (GFARM_ERR_NO_MEMORY);
}

static gfarm_error_t
gfprep_filter_hostinfohash(const char *path,
			   struct gfarm_hash_table *hash_all,
			   struct gfarm_hash_table **hash_info_p,
			   struct gfarm_hash_table *include_hash_hostname,
			   const char *include_domain,
			   struct gfarm_hash_table *exclude_hash_hostname,
			   const char *exclude_domain)
{
	int created;
	struct gfarm_hash_entry *he;
	struct gfarm_hash_iterator iter;

	*hash_info_p = gfarm_hash_table_alloc(
		HOSTHASH_SIZE, gfarm_hash_strptr, gfarm_hash_key_equal_strptr);
	if (*hash_info_p == NULL)
		return (GFARM_ERR_NO_MEMORY);

	for (gfarm_hash_iterator_begin(hash_all, &iter);
	     !gfarm_hash_iterator_is_end(&iter);
	     gfarm_hash_iterator_next(&iter)) {
		struct gfprep_host_info **hip, **hip_orig, *hi;

		he = gfarm_hash_iterator_access(&iter);
		if (he == NULL)
			continue; /* unexpected */
		hip_orig = gfarm_hash_entry_data(he);
		hi = *hip_orig;
		if (include_domain &&
		    !gfarm_host_is_in_domain(hi->hostname, include_domain))
			continue;
		if (include_hash_hostname &&
		    !gfprep_in_hostnamehash(include_hash_hostname,
					    hi->hostname))
			continue;
		if (exclude_domain &&
		    gfarm_host_is_in_domain(hi->hostname, exclude_domain))
			continue;
		if (exclude_hash_hostname &&
		    gfprep_in_hostnamehash(exclude_hash_hostname,
					   hi->hostname))
			continue;
		he = gfarm_hash_enter(*hash_info_p,
				      &hi->hostname, sizeof(char *),
				      sizeof(struct gfprep_host_info *),
				      &created);
		if (he == NULL) {
			gfarm_hash_table_free(*hash_info_p);
			return (GFARM_ERR_NO_MEMORY);
		}
		hip = gfarm_hash_entry_data(he);
		if (!created)
			gfmsg_fatal(
				"unexpected: duplicaate hostname(%s) in hash",
				hi->hostname);
		*hip = hi;
	}
	return (GFARM_ERR_NO_ERROR);
}

static void
gfprep_hostinfohash_all_free(struct gfarm_hash_table *hash_all)
{
	struct gfarm_hash_iterator iter;
	struct gfarm_hash_entry *he;
	struct gfprep_host_info *hi, **hip;

	for (gfarm_hash_iterator_begin(hash_all, &iter);
	     !gfarm_hash_iterator_is_end(&iter);
	     gfarm_hash_iterator_next(&iter)) {
		he = gfarm_hash_iterator_access(&iter);
		if (he) {
			hip = gfarm_hash_entry_data(he);
			hi = *hip;
			free(hi->hostname);
			free(hi);
		}
	}
	gfarm_hash_table_free(hash_all);
}

static gfarm_error_t
gfprep_hostinfohash_to_array(const char *path, int *nhost_infos_p,
			     struct gfprep_host_info ***host_infos_p,
			     struct gfarm_hash_table *hash_info)
{
	int i, nhost_infos;
	struct gfarm_hash_iterator iter;
	struct gfarm_hash_entry *he;
	struct gfprep_host_info **hip;
	struct gfprep_host_info **host_infos;

	nhost_infos = 0;
	for (gfarm_hash_iterator_begin(hash_info, &iter);
	     !gfarm_hash_iterator_is_end(&iter);
	     gfarm_hash_iterator_next(&iter)) {
		he = gfarm_hash_iterator_access(&iter);
		if (he)
			nhost_infos++;
	}
	if (nhost_infos_p)
		*nhost_infos_p = nhost_infos; /* number of available hosts */
	if (host_infos_p == NULL)
		return (GFARM_ERR_NO_ERROR);

	/* get hostlist */
	if (nhost_infos == 0) {
		*host_infos_p = NULL;
		return (GFARM_ERR_NO_ERROR);
	}
	GFARM_MALLOC_ARRAY(host_infos, nhost_infos);
	if (host_infos == NULL)
		return (GFARM_ERR_NO_MEMORY);
	i = 0;
	for (gfarm_hash_iterator_begin(hash_info, &iter);
	     !gfarm_hash_iterator_is_end(&iter) && i < nhost_infos;
	     gfarm_hash_iterator_next(&iter)) {
		he = gfarm_hash_iterator_access(&iter);
		if (he) {
			hip = gfarm_hash_entry_data(he);
			host_infos[i++] = *hip; /* pointer */
		}
	}
	assert(i == nhost_infos);
	if (*host_infos_p)
		free(*host_infos_p);
	*host_infos_p = host_infos;

	return (GFARM_ERR_NO_ERROR);
}

static int
gfprep_host_info_compare_for_src(const void *p1, const void *p2)
{
	struct gfprep_host_info *hi1 = *(struct gfprep_host_info **) p1;
	struct gfprep_host_info *hi2 = *(struct gfprep_host_info **) p2;
	float ratio1 = (float)hi1->n_using / (float)hi1->max_rw;
	float ratio2 = (float)hi2->n_using / (float)hi2->max_rw;

	if (ratio1 < ratio2)
		return (-1); /* high priority */
	else if (ratio1 > ratio2)
		return (1);
	else
		return (0);
}

static void
gfprep_host_info_array_sort_for_src(int nhost_infos,
				    struct gfprep_host_info **host_infos)
{
	static const char diag[] = "gfprep_host_info_array_sort_for_src";

	gfarm_mutex_lock(&cb_mutex, diag, CB_MUTEX_DIAG);
	qsort(host_infos, nhost_infos, sizeof(struct gfprep_host_info *),
	      gfprep_host_info_compare_for_src);
	gfarm_mutex_unlock(&cb_mutex, diag, CB_MUTEX_DIAG);
}

static int
gfprep_host_info_compare_by_disk_avail(const void *p1, const void *p2)
{
	struct gfprep_host_info *hi1 = *(struct gfprep_host_info **) p1;
	struct gfprep_host_info *hi2 = *(struct gfprep_host_info **) p2;

	if (hi1->disk_avail > hi2->disk_avail)
		return (-1); /* high priority */
	else if (hi1->disk_avail < hi2->disk_avail)
		return (1);
	else
		return (0);
}

static void
gfprep_host_info_array_sort_by_disk_avail(int nhost_infos,
	struct gfprep_host_info **host_infos)
{
	static const char diag[] = "gfprep_host_info_array_sort_by_disk_avail";

	gfarm_mutex_lock(&cb_mutex, diag, CB_MUTEX_DIAG);
	qsort(host_infos, nhost_infos, sizeof(struct gfprep_host_info *),
	      gfprep_host_info_compare_by_disk_avail);
	gfarm_mutex_unlock(&cb_mutex, diag, CB_MUTEX_DIAG);
}

static int
gfprep_host_info_compare_for_dst(const void *p1, const void *p2)
{
	struct gfprep_host_info *hi1 = *(struct gfprep_host_info **) p1;
	struct gfprep_host_info *hi2 = *(struct gfprep_host_info **) p2;
	float ratio1 = (float)hi1->n_using / (float)hi1->max_rw;
	float ratio2 = (float)hi2->n_using / (float)hi2->max_rw;

	if (ratio1 < ratio2)
		return (-1); /* high priority */
	else if (ratio1 > ratio2)
		return (1);
	else if (hi1->disk_avail > hi2->disk_avail)
		return (-1); /* high priority */
	else if (hi1->disk_avail < hi2->disk_avail)
		return (1);
	else
		return (0);
}

static void
gfprep_host_info_array_sort_for_dst(int nhost_infos,
	struct gfprep_host_info **host_infos)
{
	static const char diag[] = "gfprep_host_info_array_sort_for_dst";

	gfarm_mutex_lock(&cb_mutex, diag, CB_MUTEX_DIAG);
	qsort(host_infos, nhost_infos, sizeof(struct gfprep_host_info *),
	      gfprep_host_info_compare_for_dst);
	gfarm_mutex_unlock(&cb_mutex, diag, CB_MUTEX_DIAG);
}

static gfarm_error_t
gfprep_create_hostnamehash_from_array(const char *path, const char *hostfile,
				      int nhosts, char **hosts, int hashsize,
				      struct gfarm_hash_table **hashp)
{
	gfarm_error_t e;
	struct gfm_connection *gfm_server;
	struct gfarm_hash_table *hash;
	int i, p;
	char *h;

	hash = gfarm_hash_table_alloc(hashsize, gfarm_hash_casefold,
				      gfarm_hash_key_equal_casefold);
	if (hash == NULL)
		return (GFARM_ERR_NO_MEMORY);
	e = gfm_client_connection_and_process_acquire_by_path(
		path, &gfm_server);
	if (e != GFARM_ERR_NO_ERROR) {
		gfarm_hash_table_free(hash);
		return (e);
	}
	for (i = 0; i < nhosts; i++) {
		e = gfm_host_get_canonical_name(gfm_server, hosts[i], &h, &p);
		if (e != GFARM_ERR_NO_ERROR) {
			gfmsg_warn_e(e, "%s in %s", hosts[i], hostfile);
			break;
		}
		gfarm_hash_enter(hash, h, strlen(h)+1, 0, NULL);
		free(h);
	}
	gfm_client_connection_free(gfm_server);
	*hashp = hash;
	return (e);
}

static gfarm_error_t
gfprep_create_hostnamehash_from_file(const char *path,
				     const char *hostfile, int hashsize,
				     struct gfarm_hash_table **hashp)
{
	int error_line = -1, nhosts;
	gfarm_error_t e;
	char **hosts;

	if (hostfile == NULL) {
		*hashp = NULL;
		return (GFARM_ERR_NO_ERROR);
	}
	e = gfarm_hostlist_read((char *) hostfile /* UNCONST */,
				&nhosts, &hosts, &error_line);
	if (e != GFARM_ERR_NO_ERROR) {
		if (error_line != -1)
			gfmsg_error_e(e, "%s: line %d", hostfile, error_line);
		else
			gfmsg_error_e(e, "%s", hostfile);
		exit(EXIT_FAILURE);
	}
	e = gfprep_create_hostnamehash_from_array(path, hostfile,
						  nhosts, hosts,
						  HOSTHASH_SIZE, hashp);
	gfarm_strings_free_deeply(nhosts, hosts);
	return (e);
}

/* GFS_DT_REG, GFS_DT_DIR, GFS_DT_LNK, GFS_DT_UNKNOWN */
static gfarm_error_t
gfprep_get_type(GFURL url, int *modep,
	struct gfarm_timespec *mtimep, int *typep)
{
	gfarm_error_t e;
	struct gfurl_stat st;

	e = gfurl_lstat(url, &st);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (modep)
		*modep = st.mode;
	if (mtimep)
		*mtimep = st.mtime;
	if (typep)
		*typep = gfurl_stat_file_type(&st);
	return (GFARM_ERR_NO_ERROR);
}

static int
gfprep_is_dir(GFURL url, int *modep, gfarm_error_t *ep)
{
	int type;
	gfarm_error_t e;

	e = gfprep_get_type(url, modep, NULL, &type);
	if (e != GFARM_ERR_NO_ERROR) {
		if (ep)
			*ep = e;
		return (0);
	}
	if (type != GFS_DT_DIR) {
		if (ep)
			*ep = GFARM_ERR_NOT_A_DIRECTORY;
		return (0);
	}
	if (ep)
		*ep = e;
	return (1);
}

static int
gfprep_is_existing(GFURL url, int *modep, gfarm_error_t *ep)
{
	gfarm_error_t e;

	e = gfprep_get_type(url, modep, NULL, NULL);
	if (e == GFARM_ERR_NO_ERROR) {
		if (ep)
			*ep = e;
		return (1);
	} else if (e == GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY) {
		if (ep)
			*ep = GFARM_ERR_NO_ERROR;
		return (0);
	}
	if (ep)
		*ep = e;
	return (0);
}

struct remove_replica_deferred {
	GFARM_HCIRCLEQ_ENTRY(remove_replica_deferred) list;
	char *url; /* Gfarm URL */
	char *hostname;
};

static GFARM_HCIRCLEQ_HEAD(remove_replica_deferred) \
    remove_replica_deferred_head;

static void
gfprep_remove_replica_deferred_init(void)
{
	GFARM_HCIRCLEQ_INIT(remove_replica_deferred_head, list);
}

static void
gfprep_remove_replica_deferred_add(const char *url, const char *hostname)
{
	char *url2, *hostname2;
	struct remove_replica_deferred *rrd;

	GFARM_MALLOC(rrd);
	url2 = strdup(url);
	hostname2 = strdup(hostname);
	if (rrd == NULL || url2 == NULL || hostname2 == NULL) {
		free(rrd);
		free(url2);
		free(hostname2);
		gfmsg_error("cannot remove a replica: %s (%s): no memory",
		     url, hostname);
	} else {
		rrd->url = url2;
		rrd->hostname = hostname2;
		GFARM_HCIRCLEQ_INSERT_HEAD(
		    remove_replica_deferred_head, rrd, list);
	}
}

static void
gfprep_remove_replica_deferred_final(void)
{
	gfarm_error_t e;
	struct remove_replica_deferred *rrd;

	while (!GFARM_HCIRCLEQ_EMPTY(remove_replica_deferred_head, list)) {
		rrd = GFARM_HCIRCLEQ_FIRST(remove_replica_deferred_head, list);
		GFARM_HCIRCLEQ_REMOVE(rrd, list);

		e = gfs_replica_remove_by_file(rrd->url, rrd->hostname);
		if (e != GFARM_ERR_NO_ERROR)
			gfmsg_error(
			    "cannot remove a replica: %s (%s): %s",
			    rrd->url, rrd->hostname, gfarm_error_string(e));
		else
			gfmsg_info("remove a replica: %s (%s)",
			    rrd->url, rrd->hostname);

		free(rrd->url);
		free(rrd->hostname);
		free(rrd);
	}
}

struct dirstat {
	GFARM_HCIRCLEQ_ENTRY(dirstat) list;
	GFURL url;
	int orig_mode;
	int tmp_mode;
	struct gfarm_timespec mtime;
};

static mode_t mask;
static GFARM_HCIRCLEQ_HEAD(dirstat) dirstat_head;

static void
gfprep_dirstat_init(void)
{
	mask = umask(0022);
	umask(mask);

	GFARM_HCIRCLEQ_INIT(dirstat_head, list);
}

static void
gfprep_dirstat_add(GFURL url, int orig_mode, int tmp_mode,
	struct gfarm_timespec *mtimep)
{
	GFURL url2;
	struct dirstat *ds;

	GFARM_MALLOC(ds);
	url2 = gfurl_dup(url);
	if (ds == NULL || url2 == NULL) {
		free(ds);
		gfurl_free(url2);
		gfmsg_error("cannot copy mode and mtime (no memory): %s",
		    gfurl_url(url));
		return;
	}
	ds->url = url2;
	ds->orig_mode = orig_mode;
	ds->tmp_mode = tmp_mode;
	ds->mtime = *mtimep;
	GFARM_HCIRCLEQ_INSERT_HEAD(dirstat_head, ds, list);
}

static void
gfprep_dirstat_final(void)
{
	gfarm_error_t e;
	struct dirstat *ds;

	while (!GFARM_HCIRCLEQ_EMPTY(dirstat_head, list)) {
		ds = GFARM_HCIRCLEQ_FIRST(dirstat_head, list);
		GFARM_HCIRCLEQ_REMOVE(ds, list);

		gfmsg_info("restore mode and mtime: %s", gfurl_url(ds->url));

		e = gfurl_set_mtime(ds->url, &ds->mtime);
		if (e != GFARM_ERR_OPERATION_NOT_SUPPORTED)
			gfmsg_error_e(e, "update mtime: %s",
			    gfurl_url(ds->url));

		if (ds->orig_mode != ds->tmp_mode) {
			e = gfurl_chmod(ds->url, ds->orig_mode);
			gfmsg_error_e(e, "chmod(%s, mode=%o)",
			    gfurl_url(ds->url), ds->orig_mode);
		}

		gfurl_free(ds->url);
		free(ds);
	}
}

static void
gfprep_set_tmp_mode_and_mtime(
	GFURL url, int mode, struct gfarm_timespec *mtimep)
{
	gfarm_error_t e;
	int tmp_mode;

	mode = mode & 0777;
	tmp_mode = mode | 0700; /* to create files in any st_mode and mask */
	if (mode != tmp_mode) {
		e = gfurl_chmod(url, tmp_mode);
		if (e == GFARM_ERR_NO_ERROR)
			gfmsg_info("chmod(%s, mode=%o)",
			    gfurl_url(url), tmp_mode);
		else {
			gfmsg_error_e(e, "chmod(%s, mode=%o)",
			    gfurl_url(url), tmp_mode);
			exit(EXIT_FAILURE);
		}
	}

	gfprep_dirstat_add(url, mode, tmp_mode, mtimep);
}

static gfarm_error_t
gfprep_mkdir_restore(
	GFURL url, int restore, int mode,
	struct gfarm_timespec *mtimep, int skip_existing)
{
	gfarm_error_t e;
	int tmp_mode;

	mode = mode & 0777 & ~mask;
	tmp_mode = mode | 0700; /* to create files in any st_mode and mask */
	e = gfurl_mkdir(url, mode, skip_existing);
	if (e == GFARM_ERR_NO_ERROR) {
		gfmsg_info("mkdir(%s, %o) OK", gfurl_url(url), tmp_mode);
		if (restore)
			gfprep_dirstat_add(url, mode, tmp_mode, mtimep);
	}
	return (e);
}

static gfarm_error_t
gfprep_copy_symlink(GFURL src, GFURL dst, int skip_existing)
{
	gfarm_error_t e;
	char *target;

	e = gfurl_readlink(src, &target);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	e = gfurl_symlink(dst, target);
	if (skip_existing && e == GFARM_ERR_ALREADY_EXISTS)
		e = GFARM_ERR_NO_ERROR;
	if (e == GFARM_ERR_NO_ERROR)
		gfmsg_info("symlink: %s -> %s", gfurl_url(dst), target);
	free(target);

	return (e);
}

/* callback functions and data (locked by cb_mutex) */
struct pfunc_cb_data {
	int migrate;
	char *src_url;
	char *dst_url;
	struct gfprep_host_info *src_hi;
	struct gfprep_host_info *dst_hi;
	struct timeval start;
	gfarm_off_t filesize;
	char *done_p;

	void (*func_timer_begin)(struct pfunc_cb_data *);
	void (*func_timer_end)(struct pfunc_cb_data *, enum pfunc_result);
	void (*func_start)(struct pfunc_cb_data *);
	void (*func_update)(struct pfunc_cb_data *, enum pfunc_result);
};

static void (*pfunc_cb_start_copy)(struct pfunc_cb_data *) = NULL;
static void (*pfunc_cb_start_replicate)(struct pfunc_cb_data *) = NULL;
static void (*pfunc_cb_start_remove_replica)(struct pfunc_cb_data *) = NULL;

static void (*pfunc_cb_timer_begin)(struct pfunc_cb_data *) = NULL;

static void
pfunc_cb_start_copy_main(struct pfunc_cb_data *cbd)
{
	gfmsg_debug("START COPY: %s (%s:%d) -> %s (%s:%d)",
	    cbd->src_url,
	    cbd->src_hi ? cbd->src_hi->hostname : "local",
	    cbd->src_hi ? cbd->src_hi->port : 0,
	    cbd->dst_url,
	    cbd->dst_hi ? cbd->dst_hi->hostname : "local",
	    cbd->dst_hi ? cbd->dst_hi->port : 0);
}

static void
pfunc_cb_start_replicate_main(struct pfunc_cb_data *cbd)
{
	gfmsg_debug("START %s: %s (%s:%d -> %s:%d)",
	    cbd->migrate ? "MIGRATE" : "REPLICATE",  cbd->src_url,
	    cbd->src_hi ? cbd->src_hi->hostname : "local",
	    cbd->src_hi ? cbd->src_hi->port : 0,
	    cbd->dst_hi ? cbd->dst_hi->hostname : "local",
	    cbd->dst_hi ? cbd->dst_hi->port : 0);
}

static void
pfunc_cb_start_remove_replica_main(struct pfunc_cb_data *cbd)
{
	gfmsg_debug("START REMOVE REPLICA: %s (%s:%d)",
	    cbd->src_url, cbd->src_hi->hostname,
	    cbd->src_hi->port);
}

static void
pfunc_cb_timer_begin_main(struct pfunc_cb_data *cbd)
{
	gettimeofday(&cbd->start, NULL);
}

static void
pfunc_cb_start(void *data)
{
	static const char diag[] = "pfunc_cb_start";
	struct pfunc_cb_data *cbd = data;

	if (cbd == NULL)
		return;

	if (cbd->func_start) {
		gfarm_mutex_lock(&cb_mutex, diag, CB_MUTEX_DIAG);
		cbd->func_start(cbd);
		gfarm_mutex_unlock(&cb_mutex, diag, CB_MUTEX_DIAG);
	}
	if (cbd->func_timer_begin)
		cbd->func_timer_begin(cbd);
}

static void (*pfunc_cb_timer_end_copy)(
	struct pfunc_cb_data *, enum pfunc_result) = NULL;
static void (*pfunc_cb_timer_end_replicate)(
	struct pfunc_cb_data *, enum pfunc_result) = NULL;
static void (*pfunc_cb_timer_end_remove_replica)(
	struct pfunc_cb_data *, enum pfunc_result) = NULL;

static const char pfunc_cb_ok[] = "OK";
static const char pfunc_cb_ng[] = "NG";
static const char pfunc_cb_skip[] = "SKIP";
#define PF_FMT ", %.3gMB/s(%.3gs): " /* performance */

static void
timer_end(struct pfunc_cb_data *cbd, double *mbsp, double *secp)
{
	struct timeval end;
	double usec;

	gettimeofday(&end, NULL);
	gfarm_timeval_sub(&end, &cbd->start);
	usec = (double)end.tv_sec * GFARM_SECOND_BY_MICROSEC + end.tv_usec;
	/* Bytes/usec == MB/sec */
	*mbsp = (double)cbd->filesize / usec;
	*secp = usec / GFARM_SECOND_BY_MICROSEC;
}

#define RESULT (result == PFUNC_RESULT_OK) ? pfunc_cb_ok : \
	(result == PFUNC_RESULT_SKIP) ? pfunc_cb_skip : pfunc_cb_ng

static void
pfunc_cb_timer_end_copy_main(
	struct pfunc_cb_data *cbd, enum pfunc_result result)
{
	double mbs, sec;

	timer_end(cbd, &mbs, &sec);
	printf("[%s]COPY" PF_FMT "%s", RESULT, mbs, sec, cbd->src_url);
	if (cbd->src_hi)
		printf("(%s:%d)",
		    cbd->src_hi->hostname, cbd->src_hi->port);
	printf(" -> %s", cbd->dst_url);
	if (cbd->dst_hi)
		printf("(%s:%d)",
		    cbd->dst_hi->hostname, cbd->dst_hi->port);
	puts("");
}

static void
pfunc_cb_timer_end_replicate_main(
	struct pfunc_cb_data *cbd, enum pfunc_result result)
{
	double mbs, sec;

	timer_end(cbd, &mbs, &sec);
	printf("[%s]%s" PF_FMT "%s (%s:%d -> %s:%d)\n", RESULT,
	    cbd->migrate ? "MIGRATE" : "REPLICATE",
	    mbs, sec, cbd->src_url,
	    cbd->src_hi->hostname, cbd->src_hi->port,
	    cbd->dst_hi->hostname, cbd->dst_hi->port);
}

static void
pfunc_cb_timer_end_remove_replica_main(
	struct pfunc_cb_data *cbd, enum pfunc_result result)
{
	printf("[%s]REMOVE REPLICA: %s (%s:%d)\n",
	    RESULT, cbd->src_url, cbd->src_hi->hostname, cbd->src_hi->port);
}

static void
pfunc_cb_update_default(struct pfunc_cb_data *cbd, enum pfunc_result result)
{
	if (cbd->src_hi)
		cbd->src_hi->n_using--;
	if (cbd->dst_hi)
		cbd->dst_hi->n_using--;
	if (result == PFUNC_RESULT_OK) {
		total_ok_filesize += cbd->filesize;
		total_ok_filenum++;
	} else if (result == PFUNC_RESULT_SKIP) {
		total_skip_filesize += cbd->filesize;
		total_skip_filenum++;
	} else {
		total_ng_filesize += cbd->filesize;
		total_ng_filenum++;
		if (cbd->dst_hi)
			cbd->dst_hi->failed_size += cbd->filesize;
	}
	if (cbd->done_p)
		*cbd->done_p = 1;

	if (cbd->migrate && result == PFUNC_RESULT_BUSY_REMOVE_REPLICA)
		gfprep_remove_replica_deferred_add(
		    cbd->src_url, cbd->src_hi->hostname);
}

static void
pfunc_cb_update_remove_replica(
	struct pfunc_cb_data *cbd, enum pfunc_result result)
{
	if (result == PFUNC_RESULT_OK) {
		removed_replica_ok_num++;
		removed_replica_ok_filesize += cbd->filesize;
	} else {
		removed_replica_ng_num++;
		removed_replica_ng_filesize += cbd->filesize;
		cbd->src_hi->failed_size -= cbd->filesize;
	}
}

static void
pfunc_cb_end(enum pfunc_result result, void *data)
{
	static const char diag[] = "pfunc_cb_end";
	struct pfunc_cb_data *cbd = data;

	if (cbd == NULL)
		return;

	gfarm_mutex_lock(&cb_mutex, diag, CB_MUTEX_DIAG);

	if (cbd->func_timer_end)
		cbd->func_timer_end(cbd, result);

	cbd->func_update(cbd, result);

	gfarm_cond_signal(&cb_cond, diag, CB_COND_DIAG);
	gfarm_mutex_unlock(&cb_mutex, diag, CB_MUTEX_DIAG);
}

static void
pfunc_cb_free(void *data)
{
	struct pfunc_cb_data *cbd = data;

	if (cbd == NULL)
		return;

	free(cbd->src_url);
	free(cbd->dst_url);
	free(cbd);
}

static void
pfunc_cb_func_init()
{
	if (opt.debug) {
		pfunc_cb_start_copy = pfunc_cb_start_copy_main;
		pfunc_cb_start_replicate = pfunc_cb_start_replicate_main;
		pfunc_cb_start_remove_replica
			= pfunc_cb_start_remove_replica_main;
	}
	if (opt.performance_each || opt.verbose) {
		pfunc_cb_timer_begin = pfunc_cb_timer_begin_main;

		pfunc_cb_timer_end_copy = pfunc_cb_timer_end_copy_main;
		pfunc_cb_timer_end_replicate
			= pfunc_cb_timer_end_replicate_main;
		pfunc_cb_timer_end_remove_replica
			= pfunc_cb_timer_end_remove_replica_main;
	}
}

static void
gfprep_count_skip_file(gfarm_off_t filesize)
{
	gfarm_mutex_lock(&cb_mutex, "gfprep_count_skip_file", CB_MUTEX_DIAG);
	total_skip_filesize += filesize;
	total_skip_filenum++;
	gfarm_mutex_unlock(&cb_mutex, "gfprep_count_skip_file", CB_MUTEX_DIAG);
}

static void
gfprep_count_ng_file(gfarm_off_t filesize)
{
	gfarm_mutex_lock(&cb_mutex, "gfprep_count_ng_file", CB_MUTEX_DIAG);
	total_ng_filesize += filesize;
	total_ng_filenum++;
	gfarm_mutex_unlock(&cb_mutex, "gfprep_count_ng_file", CB_MUTEX_DIAG);
}

static int
copied_size_is_over(gfarm_int64_t limit, gfarm_int64_t total_requested)
{
	static const char diag[] = "copied_size_is_over";
	int over = 0;

	if (limit < 0)
		return (0);

	gfarm_mutex_lock(&cb_mutex, diag, CB_MUTEX_DIAG);
	if (total_requested - total_ng_filesize >= limit)
		over = 1;
	gfarm_mutex_unlock(&cb_mutex, diag, CB_MUTEX_DIAG);

	return (over);
}

static void
gfprep_url_realloc(char **url_p, int *url_size_p,
		   const char *dir, const char *subpath)
{
	int n;
	char *np;

	if (*url_p == NULL || *url_size_p == 0) {
		*url_size_p = 32;
		GFARM_MALLOC_ARRAY(*url_p, *url_size_p);
		if (*url_p == NULL)
			gfmsg_fatal("no memory");
	}
	for (;;) {
		n = snprintf(*url_p, *url_size_p, "%s%s%s", dir,
			     (dir[strlen(dir) - 1] == '/' ? "" : "/"),
			     (subpath[0] == '/' ? (subpath + 1) : subpath));
		if (n > -1 && n < *url_size_p)
			return;
		*url_size_p *= 2;
		GFARM_REALLOC_ARRAY(np, *url_p, *url_size_p);
		if (np == NULL)
			gfmsg_fatal("no memory");
		*url_p = np;
	}
}

struct gfprep_node {
	int index;
	char *hostname;
	gfarm_uint64_t cost;
	gfarm_list flist;
	int files_next;
	int nfiles;
	gfarm_dirtree_entry_t **files;
};

struct gfprep_nodes {
	int n_nodes;
	struct gfprep_node *nodes;
};

static int
gfprep_greedy_compare(const void *p1, const void *p2)
{
	gfarm_dirtree_entry_t *e1 = *(gfarm_dirtree_entry_t **) p1;
	gfarm_dirtree_entry_t *e2 = *(gfarm_dirtree_entry_t **) p2;

	if (e1->src_ncopy < e2->src_ncopy)
		return (-1); /* prior */
	else if (e1->src_ncopy > e2->src_ncopy)
		return (1);
	if (e1->src_size > e2->src_size)
		return (-1); /* prior */
	else if (e1->src_size < e2->src_size)
		return (1);
	return (0);
}

static void
gfprep_greedy_sort(int nents, gfarm_dirtree_entry_t **ents)
{
	qsort(ents, nents, sizeof(gfarm_dirtree_entry_t *),
	      gfprep_greedy_compare);
}

/* hostname -> gfprep_nodes */
static gfarm_error_t
gfprep_greedy_enter(struct gfarm_hash_table *hash_host_to_nodes,
		    struct gfarm_hash_table *hash_host_info,
		    gfarm_dirtree_entry_t *ent)
{
	gfarm_error_t e;
	int i, j, created, min_nodes_idx = 0;
	struct gfarm_hash_entry *he;
	struct gfprep_nodes *nodes, *min_nodes = NULL;
	char *hostname;
	struct gfprep_host_info *hi;

	for (i = 0; i < ent->src_ncopy; i++) {
		hostname = ent->src_copy[i];
		hi = gfprep_from_hostinfohash(hash_host_info, hostname);
		if (hi == NULL)
			continue;  /* ignore */
		/* key-pointer refers ent->src_copy[i] */
		he = gfarm_hash_enter(hash_host_to_nodes,
				      &hostname, sizeof(char *),
				      sizeof(struct gfprep_nodes), &created);
		if (he == NULL)
			gfmsg_fatal("no memory");
		nodes = gfarm_hash_entry_data(he);
		if (created) {
			int max_rw = hi->max_rw;
			GFARM_MALLOC_ARRAY(nodes->nodes, max_rw);
			if (nodes->nodes == NULL)
				gfmsg_fatal("no memory");
			for (j = 0; j < max_rw; j++) {
				e = gfarm_list_init(&nodes->nodes[j].flist);
				gfmsg_fatal_e(e, "gfarm_list_init");
				nodes->nodes[j].cost = 0;
				nodes->nodes[j].hostname = hostname;
				nodes->nodes[j].index = j;
			}
			nodes->n_nodes = max_rw;
		}
		for (j = 0; j < nodes->n_nodes; j++) {
			if (min_nodes == NULL ||
			    nodes->nodes[j].cost
			    < min_nodes->nodes[min_nodes_idx].cost) {
				min_nodes = nodes;
				min_nodes_idx = j;
			}
		}
	}
	if (min_nodes) {
		e = gfarm_list_add(&min_nodes->nodes[min_nodes_idx].flist,
				   ent);
		gfmsg_fatal_e(e, "gfarm_list_add");
		min_nodes->nodes[min_nodes_idx].cost += ent->src_size;
		min_nodes->nodes[min_nodes_idx].cost += opt.openfile_cost;
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfprep_greedy_nodes_assign(struct gfarm_hash_table *hash_host_to_nodes,
			   struct gfarm_hash_table *hash_host_info,
			   int n_ents,  gfarm_dirtree_entry_t **ents)
{
	gfarm_error_t e;
	int i;

	for (i = 0; i < n_ents; i++) {
		e = gfprep_greedy_enter(hash_host_to_nodes, hash_host_info,
					ents[i]);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static int
gfprep_bad_compare(const void *p1, const void *p2)
{
	int v = gfprep_greedy_compare(p1, p2);
	/* reverse */
	if (v > 0)
		return (-1);
	else if (v < 0)
		return (1);
	return (0);
}

static void
gfprep_bad_sort(int nents, gfarm_dirtree_entry_t **ents)
{
	qsort(ents, nents, sizeof(gfarm_dirtree_entry_t *),
	      gfprep_bad_compare);
}

static gfarm_error_t
gfprep_bad_nodes_assign(struct gfarm_hash_table *hash_host_to_nodes,
		       struct gfarm_hash_table *hash_host_info,
		       int n_ents,  gfarm_dirtree_entry_t **ents)
{
	gfprep_bad_sort(n_ents, ents);
	return (gfprep_greedy_nodes_assign(hash_host_to_nodes, hash_host_info,
					   n_ents, ents));
}

static void
gfprep_hash_host_to_nodes_print(const char *diag,
				struct gfarm_hash_table *hash_host_to_nodes)
{
	int i;
	struct gfarm_hash_entry *he;
	struct gfarm_hash_iterator iter;
	char **hp;
	struct gfprep_nodes *nodes;

	if (!opt.verbose)
		return;

	for (gfarm_hash_iterator_begin(hash_host_to_nodes, &iter);
	     !gfarm_hash_iterator_is_end(&iter);
	     gfarm_hash_iterator_next(&iter)) {
		he = gfarm_hash_iterator_access(&iter);
		if (he) {
			hp = gfarm_hash_entry_key(he);
			nodes = gfarm_hash_entry_data(he);
			for (i = 0; i < nodes->n_nodes; i++)
				printf("%s: node: %s[%d]: cost=%"
				       GFARM_PRId64", nfiles=%d\n",
				       diag, *hp, i, nodes->nodes[i].cost,
				       gfarm_list_length(
					       &nodes->nodes[i].flist));
		}
	}
}

static void
gfprep_hash_host_to_nodes_free(struct gfarm_hash_table *hash_host_to_nodes)
{
	int i;
	struct gfarm_hash_entry *he;
	struct gfarm_hash_iterator iter;
	struct gfprep_nodes *nodes;

	for (gfarm_hash_iterator_begin(hash_host_to_nodes, &iter);
	     !gfarm_hash_iterator_is_end(&iter);
	     gfarm_hash_iterator_next(&iter)) {
		he = gfarm_hash_iterator_access(&iter);
		if (he) {
			nodes = gfarm_hash_entry_data(he);
			for (i = 0; i < nodes->n_nodes; i++)
				gfarm_list_free(&nodes->nodes[i].flist);
			free(nodes->nodes);
		}
	}
	gfarm_hash_table_free(hash_host_to_nodes);
}

static int
gfprep_nodes_compare(const void *p1, const void *p2)
{
	struct gfprep_node *n1 = *(struct gfprep_node **) p1;
	struct gfprep_node *n2 = *(struct gfprep_node **) p2;

	if (n1->cost > n2->cost)
		return (-1); /* prior */
	else if (n1->cost < n2->cost)
		return (1);
	return (0);
}

static void
gfprep_nodes_sort(struct gfprep_node **nodesp, int n_nodes)
{
	qsort(nodesp, n_nodes, sizeof(struct gfprep_node *),
	      gfprep_nodes_compare);
}

struct gfprep_rep_info {
	gfarm_dirtree_entry_t *file;
	char *host_from;
	char *host_to;
};

struct gfprep_connection {
	gfarm_uint64_t cost; /* expected time */
	int nodes_next;
	gfarm_list nodes_base;
	int n_nodes_base;
	struct gfprep_node **nodes_base_array;
};

static int
gfprep_connections_compare(const void *p1, const void *p2)
{
	struct gfprep_connection *c1 = (struct gfprep_connection *) p1;
	struct gfprep_connection *c2 = (struct gfprep_connection *) p2;

	if (c1->cost < c2->cost)
		return (-1); /* prior */
	else if (c1->cost > c2->cost)
		return (1);
	return (0);
}

static void
gfprep_connections_sort(struct gfprep_connection *conns, int n_conns)
{
	qsort(conns, n_conns, sizeof(struct gfprep_connection),
	      gfprep_connections_compare);
}

static gfarm_error_t
gfprep_connections_assign(struct gfarm_hash_table *hash_host_to_nodes,
			  int n_conns, struct gfprep_connection **conns_p)
{
	gfarm_error_t e;
	int i, n_all_nodes, tmp_array_n;
	struct gfarm_hash_entry *he;
	struct gfarm_hash_iterator iter;
	struct gfprep_connection *conns;
	struct gfprep_nodes *nodes;
	struct gfprep_node **all_nodes_p, **new_p;

	tmp_array_n = 8;
	GFARM_MALLOC_ARRAY(all_nodes_p, tmp_array_n);
	if (all_nodes_p == NULL)
		gfmsg_fatal("no memory");
	n_all_nodes = 0;
	for (gfarm_hash_iterator_begin(hash_host_to_nodes, &iter);
	     !gfarm_hash_iterator_is_end(&iter);
	     gfarm_hash_iterator_next(&iter)) {
		he = gfarm_hash_iterator_access(&iter);
		if (he) {
			nodes = gfarm_hash_entry_data(he);
			for (i = 0; i < nodes->n_nodes; i++) {
				if (n_all_nodes >= tmp_array_n) {
					tmp_array_n *= 2;
					GFARM_REALLOC_ARRAY(new_p,
							    all_nodes_p,
							    tmp_array_n);
					if (new_p == NULL)
						gfmsg_fatal("no memory");
					all_nodes_p = new_p;
				}
				all_nodes_p[n_all_nodes] = &nodes->nodes[i];
				n_all_nodes++;
			}
		}
	}
	if (n_all_nodes == 0) {
		free(all_nodes_p);
		*conns_p = NULL;
		return (GFARM_ERR_NO_ERROR);
	}
	gfprep_nodes_sort(all_nodes_p, n_all_nodes); /* big to small */

	GFARM_MALLOC_ARRAY(conns, n_conns);
	if (conns == NULL)
		gfmsg_fatal("no memory");
	for (i = 0; i < n_conns; i++) {
		conns[i].cost = 0;
		e = gfarm_list_init(&conns[i].nodes_base);
		gfmsg_fatal_e(e, "gfarm_list_init");
	}

	for (i = 0; i < n_all_nodes; i++) {
		/* assign node to conn its cost is smallest */
		gfarm_list_add(&conns[0].nodes_base, all_nodes_p[i]);
		conns[0].cost += all_nodes_p[i]->cost;
		gfprep_connections_sort(conns, n_conns); /* small to big */
	}

	free(all_nodes_p);
	*conns_p = conns;
	return (GFARM_ERR_NO_ERROR);
}

static void
gfprep_connections_print(const char *diag,
			 struct gfprep_connection *conns, int n_conns)
{
	int i, j, nnodes;
	struct gfprep_node **nodesp;

	if (!opt.verbose)
		return;
	if (conns == NULL)
		return;
	for (i = 0; i < n_conns; i++) {
		nnodes = gfarm_list_length(&conns[i].nodes_base);
		nodesp = gfarm_array_alloc_from_list(&conns[i].nodes_base);
		if (nodesp == NULL)
			gfmsg_fatal("no memory");
		printf("%s: connection[%d]: cost=%"GFARM_PRId64
		       ", n_nodes=%d\n", diag, i, conns[i].cost, nnodes);
		for (j = 0; j < nnodes; j++)
			printf("   node: %s[%d]\n",
			       nodesp[j]->hostname, nodesp[j]->index);
		free(nodesp);
	}
}

static void
gfprep_connections_free(struct gfprep_connection *conns, int n_conns)
{
	int i;

	if (conns == NULL)
		return;
	for (i = 0; i < n_conns; i++)
		gfarm_list_free(&conns[i].nodes_base);
	free(conns);
}

/* src_size: big to small */
static int
gfprep_file_compare(const void *p1, const void *p2)
{
	gfarm_dirtree_entry_t *e1 = *(gfarm_dirtree_entry_t **) p1;
	gfarm_dirtree_entry_t *e2 = *(gfarm_dirtree_entry_t **) p2;

	if (e1->src_size > e2->src_size)
		return (-1); /* prior */
	else if (e1->src_size < e2->src_size)
		return (1);
	return (0);
}

static void
gfprep_file_sort(int nents, gfarm_dirtree_entry_t **ents)
{
	qsort(ents, nents, sizeof(gfarm_dirtree_entry_t *),
	      gfprep_file_compare);
}

static gfarm_error_t
gfprep_connections_flat(int n_conns, struct gfprep_connection *conns,
			gfarm_uint64_t threshold)
{
	gfarm_error_t e;
	int i, j, k, l, m, nnodes, nnodes_max, nents_max, found;
	gfarm_dirtree_entry_t **ents_max, *ent_max, *found_ent = NULL;
	char *copy;
	struct gfprep_node **nodesp, **nodesp_max;
	struct gfprep_node *node, *max_node, *found_node = NULL;
	struct gfprep_connection *conn, *found_conn = NULL, *max_conn;

	if (n_conns <= 1 || conns == NULL)
		return (GFARM_ERR_NO_ERROR);
	/* n_conns >= 2 */
retry:
	gfprep_connections_sort(conns, n_conns); /* cost: small to big */
	max_conn = &conns[n_conns-1]; /* biggest */
	nodesp_max = gfarm_array_alloc_from_list(&max_conn->nodes_base);
	if (nodesp_max == NULL)
		gfmsg_fatal("no memory");
	nnodes_max = gfarm_list_length(&max_conn->nodes_base);
	found = 0;
	for (i = 0; i < nnodes_max; i++) {
		if (found)
			break; /* goto E */
		max_node = nodesp_max[i];
		nents_max = gfarm_list_length(&max_node->flist);
		if (nents_max <= 0)
			continue;
		ents_max = gfarm_array_alloc_from_list(&max_node->flist);
		if (ents_max == NULL)
			gfmsg_fatal("no memory");
		gfprep_file_sort(nents_max, ents_max); /* big to small*/
		for (j = 0; j <= n_conns - 2; j++) { /* cost: small to big */
			if (found)
				break; /* goto D */
			conn = &conns[j];
			nodesp = gfarm_array_alloc_from_list(
				&conn->nodes_base); /* max conn */
			if (nodesp == NULL)
				gfmsg_fatal("no memory");
			nnodes = gfarm_list_length(&conn->nodes_base);
			for (k = 0; k < nents_max; k++) { /* big to small */
				if (found)
					break; /* goto C */
				ent_max = ents_max[k];
				if (ent_max->src_size + opt.openfile_cost +
				    conn->cost + threshold
				    >= max_conn->cost)
					continue; /* next ent_max */
				for (l = 0; l < ent_max->src_ncopy; l++) {
					if (found)
						break; /* goto B */
					copy = ent_max->src_copy[l];
					for (m = 0; m < nnodes; m++) {
						node = nodesp[m];
						if (strcmp(copy,
							   node->hostname)
						    == 0) {
							found = 1;
							found_node = node;
							found_ent = ent_max;
							found_conn = conn;
							break; /* goto A */
						}
					}
					/* A */
				}
				/* B */
			}
			/* C */
			free(nodesp);
		}
		/* D */
		if (found) {
			gfarm_list newlist;
			/* swap */
			e = gfarm_list_add(&found_node->flist, found_ent);
			gfmsg_fatal_e(e, "gfarm_list_add");
			e = gfarm_list_init(&newlist);
			gfmsg_fatal_e(e, "gfarm_list_init");
			for (k = 0; k < nents_max; k++) {
				ent_max = ents_max[k];
				if (ent_max != found_ent)
					gfarm_list_add(&newlist, ent_max);
			}
			gfarm_list_free(&max_node->flist);
			max_node->flist = newlist;
			max_node->cost -= found_ent->src_size;
			max_node->cost -= opt.openfile_cost;
			max_conn->cost -= found_ent->src_size;
			max_conn->cost -= opt.openfile_cost;
			found_node->cost += found_ent->src_size;
			found_node->cost += opt.openfile_cost;
			found_conn->cost += found_ent->src_size;
			found_conn->cost += opt.openfile_cost;
		}
		free(ents_max);
	}
	/* E */
	free(nodesp_max);
	if (found)
		goto retry;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfprep_check_disk_avail(struct gfprep_host_info *hi, gfarm_off_t src_size)
{
	gfarm_int64_t failed_size;

	gfprep_get_and_reset_failed_size(hi, &failed_size);
	hi->disk_avail += failed_size;

	/* to reduce no space risk, keep minimum disk space */
	if (hi->disk_avail >= src_size + gfarm_get_minimum_free_disk_space())
		return (GFARM_ERR_NO_ERROR);
	return (GFARM_ERR_NO_SPACE);
}

/* for WAY_GREEDY */
static gfarm_error_t
gfprep_sort_and_check_disk_avail(int n_array_dst,
				 struct gfprep_host_info **array_dst,
				 const char *dst_url, gfarm_off_t src_size)
{
	assert(array_dst && n_array_dst > 0);
	/* disk_avail: large to small */
	gfprep_host_info_array_sort_by_disk_avail(n_array_dst, array_dst);
	return (gfprep_check_disk_avail(array_dst[0], src_size));
}

/* for WAY_GREEDY */
static void
gfprep_select_dst(int n_array_dst, struct gfprep_host_info **array_dst,
		  const char *dst_url, gfarm_off_t src_size,
		  struct gfprep_host_info **dst_hi_p, int no_limit)
{
	gfarm_error_t e;
	struct gfprep_host_info *hi, *found_dst_hi, *max_dst_hi;
	int i, n_writing;

	assert(array_dst && n_array_dst > 0);
	found_dst_hi = max_dst_hi = NULL;
	for (i = 0; i < n_array_dst; i++) {
		hi = array_dst[i];
		/* XXX should ignore host which has an incomplete replica */
		e = gfprep_check_disk_avail(hi, src_size);
		if (e != GFARM_ERR_NO_ERROR) { /* no spece */
			gfmsg_warn_e(e, "check_disk_avail(%s)", hi->hostname);
			continue;
		}
		gfprep_get_n_using(hi, &n_writing);
		gfmsg_debug("%s: disk_avail=%"GFARM_PRId64
			     ", n_writing=%d"
			     ", filesize=%"GFARM_PRId64
			     ", minimum=%"GFARM_PRId64,
			     dst_url, hi->disk_avail,
			     n_writing, src_size,
			     gfarm_get_minimum_free_disk_space());
		if (n_writing < hi->max_rw) {
			found_dst_hi = hi;
			break; /* found */
		} else if (no_limit && max_dst_hi == NULL)
			max_dst_hi = hi; /* save max hi */
	}
	if (found_dst_hi == NULL && no_limit)
		found_dst_hi = max_dst_hi;
	*dst_hi_p = found_dst_hi;
}

static struct pfunc_cb_data *
gfprep_cb_data_init(
	char *done_p, int migrate, gfarm_off_t size,
	const char *src_url, struct gfprep_host_info *src_hi,
	const char *dst_url, struct gfprep_host_info *dst_hi,
	void (*func_timer_begin)(struct pfunc_cb_data *),
	void (*func_timer_end)(struct pfunc_cb_data *, enum pfunc_result),
	void (*func_start)(struct pfunc_cb_data *),
	void (*func_update)(struct pfunc_cb_data *, enum pfunc_result))
{
	struct pfunc_cb_data *cbd;

	GFARM_MALLOC(cbd);
	if (cbd == NULL)
		gfmsg_fatal("no memory");

	cbd->src_url = strdup(src_url);
	if (cbd->src_url == NULL)
		gfmsg_fatal("no memory");

	if (dst_url) {
		cbd->dst_url = strdup(dst_url);
		if (cbd->dst_url == NULL)
			gfmsg_fatal("no memory");
	} else
		cbd->dst_url = NULL;

	cbd->src_hi = src_hi;
	cbd->dst_hi = dst_hi;
	cbd->filesize = size;
	cbd->done_p = done_p;
	cbd->migrate = migrate;

	cbd->func_timer_begin = func_timer_begin;
	cbd->func_timer_end = func_timer_end;
	cbd->func_start = func_start;
	cbd->func_update = func_update;
	return (cbd);
}

static void
gfprep_do_copy(
	gfarm_pfunc_t *pfunc_handle, char *done_p,
	int opt_migrate, gfarm_off_t size,
	const char *src_url, struct gfprep_host_info *src_hi,
	const char *dst_url, struct gfprep_host_info *dst_hi)
{
	gfarm_error_t e;
	struct pfunc_cb_data *cbd;
	char *src_hostname = NULL, *dst_hostname = NULL;
	int src_port = 0, dst_port = 0;

	cbd = gfprep_cb_data_init(
	    done_p, opt_migrate, size, src_url, src_hi, dst_url, dst_hi,
	    pfunc_cb_timer_begin, pfunc_cb_timer_end_copy,
	    pfunc_cb_start_copy, pfunc_cb_update_default);
	if (src_hi) { /* src is gfarm */
		gfprep_update_n_using(src_hi, 1);
		src_hostname = src_hi->hostname;
		src_port = src_hi->port;
	}
	if (dst_hi) { /* dst is gfarm */
		gfprep_update_n_using(dst_hi, 1);
		dst_hostname = dst_hi->hostname;
		dst_port = dst_hi->port;

		/* update disk_avail for next scheduling */
		dst_hi->disk_avail -= size;
	}
	e = gfarm_pfunc_copy(
	    pfunc_handle,
	    src_url, src_hostname, src_port, size,
	    dst_url, dst_hostname, dst_port, cbd, 0,
	    opt.check_disk_avail);
	gfmsg_fatal_e(e, "gfarm_pfunc_copy");
}

static void
gfprep_do_replicate(
	gfarm_pfunc_t *pfunc_handle, char *done_p,
	int opt_migrate, gfarm_off_t size,
	const char *src_url, struct gfprep_host_info *src_hi,
	struct gfprep_host_info *dst_hi)
{
	gfarm_error_t e;
	struct pfunc_cb_data *cbd;

	assert(src_hi);
	assert(dst_hi);
	cbd = gfprep_cb_data_init(
	    done_p, opt_migrate, size, src_url, src_hi, NULL, dst_hi,
	    pfunc_cb_timer_begin, pfunc_cb_timer_end_replicate,
	    pfunc_cb_start_replicate, pfunc_cb_update_default);
	gfprep_update_n_using(src_hi, 1);
	gfprep_update_n_using(dst_hi, 1);
	e = gfarm_pfunc_replicate(
	    pfunc_handle, src_url,
	    src_hi->hostname, src_hi->port, size,
	    dst_hi->hostname, dst_hi->port,
	    cbd, opt_migrate, opt.check_disk_avail);
	gfmsg_fatal_e(e, "gfarm_pfunc_replicate_from_to");
	/* update disk_avail for next scheduling */
	dst_hi->disk_avail -= size;
}

static void
gfprep_do_remove_replica(
	gfarm_pfunc_t *pfunc_handle, char *done_p, gfarm_off_t size,
	const char *src_url, struct gfprep_host_info *src_hi)
{
	gfarm_error_t e;
	struct pfunc_cb_data *cbd;

	assert(src_hi);
	cbd = gfprep_cb_data_init(
	    done_p, 0, size, src_url, src_hi, NULL, NULL,
	    pfunc_cb_timer_begin, pfunc_cb_timer_end_remove_replica,
	    pfunc_cb_start_remove_replica, pfunc_cb_update_remove_replica);
	e = gfarm_pfunc_remove_replica(
	    pfunc_handle, src_url,
	    src_hi->hostname, src_hi->port, size, cbd);
	gfmsg_fatal_e(e, "gfarm_pfunc_remove_replica");
	/* update disk_avail for next scheduling */
	src_hi->disk_avail += size;
}

struct file_job {
	char *src_host;
	char *dst_host;
	gfarm_dirtree_entry_t *file;
};

static void
gfprep_connection_job_init(struct gfprep_connection *connp)
{
	int i;
	struct gfprep_node *node;

	connp->nodes_next = 0;
	connp->nodes_base_array =
		gfarm_array_alloc_from_list(&connp->nodes_base);
	if (connp->nodes_base_array == NULL)
		gfmsg_fatal("no memory");
	connp->n_nodes_base = gfarm_list_length(&connp->nodes_base);
	for (i = 0; i < connp->n_nodes_base; i++) {
		node = connp->nodes_base_array[i];
		node->nfiles = gfarm_list_length(&node->flist);
		if (node->nfiles <= 0) {
			node->files = NULL;
			continue;
		}
		node->files = gfarm_array_alloc_from_list(&node->flist);
		if (node->files == NULL)
			gfmsg_fatal("no memory");
		node->files_next = 0;
	}
}

static void
gfprep_connection_job_free(struct gfprep_connection *connp)
{
	int i;
	struct gfprep_node *node;

	for (i = 0; i < connp->n_nodes_base; i++) {
		node = connp->nodes_base_array[i];
		free(node->files);
		node->files_next = 0;
	}
	free(connp->nodes_base_array);
	connp->n_nodes_base = 0;
	connp->nodes_next = 0;
}

static gfarm_error_t
gfprep_connection_job_next(struct file_job *jobp,
			   struct gfprep_connection *connp)
{
	struct gfprep_node *node;

	if (connp->n_nodes_base <= 0)
		return (GFARM_ERR_NO_SUCH_OBJECT);
next:
	if (connp->nodes_next >= connp->n_nodes_base)
		return (GFARM_ERR_NO_SUCH_OBJECT);
	node = connp->nodes_base_array[connp->nodes_next];
	if (node->files == NULL) {
		connp->nodes_next++;
		goto next;
	}
	assert(node->nfiles > 0);

	jobp->src_host = node->hostname;
	jobp->dst_host = NULL;
	jobp->file = node->files[node->files_next];

	node->files_next++;
	if (node->files_next >= node->nfiles) {
skip:
		connp->nodes_next++;
		if (connp->nodes_next >= connp->n_nodes_base)
			return (GFARM_ERR_NO_ERROR); /* next: end */
		node = connp->nodes_base_array[connp->nodes_next];
		if (node->nfiles <= 0)
			goto skip;
		node->files_next = 0;
	}
	return (GFARM_ERR_NO_ERROR);
}

static int gfprep_is_term();

static gfarm_error_t
gfprep_connections_exec(gfarm_pfunc_t *pfunc_handle, int is_gfpcopy,
			int migrate, const char *src_dir, const char *dst_dir,
			int n_conns, struct gfprep_connection *conns,
			struct gfarm_hash_table *target_hash_src,
			int n_array_dst, struct gfprep_host_info **array_dst)
{
	static const char diag[] = "gfprep_connections_exec";
	gfarm_error_t e;
	char *src_url = NULL, *dst_url = NULL, *tmp_url;
	int i, n_end, src_url_size = 0, dst_url_size = 0;
	struct file_job job;
	char done[n_conns]; /* 0:doing, 1:done, -1:end, locked by cb_mutex */
	char is_done;
	struct gfprep_host_info *src_hi, *dst_hi;
	struct timespec timeout;

	if (conns == NULL)
		return (GFARM_ERR_NO_ERROR);
	if (array_dst)
		assert(n_array_dst > 0);
	for (i = 0; i < n_conns; i++) {
		gfprep_connection_job_init(&conns[i]);
		done[i] = 1;
	}
	n_end = 0;
next:
	for (i = 0; i < n_conns; i++) {
		if (gfprep_is_term()) {
			e = GFARM_ERR_NO_ERROR;
			goto end;
		}

		gfarm_mutex_lock(&cb_mutex, diag, CB_MUTEX_DIAG);
		is_done = done[i];
		gfarm_mutex_unlock(&cb_mutex, diag, CB_MUTEX_DIAG);
		if (is_done == 1) {
			e = gfprep_connection_job_next(&job, &conns[i]);
			if (e == GFARM_ERR_NO_SUCH_OBJECT) {
				n_end++;
				done[i] = -1; /* end */
				continue;
			}
			gfmsg_fatal_e(e, "gfprep_connection_job_next");
			src_hi = gfprep_from_hostinfohash(
				target_hash_src, job.src_host);
			assert(src_hi);
			gfprep_url_realloc(&src_url, &src_url_size, src_dir,
					   job.file->subpath);
			if (is_gfpcopy) {
				gfprep_url_realloc(&dst_url, &dst_url_size,
						   dst_dir, job.file->subpath);
				tmp_url = dst_url;
			} else
				tmp_url = src_url;
			dst_hi = NULL;
			while (array_dst && dst_hi == NULL) {
				e = gfprep_sort_and_check_disk_avail(
					n_array_dst, array_dst, src_url,
					job.file->src_size);
				if (e == GFARM_ERR_NO_SPACE)
					goto end; /* no space */
				gfmsg_fatal_e(
				    e, "gfprep_sort_and_check_disk_avail");
				gfprep_select_dst(n_array_dst, array_dst,
						  tmp_url, job.file->src_size,
						  &dst_hi, 1);
				assert(dst_hi); /* because no_limit == 1 */
			}
			done[i] = 0;
			if (is_gfpcopy)
				gfprep_do_copy(
				    pfunc_handle, &done[i], migrate,
				    job.file->src_size,
				    src_url, src_hi, tmp_url, dst_hi);
			else
				gfprep_do_replicate(
				    pfunc_handle, &done[i], migrate,
				    job.file->src_size,
				    src_url, src_hi, dst_hi);
		}
	}
	if (n_end < n_conns) {
		is_done = 0;
		gfarm_mutex_lock(&cb_mutex, diag, CB_MUTEX_DIAG);
		for (i = 0; i < n_conns; i++) {
			if (done[i] == 1) {
				is_done = 1;
				break;
			}
		}
		if (is_done == 0) {
			gfarm_gettime(&timeout);
			timeout.tv_sec += 2;
			if (!gfarm_cond_timedwait(
			    &cb_cond, &cb_mutex, &timeout, diag, CB_COND_DIAG))
				gfmsg_debug("cb_cond timeout");
		}
		gfarm_mutex_unlock(&cb_mutex, diag, CB_MUTEX_DIAG);
		goto next;
	}
	/* success */
	e = GFARM_ERR_NO_ERROR;
end:
	for (i = 0; i < n_conns; i++)
		gfprep_connection_job_free(&conns[i]);
	if (src_url)
		free(src_url);
	if (dst_url)
		free(dst_url);

	return (e);
}

static void
gfprep_check_dirurl_filename(
	GFURL url, GFURL *dir_urlp,
	char **file_namep, int *dir_modep, int *file_modep,
	struct gfarm_timespec *dir_mtimep, struct gfarm_timespec *file_mtimep)
{
	gfarm_error_t e;
	int type;
	GFURL dir_url;
	struct gfurl_stat st, st2;

	e = gfurl_lstat(url, &st);
	if (e != GFARM_ERR_NO_ERROR) {
		gfmsg_error_e(e, "%s", gfurl_url(url));
		exit(EXIT_FAILURE);
	}
	type = gfurl_stat_file_type(&st);
	if (type == GFS_DT_REG) {
		if (file_modep)
			*file_modep = st.mode;
		if (file_mtimep)
			*file_mtimep = st.mtime;
		if (file_namep) {
			*file_namep = gfurl_path_basename(gfurl_url(url));
			gfmsg_nomem_check(*file_namep);
		}
		if (dir_urlp || dir_modep) {
			dir_url = gfurl_parent(url);
			gfmsg_nomem_check(dir_url);
			if (dir_modep || dir_mtimep) {
				e = gfurl_lstat(dir_url, &st2);
				if (e != GFARM_ERR_NO_ERROR) {
					gfmsg_error_e(e, "%s",
					    gfurl_url(dir_url));
					exit(EXIT_FAILURE);
				}
				if (dir_modep)
					*dir_modep = st2.mode;
				if (dir_mtimep)
					*dir_mtimep = st2.mtime;
			}
			if (dir_urlp)
				*dir_urlp = dir_url;
			else
				gfurl_free(dir_url);
		}
	} else if (type == GFS_DT_DIR) {
		if (dir_urlp) {
			*dir_urlp = gfurl_dup(url);
			gfmsg_nomem_check(*dir_urlp);
		}
		if (file_namep)
			*file_namep = NULL;
		if (dir_modep)
			*dir_modep = st.mode;
		if (file_modep)
			*file_modep = 0;
		if (dir_mtimep)
			*dir_mtimep = st.mtime;
		if (file_mtimep) {
			file_mtimep->tv_sec = 0;
			file_mtimep->tv_nsec = 0;
		}
	} else {
		gfmsg_error("unsupported type: %s", gfurl_url(url));
		exit(EXIT_FAILURE);
	}
}

static int
gfprep_check_busy_and_wait(
	const char *src_url, gfarm_dirtree_entry_t *entry, int n_desire,
	int n, struct gfprep_host_info **his)
{
	struct timespec timeout;
	int i,  n_unbusy, busy, waited = 0;
	static const char diag[] = "gfprep_check_busy_and_wait";

	if (n_desire <= 0)
		return (0); /* skip */
	if (n_desire > n)
		n_desire = n;

	gfarm_mutex_lock(&cb_mutex, diag, CB_MUTEX_DIAG);
	for (;;) {
		n_unbusy = 0;
		for (i = 0; i < n; i++) {
			if (his[i]->n_using < his[i]->max_rw) {
				if (++n_unbusy >= n_desire)
					break;
			}
		}
		if (n_unbusy >= n_desire) {
			busy = 0;
			break;
		}
		if (waited) {
			busy = 1;
			break;
		}
		if (entry->n_pending <= 0) { /* first time: not wait */
			busy = 1;
			break;
		}
		gfmsg_debug("wait[n_pending=%"GFARM_PRId64"]: %s",
			     entry->n_pending, src_url);
		gfarm_gettime(&timeout);
		timeout.tv_sec += 1;
		if (!gfarm_cond_timedwait(
		    &cb_cond, &cb_mutex, &timeout, diag, CB_COND_DIAG)) {
			gfmsg_debug("timeout: waiting busy host");
			busy = 1;
			break;
		}
		waited = 1;
	}
	gfarm_mutex_unlock(&cb_mutex, diag, CB_MUTEX_DIAG);
	return (busy);
}

/* debug for gfarm_dirtree_open() */
static void
gfprep_print_list(
	GFURL src_dir, GFURL dst_dir, const char *src_base_name,
	int n_para, int n_fifo,
	int excluded_regexs_num, regex_t *excluded_regexs)
{
	gfarm_error_t e;
	gfarm_dirtree_t *dirtree_handle;
	gfarm_dirtree_entry_t *entry;
	gfarm_uint64_t n_entry;
	char *src_url = NULL, *dst_url = NULL;
	int i, src_url_size = 0, dst_url_size = 0;

	e = gfarm_dirtree_init_fork(
	    &dirtree_handle, src_dir, dst_dir,
	    n_para, n_fifo, src_base_name ? 0 : 1,
	    excluded_regexs_num, excluded_regexs);
	gfmsg_fatal_e(e, "gfarm_dirtree_init_fork: %s", gfurl_url(src_dir));
	e = gfarm_dirtree_open(dirtree_handle);
	gfmsg_fatal_e(e, "gfarm_dirtree_open");
	n_entry = 0;
	while ((e = gfarm_dirtree_checknext(dirtree_handle, &entry))
	       == GFARM_ERR_NO_ERROR) {
		if (src_base_name &&
		    strcmp(src_base_name, entry->subpath) != 0) {
			gfarm_dirtree_delete(dirtree_handle);
			continue;
		}
		n_entry++;
		gfprep_url_realloc(&src_url, &src_url_size, gfurl_url(src_dir),
		    entry->subpath);
		printf("%lld: src=%s\n", (long long)n_entry, src_url);
		if (dst_dir) {
			gfprep_url_realloc(&dst_url, &dst_url_size,
			    gfurl_url(dst_dir), entry->subpath);
			printf("%lld: dst=%s\n", (long long)n_entry, dst_url);
		}
		printf("%lld: src: "
		       "size=%lld, "
		       "m_sec=%lld, "
		       "m_nsec=%d, "
		       "ncopy=%d\n",
		       (long long)n_entry,
		       (long long)entry->src_size,
		       (long long)entry->src_m_sec,
		       entry->src_m_nsec,
		       entry->src_ncopy);
		if (dst_dir) {
			if (entry->dst_exist) {
				printf("%lld: dst: "
				       "size=%lld, "
				       "m_sec=%lld, "
				       "m_nsec=%d, "
				       "ncopy=%d\n",
				       (long long)n_entry,
				       (long long)entry->dst_size,
				       (long long)entry->dst_m_sec,
				       entry->dst_m_nsec,
				       entry->dst_ncopy);
			} else
				printf("%lld: dst: not exist\n",
				       (long long)n_entry);
		}
		for (i = 0; i < entry->src_ncopy; i++)
			printf("%lld: src_copy[%d]=%s\n",
			       (long long)n_entry, i, entry->src_copy[i]);
		for (i = 0; i < entry->dst_ncopy; i++)
			printf("%lld: dst_copy[%d]=%s\n",
			       (long long)n_entry, i, entry->dst_copy[i]);
		gfarm_dirtree_delete(dirtree_handle);
	}
	gfarm_dirtree_close(dirtree_handle);
	free(src_url);
	free(dst_url);
}

static gfarm_error_t
gfprep_unlink_to_overwrite(gfarm_dirtree_entry_t *entry, GFURL dst)
{
	gfarm_error_t e;

	if (entry->dst_d_type == GFS_DT_DIR)
		e = gfurl_rmdir(dst);
	else
		e = gfurl_unlink(dst);
	if (e == GFARM_ERR_NO_ERROR)
		entry->dst_exist = 0;
	return (e);
}

static pthread_mutex_t sig_mutex = PTHREAD_MUTEX_INITIALIZER;
static const char SIG_MUTEX_DIAG[] = "sig_mutex";

static int is_term = 0; /* sig_mutex */

static int
gfprep_is_term()
{
	int i;
	static const char diag[] = "gfprep_is_term";

	gfarm_mutex_lock(&sig_mutex, diag, SIG_MUTEX_DIAG);
	i = is_term;
	gfarm_mutex_unlock(&sig_mutex, diag, SIG_MUTEX_DIAG);

	return (i);
}

static void
gfprep_sig_add(sigset_t *sigs, int sigid, const char *name)
{
	if (sigaddset(sigs, sigid) == -1)
		gfmsg_fatal("sigaddset(%s): %s", name, strerror(errno));
}

static void
gfprep_sigs_set(sigset_t *sigs)
{
	if (sigemptyset(sigs) == -1)
		gfmsg_fatal("sigemptyset: %s", strerror(errno));

	gfprep_sig_add(sigs, SIGHUP, "SIGHUP");
	gfprep_sig_add(sigs, SIGTERM, "SIGTERM");
	gfprep_sig_add(sigs, SIGINT, "SIGINT");
}

static void *
gfprep_sigs_handler(void *p)
{
	sigset_t *sigs = p;
	int rv, sig;
	static const char diag[] = "gfprep_sigs_handler";

	for (;;) {
		if ((rv = sigwait(sigs, &sig)) != 0) {
			gfmsg_warn("sigs_handler: %s", strerror(rv));
			continue;
		}
		switch (sig) {
		case SIGHUP:
		case SIGINT:
		case SIGTERM:
			gfarm_mutex_lock(&sig_mutex, diag, SIG_MUTEX_DIAG);
			is_term = 1;
			gfarm_mutex_unlock(&sig_mutex, diag, SIG_MUTEX_DIAG);
		}
	}
	return (NULL);
}

static void
gfprep_signal_init()
{
	sigset_t sigs;
	int err;
	pthread_t signal_thread;

	gfprep_sigs_set(&sigs);

	if (pthread_sigmask(SIG_BLOCK, &sigs, NULL) == -1) /* for sigwait() */
		gfmsg_fatal("pthread_sigmask(SIG_BLOCK): %s",
		    strerror(errno));

	err = pthread_create(&signal_thread, NULL, gfprep_sigs_handler, &sigs);
	if (err != 0)
		gfmsg_fatal("pthread_create: %s", strerror(err));
}

static int
can_skip_copy(int dst_is_gfarm, int force, gfarm_dirtree_entry_t *entry)
{
	if (dst_is_gfarm && entry->dst_d_type == GFS_DT_REG &&
	    entry->dst_ncopy == 0)
		return (0);
	if (!force)
		return (entry->src_m_sec <= entry->dst_m_sec);
	else
		return (entry->src_size == entry->dst_size &&
		    entry->src_m_sec == entry->dst_m_sec);
}

static void
gfpcopy_prepare_dir_for_hpss(GFURL src, GFURL dst, GFURL *new_dstp,
	int skip_existing, int simulation,
	const char *src_base_name, int src_mode)
{
	gfarm_error_t e;
	GFURL new_dst = NULL;
	int src_is_file = (src_base_name != NULL);

	/* [1] p1/d1 hpss:///p2/  : hsi mkdir /p2/d1  */
	/* [2] p1/f1 hpss:///p2/  : hsi put - : /p2/f1 */

	if (gfurl_is_hpss(src)) {
		gfmsg_error("unsupported: source is hpss: %s", gfurl_url(src));
		exit(EXIT_FAILURE);
	}

	if (!gfurl_hpss_is_available()) {
		gfmsg_error("hsi command is not available");
		exit(EXIT_FAILURE);
	}

	if (src_is_file) { /* [2] */
		new_dst = gfurl_dup(dst);
		gfmsg_nomem_check(new_dst);
	} else { /* [1] src is directory */
		char *tmp_src_base;

		e = gfurl_exist(dst);
		if (e != GFARM_ERR_NO_ERROR) { /* not exist */
			gfmsg_error("no such directory: %s",
			    gfurl_url(dst));
			exit(EXIT_FAILURE);
		}

		tmp_src_base = gfurl_path_basename(gfurl_url(src));
		gfmsg_nomem_check(tmp_src_base);
		new_dst = gfurl_child(dst, tmp_src_base);
		gfmsg_nomem_check(new_dst);
		free(tmp_src_base);

		e = gfurl_exist(new_dst);
		if (e == GFARM_ERR_NO_ERROR) { /* exist */
			gfmsg_error("already exist: %s", gfurl_url(new_dst));
			exit(EXIT_FAILURE);
		}

		if (!simulation) {
			e = gfurl_mkdir(new_dst, src_mode, skip_existing);
			if (e != GFARM_ERR_NO_ERROR) {
				gfmsg_error_e(e, "hsi mkdir: %s",
				    gfurl_url(new_dst));
				exit(EXIT_FAILURE);
			}
		}
	}
	*new_dstp = new_dst;
}

static void
gfpcopy_prepare_dir(
	GFURL src, GFURL dst, GFURL *new_dstp,
	int force_copy, int skip_existing, int simulation,
	const char *src_base_name, int src_mode,
	struct gfarm_timespec *src_mtime)
{
	gfarm_error_t e;
	GFURL new_dst = NULL;
	int create_dst_dir = 0;
	int src_is_file = (src_base_name != NULL);

	if (gfurl_is_hpss(dst)) {
		gfpcopy_prepare_dir_for_hpss(src, dst, new_dstp,
		    skip_existing, simulation, src_base_name, src_mode);
		return;
	}

	/* [1] gfpcopy p1/d1 p2/d2(exist)     : mkdir p2/d2/d1 or overwrite */
	/* [2] gfpcopy p1/d1 p2/d2(not exist) : mkdir p2/d2 */
	/* [3] gfpcopy p1/f1 p2/(exist)       : copy p2/f1 */
	/* [4] gfpcopy p1/f1 p2/(not exist)   : ENOENT */
	/* [5] gfpcopy p1/f1 p2/f1(exist)     : ENOTDIR */
	if (gfprep_is_dir(dst, NULL, &e)) {
		assert(e == GFARM_ERR_NO_ERROR);
		if (src_is_file) { /* [3] */
			if (!force_copy) {
				GFURL tmp_dst;

				tmp_dst = gfurl_child(dst, src_base_name);
				if (tmp_dst == NULL)
					gfmsg_fatal("no memory");

				if (gfprep_is_existing(tmp_dst, NULL, &e)) {
					gfmsg_error("File exists: %s",
					    gfurl_url(tmp_dst));
					gfurl_free(tmp_dst);
					exit(EXIT_FAILURE);
				}
				if (e != GFARM_ERR_NO_ERROR) {
					gfmsg_error_e(e, "%s",
					    gfurl_url(tmp_dst));
					gfurl_free(tmp_dst);
					exit(EXIT_FAILURE);
				}
				gfurl_free(tmp_dst);
			}
			assert(e == GFARM_ERR_NO_ERROR);
			new_dst = gfurl_dup(dst);
			gfmsg_nomem_check(new_dst);
			/* e for dst */
		} else { /* [1] src is directory */
			char *tmp_src_base;

			tmp_src_base = gfurl_path_basename(gfurl_url(src));
			gfmsg_nomem_check(tmp_src_base);
			new_dst = gfurl_child(dst, tmp_src_base);
			gfmsg_nomem_check(new_dst);
			free(tmp_src_base);
			if (gfprep_is_dir(new_dst, NULL, &e)) {
				/* dst exists: overwrite */
				if (!simulation)
					gfprep_set_tmp_mode_and_mtime(
					    new_dst, src_mode, src_mtime);
			}
			/* e for dst */
		}
	} else if (!src_is_file && /* src is directory */
	     e == GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY) { /* [2] */
		new_dst = gfurl_dup(dst); /* dst does not exist */
		gfmsg_nomem_check(new_dst);
	} else { /* [4] [5] or other error */
		gfmsg_error_e(e, "%s", gfurl_url(dst));
		exit(EXIT_FAILURE);
	}

	if (e == GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY) { /* new_dst is */
		e = gfprep_mkdir_restore(new_dst, !simulation,
		    src_mode, src_mtime, skip_existing);
		if (e != GFARM_ERR_NO_ERROR) {
			gfmsg_error_e(e, "cannot create directory: %s",
			    gfurl_url(new_dst));
			exit(EXIT_FAILURE);
		}
		create_dst_dir = 1;
	} else if (e != GFARM_ERR_NO_ERROR) {
		gfmsg_error_e(e, "%s", gfurl_url(new_dst));
		exit(EXIT_FAILURE);
	}
	/* dst was created or existing */

	if (!src_is_file && gfurl_is_same_dir(src, new_dst)) {
		gfmsg_error("cannot copy: %s, into itself, %s",
		    gfurl_url(src), gfurl_url(new_dst));
		if (create_dst_dir) {
			e = gfurl_rmdir(new_dst);
			gfmsg_error_e(e, "cannot remove directory: %s",
			    gfurl_url(new_dst));
		}
		exit(EXIT_FAILURE);
	}
	if (create_dst_dir && simulation) {
		e = gfurl_rmdir(new_dst);
		if (e != GFARM_ERR_NO_ERROR) {
			gfmsg_error_e(e, "cannot remove directory: %s",
			    gfurl_url(new_dst));
			exit(EXIT_FAILURE);
		}
	}
	*new_dstp = new_dst;
}

#define simulation_enabled (opt_simulate_KBs > 0)

int
main(int argc, char *argv[])
{
	int orig_argc = argc;
	char **orig_argv = argv;
	int ch, i, j, is_gfpcopy;
	gfarm_error_t e;
	GFURL src_orig, dst_orig, src, dst;
	char *src_base_name = NULL;
	int src_mode;
	struct gfarm_timespec src_mtime;
	gfarm_pfunc_t *pfunc_handle;
	gfarm_dirtree_t *dirtree_handle;
	gfarm_dirtree_entry_t *entry;
	gfarm_uint64_t n_entry, n_file;
	int n_target; /* use gfarm_list */
	struct gfarm_hash_table *hash_srcname = NULL, *hash_dstname = NULL;
	struct gfarm_hash_table *hash_all_src, *hash_all_dst;
	struct gfarm_hash_table *hash_src, *hash_dst, *target_hash_src;
	int n_array_dst = 0, n_src_available;
	struct gfprep_host_info **array_dst = NULL;
	enum way { WAY_NOPLAN, WAY_GREEDY, WAY_BAD };
	enum way way = WAY_NOPLAN;
	gfarm_list list_to_schedule;
	struct timeval time_start, time_end;
	static gfarm_uint64_t total_requested_filesize = 0;
	enum gfmsg_level msglevel;
	/* options */
	const char *opt_src_hostfile = NULL; /* -h */
	const char *opt_dst_hostfile = NULL; /* -H */
	const char *opt_src_domain = NULL;   /* -S */
	char *opt_dst_domain = NULL;   /* -D */
	const char *opt_way = NULL; /* -w */
	gfarm_uint64_t opt_sched_threshold_size
		= 50 * 1024 * 1024; /* -W, default=50MiB */
	int opt_n_para = -1; /* -j */
	gfarm_int64_t opt_simulate_KBs = -1; /* -s and -n */
	int opt_force_copy = 0; /* -f */
	int opt_skip_existing = 0; /* -e */
	int opt_n_desire = 1;  /* -N */
	int opt_migrate = 0; /* -m */
	gfarm_int64_t opt_max_copy_size = -1; /* -M */
	int opt_remove = 0;  /* -x */
	int opt_ratio = 1; /* -R */
	int opt_limited_src = 0; /* -L */
	int opt_copy_bufsize = 64 * 1024; /* -b, default=64KiB */
	int opt_dirtree_n_para = -1; /* -J */
	int opt_dirtree_n_fifo = 10000; /* -F */
	int opt_list_only = 0; /* -l */
	gfarm_list list_excluded_regex; /* -X */
	regex_t *excluded_regexs;       /* -X */
	int excluded_regexs_num;        /* -X */
	gfarm_int64_t opt_size_min = -1; /* -z */
	gfarm_int64_t opt_size_max = -1; /* -Z */

	if (argc == 0)
		gfmsg_fatal("no argument");
	program_name = basename(argv[0]);

	e = gfarm_initialize(&orig_argc, &orig_argv);
	gfmsg_fatal_e(e, "gfarm_initialize");

	e = gfarm_list_init(&list_excluded_regex);
	gfmsg_fatal_e(e, "gfarm_list_init");

	while ((ch = getopt(argc, argv,
	    "N:h:j:w:W:s:S:D:H:R:M:b:J:F:C:c:LemnpPqvdfUlxX:z:Z:?")) != -1) {
		switch (ch) {
		case 'w':
			opt_way = optarg;
			break;
		case 'W':
			opt_sched_threshold_size
				= strtol(optarg, NULL, 0) * 1024;
			break;
		case 'S':
			opt_src_domain = optarg;
			break;
		case 'D':
			opt_dst_domain = optarg;
			break;
		case 'h':
			opt_src_hostfile = optarg;
			break;
		case 'H':
			opt_dst_hostfile = optarg;
			break;
		case 'L':
			opt_limited_src = 1;
			break;
		case 'j':
			opt_n_para = strtol(optarg, NULL, 0);
			break;
		case 's':
			opt_simulate_KBs = strtoll(optarg, NULL, 0);
			break;
		case 'n':
			opt_simulate_KBs = 1000000000000LL; /* 1PB/s */
			break;
		case 'p':
			opt.performance = 1;
			break;
		case 'P':
			opt.performance = 1;
			opt.performance_each = 1;
			break;
		case 'q':
			opt.quiet = 1; /* shut up warnings */
			break;
		case 'v':
			opt.verbose = 1; /* print more information */
			break;
		case 'd':
			opt.debug = 1;
			break;
		case 'R': /* hidden option: function not implemented */
			opt_ratio = strtol(optarg, NULL, 0);
			break;
		case 'J':
			opt_dirtree_n_para = strtol(optarg, NULL, 0);
			break;
		case 'F':
			opt_dirtree_n_fifo = strtol(optarg, NULL, 0);
			break;
		case 'U':
			opt.check_disk_avail = 0;
			break;
		case 'l': /* hidden option: for debug */
			opt_list_only = 1;
			break;
		case 'C': /* hidden option: for -w noplan */
			opt.openfile_cost = strtol(optarg, NULL, 0);
			if (opt.openfile_cost < 0)
				opt.openfile_cost = 0;
			break;
		case 'c': /* hidden option */
			/* concurrency per gfsd instead of ncpu */
			opt.max_rw = strtol(optarg, NULL, 0);
			break;
		case 'N': /* gfprep */
			opt_n_desire = strtol(optarg, NULL, 0);
			break;
		case 'm': /* gfprep */
			opt_migrate = 1;
			opt_limited_src = 1;
			break;
		case 'M':
			e = gfarm_humanize_number_to_int64(
			    &opt_max_copy_size, optarg);
			if (e != GFARM_ERR_NO_ERROR) {
				gfmsg_error("-M %s: invalid number", optarg);
				exit(EXIT_FAILURE);
			}
			break;
		case 'f': /* gfpcopy */
			opt_force_copy = 1;
			break;
		case 'b': /* gfpcopy */
			opt_copy_bufsize = strtol(optarg, NULL, 0);
			break;
		case 'e': /* gfpcopy */
			opt_skip_existing = 1;
			break;
		case 'x': /* gfprep */
			opt_remove = 1;
			break;
		case 'X':
			gfarm_list_add(&list_excluded_regex, optarg);
			break;
		case 'z':
			e = gfarm_humanize_number_to_int64(
			    &opt_size_min, optarg);
			if (e != GFARM_ERR_NO_ERROR) {
				gfmsg_error("-z %s: invalid number", optarg);
				exit(EXIT_FAILURE);
			}
			break;
		case 'Z':
			e = gfarm_humanize_number_to_int64(
			    &opt_size_max, optarg);
			if (e != GFARM_ERR_NO_ERROR) {
				gfmsg_error("-Z %s: invalid number", optarg);
				exit(EXIT_FAILURE);
			}
			break;
		case '?':
		default:
			gfprep_usage_common(0);
			return (0);
		}
	}
	argc -= optind;
	argv += optind;
	/* line buffered */
	setvbuf(stdout, (char *) NULL, _IOLBF, 0);
	setvbuf(stderr, (char *) NULL, _IOLBF, 0);

	if (opt.debug) {
		opt.quiet = 0;
		opt.verbose = 1;
		msglevel = GFMSG_LEVEL_DEBUG;
	} else if (opt.verbose) {
		opt.quiet = 0;
		msglevel = GFMSG_LEVEL_INFO;
	} else if (opt.quiet)
		msglevel = GFMSG_LEVEL_ERROR;
	else /* default */
		msglevel = GFMSG_LEVEL_WARNING;

	gfmsg_init(program_name, msglevel);

	if (strcmp(program_name, name_gfpcopy) == 0) { /* gfpcopy */
		if (argc != 2)
			gfprep_usage_common(1);
		src_orig = gfurl_init(argv[0]);
		gfmsg_nomem_check(src_orig);
		if (gfurl_is_rootdir(src_orig)) {
			gfmsg_error("cannot copy root directory: %s",
			    gfurl_url(src_orig));
			exit(EXIT_FAILURE);
		}
		dst_orig = gfurl_init(argv[1]);
		gfmsg_nomem_check(dst_orig);
		is_gfpcopy = 1;
	} else { /* gfprep */
		if (argc != 1)
			gfprep_usage_common(1);
		src_orig = gfurl_init(argv[0]);
		gfmsg_nomem_check(src_orig);
		if (!gfurl_is_gfarm(src_orig))
			gfprep_usage_common(1);
		dst_orig = gfurl_dup(src_orig);
		gfmsg_nomem_check(dst_orig);
		is_gfpcopy = 0;
	}
	gfmsg_debug("set options...done");

	/* validate options */
	excluded_regexs_num = gfarm_list_length(&list_excluded_regex);
	if (excluded_regexs_num > 0) {
		regex_t *preg;
		char *regex;

		GFARM_MALLOC_ARRAY(excluded_regexs, excluded_regexs_num);
		gfmsg_nomem_check(excluded_regexs);

		for (i = 0; i < excluded_regexs_num; i++) {
			preg = &excluded_regexs[i];
			regex = GFARM_LIST_ELEM(list_excluded_regex, i);
			if (regcomp(preg, regex, REG_EXTENDED|REG_NOSUB) != 0) {
				gfmsg_error("%s: invalid regular expression",
				    regex);
				exit(EXIT_FAILURE);
			}
		}
	} else
		excluded_regexs = NULL;

	if (opt_way) {
		if (strcmp(opt_way, "noplan") == 0)
			way = WAY_NOPLAN;
		else if (strcmp(opt_way, "greedy") == 0)
			way = WAY_GREEDY;
		else if (strcmp(opt_way, "bad") == 0)
			way = WAY_BAD;
		else {
			gfmsg_error("unknown scheduling way: %s", opt_way);
			exit(EXIT_FAILURE);
		}
	}
	if (is_gfpcopy) { /* gfpcopy */
		if (opt_n_desire > 1) /* -N */
			gfprep_usage_common(1);
		if (opt_migrate) /* -m */
			gfprep_usage_common(1);
		if (opt_remove) /* -x */
			gfprep_usage_common(1);
		if (!gfurl_is_gfarm(src_orig) &&
		    (opt_src_domain || opt_src_hostfile)) {
			gfmsg_error(
			    "%s needs neither -S nor -h", gfurl_url(src_orig));
			exit(EXIT_FAILURE);
		}
		if (!gfurl_is_gfarm(dst_orig) &&
		    (opt_dst_domain || opt_dst_hostfile)) {
			gfmsg_error(
			    "%s needs neither -D nor -H", gfurl_url(dst_orig));
			exit(EXIT_FAILURE);
		}
		if (gfurl_is_hpss(src_orig)) {
			gfmsg_error(
			    "copying from HPSS (%s) is not supported",
			    gfurl_url(src_orig));
			exit(EXIT_FAILURE);
		}
	} else { /* gfprep */
		if (opt_force_copy || opt_skip_existing) /* -f or -e */
			gfprep_usage_common(1);
		if (opt_migrate) {
			if (opt_n_desire > 1) { /* -m and -N */
				gfmsg_error("gfprep needs either -N or -m");
				exit(EXIT_FAILURE);
			}
			if (opt_remove) { /* -x */
				gfmsg_error("gfprep -m does not need -x");
				exit(EXIT_FAILURE);
			}
			if (opt_src_domain == NULL &&
			    opt_src_hostfile == NULL &&
			    opt_dst_domain == NULL &&
			    opt_dst_hostfile == NULL) {
				gfmsg_error(
				    "gfprep -m needs -S or -h or -D or -H");
				exit(EXIT_FAILURE);
			}
			if (way != WAY_NOPLAN) {
				gfmsg_error("gfprep -m needs -w noplan");
				exit(EXIT_FAILURE);
			}
		} else { /* normal */
			if (opt_n_desire <= 0) /* -N */
				gfprep_usage_common(1);
			if (way != WAY_NOPLAN) {
				if (opt_n_desire > 1) {
					gfmsg_error(
					    "gfprep -N needs -w noplan");
					exit(EXIT_FAILURE);
				}
				if (opt_remove) {
					gfmsg_error(
					    "gfprep -x needs -w noplan");
					exit(EXIT_FAILURE);
				}
			}
		}
	}

	if (opt_dst_domain == NULL) {
		opt_dst_domain = gfarm_schedule_write_target_domain();
		if (opt_dst_domain)
			gfmsg_info("write_target_domain: %s", opt_dst_domain);
	} else if (strcmp(opt_dst_domain, "") == 0)
		opt_dst_domain = NULL; /* select from all nodes */
	if (opt_dst_domain != NULL) {
		/*
		 * NOTE: gfarm_schedule_write_target_domain() may be
		 * free()ed after gfarm_terminate().
		 */
		opt_dst_domain = strdup(opt_dst_domain);
		if (opt_dst_domain == NULL)
			gfmsg_fatal("no memory");
	}

	if (opt_n_para <= 0)
		opt_n_para = gfarm_ctxp->client_parallel_copy;
	if (opt_n_para <= 0) {
		gfmsg_error("client_parallel_copy must be "
			     "a positive interger");
		exit(EXIT_FAILURE);
	}
	gfmsg_debug("number of parallel to copy: %d", opt_n_para);
	if (opt_dirtree_n_para <= 0)
		opt_dirtree_n_para = GFPREP_PARALLEL_DIRTREE;
	if (opt_dirtree_n_para <= 0) {
		gfmsg_error("-J must be a positive interger");
		exit(EXIT_FAILURE);
	}
	gfmsg_debug("number of parallel to read dirents: %d",
	    opt_dirtree_n_para);
	gfmsg_debug("number of child-processes: %d",
		     opt_n_para + opt_dirtree_n_para);
	if (opt_simulate_KBs == 0)
		gfprep_usage_common(1);
	if (opt_copy_bufsize <= 0)
		gfprep_usage_common(1);
	if (opt_dirtree_n_fifo <= 0)
		gfprep_usage_common(1);

	gfprep_check_dirurl_filename(src_orig, &src, &src_base_name,
	    &src_mode, NULL, &src_mtime, NULL);

	if (is_gfpcopy)
		dst = gfurl_dup(dst_orig);
	else /* gfprep */
		dst = gfurl_dup(src);
	gfmsg_nomem_check(dst);

	if (opt_list_only) {
		e = gfarm_terminate();
		gfmsg_fatal_e(e, "gfarm_terminate");
		gfprep_print_list(
		    src, is_gfpcopy ? dst : NULL, src_base_name,
		    opt_dirtree_n_para, opt_dirtree_n_fifo,
		    excluded_regexs_num, excluded_regexs);
		gfurl_free(src_orig);
		gfurl_free(dst_orig);
		gfurl_free(src);
		gfurl_free(dst);
		free(src_base_name);
		exit(0);
	}

	gfprep_dirstat_init();
	if (opt_migrate)
		gfprep_remove_replica_deferred_init();

	if (is_gfpcopy) { /* gfpcopy */
		GFURL new_dst;

		gfpcopy_prepare_dir(src, dst, &new_dst,
		    opt_force_copy, opt_skip_existing, simulation_enabled,
		    src_base_name, src_mode, &src_mtime);
		gfurl_free(dst);
		dst = new_dst;
	}

	e = gfarm_terminate();
	gfmsg_fatal_e(e, "gfarm_terminate");
	gfmsg_debug("validate options...done");

	gfprep_signal_init();

	if (opt.performance)
		gettimeofday(&time_start, NULL);

	/* after gfarm_terminate() --------------------------------- */

	/* fork() before gfarm_initialize() and pthread_create() */
	e = gfarm_pfunc_init_fork(
	    &pfunc_handle, opt_n_para, 1, opt_simulate_KBs, opt_copy_bufsize,
	    opt_skip_existing, pfunc_cb_start, pfunc_cb_end, pfunc_cb_free);
	gfmsg_fatal_e(e, "gfarm_pfunc_init_fork");

	e = gfarm_dirtree_init_fork(&dirtree_handle, src,
	    is_gfpcopy ? dst : NULL,
	    opt_dirtree_n_para, opt_dirtree_n_fifo, src_base_name ? 0 : 1,
	    excluded_regexs_num, excluded_regexs);
	gfmsg_fatal_e(e, "gfarm_dirtree_init_fork: %s", gfurl_url(src));

	pfunc_cb_func_init();

	/* create threads */
	e = gfarm_pfunc_start(pfunc_handle);
	if (e != GFARM_ERR_NO_ERROR) {
		gfarm_pfunc_terminate(pfunc_handle);
		gfarm_pfunc_join(pfunc_handle);
		gfmsg_fatal_e(e, "gfarm_pfunc_start");
	}
	gfmsg_debug("pfunc_start...done");

	e = gfarm_dirtree_open(dirtree_handle);
	if (e != GFARM_ERR_NO_ERROR) {
		gfarm_dirtree_close(dirtree_handle);
		gfmsg_fatal_e(e, "gfarm_dirtree_open");
	}
	gfmsg_debug("dirtree_open...done");

	/* Do not fork below here */

	/* before gfarm_initialize() -------------------------------- */

	e = gfarm_initialize(&orig_argc, &orig_argv);
	gfmsg_fatal_e(e, "gfarm_initialize");

	if (gfurl_is_gfarm(src) && opt_src_hostfile) {
		e = gfprep_create_hostnamehash_from_file(
		    gfurl_url(src), opt_src_hostfile,
		    HOSTHASH_SIZE, &hash_srcname);
		gfmsg_fatal_e(e,
		    "gfprep_create_hostnamehash_from_file for src");
	}
	if (gfurl_is_gfarm(dst) && opt_dst_hostfile) {
		e = gfprep_create_hostnamehash_from_file(
		    gfurl_url(dst), opt_dst_hostfile,
		    HOSTHASH_SIZE, &hash_dstname);
		gfmsg_fatal_e(e,
		    "gfprep_create_hostnamehash_from_file for dst");
	}

	if (gfurl_is_gfarm(src)) {
		int n_src_all = 0;
		struct gfarm_hash_table *exclude_hash_dstname;
		const char *exclude_dst_domain;

		e = gfprep_create_hostinfohash_all(
		    gfurl_url(src), &n_src_all, &hash_all_src);
		gfmsg_fatal_e(e, "gfprep_create_hostinfohash_all");
		if (n_src_all == 0) {
			gfmsg_error("no available node for source");
			exit(EXIT_FAILURE);
		}
		if (is_gfpcopy) {
			/* not exclude */
			exclude_hash_dstname = NULL;
			exclude_dst_domain = NULL;
		} else {
			exclude_hash_dstname = hash_dstname;
			exclude_dst_domain = opt_dst_domain;
		}
		e = gfprep_filter_hostinfohash(
		    gfurl_url(src), hash_all_src,
		    &hash_src, hash_srcname, opt_src_domain,
		    exclude_hash_dstname, exclude_dst_domain);
		gfmsg_fatal_e(e, "gfprep_filter_hostinfohash for source");
		/* count n_src_available only */
		e = gfprep_hostinfohash_to_array(
		    gfurl_url(src), &n_src_available, NULL, hash_src);
		gfmsg_fatal_e(e, "gfprep_hostinfohash_to_array for soruce");
		if (n_src_available == 0) {
			gfmsg_error(
			    "no available node for source "
			    "(wrong -S/-h/-D/-H or write_target_domain ?)");
			exit(EXIT_FAILURE);
		}
		/* src scope */
		target_hash_src = opt_limited_src ? hash_src : hash_all_src;
	} else {
		hash_all_src = NULL;
		hash_src = NULL;
		target_hash_src = NULL;
		n_src_available = 0;
	}

	if (gfurl_is_gfarm(dst)) {
		struct gfarm_hash_table *this_hash_all_dst;
		struct gfarm_hash_table *exclude_hash_srcname;
		const char *exclude_src_domain;

		if (!is_gfpcopy) { /* gfprep */
			exclude_hash_srcname = hash_srcname;
			exclude_src_domain = opt_src_domain;
		} else { /* gfpcopy */
			/* not exclude */
			exclude_hash_srcname = NULL;
			exclude_src_domain = NULL;
		}
		if (gfurl_is_same_gfmd(src, dst)) {
			hash_all_dst = NULL;
			this_hash_all_dst = hash_all_src;
		} else { /* different gfmd */
			int n_all_dst = 0;

			e = gfprep_create_hostinfohash_all(
			    gfurl_url(dst), &n_all_dst, &hash_all_dst);
			gfmsg_fatal_e(e, "gfprep_create_hostinfohash_all");
			if (n_all_dst == 0) {
				gfmsg_error(
				    "no available node for destination");
				exit(EXIT_FAILURE);
			}
			this_hash_all_dst = hash_all_dst;
		}
		e = gfprep_filter_hostinfohash(
			gfurl_url(dst), this_hash_all_dst,
			&hash_dst, hash_dstname, opt_dst_domain,
			exclude_hash_srcname, exclude_src_domain);
		gfmsg_fatal_e(e, "gfprep_filter_hostinfohash for destination");
		e = gfprep_hostinfohash_to_array(
		    gfurl_url(dst), &n_array_dst, &array_dst, hash_dst);
		gfmsg_fatal_e(e,
		    "gfprep_hostinfohash_to_array for destination");
		if (n_array_dst == 0) {
			gfmsg_error(
			    "no available node for destination "
			    "(wrong -S/-h/-D/-H or write_target_domain ?)");
			exit(EXIT_FAILURE);
		}
	} else {
		hash_all_dst = NULL;
		hash_dst = NULL;
		array_dst = NULL;
	}
	gfmsg_debug("create hash...done");

	if (!gfurl_is_gfarm(src) || n_src_available == 1)
		way = WAY_NOPLAN;
	if (way != WAY_NOPLAN) {
		e = gfarm_list_init(&list_to_schedule);
		gfmsg_fatal_e(e, "gfarm_list_init");
	}
	gfmsg_info("scheduling method = %s",
	    way == WAY_NOPLAN ? "noplan" : "greedy");

	n_entry = n_file = 0;
	n_target = 0;
	while ((e = gfarm_dirtree_checknext(dirtree_handle, &entry))
	       == GFARM_ERR_NO_ERROR && !gfprep_is_term()) {
		struct gfprep_host_info *src_hi, *dst_hi;
		struct gfprep_host_info **src_select_array = NULL;
		struct gfprep_host_info **dst_select_array = NULL;
		struct gfprep_host_info **dst_exist_array = NULL;
		int n_desire;
		int n_src_select, n_dst_select, n_dst_exist;
		const char *src_url = NULL, *dst_url = NULL;
		GFURL src_gfurl = NULL, dst_gfurl = NULL;

		if (src_base_name &&
		    strcmp(src_base_name, entry->subpath) != 0)
			goto next_entry; /* skip */
		if (entry->n_pending == 0) { /* new entry (not pending) */
			n_entry++;
			if (entry->src_d_type == GFS_DT_REG) /* a file */
				n_file++;
		}
		src_gfurl = gfurl_child(src, entry->subpath);
		gfmsg_nomem_check(src_gfurl);
		src_url = gfurl_url(src_gfurl);

		if (entry->src_d_type == GFS_DT_REG) {
			if (opt_size_min >= 0 &&
			    opt_size_min > entry->src_size) {
				gfmsg_info("skip, size %lld(min) > %lld: %s",
				    (long long)opt_size_min,
				    (long long)entry->src_size, src_url);
				goto next_entry; /* skip */
			}
			if (opt_size_max >= 0 &&
			    opt_size_max < entry->src_size) {
				gfmsg_info("skip, size %lld(max) < %lld: %s",
				    (long long)opt_size_max,
				    (long long)entry->src_size, src_url);
				goto next_entry; /* skip */
			}
		}

		if (opt.debug) {
			gfmsg_debug(
			    "src_url=%s: size=%lld, ncopy=%d, mtime=%lld",
			    src_url, (long long)entry->src_size,
			    entry->src_ncopy, (long long)entry->src_m_sec);
			for (i = 0; i < entry->src_ncopy; i++)
				gfmsg_debug("src_url=%s: copy[%d]=%s",
				    src_url, i, entry->src_copy[i]);
		}

		if (!is_gfpcopy) { /* gfprep */
			if (entry->src_d_type != GFS_DT_REG) /* not a file */
				goto next_entry; /* nothing to do */
		} else { /* gfpcopy */
			dst_gfurl = gfurl_child(dst, entry->subpath);
			gfmsg_nomem_check(dst_gfurl);
			dst_url = gfurl_url(dst_gfurl);

			gfmsg_debug("dst[%s]=%s: ncopy=%d, mtime=%lld",
			    entry->dst_exist ? "exist" : "noent",
			    dst_url, entry->dst_ncopy,
			    (long long)entry->dst_m_sec);
			if (entry->src_d_type == GFS_DT_REG) {
				if (entry->dst_exist) {
					if (entry->dst_d_type == GFS_DT_REG) {
						if (can_skip_copy(
						    gfurl_is_gfarm(dst_gfurl),
						    opt_force_copy, entry)) {
							gfmsg_info(
							"skip: already exists:"
							" %s", dst_url);
							gfprep_count_skip_file(
							    entry->src_size);
							goto next_entry;
						}
						/* overwrite, not unlink */
					} else if (!simulation_enabled) {
						e = gfprep_unlink_to_overwrite(
						   entry, dst_gfurl);
						if (e != GFARM_ERR_NO_ERROR) {
							gfmsg_error_e(e,
							    "cannot overwrite:"
							    " %s", dst_url);
							exit(EXIT_FAILURE);
						}
					}
					gfmsg_debug(
					    "overwrite: "
					    "src_size=%lld, "
					    "dst_size=%lld, "
					    "src_m_sec=%lld, "
					    "dst_m_sec=%lld, "
					    "src_m_nsec=%d, "
					    "dst_m_nsec=%d",
					    (long long)entry->src_size,
					    (long long)entry->dst_size,
					    (long long)entry->src_m_sec,
					    (long long)entry->dst_m_sec,
					    entry->dst_m_nsec,
					    entry->dst_m_nsec);
				}
				/* through: copy a file */
			} else if (entry->src_d_type == GFS_DT_LNK) {
				if (entry->dst_exist) {
					if (entry->dst_d_type == GFS_DT_LNK &&
					    can_skip_copy(
						gfurl_is_gfarm(dst_gfurl),
						opt_force_copy, entry)) {
						gfmsg_info(
						    "already exists: %s",
						    dst_url);
						goto next_entry;
					}
					if (!simulation_enabled) {
						e = gfprep_unlink_to_overwrite(
						   entry, dst_gfurl);
						if (e != GFARM_ERR_NO_ERROR) {
							gfmsg_error_e(e,
							    "cannot overwrite:"
							    " %s", dst_url);
							exit(EXIT_FAILURE);
						}
					}
				}
				if (simulation_enabled)
					goto next_entry;
				e = gfprep_copy_symlink(
				    src_gfurl, dst_gfurl, opt_skip_existing);
				if (e == GFARM_ERR_NO_ERROR) {
					struct gfarm_timespec mtime;

					mtime.tv_sec = entry->src_m_sec;
					mtime.tv_nsec = entry->src_m_nsec;
					e = gfurl_set_mtime(dst_gfurl, &mtime);
					gfmsg_error_e(e, "cannot set mtime: %s",
					    dst_url);
				} else
					gfmsg_error_e(e,
					    "cannot copy symlink: %s", dst_url);
				if (e != GFARM_ERR_NO_ERROR &&
				    e != GFARM_ERR_OPERATION_NOT_SUPPORTED)
					exit(EXIT_FAILURE);
				goto next_entry;
			} else if (entry->src_d_type == GFS_DT_DIR) {
				struct gfarm_timespec mtime;

				if (simulation_enabled)
					goto next_entry;

				mtime.tv_sec = entry->src_m_sec;
				mtime.tv_nsec = entry->src_m_nsec;
				if (entry->dst_exist) {
					if (entry->dst_d_type == GFS_DT_DIR) {
						gfprep_set_tmp_mode_and_mtime(
						    dst_gfurl,
						    entry->src_mode, &mtime);
						goto next_entry;
					} else {
						e = gfprep_unlink_to_overwrite(
						   entry, dst_gfurl);
						if (e != GFARM_ERR_NO_ERROR) {
							gfmsg_error_e(e,
							    "cannot overwrite:"
							    " %s", dst_url);
							exit(EXIT_FAILURE);
						}
						/* deleted */
					}
				}
				e = gfprep_mkdir_restore(dst_gfurl, 1,
				    entry->src_mode, &mtime, opt_skip_existing);
				if (e != GFARM_ERR_NO_ERROR) {
					gfmsg_error_e(e,
					   "cannot create directory: %s",
					   dst_url);
					exit(EXIT_FAILURE);
				}
				goto next_entry;
			} else {
				gfmsg_warn(
				    "cannot copy (unsupported type): %s",
				    src_url);
				goto next_entry;
			}
		}

		/* ----- a file ----- */
		/* select a file within specified src  */
		if (hash_src && (hash_srcname || opt_src_domain ||
		    opt_migrate)) {
			int found = 0;

			for (i = 0; i < entry->src_ncopy; i++) {
				if (gfprep_in_hostinfohash(
					hash_src, entry->src_copy[i])) {
					found = 1;
					break;
				}
			}
			if (!found) {
				gfmsg_info("skip: not a target file: %s",
				    src_url);
				gfprep_count_skip_file(entry->src_size);
				goto next_entry;
			}
		}
		/* 0 byte file */
		if (entry->src_size == 0) {
			if (is_gfpcopy) {
				/* not specified src/dst host */
				gfprep_do_copy(
				    pfunc_handle, NULL, 0, entry->src_size,
				    src_url, NULL, dst_url, NULL);
				goto next_entry;
			} else { /* gfprep */
				assert(gfurl_is_gfarm(src));
				/* gfprep: ignore 0 byte: not replicate */
				if (entry->src_ncopy == 0) {
					gfmsg_info("skip: size=0, ncopy=0: %s",
					    src_url);
					gfprep_count_skip_file(entry->src_size);
					goto next_entry; /* skip */
				}
			}
		} else if (gfurl_is_gfarm(src) &&
			   entry->src_ncopy == 0) { /* entry->src_size > 0 */
			/* shortcut */
			gfmsg_error("no available replica: %s", src_url);
			gfprep_count_ng_file(entry->src_size);
			goto next_entry;
		}

		/* ----- WAY_GREEDY or WAY_BAD ----- */
		if (way != WAY_NOPLAN) {
			/* check an existing replica within hash_dst */
			if (!is_gfpcopy) { /* gfprep */
				int found = 0;

				assert(hash_dst);
				for (i = 0; i < entry->src_ncopy; i++) {
					if (gfprep_in_hostinfohash(
						    hash_dst,
						    entry->src_copy[i])) {
						found = 1;
						gfmsg_info(
						"skip: replica already exists: "
						"%s (%s)",
						src_url, entry->src_copy[i]);
						break;
					}
				}
				if (found) { /* not replicate */
					gfprep_count_skip_file(entry->src_size);
					goto next_entry;
				}
			}
			n_target++;
			if (n_target <= 0) { /* overflow */
				gfmsg_warn("too many target entries");
				gfurl_free(src_gfurl);
				gfurl_free(dst_gfurl);
				break;
			}
			e = gfarm_dirtree_next(dirtree_handle, &entry);
			gfmsg_fatal_e(e, "gfarm_dirtree_next");
			e = gfarm_list_add(&list_to_schedule, entry);
			gfmsg_fatal_e(e, "gfarm_list_add");
			gfurl_free(src_gfurl);
			gfurl_free(dst_gfurl);
			continue; /* next entry */
		}

		/* ----- WAY_NOPLAN ----- */
		/* select existing replicas from target_hash_src */
		if (gfurl_is_gfarm(src)) {
			gfarm_list src_select_list;

			assert(target_hash_src);
			e = gfarm_list_init(&src_select_list);
			gfmsg_fatal_e(e, "gfarm_list_init");
			/* select existing replicas from hash_src */
			for (i = 0; i < entry->src_ncopy; i++) {
				src_hi = gfprep_from_hostinfohash(
					target_hash_src, entry->src_copy[i]);
				if (src_hi) {
					e = gfarm_list_add(&src_select_list,
							   src_hi);
					gfmsg_fatal_e(e, "gfarm_list_add");
				}
			}
			n_src_select = gfarm_list_length(&src_select_list);
			if (n_src_select == 0) {
				gfarm_list_free(&src_select_list);
				gfmsg_error(
				    "no available replica for source: %s",
				    src_url);
				gfprep_count_ng_file(entry->src_size);
				goto next_entry;
			}
			/* n_src_select > 0 */
			src_select_array = gfarm_array_alloc_from_list(
				&src_select_list);
			gfarm_list_free(&src_select_list);
			if (src_select_array == NULL)
				gfmsg_fatal("no memory");
			/* prefer unbusy host */
			gfprep_host_info_array_sort_for_src(
				n_src_select, src_select_array);
			/* at least 1 unbusy host */
			if (gfprep_check_busy_and_wait(
				    src_url, entry, 1,
				    n_src_select, src_select_array)) {
				free(src_select_array);
				gfmsg_debug("pending: src hosts are busy: %s",
					     src_url);
				entry->n_pending++;
				gfarm_dirtree_pending(dirtree_handle);
				gfurl_free(src_gfurl);
				gfurl_free(dst_gfurl);
				continue; /* pending */
			}
		} else {
			src_select_array = NULL;
			n_src_select = 0;
		}
		/* select existing replicas and target hosts within dst */
		dst_exist_array = NULL;
		dst_select_array = NULL; /* target hosts */
		n_dst_exist = 0;
		n_dst_select = 0;
		if (gfurl_is_gfarm(dst)) {
			gfarm_list dst_select_list, dst_exist_list;
			int found;

			assert(array_dst);
			e = gfarm_list_init(&dst_select_list);
			gfmsg_fatal_e(e, "gfarm_list_init");
			e = gfarm_list_init(&dst_exist_list);
			gfmsg_fatal_e(e, "gfarm_list_init");
			/* select dst hosts those do not have the replica */
			for (i = 0; i < n_array_dst; i++) {
				found = 0;
				if (!is_gfpcopy) { /* gfprep */
					for (j = 0; j < entry->src_ncopy;
					     j++) {
						if (strcmp(
							array_dst[i]->hostname,
							entry->src_copy[j])
						    == 0) {
							found = 1;
							break;
						}
					}
				}
				if (found) {
					e = gfarm_list_add(&dst_exist_list,
							   array_dst[i]);
					gfmsg_fatal_e(e, "gfarm_list_add");
					n_dst_exist++;
				} else {
					e = gfprep_check_disk_avail(
						array_dst[i], entry->src_size);
					if (e == GFARM_ERR_NO_SPACE)
						continue;
					gfmsg_error_e(e, "check_disk_avail");
					e = gfarm_list_add(&dst_select_list,
							   array_dst[i]);
					gfmsg_fatal_e(e, "gfarm_list_add");
					n_dst_select++;
				}
			}
			if (n_dst_exist <= 0) {
				dst_exist_array = NULL;
				gfarm_list_free(&dst_exist_list);
			} else {
				dst_exist_array =
					gfarm_array_alloc_from_list(
						&dst_exist_list);
				gfarm_list_free(&dst_exist_list);
				if (dst_exist_array == NULL)
					gfmsg_fatal("no memory");
				/* to remove files */
				/* disk_avail: large to small */
				gfprep_host_info_array_sort_by_disk_avail(
					n_dst_exist, dst_exist_array);
			}
			if (n_dst_select <= 0) {
				dst_select_array = NULL;
				gfarm_list_free(&dst_select_list);
			} else {
				dst_select_array =
					gfarm_array_alloc_from_list(
						&dst_select_list);
				gfarm_list_free(&dst_select_list);
				if (dst_select_array == NULL)
					gfmsg_fatal("no memory");
				/* prefer unbusy and much disk_avail */
				gfprep_host_info_array_sort_for_dst(
					n_dst_select, dst_select_array);
				n_desire = opt_n_desire - n_dst_exist;
				if (gfprep_check_busy_and_wait(
					    src_url, entry, n_desire,
					    n_dst_select, dst_select_array)) {
					free(src_select_array);
					free(dst_select_array);
					free(dst_exist_array);
					gfurl_free(src_gfurl);
					gfurl_free(dst_gfurl);
					gfmsg_debug(
					    "pending: dst hosts are busy: %s",
					    src_url);
					entry->n_pending++;
					gfarm_dirtree_pending(dirtree_handle);
					continue; /* pending */
				}
			}
		}
		if (is_gfpcopy) { /* gfpcopy */
			assert(gfurl_is_gfarm(src) ? n_src_select > 0 : 1);
			if (gfurl_is_gfarm(dst) && n_dst_select <= 0) {
				gfmsg_error(
				    "insufficient number of destination nodes"
				    " to copy (n_dst=%d): %s",
				    n_dst_select, src_url);
				gfprep_count_ng_file(entry->src_size);
				goto next_entry;
			}
			n_desire = 1;
		} else if (opt_migrate) { /* gfprep -m */
			assert(n_src_select > 0);
			if (n_dst_select < n_src_select) {
				gfmsg_error(
				    "insufficient number of destination nodes"
				    " to migrate (n_src=%d, n_dst=%d): %s",
				    n_src_select, n_dst_select, src_url);
				gfprep_count_ng_file(entry->src_size);
				goto next_entry;
			} else
				n_desire = n_src_select;
		} else { /* gfprep -N */
			assert(n_src_select > 0);
			n_desire = opt_n_desire - n_dst_exist;
			if (n_desire == 0) {
				gfmsg_info("skip: enough replicas: %s",
				    src_url);
				gfprep_count_skip_file(entry->src_size);
				goto next_entry;
			} else if (n_desire < 0) {
				gfmsg_info("skip: too many replicas(num=%d):"
				    " %s", -n_desire, src_url);
				if (!opt_remove) {
					gfprep_count_skip_file(entry->src_size);
					goto next_entry;
				}
				/* disk_avail: small to large */
				for (i = n_dst_exist - 1; i >= 0; i--) {
					gfprep_do_remove_replica(
					    pfunc_handle, NULL,
					    entry->src_size,
					    src_url, dst_exist_array[i]);
					n_desire++;
					if (n_desire >= 0)
						break;
				}
				goto next_entry;
			} else if (n_dst_select < n_desire) {/* n_desire > 0 */
				gfmsg_error(
				    "insufficient number of destination nodes"
				    " to replicate (n_desire=%d, n_dst=%d):"
				    " %s", n_desire, n_dst_select, src_url);
				gfprep_count_ng_file(entry->src_size);
				goto next_entry;
			}
		}
		assert(dst_select_array ? n_dst_select >= n_desire : 1);
		j = 0; /* for src */
		for (i = 0; i < n_desire; i++) {
			if (dst_select_array)
				dst_hi = dst_select_array[i];
			else
				dst_hi = NULL;
			if (src_select_array) {
				src_hi = src_select_array[j];
				j++;
				if (j >= n_src_select)
					j = 0;
			} else
				src_hi = NULL;
			if (is_gfpcopy)
				gfprep_do_copy(pfunc_handle,
				    NULL, opt_migrate, entry->src_size,
				    src_url, src_hi, dst_url, dst_hi);
			else
				gfprep_do_replicate(pfunc_handle,
				    NULL, opt_migrate, entry->src_size,
				    src_url, src_hi, dst_hi);

			total_requested_filesize += entry->src_size;
		}
next_entry:
		free(src_select_array);
		free(dst_select_array);
		free(dst_exist_array);
		(void)gfarm_dirtree_delete(dirtree_handle);
		gfurl_free(src_gfurl);
		gfurl_free(dst_gfurl);

		if (copied_size_is_over(
		    opt_max_copy_size, total_requested_filesize)) {
			gfmsg_warn(
			    "copy stopped: copied size %lld >="
			    " limit size %lld(-M option)",
			    (long long)total_requested_filesize,
			    (long long)opt_max_copy_size);
			break; /* stop */
		}
	}
	/* GFARM_ERR_NO_SUCH_OBJECT: end */
	if (e != GFARM_ERR_NO_ERROR && e != GFARM_ERR_NO_SUCH_OBJECT) {
		gfarm_pfunc_terminate(pfunc_handle);
		gfmsg_fatal_e(e, "gfarm_dirtree_checknext");
	}

	e = gfarm_dirtree_close(dirtree_handle);
	gfmsg_warn_e(e, "gfarm_dirtree_close");

	/* ----- WAY_GREEDY or WAY_BAD ----- */
	if (way != WAY_NOPLAN) {
		struct gfarm_hash_table *hash_host_to_nodes;
		gfarm_dirtree_entry_t **ents;
		int n_ents, n_connections;
		struct gfprep_connection *connections;
		struct timeval sched_start, sched_end;

		assert(gfurl_is_gfarm(src));
		assert(hash_src);
		assert(target_hash_src);

		gettimeofday(&sched_start, NULL);
		ents = gfarm_array_alloc_from_list(&list_to_schedule);
		if (ents == NULL)
			gfmsg_fatal("no memory");
		n_ents = gfarm_list_length(&list_to_schedule);
		gfmsg_info("target file num = %d", n_ents);

		/* [HASH] Nodes: hostname to filelist */
		/* assign Files to Nodes as equally as possible */
		hash_host_to_nodes = gfarm_hash_table_alloc(
			HOSTHASH_SIZE, gfarm_hash_casefold_strptr,
			gfarm_hash_key_equal_casefold_strptr);
		if (hash_host_to_nodes == NULL)
			gfmsg_fatal("no memory");

		if (way == WAY_GREEDY) {
			gfprep_greedy_sort(n_ents, ents);
			e = gfprep_greedy_nodes_assign(hash_host_to_nodes,
						       target_hash_src,
						       n_ents, ents);
			gfmsg_fatal_e(e, "gfprep_greedy_nodes_assign");
		} else if (way == WAY_BAD) {
			e = gfprep_bad_nodes_assign(hash_host_to_nodes,
						    target_hash_src,
						    n_ents, ents);
			gfmsg_fatal_e(e, "gfprep_bad_nodes_assign");
		} else
			gfmsg_fatal("unknown scheduling way: %d", way);
		gfprep_hash_host_to_nodes_print("ASSIGN", hash_host_to_nodes);

		/* [ARRAY] Connections */
		/* assign Nodes to Connections as equally as possible */
		n_connections = opt_n_para;
		e = gfprep_connections_assign(hash_host_to_nodes,
					      n_connections, &connections);
		gfmsg_fatal_e(e, "gfprep_connections_assign");
		gfprep_connections_print("ASSIGN", connections, n_connections);

		/* swap file-copies(replicas) to flat Connections */
		e = gfprep_connections_flat(n_connections, connections,
					    opt_sched_threshold_size);
		gfmsg_fatal_e(e, "gfprep_connections_flat");
		gfprep_hash_host_to_nodes_print("FLAT", hash_host_to_nodes);
		gfprep_connections_print("FLAT", connections, n_connections);

		/* replicate files before execution to flat Connections */
		if (opt_ratio > 1)
			gfmsg_fatal_e(GFARM_ERR_FUNCTION_NOT_IMPLEMENTED,
				       "-R option");

		if (opt.performance) {
			gettimeofday(&sched_end, NULL);
			gfarm_timeval_sub(&sched_end, &sched_start);
			printf("scheduling_time: %ld.%06ld sec.\n",
			       (long) sched_end.tv_sec,
			       (long) sched_end.tv_usec);
		}

		/* execute jobs from each Connections */
		e = gfprep_connections_exec(pfunc_handle, is_gfpcopy,
		    opt_migrate, gfurl_url(src), gfurl_url(dst),
		    n_connections, connections, target_hash_src,
		    n_array_dst, array_dst);
		gfmsg_fatal_e(e, "gfprep_connections_exec");

		gfprep_connections_free(connections, n_connections);
		gfprep_hash_host_to_nodes_free(hash_host_to_nodes);
		gfarm_dirtree_array_free(n_ents, ents);
	}

	if (gfprep_is_term()) {
		gfmsg_warn("interrupted");
		gfarm_pfunc_terminate(pfunc_handle);
	}

	e = gfarm_pfunc_join(pfunc_handle);
	gfmsg_error_e(e, "gfarm_pfunc_join");

	/* set mode and mtime for directories */
	gfprep_dirstat_final();

	/* remove replicas to migrate */
	if (opt_migrate)
		gfprep_remove_replica_deferred_final();

	if (opt.performance) {
		char *prefix = is_gfpcopy ? "copied" : "replicated";
		gettimeofday(&time_end, NULL);
		gfarm_timeval_sub(&time_end, &time_start);
		printf("all_entries_num: %"GFARM_PRId64"\n", n_entry);
		printf("all_files_num: %"GFARM_PRId64"\n", n_file);
		printf("%s_file_num: %"GFARM_PRId64"\n",
		       prefix, total_ok_filenum);
		printf("%s_file_size: %"GFARM_PRId64"\n",
		       prefix, total_ok_filesize);
		if (total_skip_filenum > 0) {
			printf("skipped_file_num: %"GFARM_PRId64"\n",
			       total_skip_filenum);
			printf("skipped_file_size: %"GFARM_PRId64"\n",
			       total_skip_filesize);
		}
		if (total_ng_filenum > 0) {
			printf("failed_file_num: %"GFARM_PRId64"\n",
			       total_ng_filenum);
			printf("failed_file_size: %"GFARM_PRId64"\n",
			       total_ng_filesize);
		}
		if (removed_replica_ok_num > 0) {
			printf("removed_replica_num: %"GFARM_PRId64"\n",
			       removed_replica_ok_num);
			printf("removed_replica_size: %"GFARM_PRId64"\n",
			       removed_replica_ok_filesize);
		}
		if (removed_replica_ng_num > 0) {
			printf("failed_to_remove_replica_num: "
			       "%"GFARM_PRId64"\n", removed_replica_ng_num);
			printf("failed_to_remove_replica_num: "
			       "%"GFARM_PRId64"\n",
			       removed_replica_ng_filesize);
		}
		/* Bytes/usec == MB/sec */
		printf("total_throughput: %.6f MB/s\n",
		       (double)total_ok_filesize /
		       ((double)time_end.tv_sec * GFARM_SECOND_BY_MICROSEC
		       + time_end.tv_usec));
		printf("total_time: %lld.%06d sec.\n",
		       (long long)time_end.tv_sec, (int)time_end.tv_usec);
	}

	e = gfarm_terminate();
	gfmsg_warn_e(e, "gfarm_terminate");

	if (hash_src) /* hash only */
		gfarm_hash_table_free(hash_src);
	if (hash_dst) /* hash only */
		gfarm_hash_table_free(hash_dst);
	if (hash_all_src) /* with original data */
		gfprep_hostinfohash_all_free(hash_all_src);
	if (hash_all_dst) /* with original data */
		gfprep_hostinfohash_all_free(hash_all_dst);
	if (hash_srcname)
		gfarm_hash_table_free(hash_srcname);
	if (hash_dstname)
		gfarm_hash_table_free(hash_dstname);
	if (way != WAY_NOPLAN)
		gfarm_list_free(&list_to_schedule);
	free(array_dst);

	if (excluded_regexs != NULL) {
		for (i = 0; i < excluded_regexs_num; i++)
			regfree(&excluded_regexs[i]);
		free(excluded_regexs);
	}
	gfarm_list_free(&list_excluded_regex);

	gfurl_free(src_orig);
	gfurl_free(dst_orig);
	gfurl_free(src);
	gfurl_free(dst);
	free(src_base_name);
	free(opt_dst_domain);

	return (total_ng_filenum > 0 ? 1 : 0);
}
