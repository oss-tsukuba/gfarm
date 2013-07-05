/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <limits.h>
#include <assert.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <errno.h>

#include <gfarm/gfarm.h>

#include "hash.h"
#include "nanosec.h"
#include "thrsubr.h"
#include "gfutil.h"
#include "queue.h"

#include "config.h"
#include "gfarm_path.h"
#include "gfm_client.h"
#include "host.h"
#include "lookup.h"

#include "gfarm_list.h"

#include "gfprep.h"
#include "gfarm_parallel.h"
#include "gfarm_dirtree.h"
#include "gfarm_pfunc.h"

#define HOSTHASH_SIZE 101

static const char name_gfprep[] = "gfprep";
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
#define CB_MUTEX_DIAG "cb_mutex"
#define CB_COND_DIAG "cb_cond"

/* locked by cb_mutex */
static gfarm_uint64_t total_ok_filesize = 0;
static gfarm_uint64_t total_ok_filenum = 0;
static gfarm_uint64_t total_ng_filesize = 0;
static gfarm_uint64_t total_ng_filenum = 0;
static gfarm_uint64_t removed_replica_ok_num = 0;
static gfarm_uint64_t removed_replica_ng_num = 0;
static gfarm_uint64_t removed_replica_ok_filesize = 0;
static gfarm_uint64_t removed_replica_ng_filesize = 0;

/* -------------------------------------------------------------- */

const char GFPREP_FILE_URL_PREFIX[] = "file:";

int
gfprep_url_is_local(const char *url)
{
	return (url ?
		memcmp(url, GFPREP_FILE_URL_PREFIX,
		       GFPREP_FILE_URL_PREFIX_LENGTH) == 0 : 0);
}

int
gfprep_url_is_gfarm(const char *url)
{
	return (url ? gfarm_is_url(url) : 0);
}

int
gfprep_vasprintf(char **strp, const char *format, va_list ap)
{
	int n, size = 32;
	char *p, *np;
	va_list aq;

	GFARM_MALLOC_ARRAY(p, size);
	if (p == NULL)
		return (-1);
	for (;;) {
		va_copy(aq, ap);
		n = vsnprintf(p, size, format, aq);
		va_end(aq);
		if (n > -1 && n < size) {
			*strp = p;
			return (strlen(p));
		}
		if (n > -1) /* glibc 2.1 */
			size = n + 1;
		else /* n == -1: glibc 2.0 */
			size *= 2;
		GFARM_REALLOC_ARRAY(np, p, size);
		if (np == NULL) {
			free(p);
			*strp = NULL;
			return (-1);
		} else
			p = np;
	}
}

int
gfprep_asprintf(char **strp, const char *format, ...)
{
	va_list ap;
	int retv;

	va_start(ap, format);
	retv = gfprep_vasprintf(strp, format, ap);
	va_end(ap);
	return (retv);
}

/* -------------------------------------------------------------- */

static void
gfprep_usage()
{
	fprintf(stderr,
"\t[-N <#replica>] [-x (remove surplus replicas)] [-m (migrate)]\n"
"\t<gfarm_url(gfarm:///...) or relative-path>\n");
}

static void
gfpcopy_usage()
{
	fprintf(stderr,
"\t[-f (force copy)(overwrite)] [-b <#bufsize to copy>]\n"
"\t<src_url(gfarm:///... or file:///...) or relative-path>\n"
"\t<dst_dir(gfarm:///... or file:///...) or relative-path>\n");
}

static void
gfprep_usage_common(int error)
{
	fprintf(stderr,
"Usage: %s [-?] [-q (quiet)] [-v (verbose)] [-d (debug)]\n"
"\t[-S <source domainname to select a replica>]\n"
"\t[-h <source hostfile to select a replica>]\n"
"\t[-L (select a src_host within specified source)(limited scope)]\n"
"\t[-D <destination domainname\n"
"\t     ('' means all nodes even if write_target_domain is set)>]\n"
"\t[-H <destination hostfile>]\n"
"\t[-j <#parallel(connections)>]\n"
"\t[-w <scheduling way (noplan,greedy)(default:noplan)>]\n"
"\t[-W <#KB> (threshold size to flat connections cost)(for -w greedy)]\n"
"\t[-p (report total performance) | -P (report each and total performance)]"
"\t[-n (not execute)] [-s <#KB/s(simulate)>]\n"
"\t[-U (disable checking disk_avail)(fast)]\n"
"\t[-M <#byte(maximum total size of copied file)>(default: unlimited)]\n"
/* "\t[-R <#ratio (throughput: local=remote*ratio)(for -w greedy)>]\n" */
/* "\t[-J <#parallel(read dirents)>]\n" */
"\t[-F <#dirents(readahead)>]\n",
		program_name);
	if (strcmp(program_name, name_gfpcopy) == 0)
		gfpcopy_usage();
	else
		gfprep_usage();
	if (error)
		exit(EXIT_FAILURE);
}

static void
gfprep_vfprintf(FILE *out, const char *level, int quiet, gfarm_error_t *ep,
		const char *format, va_list ap)
{
	if (quiet)
		return;
	if (level)
		fprintf(out, "%s[%s] ", program_name, level);
	vfprintf(out, format, ap);
	if (ep)
		fprintf(out, ": %s\n", gfarm_error_string(*ep));
	else
		fprintf(out, "\n");
}

static void
gfprep_err_v(const char *level, int quiet, gfarm_error_t *ep,
	     const char *format, va_list ap)
{
	if (quiet)
		return;
	gfprep_vfprintf(stderr, level, quiet, ep, format, ap);
}

#if 0
static void
gfprep_msg_v(int quiet, const char *format, va_list ap)
{
	if (quiet)
		return;
	gfprep_vfprintf(stdout, NULL, quiet, NULL, format, ap);
}
#endif

static void gfprep_fatal_e(gfarm_error_t, const char *, ...) \
	GFLOG_PRINTF_ARG(2, 3);
static void gfprep_error_e(gfarm_error_t, const char *, ...) \
	GFLOG_PRINTF_ARG(2, 3);
static void gfprep_warn_e(gfarm_error_t, const char *, ...) \
	GFLOG_PRINTF_ARG(2, 3);
static void gfprep_fatal(const char *, ...) GFLOG_PRINTF_ARG(1, 2);
static void gfprep_error(const char *, ...) GFLOG_PRINTF_ARG(1, 2);
static void gfprep_warn(const char *, ...) GFLOG_PRINTF_ARG(1, 2);
static void gfprep_verbose(const char *, ...) GFLOG_PRINTF_ARG(1, 2);
static void gfprep_debug(const char *, ...) GFLOG_PRINTF_ARG(1, 2);
#if 0
static void gfprep_msg(int, const char *, ...) GFLOG_PRINTF_ARG(2, 3);
#endif

static void
gfprep_fatal_e(gfarm_error_t e, const char *format, ...)
{
	va_list ap;

	if (e == GFARM_ERR_NO_ERROR)
		return;
	va_start(ap, format);
	gfprep_err_v("FATAL", 0, &e, format, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

static void
gfprep_fatal(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	gfprep_err_v("FATAL", 0, NULL, format, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

static void
gfprep_error_e(gfarm_error_t e, const char *format, ...)
{
	va_list ap;

	if (e == GFARM_ERR_NO_ERROR)
		return;
	va_start(ap, format);
	gfprep_err_v("ERROR", 0, &e, format, ap);
	va_end(ap);
}

static void
gfprep_error(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	gfprep_err_v("ERROR", 0, NULL, format, ap);
	va_end(ap);
}

static void
gfprep_warn_e(gfarm_error_t e, const char *format, ...)
{
	va_list ap;

	if (e == GFARM_ERR_NO_ERROR)
		return;
	va_start(ap, format);
	gfprep_err_v("WARN", opt.quiet, &e, format, ap);
	va_end(ap);
}

static void
gfprep_warn(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	gfprep_err_v("WARN", opt.quiet, NULL, format, ap);
	va_end(ap);
}

static void
gfprep_verbose(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	gfprep_err_v("INFO", !opt.verbose, NULL, format, ap);
	va_end(ap);
}

static void
gfprep_debug(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	gfprep_err_v("DEBUG", !opt.debug, NULL, format, ap);
	va_end(ap);
}

#if 0
static void
gfprep_msg(int quiet, const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	gfprep_msg_v(quiet, format, ap);
	va_end(ap);
}
#endif

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
			gfprep_fatal(
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
			gfprep_fatal(
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
			gfprep_warn_e(e, "%s in %s", hosts[i], hostfile);
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
			gfprep_error_e(e, "%s: line %d", hostfile, error_line);
		else
			gfprep_error_e(e, "%s", hostfile);
		exit(EXIT_FAILURE);
	}
	e = gfprep_create_hostnamehash_from_array(path, hostfile,
						  nhosts, hosts,
						  HOSTHASH_SIZE, hashp);
	gfarm_strings_free_deeply(nhosts, hosts);
	return (e);
}

/* GFS_DT_REG, GFS_DT_DIR, GFS_DT_LNK, GFS_DT_UNKNOWN */
static int
gfprep_get_type(int is_gfarm, const char *url, int *modep,
	struct gfarm_timespec *mtimep, gfarm_error_t *ep)
{
	if (is_gfarm) {
		struct gfs_stat st;
		gfarm_error_t e;

		e = gfs_lstat(url, &st);
		if (ep)
			*ep = e;
		if (e != GFARM_ERR_NO_ERROR)
			return (GFS_DT_UNKNOWN);
		if (modep)
			*modep = (int) st.st_mode & 07777;
		if (mtimep)
			*mtimep = st.st_mtimespec;
		gfs_stat_free(&st);
		if (GFARM_S_ISREG(st.st_mode))
			return (GFS_DT_REG);
		else if (GFARM_S_ISDIR(st.st_mode))
			return (GFS_DT_DIR);
		else if (GFARM_S_ISLNK(st.st_mode))
			return (GFS_DT_LNK);
		return (GFS_DT_UNKNOWN);
	} else {
		int retv;
		struct stat st;
		const char *p = url + GFPREP_FILE_URL_PREFIX_LENGTH;

		errno = 0;
		retv = lstat(p, &st);
		if (ep)
			*ep = gfarm_errno_to_error(errno);
		if (retv == -1)
			return (GFS_DT_UNKNOWN);
		if (modep)
			*modep = (int) st.st_mode & 07777;
		if (mtimep) {
			mtimep->tv_sec = st.st_mtime;
			mtimep->tv_nsec = gfarm_stat_mtime_nsec(&st);
		}
		if (S_ISREG(st.st_mode))
			return (GFS_DT_REG);
		else if (S_ISDIR(st.st_mode))
			return (GFS_DT_DIR);
		else if (S_ISLNK(st.st_mode))
			return (GFS_DT_LNK);
		return (GFS_DT_UNKNOWN);
	}
}

static int
gfprep_is_dir(int is_gfarm, const char *url, int *modep, gfarm_error_t *ep)
{
	int type;
	gfarm_error_t e;

	type = gfprep_get_type(is_gfarm, url, modep, NULL, &e);
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
	return (1);
}

static int
gfprep_is_existed(int is_gfarm, const char *url, int *modep, gfarm_error_t *ep)
{
	gfarm_error_t e;

	gfprep_get_type(is_gfarm, url, modep, NULL, &e);
	if (e == GFARM_ERR_NO_ERROR) {
		if (ep)
			*ep = e;
		return (1);
	} else if (e == GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY) {
		if (ep)
			*ep = GFARM_ERR_NO_ERROR;
		return (0);
	}
	gfprep_error_e(e, "%s", url);
	if (ep)
		*ep = e;
	return (0);
}

static int
gfprep_is_same_gfmd(const char *src_url, int src_is_gfarm,
		    const char *dst_url, int dst_is_gfarm)
{
	gfarm_error_t e;
	char *src_hostname, *dst_hostname;
	int src_port, dst_port;
	int retv;

	if (src_url == NULL || dst_url == NULL || src_is_gfarm != dst_is_gfarm)
		return (0);  /* different */
	if (src_url == dst_url)
		return (1); /* shortcut */

	e = gfarm_get_hostname_by_url(src_url, &src_hostname, &src_port);
	if (e != GFARM_ERR_NO_ERROR) {
		gfprep_error_e(e, "gfarm_get_hostname_by_url(%s)", src_url);
		return (1); /* error */
	}
	e = gfarm_get_hostname_by_url(dst_url, &dst_hostname, &dst_port);
	if (e != GFARM_ERR_NO_ERROR) {
		free(src_hostname);
		gfprep_error_e(e, "gfarm_get_hostname_by_url(%s)", dst_url);
		return (1); /* error */
	}
	if (src_port != dst_port) {
		retv = 0; /* different */
		goto end;
	} else if (strcmp(src_hostname, dst_hostname) != 0) {
		retv = 0; /* different */
		goto end;
	}
	/* same */
	retv = 1;
end:
	free(src_hostname);
	free(dst_hostname);
	return (retv);
}

static int
gfprep_is_same_name(const char *src_url, int src_is_gfarm,
		    const char *dst_url, int dst_is_gfarm)
{
	gfarm_error_t e;
	struct stat st;
	struct gfs_stat gst;
	int retv;
	gfarm_ino_t g_src_ino, g_dst_ino;
	gfarm_uint64_t g_src_gen, g_dst_gen;

	if (src_is_gfarm != dst_is_gfarm)
		return (0); /* different */
	else if (src_is_gfarm == 0 && dst_is_gfarm == 0) {
		const char *s = src_url;
		char *d = (char *) dst_url, *d2, *d_tmp, *d_tmp2;
		ino_t src_ino;
		dev_t src_dev;
		s += GFPREP_FILE_URL_PREFIX_LENGTH;
		d += GFPREP_FILE_URL_PREFIX_LENGTH;
		retv = lstat(s, &st);
		if (retv == -1) {
			gfprep_error("lstat(%s): %s", s, strerror(errno));
			return (1); /* unexpected */
		}
		src_ino = st.st_ino;
		src_dev = st.st_dev;
		d_tmp = strdup(d);
		if (d_tmp == NULL) {
			gfprep_error("no memory");
			return (1); /* error */
		}
		for (;;) {
			retv = lstat(d, &st);
			if (retv == -1) {
				free(d_tmp);
				gfprep_error("lstat(%s): %s",
					d, strerror(errno));
				return (1); /* unexpected */
			}
			if (src_dev == st.st_dev &&
			    src_ino == st.st_ino) {
				free(d_tmp);
				return (1); /* same name */
			}
			d_tmp2 = strdup(d);
			if (d_tmp2 == NULL) {
				free(d_tmp);
				gfprep_fatal("no memory");
			}
			d2 = dirname(d_tmp2);
			if (strcmp(d, d2) == 0) {
				free(d_tmp);
				free(d_tmp2);
				break;
			}
			free(d_tmp); /* not use d */
			d_tmp = d_tmp2;
			d = d2;
		}
		return (0); /* different */
	}
	assert(src_is_gfarm == 1);
	assert(dst_is_gfarm == 1);

	if (!gfprep_is_same_gfmd(src_url, src_is_gfarm, dst_url, dst_is_gfarm))
		return (0);

	/* same gfmd */
	e = gfs_lstat(src_url, &gst);
	if (e != GFARM_ERR_NO_ERROR) {
		gfprep_error_e(e, "gfs_lstat(%s)", src_url);
		return (0); /* unexpected */
	} else {
		char *d = strdup(dst_url), *d2;
		g_src_ino = gst.st_ino;
		g_src_gen = gst.st_gen;
		gfs_stat_free(&gst);
		for (;;) {
			e = gfs_lstat(d, &gst);
			if (e != GFARM_ERR_NO_ERROR) {
				gfprep_error_e(e, "gfs_lstat(%s)", d);
				free(d);
				return (0); /* unexpected */
			}
			g_dst_ino = gst.st_ino;
			g_dst_gen = gst.st_gen;
			gfs_stat_free(&gst);
			if (g_src_ino == g_dst_ino && g_src_gen == g_dst_gen) {
				free(d);
				return (1); /* same name */
			}
			d2 = gfarm_url_dir(d);
			if (d2 == NULL)
				gfprep_fatal("no memory");
			if (strcmp(d, d2) == 0) {
				free(d);
				free(d2);
				return (0); /* different */
			}
			free(d);
			d = d2;
		}
	}
}

static gfarm_error_t
gfprep_set_mtime(int is_gfarm, const char *url, struct gfarm_timespec *mtimep)
{
	gfarm_error_t e;

	if (mtimep == NULL)
		return (GFARM_ERR_NO_ERROR);

	if (is_gfarm) {
		struct gfarm_timespec gt[2];

		gt[0].tv_sec = gt[1].tv_sec = mtimep->tv_sec;
		gt[0].tv_nsec = gt[1].tv_nsec = mtimep->tv_nsec;
		e = gfs_lutimes(url, gt);
		if (e != GFARM_ERR_NO_ERROR) {
			gfprep_error_e(e, "gfs_lutimes(%s)", url);
			return (e);
		}
	} else { /* to local */
		int retv, save_errno;
		struct timespec ts[2];
		const char *path = url + GFPREP_FILE_URL_PREFIX_LENGTH;

		ts[0].tv_sec = ts[1].tv_sec = mtimep->tv_sec;
		ts[0].tv_nsec = ts[1].tv_nsec = mtimep->tv_nsec;
		retv = gfarm_local_lutimens(path, ts);
		if (retv == -1) {
			save_errno = errno;
			e = gfarm_errno_to_error(save_errno);
			if (save_errno == EOPNOTSUPP) {
				if (opt.verbose)
					gfprep_warn_e(e,
					    "gfarm_local_lutimens(%s)", url);
			} else
				gfprep_error_e(
				    e, "gfarm_local_lutimens(%s)", url);
			return (e);
		}
	}
	return (GFARM_ERR_NO_ERROR);
}

/* do not call this for symlinks */
static gfarm_error_t
gfprep_chmod(int is_gfarm, const char *url, int mode)
{
	gfarm_error_t e;

	if (is_gfarm) {
		e = gfs_lchmod(url, (gfarm_mode_t)mode);
		if (e != GFARM_ERR_NO_ERROR) {
			gfprep_error_e(e, "gfs_lchmod(%s, %o)", url, mode);
			return (e);
		}
	} else { /* to local */
		int retv;
		const char *path = url + GFPREP_FILE_URL_PREFIX_LENGTH;

		retv = chmod(path, (mode_t)mode);
		if (retv == -1) {
			e = gfarm_errno_to_error(errno);
			gfprep_error_e(e, "chmod(%s, %o)", url, mode);
			return (e);
		}
	}
	return (GFARM_ERR_NO_ERROR);
}

struct dirstat {
	GFARM_HCIRCLEQ_ENTRY(dirstat) list;
	int is_gfarm;
	char *url;
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
gfprep_dirstat_add(int is_gfarm, const char *url, int orig_mode, int tmp_mode,
	struct gfarm_timespec *mtimep)
{
	char *url2;
	struct dirstat *ds;

	GFARM_MALLOC(ds);
	url2 = strdup(url);
	if (ds == NULL || url2 == NULL) {
		free(ds);
		free(url2);
		gfprep_error("cannot copy mode and mtime (no memory): %s: ",
		    url);
	} else {
		ds->is_gfarm = is_gfarm;
		ds->url = url2;
		ds->orig_mode = orig_mode;
		ds->tmp_mode = tmp_mode;
		ds->mtime = *mtimep;
		GFARM_HCIRCLEQ_INSERT_HEAD(dirstat_head, ds, list);
	}
}

static void
gfprep_dirstat_final(void)
{
	struct dirstat *ds;

	while (!GFARM_HCIRCLEQ_EMPTY(dirstat_head, list)) {
		ds = GFARM_HCIRCLEQ_FIRST(dirstat_head, list);
		GFARM_HCIRCLEQ_REMOVE(ds, list);

		gfprep_verbose("set mode and mtime: %s", ds->url);

		(void)gfprep_set_mtime(ds->is_gfarm, ds->url, &ds->mtime);

		if (ds->orig_mode != ds->tmp_mode)
			(void)gfprep_chmod(ds->is_gfarm, ds->url,
			    ds->orig_mode);

		free(ds->url);
		free(ds);
	}
}

static void
gfprep_set_tmp_mode_and_mtime(int is_gfarm, const char *url, int mode,
	struct gfarm_timespec *mtimep)
{
	gfarm_error_t e;
	int tmp_mode;

	mode = mode & 0777;
	tmp_mode = mode | 0700; /* to create files in any st_mode and mask */
	if (mode != tmp_mode) {
		e = gfprep_chmod(is_gfarm, url, tmp_mode);
		if (e == GFARM_ERR_NO_ERROR)
			gfprep_verbose("chmod(mode=%o): %s", tmp_mode, url);
	}

	gfprep_dirstat_add(is_gfarm, url, mode, tmp_mode, mtimep);
}

static gfarm_error_t
gfprep_mkdir(int is_gfarm, const char *url, int restore, int mode,
	struct gfarm_timespec *mtimep)
{
	gfarm_error_t e;
	int tmp_mode;

	mode = mode & 0777 & ~mask;
	tmp_mode = mode | 0700; /* to create files in any st_mode and mask */
	if (is_gfarm) {
		e = gfs_mkdir(url, (gfarm_mode_t)tmp_mode);
		if (e != GFARM_ERR_NO_ERROR) {
			gfprep_error_e(e, "gfs_mkdir(%s)", url);
			return (e);
		}
	} else { /* to local */
		int retv;
		const char *path = url;

		path += GFPREP_FILE_URL_PREFIX_LENGTH;
		retv = mkdir(path, (mode_t)tmp_mode);
		if (retv == -1) {
			e = gfarm_errno_to_error(errno);
			gfprep_error_e(e, "mkdir(%s)", url);
			return (e);
		}
	}
	gfprep_verbose("mkdir(mode=%o): %s", tmp_mode, url);

	if (restore)
		gfprep_dirstat_add(is_gfarm, url, mode, tmp_mode, mtimep);

	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfprep_rmdir(int is_gfarm, const char *url)
{
	gfarm_error_t e;

	if (is_gfarm) {
		e = gfs_rmdir(url);
		if (e != GFARM_ERR_NO_ERROR) {
			gfprep_error_e(e, "gfs_rmdir(%s)", url);
			return (e);
		}
	} else { /* to local */
		int retv;
		const char *path = url;
		path += GFPREP_FILE_URL_PREFIX_LENGTH;
		retv = rmdir(path);
		if (retv == -1) {
			e = gfarm_errno_to_error(errno);
			gfprep_error_e(e, "rmdir(%s)", url);
			return (e);
		}
	}
	gfprep_verbose("rmdir: %s", url);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfprep_unlink(int is_gfarm, const char *url)
{
	gfarm_error_t e;

	if (is_gfarm) {
		e = gfs_unlink(url);
		if (e != GFARM_ERR_NO_ERROR) {
			gfprep_error_e(e, "gfs_unlink(%s)", url);
			return (e);
		}
	} else { /* to local */
		int retv;
		const char *path = url;
		path += GFPREP_FILE_URL_PREFIX_LENGTH;
		retv = unlink(path);
		if (retv == -1) {
			e = gfarm_errno_to_error(errno);
			gfprep_error_e(e, "unlink(%s)", url);
			return (e);
		}
	}
	gfprep_verbose("unlink: %s", url);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfprep_readlink(int is_gfarm, const char *url, char **targetp)
{
	gfarm_error_t e;

	if (is_gfarm) {
		e = gfs_readlink(url, targetp);
		if (e != GFARM_ERR_NO_ERROR) {
			gfprep_error_e(e, "gfs_readlink(%s)", url);
			return (e);
		}
	} else { /* to local */
		const char *path = url;
		size_t bufsize = 4096;
		char buf[bufsize];
		ssize_t len;
		path += GFPREP_FILE_URL_PREFIX_LENGTH;
		len = readlink(path, buf, bufsize - 1);
		if (len == -1) {
			e = gfarm_errno_to_error(errno);
			gfprep_error_e(e, "readlink(%s)", url);
			return (e);
		}
		buf[len] = '\0';
		*targetp = strdup(buf);
		if (targetp == NULL) {
			gfprep_error("readlink(%s): no memory", url);
			return (GFARM_ERR_NO_MEMORY);
		}
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfprep_symlink(int is_gfarm, const char *url, char *target)
{
	gfarm_error_t e;

	if (is_gfarm) {
		e = gfs_symlink(target, url);
		if (e != GFARM_ERR_NO_ERROR) {
			gfprep_error_e(e, "gfs_symlink(%s)", url);
			return (e);
		}
	} else { /* to local */
		const char *path = url;
		int retv;
		path += GFPREP_FILE_URL_PREFIX_LENGTH;
		retv = symlink(target, path);
		if (retv == -1) {
			e = gfarm_errno_to_error(errno);
			gfprep_error_e(e, "symlink(%s)", url);
			return (e);
		}
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfprep_copy_symlink(int src_is_gfarm, const char *src_url,
		    int dst_is_gfarm, const char *dst_url)
{
	gfarm_error_t e;
	char *target;

	e = gfprep_readlink(src_is_gfarm, src_url, &target);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	e = gfprep_symlink(dst_is_gfarm, dst_url, target);
	if (e == GFARM_ERR_NO_ERROR)
		gfprep_verbose("symlink: %s -> %s", dst_url, target);
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
	void (*func_timer_end)(struct pfunc_cb_data *, int);
	void (*func_start)(struct pfunc_cb_data *);
	void (*func_update)(struct pfunc_cb_data *, int);
};

static void (*pfunc_cb_start_copy)(struct pfunc_cb_data *) = NULL;
static void (*pfunc_cb_start_replicate)(struct pfunc_cb_data *) = NULL;
static void (*pfunc_cb_start_remove_replica)(struct pfunc_cb_data *) = NULL;

static void (*pfunc_cb_timer_begin)(struct pfunc_cb_data *) = NULL;

static void
pfunc_cb_start_copy_main(struct pfunc_cb_data *cbd)
{
	gfprep_debug("START COPY: %s (%s:%d) -> %s (%s:%d)",
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
	gfprep_debug("START %s: %s (%s:%d -> %s:%d)",
	    cbd->migrate ? "MIGRATE" : "REPLICATE",  cbd->src_url,
	    cbd->src_hi ? cbd->src_hi->hostname : "local",
	    cbd->src_hi ? cbd->src_hi->port : 0,
	    cbd->dst_hi ? cbd->dst_hi->hostname : "local",
	    cbd->dst_hi ? cbd->dst_hi->port : 0);
}

static void
pfunc_cb_start_remove_replica_main(struct pfunc_cb_data *cbd)
{
	gfprep_debug("START REMOVE REPLICA: %s (%s:%d)",
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

static void (*pfunc_cb_timer_end_copy)(struct pfunc_cb_data *, int) = NULL;
static void (*pfunc_cb_timer_end_replicate)(struct pfunc_cb_data *, int)
= NULL;
static void (*pfunc_cb_timer_end_remove_replica)(struct pfunc_cb_data *, int)
= NULL;

static const char pfunc_cb_ok[] = "OK";
static const char pfunc_cb_ng[] = "NG";
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

static void
pfunc_cb_timer_end_copy_main(struct pfunc_cb_data *cbd, int success)
{
	double mbs, sec;

	timer_end(cbd, &mbs, &sec);
	printf("[%s]COPY" PF_FMT "%s",
	    success ? pfunc_cb_ok : pfunc_cb_ng, mbs, sec, cbd->src_url);
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
pfunc_cb_timer_end_replicate_main(struct pfunc_cb_data *cbd, int success)
{
	double mbs, sec;

	timer_end(cbd, &mbs, &sec);
	printf("[%s]%s" PF_FMT "%s (%s:%d -> %s:%d)\n",
	    success ? pfunc_cb_ok : pfunc_cb_ng,
	    cbd->migrate ? "MIGRATE" : "REPLICATE",
	    mbs, sec, cbd->src_url,
	    cbd->src_hi->hostname, cbd->src_hi->port,
	    cbd->dst_hi->hostname, cbd->dst_hi->port);
}

static void
pfunc_cb_timer_end_remove_replica_main(struct pfunc_cb_data *cbd, int success)
{
	printf("[%s]REMOVE REPLICA: %s (%s:%d)\n",
	    success ? pfunc_cb_ok : pfunc_cb_ng, cbd->src_url,
	    cbd->src_hi->hostname, cbd->src_hi->port);
}

static void
pfunc_cb_update_default(struct pfunc_cb_data *cbd, int success)
{
	if (cbd->src_hi)
		cbd->src_hi->n_using--;
	if (cbd->dst_hi)
		cbd->dst_hi->n_using--;
	if (success) {
		total_ok_filesize += cbd->filesize;
		total_ok_filenum++;
	} else {
		total_ng_filesize += cbd->filesize;
		total_ng_filenum++;
		if (cbd->dst_hi)
			cbd->dst_hi->failed_size += cbd->filesize;
	}
	if (cbd->done_p)
		*cbd->done_p = 1;
}

static void
pfunc_cb_update_remove_replica(struct pfunc_cb_data *cbd, int success)
{
	if (success) {
		removed_replica_ok_num++;
		removed_replica_ok_filesize += cbd->filesize;
	} else {
		removed_replica_ng_num++;
		removed_replica_ng_filesize += cbd->filesize;
		cbd->src_hi->failed_size -= cbd->filesize;
	}
}

static void
pfunc_cb_end(int success, void *data)
{
	static const char diag[] = "pfunc_cb_end";
	struct pfunc_cb_data *cbd = data;

	if (cbd == NULL)
		return;

	gfarm_mutex_lock(&cb_mutex, diag, CB_MUTEX_DIAG);

	if (cbd->func_timer_end)
		cbd->func_timer_end(cbd, success);

	cbd->func_update(cbd, success);

	gfarm_cond_signal(&cb_cond, diag, CB_COND_DIAG);
	gfarm_mutex_unlock(&cb_mutex, diag, CB_MUTEX_DIAG);

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
	if (total_requested - total_ng_filesize > limit)
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
			gfprep_fatal("no memory");
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
			gfprep_fatal("no memory");
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
			gfprep_fatal("no memory");
		nodes = gfarm_hash_entry_data(he);
		if (created) {
			int max_rw = hi->max_rw;
			GFARM_MALLOC_ARRAY(nodes->nodes, max_rw);
			if (nodes->nodes == NULL)
				gfprep_fatal("no memory");
			for (j = 0; j < max_rw; j++) {
				e = gfarm_list_init(&nodes->nodes[j].flist);
				gfprep_fatal_e(e, "gfarm_list_init");
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
		gfprep_fatal_e(e, "gfarm_list_add");
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
		gfprep_fatal("no memory");
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
						gfprep_fatal("no memory");
					all_nodes_p = new_p;
				}
				all_nodes_p[n_all_nodes] = &nodes->nodes[i];
				n_all_nodes++;
			}
		}
	}
	if (n_all_nodes == 0) {
		*conns_p = NULL;
		return (GFARM_ERR_NO_ERROR);
	}
	gfprep_nodes_sort(all_nodes_p, n_all_nodes); /* big to small */

	GFARM_MALLOC_ARRAY(conns, n_conns);
	if (conns == NULL)
		gfprep_fatal("no memory");
	for (i = 0; i < n_conns; i++) {
		conns[i].cost = 0;
		e = gfarm_list_init(&conns[i].nodes_base);
		gfprep_fatal_e(e, "gfarm_list_init");
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
			gfprep_fatal("no memory");
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
		gfprep_fatal("no memory");
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
			gfprep_fatal("no memory");
		gfprep_file_sort(nents_max, ents_max); /* big to small*/
		for (j = 0; j <= n_conns - 2; j++) { /* cost: small to big */
			if (found)
				break; /* goto D */
			conn = &conns[j];
			nodesp = gfarm_array_alloc_from_list(
				&conn->nodes_base); /* max conn */
			if (nodesp == NULL)
				gfprep_fatal("no memory");
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
			gfprep_fatal_e(e, "gfarm_list_add");
			e = gfarm_list_init(&newlist);
			gfprep_fatal_e(e, "gfarm_list_init");
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

	if (hi->disk_avail >= src_size &&
	    hi->disk_avail >= gfarm_get_minimum_free_disk_space())
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
			gfprep_warn_e(e, "check_disk_avail(%s)", hi->hostname);
			continue;
		}
		gfprep_get_n_using(hi, &n_writing);
		gfprep_debug("%s: disk_avail=%"GFARM_PRId64
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
	void (*func_timer_end)(struct pfunc_cb_data *, int),
	void (*func_start)(struct pfunc_cb_data *),
	void (*func_update)(struct pfunc_cb_data *, int))
{
	struct pfunc_cb_data *cbd;

	GFARM_MALLOC(cbd);
	if (cbd == NULL)
		gfprep_fatal("no memory");

	cbd->src_url = strdup(src_url);
	if (cbd->src_url == NULL)
		gfprep_fatal("no memory");

	if (dst_url) {
		cbd->dst_url = strdup(dst_url);
		if (cbd->dst_url == NULL)
			gfprep_fatal("no memory");
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
	gfprep_fatal_e(e, "gfarm_pfunc_copy");
}

static void
gfprep_do_replicate(
	gfarm_pfunc_t *pfunc_handle, char *done_p,
	int opt_migrate, gfarm_off_t size,
	const char *src_url, struct gfprep_host_info *src_hi,
	const char *dst_url, struct gfprep_host_info *dst_hi)
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
	gfprep_fatal_e(e, "gfarm_pfunc_replicate_from_to");
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
	gfprep_fatal_e(e, "gfarm_pfunc_remove_replica");
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
		gfprep_fatal("no memory");
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
			gfprep_fatal("no memory");
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
			gfprep_fatal_e(e, "gfprep_connection_job_next");
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
				gfprep_fatal_e(
					e,
					"gfprep_sort_and_check_disk_avail");
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
				    src_url, src_hi, tmp_url, dst_hi);
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
				gfprep_debug("cb_cond timeout");
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
gfprep_check_dirurl_filename(int is_gfarm, const char *url,
	char **dir_urlp, char **file_namep, int *dir_modep, int *file_modep,
	struct gfarm_timespec *dir_mtimep, struct gfarm_timespec *file_mtimep)
{
	gfarm_error_t e;
	int type, retv, mode;
	char *tmp, *tmp2, *dir_url;
	struct gfarm_timespec mtime;

	type = gfprep_get_type(is_gfarm, url, &mode, &mtime, &e);
	if (e != GFARM_ERR_NO_ERROR) {
		gfprep_error_e(e, "%s", url);
		exit(EXIT_FAILURE);
	}
	if (type == GFS_DT_REG) {
		if (file_modep)
			*file_modep = mode;
		if (file_mtimep)
			*file_mtimep = mtime;
		if (file_namep) {
			tmp = strdup(url);
			if (tmp == NULL)
				gfprep_fatal("no memory");
			tmp2 = basename(tmp);
			*file_namep = strdup(tmp2);
			free(tmp);
			if (*file_namep == NULL)
				gfprep_fatal("no memory");
		}
		if (dir_urlp || dir_modep) {
			if (is_gfarm)
				dir_url = gfarm_url_dir(url);
			else {
				tmp = strdup(url);
				if (tmp == NULL)
					gfprep_fatal("no memory");
				tmp2 = dirname(tmp +
					       GFPREP_FILE_URL_PREFIX_LENGTH);
				retv = gfprep_asprintf(&dir_url,
						       "file:%s", tmp2);
				if (retv == -1)
					dir_url = NULL;
				free(tmp);
			}
			if (dir_url == NULL)
				gfprep_fatal("no memory");
			if (dir_modep || dir_mtimep) {
				(void)gfprep_get_type(is_gfarm, dir_url, &mode,
				    &mtime, &e);
				if (e != GFARM_ERR_NO_ERROR) {
					gfprep_error_e(e, "%s", url);
					exit(EXIT_FAILURE);
				}
				if (dir_modep)
					*dir_modep = mode;
				if (dir_mtimep)
					*dir_mtimep = mtime;
			}
			if (dir_urlp)
				*dir_urlp = dir_url;
			else
				free(dir_url);
		}
	} else if (type == GFS_DT_DIR) {
		if (dir_urlp) {
			*dir_urlp = strdup(url);
			if (*dir_urlp == NULL)
				gfprep_fatal("no memory");
		}
		if (file_namep)
			*file_namep = NULL;
		if (dir_modep)
			*dir_modep = mode;
		if (file_modep)
			*file_modep = 0;
		if (dir_mtimep)
			*dir_mtimep = mtime;
		if (file_mtimep) {
			file_mtimep->tv_sec = 0;
			file_mtimep->tv_nsec = 0;
		}
	} else {
		gfprep_error("unsupported entry: %s", url);
		exit(EXIT_FAILURE);
	}
}

/* gfarm:// -> gfarm:/// */
static void
gfprep_convert_gfarm_rootdir(char **urlp)
{
	if (urlp == NULL || *urlp == NULL)
		return;
	if (strcmp(*urlp, "gfarm://") == 0) {
		free(*urlp);
		*urlp = strdup("gfarm:///");
		if (*urlp == NULL)
			gfprep_fatal("no memory");
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
		gfprep_debug("wait[n_pending=%"GFARM_PRId64"]: %s",
			     entry->n_pending, src_url);
		gfarm_gettime(&timeout);
		timeout.tv_sec += 1;
		if (!gfarm_cond_timedwait(
		    &cb_cond, &cb_mutex, &timeout, diag, CB_COND_DIAG)) {
			gfprep_debug("timeout: waiting busy host");
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
	const char *src_dir, const char *dst_dir, const char *src_base_name,
	int n_para, int n_fifo)
{
	gfarm_error_t e;
	gfarm_dirtree_t *dirtree_handle;
	gfarm_dirtree_entry_t *entry;
	gfarm_uint64_t n_entry;
	char *src_url = NULL, *dst_url = NULL;
	int i, src_url_size = 0, dst_url_size = 0;

	e = gfarm_dirtree_open(
		&dirtree_handle, src_dir, dst_dir,
		n_para, n_fifo,
		src_base_name ? 0 : 1);
	gfprep_fatal_e(e, "gfarm_dirtree_open");
	n_entry = 0;
	while ((e = gfarm_dirtree_checknext(dirtree_handle, &entry))
	       == GFARM_ERR_NO_ERROR) {
		if (src_base_name &&
		    strcmp(src_base_name, entry->subpath) != 0) {
			gfarm_dirtree_delete(dirtree_handle);
			continue;
		}
		n_entry++;
		gfprep_url_realloc(
			&src_url, &src_url_size, src_dir, entry->subpath);
		printf("%lld: src=%s\n", (long long)n_entry, src_url);
		if (dst_dir) {
			gfprep_url_realloc(
				&dst_url, &dst_url_size,
				dst_dir, entry->subpath);
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

/* XXX gfutil? */
static gfarm_error_t
gfarm_local_realpath(const char *path, char **realpathp)
{
	gfarm_error_t e;
	char *rv, *parent, *p;
	const char *base;
	int overflow = 0;
	size_t len;
	char buf[PATH_MAX];

	rv = realpath(path, buf);
	if (rv) {
		*realpathp = strdup(rv);
		if (*realpathp == NULL)
			gfprep_fatal("no memory");
		return (GFARM_ERR_NO_ERROR);
	}
	if (errno != ENOENT) {
		e = gfarm_errno_to_error(errno);
		/* XXX gflog_debug save_errno */
		return (e);
	}
	if (path[0] == '\0' || /* "" */
	    (path[0] == '/' && path[1] == '\0') || /* "/" */
	    (path[0] == '.' && (path[1] == '\0' || /* "." */
			(path[1] == '.' && path[2] == '\0')))) /* ".." */
		return (GFARM_ERR_INVALID_ARGUMENT); /* unexpected */

	/* to create new entry */
	parent = gfarm_path_dir(path); /* malloc'ed */
	if (parent == NULL)
		return (GFARM_ERR_NO_MEMORY);
	p = realpath(parent, buf);
	if (p == NULL) {
		e = gfarm_errno_to_error(errno);
		/* XXX gflog_debug save_errno */
		free(parent);
		return (e);
	}
	free(parent);
	base = gfarm_path_dir_skip(path);
	len = gfarm_size_add(&overflow, strlen(p), strlen(base));
	if (overflow)
		return (GFARM_ERR_RESULT_OUT_OF_RANGE);
	len = gfarm_size_add(&overflow, len, 2); /* '/' and '\0' */
	if (overflow)
		return (GFARM_ERR_RESULT_OUT_OF_RANGE);
	GFARM_MALLOC_ARRAY(rv, len);
	if (rv == NULL)
		return (GFARM_ERR_NO_MEMORY);
	strcpy(rv, p);
	if (strcmp(p, "/") != 0)
		strcat(rv, "/");
	strcat(rv, base);
	*realpathp = rv;
	return (GFARM_ERR_NO_ERROR);
}

static char *
gfprep_path_to_real_url(const char *path)
{
	gfarm_error_t e;
	char *real, *p;
	size_t len;
	int overflow = 0;

	if (gfprep_url_is_gfarm(path) || gfprep_url_is_local(path)) {
		real = strdup(path);
		if (real == NULL)
			gfprep_fatal("no memory");
		return (real);
	}

	e = gfarm_realpath_by_gfarm2fs(path, &real);
	if (e == GFARM_ERR_NO_ERROR)
		return (real); /* gfarm://host:port/... */

	e = gfarm_local_realpath(path, &real);
	if (e != GFARM_ERR_NO_ERROR) {
		gfprep_error("gfarm_local_realpath: %s: %s",
		    path, gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}
	/* file://...<real>...\0 */
	len = gfarm_size_add(&overflow, strlen(real),
	    GFPREP_FILE_URL_PREFIX_LENGTH + 2 + 1);
	if (overflow)
		gfprep_fatal("gfprep_path_to_real_url: overflow");
	GFARM_MALLOC_ARRAY(p, len);
	if (p == NULL)
		gfprep_fatal("no memory");
	strcpy(p, GFPREP_FILE_URL_PREFIX);
	strcat(p, "//");
	strcat(p, real);
	free(real);
	return (p);
}

static gfarm_error_t
gfprep_unlink_to_overwrite(gfarm_dirtree_entry_t *entry, int dst_is_gfarm,
	const char *dst_url)
{
	gfarm_error_t e;

	if (entry->dst_d_type == GFS_DT_DIR)
		e = gfprep_rmdir(dst_is_gfarm, dst_url);
	else
		e = gfprep_unlink(dst_is_gfarm, dst_url);
	if (e == GFARM_ERR_NO_ERROR)
		entry->dst_exist = 0;
	return (e);
}

int
main(int argc, char *argv[])
{
	int orig_argc = argc;
	char **orig_argv = argv;
	int ch, i, j, retv, is_gfpcopy;
	gfarm_error_t e;
	char *src_orig_path, *dst_orig_path;
	char *src_real_url, *dst_real_url, *src_base_name;
	char *src_dir, *dst_dir;
	int src_is_gfarm, dst_is_gfarm;
	int src_dir_mode;
	int should_restore; /* for mtime and mode of directories */
	struct gfarm_timespec src_dir_mtime;
	gfarm_pfunc_t *pfunc_handle;
	gfarm_dirtree_t *dirtree_handle;
	gfarm_dirtree_entry_t *entry;
	gfarm_uint64_t n_entry;
	int n_target; /* use gfarm_list */
	struct gfarm_hash_table *hash_srcname = NULL, *hash_dstname = NULL;
	struct gfarm_hash_table *hash_all_src, *hash_all_dst;
	struct gfarm_hash_table *hash_src, *hash_dst, *target_hash_src;
	int n_array_dst = 0, n_src_available;
	struct gfprep_host_info **array_dst = NULL;
	char *src_url = NULL, *dst_url = NULL;
	int src_url_size = 0, dst_url_size = 0;
	enum way { WAY_NOPLAN, WAY_GREEDY, WAY_BAD };
	enum way way = WAY_NOPLAN;
	gfarm_list list_to_schedule;
	struct timeval time_start, time_end;
	static gfarm_uint64_t total_requested_filesize = 0;
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

	if (argc == 0)
		gfprep_fatal("no argument");
	program_name = basename(argv[0]);

	while ((ch = getopt(
			argc, argv,
			"N:h:j:w:W:s:S:D:H:R:M:b:J:F:C:c:LxmnpPqvdfUZl?"))
	       != -1) {
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
			opt.verbose = 1; /* print more informations */
			break;
		case 'd':
			opt.debug = 1;
			break;
		case 'R': /* hidden option: function not implemented */
			opt_ratio = strtol(optarg, NULL, 0);;
			break;
		case 'J': /* hidden option */
			opt_dirtree_n_para = strtol(optarg, NULL, 0);;
			break;
		case 'F':
			opt_dirtree_n_fifo = strtol(optarg, NULL, 0);;
			break;
		case 'U':
			opt.check_disk_avail = 0;
			break;
		case 'l': /* hidden option: for debug */
			opt_list_only = 1;
			break;
		case 'C': /* hidden option: for -w noplan */
			opt.openfile_cost = strtol(optarg, NULL, 0);;
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
		case 'x': /* gfprep */
			opt_remove = 1;
			break;
		case 'm': /* gfprep */
			opt_migrate = 1;
			opt_limited_src = 1;
			break;
		case 'M':
			opt_max_copy_size = strtoll(optarg, NULL, 0);
			break;
		case 'f': /* gfpcopy */
			opt_force_copy = 1;
			break;
		case 'b': /* gfpcopy */
			opt_copy_bufsize = strtol(optarg, NULL, 0);;
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

	e = gfarm_initialize(&orig_argc, &orig_argv);
	gfprep_fatal_e(e, "gfarm_initialize");

	if (opt.debug) {
		opt.quiet = 0;
		opt.verbose = 1;
	} else if (opt.verbose)
		opt.quiet = 0;
	if (strcmp(program_name, name_gfpcopy) == 0) { /* gfpcopy */
		if (argc != 2)
			gfprep_usage_common(1);
		src_orig_path = argv[0];
		dst_orig_path = argv[1];
		src_real_url = gfprep_path_to_real_url(src_orig_path);
		dst_real_url = gfprep_path_to_real_url(dst_orig_path);
		src_is_gfarm = gfprep_url_is_gfarm(src_real_url);
		dst_is_gfarm = gfprep_url_is_gfarm(dst_real_url);
		if (!src_is_gfarm && !gfprep_url_is_local(src_real_url))
			gfprep_usage_common(1);
		if (!dst_is_gfarm && !gfprep_url_is_local(dst_real_url))
			gfprep_usage_common(1);
		is_gfpcopy = 1;
	} else { /* gfprep */
		if (argc != 1)
			gfprep_usage_common(1);
		src_orig_path = argv[0];
		dst_orig_path = src_orig_path;
		src_real_url = gfprep_path_to_real_url(src_orig_path);
		dst_real_url = strdup(src_real_url);
		if (dst_real_url == NULL)
			gfprep_fatal("no memory");
		src_is_gfarm = gfprep_url_is_gfarm(src_real_url);
		if (!src_is_gfarm)
			gfprep_usage_common(1);
		dst_is_gfarm = 1;
		is_gfpcopy = 0;
	}
	gfprep_debug("set options...done");

	/* validate options */
	if (opt_way) {
		if (strcmp(opt_way, "noplan") == 0)
			way = WAY_NOPLAN;
		else if (strcmp(opt_way, "greedy") == 0)
			way = WAY_GREEDY;
		else if (strcmp(opt_way, "bad") == 0)
			way = WAY_BAD;
		else {
			gfprep_error("unknown scheduling way: %s", opt_way);
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
		if (!src_is_gfarm && (opt_src_domain || opt_src_hostfile)) {
			gfprep_error(
			    "%s needs neither -S nor -h", src_real_url);
			exit(EXIT_FAILURE);
		}
		if (!dst_is_gfarm && (opt_dst_domain || opt_dst_hostfile)) {
			gfprep_error(
			    "%s needs neither -D nor -H", dst_real_url);
			exit(EXIT_FAILURE);
		}
	} else { /* gfprep */
		if (opt_force_copy) /* -f */
			gfprep_usage_common(1);
		if (opt_migrate) {
			if (opt_n_desire > 1) { /* -m and -N */
				gfprep_error("gfprep needs either -N or -m");
				exit(EXIT_FAILURE);
			}
			if (opt_remove) { /* -x */
				gfprep_error("gfprep -m does not need -x");
				exit(EXIT_FAILURE);
			}
			if (opt_src_domain == NULL &&
			    opt_src_hostfile == NULL &&
			    opt_dst_domain == NULL &&
			    opt_dst_hostfile == NULL) {
				gfprep_error(
				    "gfprep -m needs -S or -h or -D or -H");
				exit(EXIT_FAILURE);
			}
			if (way != WAY_NOPLAN) {
				gfprep_error("gfprep -m needs -w noplan");
				exit(EXIT_FAILURE);
			}
		} else { /* normal */
			if (opt_n_desire <= 0) /* -N */
				gfprep_usage_common(1);
			if (way != WAY_NOPLAN) {
				if (opt_n_desire > 1) {
					gfprep_error(
					    "gfprep -N needs -w noplan");
					exit(EXIT_FAILURE);
				}
				if (opt_remove) {
					gfprep_error(
					    "gfprep -x needs -w noplan");
					exit(EXIT_FAILURE);
				}
			}
		}
	}

	if (opt_dst_domain == NULL) {
		opt_dst_domain = gfarm_schedule_write_target_domain();
		gfprep_verbose("use write_target_domain: %s", opt_dst_domain);
	} else if (strcmp(opt_dst_domain, "") == 0)
		opt_dst_domain = NULL; /* select from all nodes */
	if (opt_dst_domain != NULL) {
		/*
		 * NOTE: gfarm_schedule_write_target_domain() may be
		 * free()ed after gfarm_terminate().
		 */
		opt_dst_domain = strdup(opt_dst_domain);
		if (opt_dst_domain == NULL)
			gfprep_fatal("no memory");
	}

	if (opt_n_para <= 0)
		opt_n_para = gfarm_client_parallel_copy;
	if (opt_n_para <= 0) {
		gfprep_error("client_parallel_copy must be "
			     "a positive interger");
		exit(EXIT_FAILURE);
	}
	gfprep_debug("number of parallel: %d", opt_n_para);
	if (opt_dirtree_n_para <= 0)
		opt_dirtree_n_para = opt_n_para;
	gfprep_debug("number of child-processes: %d",
		     opt_n_para + opt_dirtree_n_para);
	if (opt_simulate_KBs == 0)
		gfprep_usage_common(1);
	if (opt_copy_bufsize <= 0)
		gfprep_usage_common(1);
	if (opt_dirtree_n_fifo <= 0)
		gfprep_usage_common(1);

	gfprep_check_dirurl_filename(
	    src_is_gfarm, src_real_url, &src_dir, &src_base_name,
	    &src_dir_mode, NULL, &src_dir_mtime, NULL);
	gfprep_convert_gfarm_rootdir(&src_dir);

	if (is_gfpcopy)
		dst_dir = strdup(dst_real_url);
	else
		dst_dir = strdup(src_dir);
	if (dst_dir == NULL)
		gfprep_fatal("no memory");

	if (opt_list_only) {
		e = gfarm_terminate();
		gfprep_fatal_e(e, "gfarm_terminate");
		gfprep_print_list(
			src_dir, is_gfpcopy ? dst_dir : NULL, src_base_name,
			opt_dirtree_n_para, opt_dirtree_n_fifo);
		exit(0);
	}

	gfprep_dirstat_init();

	if (is_gfpcopy) { /* gfpcopy */
		int create_dst_dir = 0, checked;

		/* [1] gfpcopy p1/d1 p2/d2(exist)     : mkdir p2/d2/d1 */
		/* [2] gfpcopy p1/d1 p2/d2(not exist) : mkdir p2/d2 */
		/* [3] gfpcopy p1/f1 p2/(exist)       : copy p2/f1 */
		/* [4] gfpcopy p1/f1 p2/(not exist)   : ENOENT */
		/* [5] gfpcopy p1/f1 p2/f1(exist)     : ENOTDIR */
		if (gfprep_is_dir(dst_is_gfarm, dst_dir, NULL, &e)) {
			/* exist dst_dir */
			assert(e == GFARM_ERR_NO_ERROR);
			if (src_base_name) { /* [3] */
				if (!opt_force_copy) {
					char *tmp_dst_file;
					retv = gfprep_asprintf(
						&tmp_dst_file, "%s%s%s",
						dst_dir,
						(dst_dir[strlen(dst_dir) - 1]
						 == '/' ? "" : "/"),
						(src_base_name[0] == '/' ?
						 (src_base_name + 1) :
						 src_base_name));
					if (retv == -1)
						gfprep_fatal("no memory");
					if (gfprep_is_existed(
						    dst_is_gfarm, tmp_dst_file,
						    NULL, &e)) {
						gfprep_error(
							"File exists: %s",
							tmp_dst_file);
						free(tmp_dst_file);
						exit(EXIT_FAILURE);
					}
					if (e != GFARM_ERR_NO_ERROR)
						exit(EXIT_FAILURE);
					free(tmp_dst_file);
				}
				checked = 1;
			} else { /* [1] */
				char *tmp_dst_dir;
				char *tmp_src_dir = strdup(src_dir);
				char *tmp_src_base;
				if (tmp_src_dir == NULL)
					gfprep_fatal("no memory");
				tmp_src_base = basename(tmp_src_dir);
				if (strcmp(tmp_src_base, GFARM_URL_PREFIX)
				    == 0) {
					/* src_dir is Gfarm root directory */
					free(tmp_src_dir);
					tmp_src_dir = strdup("");
					if (tmp_src_dir == NULL)
						gfprep_fatal("no memory");
					tmp_src_base = tmp_src_dir;
				}
				retv = gfprep_asprintf(
					&tmp_dst_dir, "%s%s%s", dst_dir,
					(dst_dir[strlen(dst_dir) - 1] == '/'
					 ? "" : "/"),
					(tmp_src_base[0] == '/' ?
					 (tmp_src_base + 1) : tmp_src_base));
				if (retv == -1)
					gfprep_fatal("no memory");
				free(tmp_src_dir);
				free(dst_dir);
				dst_dir = tmp_dst_dir;
				checked = 0;
			}
		} else if (src_base_name == NULL &&
			   e == GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY) /* [2] */
			checked = 1; /* not exist */
		else { /* [4] [5] or other error */
			gfprep_error_e(e, "dst_dir(%s)", dst_dir);
			exit(EXIT_FAILURE);
		}
		gfprep_convert_gfarm_rootdir(&dst_dir);

		should_restore = (opt_simulate_KBs <= 0 && !opt_list_only);

		if (checked == 0 &&
		    gfprep_is_dir(dst_is_gfarm, dst_dir, NULL, &e)) {
			/* OK */
			if (should_restore)
				gfprep_set_tmp_mode_and_mtime(dst_is_gfarm,
				    dst_dir, src_dir_mode, &src_dir_mtime);
		} else if (e == GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY) {
			e = gfprep_mkdir(dst_is_gfarm, dst_dir, should_restore,
			    src_dir_mode, &src_dir_mtime);
			if (e != GFARM_ERR_NO_ERROR)
				exit(EXIT_FAILURE);
			create_dst_dir = 1;
		} else if (e != GFARM_ERR_NO_ERROR) {
			gfprep_error_e(e, "dst_dir(%s)", dst_dir);
			exit(EXIT_FAILURE);
		}
		if (src_base_name == NULL &&
		    gfprep_is_same_name(src_dir, src_is_gfarm,
					dst_dir, dst_is_gfarm)) {
			gfprep_error(
				"cannot copy: "
				"src_dir(%s), into itself, dst_dir(%s)",
				src_dir, dst_dir);
			if (create_dst_dir)
				(void) gfprep_rmdir(dst_is_gfarm, dst_dir);
			exit(EXIT_FAILURE);
		}
		if (create_dst_dir && (opt_simulate_KBs > 0 || opt_list_only))
			(void) gfprep_rmdir(dst_is_gfarm, dst_dir);
	}
	e = gfarm_terminate();
	gfprep_fatal_e(e, "gfarm_terminate");
	gfprep_debug("validate options...done");

	/* not gfarm initialized ------------------------- */
	if (opt.performance)
		gettimeofday(&time_start, NULL);

	/* create child-processes before gfarm_initialize() */
	e = gfarm_pfunc_start(&pfunc_handle, opt_n_para, 1, opt_simulate_KBs,
			      opt_copy_bufsize, pfunc_cb_start, pfunc_cb_end);
	gfprep_fatal_e(e, "gfarm_pfunc_start");
	gfprep_debug("pfunc_start...done");
	pfunc_cb_func_init();

	/* create child-processes before gfarm_initialize() */
	e = gfarm_dirtree_open(&dirtree_handle, src_dir,
			       is_gfpcopy ? dst_dir : NULL,
			       opt_dirtree_n_para, opt_dirtree_n_fifo,
			       src_base_name ? 0 : 1);
	gfprep_fatal_e(e, "gfarm_dirtree");
	gfprep_debug("dirtree_open...done");

	/* access to gfarm ------------------------------------------ */
	e = gfarm_initialize(&orig_argc, &orig_argv);
	gfprep_fatal_e(e, "gfarm_initialize");
	if (opt_src_hostfile) {
		e = gfprep_create_hostnamehash_from_file(
			src_dir, opt_src_hostfile,
			HOSTHASH_SIZE, &hash_srcname);
		gfprep_fatal_e(e,
			       "gfprep_create_hostnamehash_from_file for src");
	}
	if (opt_dst_hostfile) {
		e = gfprep_create_hostnamehash_from_file(
			dst_dir, opt_dst_hostfile,
			HOSTHASH_SIZE, &hash_dstname);
		gfprep_fatal_e(e,
			       "gfprep_create_hostnamehash_from_file for dst");
	}

	if (src_is_gfarm) {
		int n_src_all = 0;
		struct gfarm_hash_table *exclude_hash_dstname;
		const char *exclude_dst_domain;

		e = gfprep_create_hostinfohash_all(
		    src_dir, &n_src_all, &hash_all_src);
		gfprep_fatal_e(e, "gfprep_create_hostinfohash_all");
		if (n_src_all == 0) {
			gfprep_error("no available node for source");
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
		e = gfprep_filter_hostinfohash(src_dir, hash_all_src,
					       &hash_src, hash_srcname,
					       opt_src_domain,
					       exclude_hash_dstname,
					       exclude_dst_domain);
		gfprep_fatal_e(e, "gfprep_filter_hostinfohash for source");
		/* count n_src_available only */
		e = gfprep_hostinfohash_to_array(src_dir, &n_src_available,
						 NULL, hash_src);
		gfprep_fatal_e(e, "gfprep_hostinfohash_to_array for soruce");
		if (n_src_available == 0) {
			gfprep_error(
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
	}

	if (dst_is_gfarm) {
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
		if (gfprep_is_same_gfmd(src_dir, src_is_gfarm,
					dst_dir, dst_is_gfarm)) {
			hash_all_dst = NULL;
			this_hash_all_dst = hash_all_src;
		} else { /* different gfmd */
			int n_all_dst = 0;

			e = gfprep_create_hostinfohash_all(
			    dst_dir, &n_all_dst, &hash_all_dst);
			gfprep_fatal_e(e, "gfprep_create_hostinfohash_all");
			if (n_all_dst == 0) {
				gfprep_error(
				    "no available node for destination");
				exit(EXIT_FAILURE);
			}
			this_hash_all_dst = hash_all_dst;
		}
		e = gfprep_filter_hostinfohash(dst_dir, this_hash_all_dst,
					       &hash_dst, hash_dstname,
					       opt_dst_domain,
					       exclude_hash_srcname,
					       exclude_src_domain);
		gfprep_fatal_e(
		    e, "gfprep_filter_hostinfohash for destination");
		e = gfprep_hostinfohash_to_array(dst_dir, &n_array_dst,
						 &array_dst, hash_dst);
		gfprep_fatal_e(
		    e, "gfprep_hostinfohash_to_array for destination");
		if (n_array_dst == 0) {
			gfprep_error(
			    "no available node for destination "
			    "(wrong -S/-h/-D/-H or write_target_domain ?)");
			exit(EXIT_FAILURE);
		}
	} else {
		hash_all_dst = NULL;
		hash_dst = NULL;
		array_dst = NULL;
	}
	gfprep_debug("create hash...done");

	if (!src_is_gfarm || n_src_available == 1)
		way = WAY_NOPLAN;
	if (way != WAY_NOPLAN) {
		e = gfarm_list_init(&list_to_schedule);
		gfprep_fatal_e(e, "gfarm_list_init");
	}
	gfprep_verbose("way = %s", way == WAY_NOPLAN ? "noplan" : "greedy");

	n_entry = 0;
	n_target = 0;
	while ((e = gfarm_dirtree_checknext(dirtree_handle, &entry))
	       == GFARM_ERR_NO_ERROR) {
		struct gfprep_host_info *src_hi, *dst_hi;
		struct gfprep_host_info **src_select_array;
		struct gfprep_host_info **dst_select_array, **dst_exist_array;
		int n_desire;
		int n_src_select, n_dst_select, n_dst_exist;

		if (copied_size_is_over(
		    opt_max_copy_size, total_requested_filesize))
			break; /* stop */

		if (src_base_name &&
		    strcmp(src_base_name, entry->subpath) != 0)
			goto next_entry;
		if (entry->n_pending == 0) /* new entry (not pending) */
			n_entry++;
		gfprep_url_realloc(&src_url, &src_url_size, src_dir,
				   entry->subpath);
		if (opt.debug) {
			gfprep_debug(
				"src_url=%s: size=%"GFARM_PRId64
				", ncopy=%d, mtime=%"GFARM_PRId64,
				src_url, entry->src_size, entry->src_ncopy,
				entry->src_m_sec);
			for (i = 0; i < entry->src_ncopy; i++)
				gfprep_debug("src_url=%s: copy[%d]=%s",
					     src_url, i, entry->src_copy[i]);
		}
		if (!is_gfpcopy) { /* gfprep */
			if (entry->src_d_type != GFS_DT_REG) /* not a file */
				goto next_entry; /* nothing to do */
		} else { /* gfpcopy */
			gfprep_url_realloc(&dst_url, &dst_url_size,
					   dst_dir, entry->subpath);
			gfprep_debug("dst[%s]=%s: ncopy=%d, mtime=%"
				     GFARM_PRId64,
				     entry->dst_exist ? "exist" : "noent",
				     dst_url, entry->dst_ncopy,
				     entry->dst_m_sec);
			if (entry->src_d_type == GFS_DT_REG) {
				if (entry->dst_exist) {
					if (entry->dst_d_type == GFS_DT_REG) {
						if (entry->src_size ==
						    entry->dst_size &&
						    ((!opt_force_copy &&
						      entry->src_m_sec <=
						      entry->dst_m_sec)
						     ||
						     (opt_force_copy &&
						      entry->src_m_sec ==
						      entry->dst_m_sec))) {
							gfprep_verbose(
							  "already exists: %s",
							  dst_url);
							goto next_entry;
						}
						/* overwrite, not unlink */
					} else if (opt_simulate_KBs <= 0) {
						e = gfprep_unlink_to_overwrite(
						   entry, dst_is_gfarm,
						   dst_url);
						if (e != GFARM_ERR_NO_ERROR) {
							gfprep_error(
							    "cannot overwrite:"
							    " %s", dst_url);
							goto next_entry;
						}
					}
					gfprep_debug(
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
					    ((!opt_force_copy &&
					      entry->src_m_sec <=
					      entry->dst_m_sec)
					     ||
					     (opt_force_copy &&
					      entry->src_m_sec ==
					      entry->dst_m_sec))) {
						gfprep_verbose(
						    "already exists: %s",
						    dst_url);
						goto next_entry;
					}
					if (opt_simulate_KBs <= 0) {
						e = gfprep_unlink_to_overwrite(
						   entry, dst_is_gfarm,
						   dst_url);
						if (e != GFARM_ERR_NO_ERROR) {
							gfprep_error(
							    "cannot overwrite:"
							    " %s", dst_url);
							goto next_entry;
						}
					}
				}
				if (opt_simulate_KBs > 0)
					goto next_entry;
				e = gfprep_copy_symlink(
				    src_is_gfarm, src_url,
				    dst_is_gfarm, dst_url);
				if (e == GFARM_ERR_NO_ERROR) {
					struct gfarm_timespec mtime;

					mtime.tv_sec = entry->src_m_sec;
					mtime.tv_nsec = entry->src_m_nsec;
					(void)gfprep_set_mtime(
					    dst_is_gfarm, dst_url, &mtime);
				}
				goto next_entry;
			} else if (entry->src_d_type == GFS_DT_DIR) {
				struct gfarm_timespec mtime;

				if (opt_simulate_KBs > 0)
					goto next_entry;

				mtime.tv_sec = entry->src_m_sec;
				mtime.tv_nsec = entry->src_m_nsec;
				if (entry->dst_exist) {
					if (entry->dst_d_type == GFS_DT_DIR) {
						gfprep_set_tmp_mode_and_mtime(
						    dst_is_gfarm, dst_url,
						    entry->src_mode, &mtime);
						goto next_entry;
					} else {
						e = gfprep_unlink_to_overwrite(
						   entry, dst_is_gfarm,
						   dst_url);
						if (e != GFARM_ERR_NO_ERROR) {
							gfprep_error(
							    "cannot overwrite:"
							    " %s", dst_url);
							goto next_entry;
						}
					}
				}
				e = gfprep_mkdir(dst_is_gfarm, dst_url, 1,
				    entry->src_mode, &mtime);
				if (e != GFARM_ERR_NO_ERROR)
					gfprep_error(
					   "cannot make a directory: %s",
					   dst_url);
				goto next_entry;
			} else {
				gfprep_warn(
				    "cannot copy (unsupported type): %s",
				    src_url);
				goto next_entry;
			}
		}

		/* ----- a file ----- */
		/* select a file within specified src  */
		if (hash_src && (hash_srcname || opt_src_domain)) {
			int found = 0;

			for (i = 0; i < entry->src_ncopy; i++) {
				if (gfprep_in_hostinfohash(
					hash_src, entry->src_copy[i])) {
					found = 1;
					break;
				}
			}
			if (!found) {
				gfprep_verbose("not a target file: %s",
					       src_url);
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
			} else {
				assert(src_is_gfarm);
				/* gfprep: ignore 0 byte: not replicate */
				if (entry->src_ncopy == 0)
					goto next_entry; /* skip */
			}
		} else if (src_is_gfarm &&
			   entry->src_ncopy == 0) { /* entry->src_size > 0 */
			/* shortcut */
			gfprep_error("no available replica: %s", src_url);
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
						gfprep_verbose(
						"replica already exists: "
						"%s (%s)",
						src_url, entry->src_copy[i]);
						break;
					}
				}
				if (found) /* not replicate */
					goto next_entry;
			}
			n_target++;
			if (n_target <= 0) { /* overflow */
				gfprep_warn("too many target entries");
				break;
			}
			e = gfarm_dirtree_next(dirtree_handle, &entry);
			gfprep_fatal_e(e, "gfarm_dirtree_next");
			e = gfarm_list_add(&list_to_schedule, entry);
			gfprep_fatal_e(e, "gfarm_list_add");
			continue; /* next entry */
		}

		/* ----- WAY_NOPLAN ----- */
		/* select existing replicas from target_hash_src */
		if (src_is_gfarm) {
			gfarm_list src_select_list;
			assert(target_hash_src);
			e = gfarm_list_init(&src_select_list);
			gfprep_fatal_e(e, "gfarm_list_init");
			/* select existing replicas from hash_src */
			for (i = 0; i < entry->src_ncopy; i++) {
				src_hi = gfprep_from_hostinfohash(
					target_hash_src, entry->src_copy[i]);
				if (src_hi) {
					e = gfarm_list_add(&src_select_list,
							   src_hi);
					gfprep_fatal_e(e, "gfarm_list_add");
				}
			}
			n_src_select = gfarm_list_length(&src_select_list);
			if (n_src_select == 0) {
				gfarm_list_free(&src_select_list);
				gfprep_error(
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
				gfprep_fatal("no memory");
			/* prefer unbusy host */
			gfprep_host_info_array_sort_for_src(
				n_src_select, src_select_array);
			/* at least 1 unbusy host */
			if (gfprep_check_busy_and_wait(
				    src_url, entry, 1,
				    n_src_select, src_select_array)) {
				free(src_select_array);
				gfprep_debug("pending: src hosts are busy: %s",
					     src_url);
				entry->n_pending++;
				gfarm_dirtree_pending(dirtree_handle);
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
		if (dst_is_gfarm) {
			gfarm_list dst_select_list, dst_exist_list;
			int found;

			assert(array_dst);
			e = gfarm_list_init(&dst_select_list);
			gfprep_fatal_e(e, "gfarm_list_init");
			e = gfarm_list_init(&dst_exist_list);
			gfprep_fatal_e(e, "gfarm_list_init");
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
					gfprep_fatal_e(e, "gfarm_list_add");
					n_dst_exist++;
				} else {
					e = gfprep_check_disk_avail(
						array_dst[i], entry->src_size);
					if (e == GFARM_ERR_NO_SPACE)
						continue;
					gfprep_error_e(e, "check_disk_avail");
					e = gfarm_list_add(&dst_select_list,
							   array_dst[i]);
					gfprep_fatal_e(e, "gfarm_list_add");
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
					gfprep_fatal("no memory");
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
					gfprep_fatal("no memory");
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
					gfprep_debug(
					    "pending: dst hosts are busy: %s",
					    src_url);
					entry->n_pending++;
					gfarm_dirtree_pending(dirtree_handle);
					continue; /* pending */
				}
			}
		}
		if (is_gfpcopy) { /* gfpcopy */
			assert(src_is_gfarm ? n_src_select > 0 : 1);
			if (dst_is_gfarm && n_dst_select <= 0) {
				gfprep_error(
				    "insufficient number of destination nodes"
				    " to copy (n_dst=%d): %s",
				    n_dst_select, src_url);
				gfprep_count_ng_file(entry->src_size);
				goto next_entry_with_free;
			}
			n_desire = 1;
		} else if (opt_migrate) { /* gfprep -m */
			assert(n_src_select > 0);
			if (n_dst_select < n_src_select) {
				gfprep_error(
				    "insufficient number of destination nodes"
				    " to migrate (n_src=%d, n_dst=%d): %s",
				    n_src_select, n_dst_select, src_url);
				n_desire = n_dst_select;
			} else
				n_desire = n_src_select;
			if (n_desire <= 0) {
				gfprep_count_ng_file(entry->src_size);
				goto next_entry_with_free;
			}
		} else { /* gfprep -N */
			assert(n_src_select > 0);
			n_desire = opt_n_desire - n_dst_exist;
			if (n_desire == 0) {
				gfprep_verbose("enough replicas: %s", src_url);
				goto next_entry_with_free;
			} else if (n_desire < 0) {
				gfprep_verbose("too many replicas(%d): %s",
					       -n_desire, src_url);
				if (!opt_remove)
					goto next_entry_with_free;
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
				goto next_entry_with_free;
			} else if (n_dst_select < n_desire) {/* n_desire > 0 */
				gfprep_error(
				    "insufficient number of destination nodes"
				    " to replicate (n_desire=%d, n_dst=%d):"
				    " %s", n_desire, n_dst_select, src_url);
				n_desire = n_dst_select;
				if (n_desire <= 0) {
					gfprep_count_ng_file(entry->src_size);
					goto next_entry_with_free;
				}
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
				    src_url, src_hi, dst_url, dst_hi);

			total_requested_filesize += entry->src_size;
		}
next_entry_with_free:
		free(src_select_array);
		free(dst_select_array);
		free(dst_exist_array);
next_entry:
		(void) gfarm_dirtree_delete(dirtree_handle);
	}
	/* GFARM_ERR_NO_SUCH_OBJECT: end */
	if (e != GFARM_ERR_NO_ERROR && e != GFARM_ERR_NO_SUCH_OBJECT) {
		gfarm_pfunc_interrupt(pfunc_handle);
		gfprep_fatal_e(e, "gfarm_dirtree_checknext");
	}
	e = gfarm_dirtree_close(dirtree_handle);
	gfprep_warn_e(e, "gfarm_dirtree_close");

	/* ----- WAY_GREEDY or WAY_BAD ----- */
	if (way != WAY_NOPLAN) {
		struct gfarm_hash_table *hash_host_to_nodes;
		gfarm_dirtree_entry_t **ents;
		int n_ents, n_connections;
		struct gfprep_connection *connections;
		struct timeval sched_start, sched_end;

		assert(src_is_gfarm);
		assert(hash_src);
		assert(target_hash_src);

		gettimeofday(&sched_start, NULL);
		ents = gfarm_array_alloc_from_list(&list_to_schedule);
		if (ents == NULL)
			gfprep_fatal("no memory");
		n_ents = gfarm_list_length(&list_to_schedule);
		gfprep_verbose("target file num = %d", n_ents);

		/* [HASH] Nodes: hostname to filelist */
		/* assign Files to Nodes as equally as possible */
		hash_host_to_nodes = gfarm_hash_table_alloc(
			HOSTHASH_SIZE, gfarm_hash_casefold_strptr,
			gfarm_hash_key_equal_casefold_strptr);
		if (hash_host_to_nodes == NULL)
			gfprep_fatal("no memory");

		if (way == WAY_GREEDY) {
			gfprep_greedy_sort(n_ents, ents);
			e = gfprep_greedy_nodes_assign(hash_host_to_nodes,
						       target_hash_src,
						       n_ents, ents);
			gfprep_fatal_e(e, "gfprep_greedy_nodes_assign");
		} else if (way == WAY_BAD) {
			e = gfprep_bad_nodes_assign(hash_host_to_nodes,
						    target_hash_src,
						    n_ents, ents);
			gfprep_fatal_e(e, "gfprep_bad_nodes_assign");
		} else
			gfprep_fatal("unknown scheduling way: %d", way);
		gfprep_hash_host_to_nodes_print("ASSIGN", hash_host_to_nodes);

		/* [ARRAY] Connections */
		/* assign Nodes to Connections as equally as possible */
		n_connections = opt_n_para;
		e = gfprep_connections_assign(hash_host_to_nodes,
					      n_connections, &connections);
		gfprep_fatal_e(e, "gfprep_connections_assign");
		gfprep_connections_print("ASSIGN", connections, n_connections);

		/* swap file-copies(replicas) to flat Connections */
		e = gfprep_connections_flat(n_connections, connections,
					    opt_sched_threshold_size);
		gfprep_fatal_e(e, "gfprep_connections_flat");
		gfprep_hash_host_to_nodes_print("FLAT", hash_host_to_nodes);
		gfprep_connections_print("FLAT", connections, n_connections);

		/* replicate files before execution to flat Connections */
		if (opt_ratio > 1)
			gfprep_fatal_e(GFARM_ERR_FUNCTION_NOT_IMPLEMENTED,
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
					    opt_migrate, src_dir, dst_dir,
					    n_connections, connections,
					    target_hash_src,
					    n_array_dst, array_dst);
		gfprep_fatal_e(e, "gfprep_connections_exec");

		gfprep_connections_free(connections, n_connections);
		gfprep_hash_host_to_nodes_free(hash_host_to_nodes);
		gfarm_dirtree_array_free(n_ents, ents);
	}
	e = gfarm_pfunc_join(pfunc_handle);
	gfprep_error_e(e, "gfarm_pfunc_join");

	/* set mode and mtime for directories */
	gfprep_dirstat_final();

	if (opt.performance) {
		char *prefix = is_gfpcopy ? "copied" : "replicated";
		gettimeofday(&time_end, NULL);
		gfarm_timeval_sub(&time_end, &time_start);
		printf("all_entries_num: %"GFARM_PRId64"\n", n_entry);
		printf("%s_file_num: %"GFARM_PRId64"\n",
		       prefix, total_ok_filenum);
		printf("%s_file_size: %"GFARM_PRId64"\n",
		       prefix, total_ok_filesize);
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
	free(src_url);
	free(dst_url);
	free(src_base_name);
	free(src_dir);
	free(dst_dir);
	free(src_real_url);
	free(dst_real_url);
	free(opt_dst_domain);

	e = gfarm_terminate();
	gfprep_warn_e(e, "gfarm_terminate");

	return (total_ng_filenum > 0 ? 1 : 0);
}
