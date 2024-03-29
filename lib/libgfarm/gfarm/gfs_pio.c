/*
 * $Id$
 */

#include <sys/types.h> /* mode_t */
#include <sys/stat.h> /* umask() */
#include <sys/time.h>
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>	/* [FRWX]_OK */
#include <errno.h>
#include <pthread.h>

#include <openssl/evp.h>

#define GFARM_INTERNAL_USE
#include <gfarm/gfarm.h>

#include "timer.h"
#include "gfutil.h"
#define GFARM_USE_OPENSSL
#include "msgdigest.h"
#include "queue.h"
#include "thrsubr.h"

#include "config.h"
#include "context.h"
#include "liberror.h"
#include "filesystem.h"
#include "gfs_profile.h"
#include "gfm_proto.h"
#include "gfm_client.h"
#include "gfs_proto.h"	/* GFS_PROTO_FSYNC_* */
#define GFARM_USE_GFS_PIO_INTERNAL_CKSUM_INFO
#include "gfs_io.h"
#include "gfs_pio.h"
#include "gfs_pio_impl.h"
#include "gfp_xdr.h"
#include "gfs_failover.h"
#include "gfs_file_list.h"

#define staticp	(gfarm_ctxp->gfs_pio_static)

struct gfarm_gfs_pio_static {
	double create_time, open_time, close_time;
	double seek_time, truncate_time;
	double read_time, write_time;
	double sync_time, datasync_time;
	double getline_time, getc_time, putc_time;
	unsigned long long read_size, write_size;
	unsigned long long create_count, open_count;
	unsigned long long close_count;
	unsigned long long seek_count, truncate_count;
	unsigned long long read_count, write_count;
	unsigned long long sync_count, datasync_count;
	unsigned long long getline_count, getc_count, putc_count;
};

static gfarm_error_t flush_internal(GFS_File gf);

gfarm_error_t
gfarm_gfs_pio_static_init(struct gfarm_context *ctxp)
{
	struct gfarm_gfs_pio_static *s;

	GFARM_MALLOC(s);
	if (s == NULL)
		return (GFARM_ERR_NO_MEMORY);

	s->create_time =
	s->open_time =
	s->close_time =
	s->seek_time =
	s->truncate_time =
	s->read_time =
	s->write_time =
	s->sync_time =
	s->datasync_time =
	s->getline_time =
	s->getc_time =
	s->putc_time = 0;
	s->read_size =
	s->write_size =
	s->create_count =
	s->open_count =
	s->close_count =
	s->seek_count =
	s->truncate_count =
	s->read_count =
	s->write_count =
	s->sync_count =
	s->datasync_count =
	s->getline_count =
	s->getc_count =
	s->putc_count = 0;

	ctxp->gfs_pio_static = s;
	return (GFARM_ERR_NO_ERROR);
}

void
gfarm_gfs_pio_static_term(struct gfarm_context *ctxp)
{
	free(ctxp->gfs_pio_static);
}

struct gfs_file_list {
	GFARM_HCIRCLEQ_HEAD(gfs_file) files;
	pthread_mutex_t mutex;
};

static void
gfs_pio_mutex_init(pthread_mutex_t *mutex, const char *where)
{
	/* recursive lock may not be necessary but just in case */
	gfarm_mutex_recursive_init(mutex, where, "GFS_File");
}

static void
gfs_pio_mutex_destroy(pthread_mutex_t *mutex, const char *where)
{
	gfarm_mutex_destroy(mutex, where, "GFS_File");
}

static void
gfs_pio_mutex_lock(pthread_mutex_t *mutex, const char *where)
{
	gfarm_mutex_lock(mutex, where, "GFS_File");
}

static void
gfs_pio_mutex_unlock(pthread_mutex_t *mutex, const char *where)
{
	gfarm_mutex_unlock(mutex, where, "GFS_File");
}

/*
 * GFARM_ERRMSG_GFS_PIO_IS_EOF is used as mark of EOF,
 * and shouldn't be returned to caller functions.
 */

int
gfs_pio_eof(GFS_File gf)
{
	int rv;

	gfs_pio_mutex_lock(&gf->mutex, __func__);
	rv = (gf->error == GFARM_ERRMSG_GFS_PIO_IS_EOF);
	gfs_pio_mutex_unlock(&gf->mutex, __func__);
	return (rv);
}

#define GFS_PIO_ERROR(gf) \
	((gf)->error != GFARM_ERRMSG_GFS_PIO_IS_EOF ? \
	 (gf)->error : GFARM_ERR_NO_ERROR)

gfarm_error_t
gfs_pio_error_unlocked(GFS_File gf)
{
	return (GFS_PIO_ERROR(gf));
}

gfarm_error_t
gfs_pio_error(GFS_File gf)
{
	gfarm_error_t e;

	gfs_pio_mutex_lock(&gf->mutex, __func__);
	e = gfs_pio_error_unlocked(gf);
	gfs_pio_mutex_unlock(&gf->mutex, __func__);
	return (e);
}

void
gfs_pio_clearerr(GFS_File gf)
{
	gfs_pio_mutex_lock(&gf->mutex, __func__);
	gf->error = GFARM_ERR_NO_ERROR;
	gfs_pio_mutex_unlock(&gf->mutex, __func__);
}

static gfarm_error_t
gfs_pio_is_view_set(GFS_File gf)
{
	return (gf->view_context != NULL);
}

gfarm_error_t
gfs_pio_set_view_default(GFS_File gf)
{
	gfarm_error_t e, e_save = GFARM_ERR_NO_ERROR;

	if (gfs_pio_is_view_set(gf)) {
		if ((gf->mode & GFS_FILE_MODE_WRITE) != 0)
			e_save = flush_internal(gf);
		e = (*gf->ops->view_close)(gf);
		if (e_save == GFARM_ERR_NO_ERROR)
			e_save = e;
	}
	gf->ops = NULL;
	gf->view_context = NULL;
#if 0 /* not yet in gfarm v2 */
	gf->view_flags = 0;
#endif /* not yet in gfarm v2 */
	gf->error = e_save;

	if (e_save != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001292,
			"gfs_pio_isview_set() is false or "
			"view_close() failed: %s",
			gfarm_error_string(e_save));
	}

	return (e_save);
}

static gfarm_error_t
gfs_pio_check_view_default(GFS_File gf)
{
	gfarm_error_t e;

	e = GFS_PIO_ERROR(gf);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001293,
			"GFS_PIO_ERROR: %s",
			gfarm_error_string(e));
		return (e);
	}

	if (!gfs_pio_is_view_set(gf)) /* view isn't set yet */
#if 0 /* not yet in gfarm v2 */
		return (gfs_pio_set_view_global(gf, 0));
#else /* not yet in gfarm v2 */
		return (gfs_pio_internal_set_view_section(gf, NULL));
#endif /* not yet in gfarm v2 */
	return (GFARM_ERR_NO_ERROR);
}

struct gfm_connection *
gfs_pio_metadb(GFS_File gf)
{
	return (gf->gfm_server);
}

/* gfs_pio_fileno returns a network-wide file descriptor in Gfarm v2 */
int
gfs_pio_fileno(GFS_File gf)
{
	return (gf == NULL ? -1 : gf->fd);
}

/* only gflog_*() can use the return value gfs_pio_url() due to NULL cases */
char *
gfs_pio_url(GFS_File gf)
{
	return (gf == NULL ? "(GFS_File is null)" :
		gf->url == NULL ? "(GFS_File::url is null)" : gf->url);
}

#ifndef __KERNEL__	/* not support failover */
#ifndef NDEBUG
static int
check_connection_in_file_list(GFS_File gf, void *closure)
{
	struct gfm_connection *gfm_server = closure;

	/*
	 * all gfm_connection related to GFS_File in opened file list MUST be
	 * the same instance to execute failover process against all opened
	 * files at the same time in gfs_pio_failover().
	 */
	assert(gf->gfm_server == gfm_server);
	return (1);
}
#endif
#endif /* __KERNEL__ */

#ifdef GFARM_USE_OPENSSL
int
gfs_pio_md_init(const char *md_type_name, EVP_MD_CTX **md_ctxp, char *url)
{
	EVP_MD_CTX *md_ctx;
	int cause;

	md_ctx = gfarm_msgdigest_alloc_by_name(md_type_name, &cause);
	if (md_ctx != NULL) {
		*md_ctxp = md_ctx;
		return (1);
	}

	if (cause)
		gflog_debug(GFARM_MSG_1004518,
		    "%s: digest type <%s> - %s",
		    url, md_type_name, strerror(cause));
	*md_ctxp = NULL;
	return (0);
}

static int
gfs_pio_md_is_valid(GFS_File gf)
{
	int valid = 1;
	gfarm_error_t e;
	struct gfs_stat_cksum cksum;

	e = gfs_fstat_cksum(gf, &cksum);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1004200, "%s: couldn't get cksum: %s",
		    gfs_pio_url(gf), gfarm_error_string(e));
		return (0);
	}
	if ((cksum.flags & (
	    GFM_PROTO_CKSUM_GET_MAYBE_EXPIRED |
	    GFM_PROTO_CKSUM_GET_EXPIRED)) != 0 ||
	    cksum.len == 0)
		valid = 0;
	gfs_stat_cksum_free(&cksum);
	return (valid);
}

gfarm_error_t
gfs_pio_md_finish(GFS_File gf)
{
	char md_string[GFARM_MSGDIGEST_STRSIZE];
	size_t md_strlen;

	assert(gf->md.cksum_type != NULL);
	assert((gf->mode & GFS_FILE_MODE_DIGEST_FINISH) == 0);
	assert((gf->mode & GFS_FILE_MODE_DIGEST_CALC) != 0);

	md_strlen = gfarm_msgdigest_to_string_and_free(gf->md_ctx, md_string);
	gf->mode |= GFS_FILE_MODE_DIGEST_FINISH;

	if ((gf->mode & GFS_FILE_MODE_MODIFIED) != 0 ||
	    (gf->mode & GFS_FILE_MODE_DIGEST_AVAIL) == 0) {
		memcpy(gf->md.cksum, md_string, md_strlen);
		gf->md.cksum_len = md_strlen;
	} else if (memcmp(md_string, gf->md.cksum, gf->md.cksum_len) != 0 &&
	    gfs_pio_md_is_valid(gf)) {
		gflog_debug(GFARM_MSG_1003942,
		    "%s: checksum mismatch <%.*s> expected, but <%.*s>",
		    gfs_pio_url(gf),
		    (int)gf->md.cksum_len, gf->md.cksum,
		    (int)md_strlen, md_string);
		return (GFARM_ERR_CHECKSUM_MISMATCH);
	}
	return (GFARM_ERR_NO_ERROR);
}
#endif /* GFARM_USE_OPENSSL */

/* only gfs_pio_failover.c:gfs_pio_reopen() is allowed to use this function */
gfarm_error_t
gfs_pio_reopen_fd(GFS_File gf,
	struct gfm_connection **gfm_serverp, int *fdp, int *typep,
	char **real_urlp, gfarm_ino_t *inump, gfarm_uint64_t *igenp)
{
	if (gf->url != NULL) {
		return (gfm_open_fd(gf->url,
		    gf->open_flags & ~GFARM_FILE_TRUNC,
		    gfm_serverp, fdp, typep, real_urlp, inump, igenp, NULL));
	} else {
		gfarm_error_t e = gfm_fhopen_fd(gf->ino, gf->gen,
		    gf->open_flags & ~GFARM_FILE_TRUNC,
		    gfm_serverp, fdp, typep, NULL);

		if (e == GFARM_ERR_NO_ERROR) {
			*real_urlp = NULL;
			*inump = gf->ino;
			*igenp = gf->gen;
		}
		return (e);
	}
}

static gfarm_error_t
gfs_file_alloc(struct gfm_connection *gfm_server, gfarm_int32_t fd, int flags,
	char *url, gfarm_ino_t ino, gfarm_uint64_t gen,
	struct gfs_pio_internal_cksum_info *cip, GFS_File *gfp)
{
	GFS_File gf;
	char *buffer = NULL;
	struct gfs_file_list *gfl;

	GFARM_MALLOC(gf);
	if (gf == NULL) {
		gflog_debug(GFARM_MSG_1001294,
			"allocation of GFS_File failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}
	memset(gf, 0, sizeof(*gf));
	gf->gfm_server = gfm_server;
	gf->fd = fd;
	gf->mode = 0;
	switch (flags & GFARM_FILE_ACCMODE) {
	case GFARM_FILE_RDONLY:
		gf->mode |= GFS_FILE_MODE_READ;
		break;
	case GFARM_FILE_WRONLY:
		gf->mode |= GFS_FILE_MODE_WRITE;
		break;
	case GFARM_FILE_RDWR:
		gf->mode |= GFS_FILE_MODE_READ|GFS_FILE_MODE_WRITE;
		break;
	}
	if ((flags & GFARM_FILE_TRUNC) != 0)
		gf->mode |= GFS_FILE_MODE_MODIFIED;

	if ((flags & GFARM_FILE_UNBUFFERED) == 0 &&
	    gfarm_ctxp->client_file_bufsize > 0) {
		GFARM_MALLOC_ARRAY(buffer, gfarm_ctxp->client_file_bufsize);
		if (buffer == NULL) {
			free(gf);
			gflog_debug(GFARM_MSG_1003943,
				"allocation of GFS_File's buffer failed: %s",
				gfarm_error_string(GFARM_ERR_NO_MEMORY));
			return (GFARM_ERR_NO_MEMORY);
		}
	}

	gf->open_flags = flags;
	gf->error = GFARM_ERR_NO_ERROR;
	gf->io_offset = 0;

	gf->buffer = buffer;
	gf->bufsize = gfarm_ctxp->client_file_bufsize;
	gf->p = 0;
	gf->length = 0;
	gf->offset = 0;
	gf->ino = ino;
	gf->gen = gen;
	gf->url = url;

	gf->md_offset = 0;
	gf->md.filesize = -1;
	gf->md.cksum_type = NULL;
	gf->md.cksum_len = 0;
	gf->md.cksum_get_flags = 0;
	gf->md.cksum_set_flags = 0;
#ifdef GFARM_USE_OPENSSL
	if (cip == NULL) {
		/* do not calculate cksum */
	} else if (!gfs_pio_md_init(cip->cksum_type, &gf->md_ctx, url)) {
		free(cip->cksum_type);
	} else {
		gf->md = *cip;
		if (cip->cksum_len > 0)
			gf->mode |= GFS_FILE_MODE_DIGEST_AVAIL;
		if ((cip->cksum_get_flags & (
		    GFM_PROTO_CKSUM_GET_MAYBE_EXPIRED|
		    GFM_PROTO_CKSUM_GET_EXPIRED)) == 0)
			gf->mode |= GFS_FILE_MODE_DIGEST_CALC;
	}
#endif /* GFARM_USE_OPENSSL */

	gf->view_context = NULL;
	gfs_pio_set_view_default(gf);

	gfl = gfarm_filesystem_opened_file_list(
	    gfarm_filesystem_get_by_connection(gfm_server));
#ifndef __KERNEL__	/* not support failover */
#ifndef NDEBUG
	gfs_pio_file_list_foreach(gfl, check_connection_in_file_list,
	    gfm_server);
#endif
#endif /* __KERNEL__ */
	gfs_pio_mutex_init(&gf->mutex, __func__);
	gfs_pio_file_list_add(gfl, gf);

	*gfp = gf;
	return (GFARM_ERR_NO_ERROR);
}

static void
gfs_file_free(GFS_File gf)
{
	free(gf->buffer);
	free(gf->url);
	free(gf->md.cksum_type);
	/* do not touch gf->pi here */
	gfs_pio_mutex_destroy(&gf->mutex, __func__);
	free(gf);
}

static gfarm_error_t
count_incomplete(const char *url, int *np)
{
	gfarm_error_t e;
	struct gfs_replica_info *ri;
	int i, n, num_incomplete = 0;

	e = gfs_replica_info_by_name(url,
	    GFS_REPLICA_INFO_INCLUDING_INCOMPLETE_COPY, &ri);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1005073,
		    "gfs_replica_info_by_name(%s): %s",
		    url, gfarm_error_string(e));
		return (e);
	}
	n = gfs_replica_info_number(ri);
	for (i = 0; i < n; i++)
		if (gfs_replica_info_nth_is_incomplete(ri, i))
			++num_incomplete;
	gfs_replica_info_free(ri);
	*np = num_incomplete;
	return (e);
}

static gfarm_error_t
wait_for_replication(const char *url)
{
	gfarm_error_t e;
	int n;

	while ((e = count_incomplete(url, &n)) == GFARM_ERR_NO_ERROR) {
		if (n == 0)
			break;
		gflog_info(GFARM_MSG_1005074,
		    "%s: wait for %d replica creation%s, "
		    "sleep 1 sec", url, n, n > 1 ? "s" : "");
		sleep(1);
	}
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1005075,
		    "wait_for_replication(%s), count_incomplete: %s",
		    url, gfarm_error_string(e));
	}
	return (e);
}

static gfarm_error_t
replicate_to_writable_host(const char *url)
{
	gfarm_error_t e;
	int i, available_nhosts, nhosts, *ports;
	struct gfarm_host_sched_info *available_hosts;
	char **hosts;

	e = gfarm_schedule_hosts_domain_all(url, "", &available_nhosts,
	    &available_hosts);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	nhosts = available_nhosts;
	GFARM_MALLOC_ARRAY(hosts, nhosts);
	GFARM_MALLOC_ARRAY(ports, nhosts);
	if (hosts == NULL || ports == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_debug(GFARM_MSG_1005076,
		    "allocation of hosts and ports failed: %s",
		    gfarm_error_string(e));
		goto finish;
	}
	e = gfarm_schedule_hosts_acyclic_to_write(url,
	    available_nhosts, available_hosts, &nhosts, hosts, ports);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1005077,
		    "gfarm_schedule_hosts_acyclic_to_write: %s",
		    gfarm_error_string(e));
	} else {
		for (i = 0; i < nhosts; i++) {
			/* UNCONST */
			e = gfs_replicate_to((char *)url, hosts[i], ports[i]);
			if (e == GFARM_ERR_NO_ERROR ||
			    e == GFARM_ERR_OPERATION_ALREADY_IN_PROGRESS)
				break;
			else
				gflog_debug(GFARM_MSG_1005078,
				    "gfs_replicate_to: %s",
				    gfarm_error_string(e));
		}
	}
finish:
	gfarm_host_sched_info_free(available_nhosts, available_hosts);
	free(hosts);
	free(ports);
	return (e);
}

static gfarm_error_t
open_common(int create,
	const char *path, int flags, gfarm_mode_t mode,
	struct gfm_connection **gfm_serverp, int *fdp, int *typep,
	gfarm_ino_t *inump, gfarm_uint64_t *igenp, char **urlp,
	struct gfs_pio_internal_cksum_info *cksum_info)
{
	gfarm_error_t e;

retry:
	if (create)
		e = gfm_create_fd(path, flags, mode, gfm_serverp, fdp, typep,
		    inump, igenp, urlp, cksum_info);
	else
		e = gfm_open_fd(path, flags, gfm_serverp, fdp, typep,
		    urlp, inump, igenp, cksum_info);
	if (e == GFARM_ERR_NO_SPACE && gfarm_ctxp->replication_at_write_open) {
		gfarm_error_t e2 = replicate_to_writable_host(path);

		if (e2 == GFARM_ERR_NO_ERROR)
			goto retry;
		else if (e2 == GFARM_ERR_OPERATION_ALREADY_IN_PROGRESS) {
			e2 = wait_for_replication(path);
			if (e2 == GFARM_ERR_NO_ERROR)
				goto retry;
		}
		/* GFARM_ERR_NO_SPACE when e2 is error */
	}
	return (e);
}

gfarm_error_t
gfs_pio_create_igen(const char *url, int flags, gfarm_mode_t mode,
	GFS_File *gfp, gfarm_ino_t *inop, gfarm_uint64_t *genp)
{
	gfarm_error_t e;
	struct gfm_connection *gfm_server;
	int fd, type;
	gfarm_timerval_t t1, t2;
	char *real_url = NULL;
	/* for gfarm_file_trace */
	int src_port;
	gfarm_ino_t inum;
	gfarm_uint64_t gen;
	struct gfs_pio_internal_cksum_info ci, *cip =
	    gfarm_ctxp->client_digest_check ? &ci : NULL;

	GFARM_KERNEL_UNUSE2(t1, t2);
	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	e = open_common(1, url, flags, mode, &gfm_server, &fd, &type,
	    &inum, &gen, &real_url, cip);
	if (e == GFARM_ERR_NO_ERROR) {
		if (type != GFS_DT_REG) {
			e = type == GFS_DT_DIR ? GFARM_ERR_IS_A_DIRECTORY :
			    type == GFS_DT_LNK ? GFARM_ERR_IS_A_SYMBOLIC_LINK :
			    GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else
			e = gfs_file_alloc(gfm_server, fd, flags, real_url,
			    inum, gen, cip, gfp);
		if (e != GFARM_ERR_NO_ERROR) {
			free(real_url);
			/* ignore result */
			(void)gfm_close_fd(gfm_server, fd, NULL, NULL);
			gfm_client_connection_free(gfm_server);
			gflog_debug(GFARM_MSG_1001295,
				"creation of pio for URL (%s) failed: %s",
				url,
				gfarm_error_string(e));
		}
		if (inop)
			*inop = inum;
		if (genp)
			*genp = gen;
	} else {
		gflog_debug(GFARM_MSG_1001296,
			"creation of file descriptor for URL (%s): %s",
			url,
			gfarm_error_string(e));
	}

	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(staticp->create_time += gfarm_timerval_sub(&t2, &t1));
	gfs_profile(staticp->create_count++);

	if (gfarm_ctxp->file_trace && e == GFARM_ERR_NO_ERROR) {
		gfm_client_source_port(gfm_server, &src_port);
		gflog_trace(GFARM_MSG_1003267,
		    "%s/%s/%s/%d/CREATE/%s/%d/%lld/%lld///\"%s\"///",
		    gfarm_get_local_username(),
		    gfm_client_username(gfm_server),
		    gfarm_host_get_self_name(), src_port,
		    gfm_client_hostname(gfm_server),
		    gfm_client_port(gfm_server),
		    (unsigned long long)inum, (unsigned long long)gen, url);
	}

	return (e);
}
gfarm_error_t
gfs_pio_create(const char *url, int flags, gfarm_mode_t mode, GFS_File *gfp)
{
	return (gfs_pio_create_igen(url, flags, mode, gfp, NULL, NULL));
}

gfarm_error_t
gfs_pio_open(const char *url, int flags, GFS_File *gfp)
{
	gfarm_error_t e;
	struct gfm_connection *gfm_server;
	int fd, type;
	gfarm_timerval_t t1, t2;
	gfarm_ino_t ino;
	gfarm_uint64_t gen;
	char *real_url = NULL;
	struct gfs_pio_internal_cksum_info ci, *cip =
	    gfarm_ctxp->client_digest_check ? &ci : NULL;

	GFARM_KERNEL_UNUSE2(t1, t2);
	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	e = open_common(0, url, flags, 0, &gfm_server, &fd, &type,
	    &ino, &gen, &real_url, cip);
	if (e == GFARM_ERR_NO_ERROR) {
		if (type != GFS_DT_REG) {
			e = type == GFS_DT_DIR ? GFARM_ERR_IS_A_DIRECTORY :
			    type == GFS_DT_LNK ? GFARM_ERR_IS_A_SYMBOLIC_LINK :
			    GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else
			e = gfs_file_alloc(gfm_server, fd, flags, real_url,
			    ino, gen, cip, gfp);
		if (e != GFARM_ERR_NO_ERROR) {
			free(real_url);
			/* ignore result */
			(void)gfm_close_fd(gfm_server, fd, NULL, NULL);
			gfm_client_connection_free(gfm_server);
			gflog_debug(GFARM_MSG_1001297,
				"open operation on pio for URL (%s) failed: %s",
				url,
				gfarm_error_string(e));
		}
	} else {
		gflog_debug(GFARM_MSG_1001298,
			"open operation on file descriptor for URL (%s) "
			"failed: %s",
			url,
			gfarm_error_string(e));
	}

	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(staticp->open_time += gfarm_timerval_sub(&t2, &t1));
	gfs_profile(staticp->open_count++);
	return (e);
}

gfarm_error_t
gfs_pio_fhopen(gfarm_ino_t inum, gfarm_uint64_t gen, int flags, GFS_File *gfp)
{
	gfarm_error_t e;
	struct gfm_connection *gfm_server;
	int fd, type;
	gfarm_timerval_t t1, t2;
	struct gfs_pio_internal_cksum_info ci, *cip =
	    gfarm_ctxp->client_digest_check ? &ci : NULL;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	GFARM_KERNEL_UNUSE2(t1, t2);
	gfs_profile(gfarm_gettimerval(&t1));

	if ((e = gfm_fhopen_fd(inum, gen, flags, &gfm_server, &fd, &type, cip))
	    == GFARM_ERR_NO_ERROR) {
		if (type != GFS_DT_REG) {
			e = type == GFS_DT_DIR ? GFARM_ERR_IS_A_DIRECTORY :
			    type == GFS_DT_LNK ? GFARM_ERR_IS_A_SYMBOLIC_LINK :
			    GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else
			e = gfs_file_alloc(gfm_server, fd, flags, NULL,
			    inum, gen, cip, gfp);
		if (e != GFARM_ERR_NO_ERROR) {
			/* ignore result */
			(void)gfm_close_fd(gfm_server, fd, NULL, NULL);
			gfm_client_connection_free(gfm_server);
		}
	}
	if (e != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1003739,
		    "gfs_pio_fhopen(%lld:%lld): %s",
		    (long long)inum, (long long)gen, gfarm_error_string(e));

	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(staticp->open_time += gfarm_timerval_sub(&t2, &t1));
	gfs_profile(staticp->open_count++);
	return (e);
}

#if 0 /* not yet in gfarm v2 */

gfarm_error_t
gfs_pio_get_nfragment(GFS_File gf, int *nfragmentsp)
{
	if (GFS_FILE_IS_PROGRAM(gf))
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	if ((gf->mode & GFS_FILE_MODE_NSEGMENTS_FIXED) == 0)
		return (GFARM_ERR_FRAGMENT_INDEX_NOT_AVAILABLE);
	*nfragmentsp = gf->pi.status.st_nsections;
	return (GFARM_ERR_NO_ERROR);
}

#endif /* not yet in gfarm v2 */

static gfarm_error_t gfs_pio_view_fstat(GFS_File, struct gfs_stat *);

gfarm_error_t
gfs_pio_close_getgen(GFS_File gf, gfarm_uint64_t *igenp)
{
	gfarm_error_t e, e_save;
	gfarm_timerval_t t1, t2;
	struct gfarm_filesystem *fs = gfarm_filesystem_get_by_connection(
		gf->gfm_server);
	struct gfs_pio_internal_cksum_info *cip = NULL;
	struct gfs_stat gst;
	gfarm_off_t filesize = gf->md.filesize;
	int is_local = 0;

	GFARM_KERNEL_UNUSE2(t1, t2);
	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	gfs_pio_mutex_lock(&gf->mutex, __func__);

	/*
	 * no need to check and set the default file view here
	 * because neither gfs_pio_flush nor view_close is not
	 * needed unless the file view is specified by some
	 * operation.
	 */
	e_save = GFARM_ERR_NO_ERROR;
	if (gfs_pio_is_view_set(gf)) {
		struct gfs_file_section_context *vc = gf->view_context;

		is_local = vc->ops == &gfs_pio_local_storage_ops;
		if ((gf->mode & GFS_FILE_MODE_WRITE) != 0)
			e_save = flush_internal(gf);

		/* for client-side cksum calculation */
		if ((gf->mode &
		    (GFS_FILE_MODE_DIGEST_CALC|GFS_FILE_MODE_DIGEST_FINISH)) ==
		    (GFS_FILE_MODE_DIGEST_CALC)) {
			/*
			 * this is slow, if the filesystem node is remote,
			 * but necessary to detect file corruption which
			 * is caused by e.g. a kernel bug on the filesystem
			 * node.
			 * see r9289, r9349, and the following test with
			 * a corrupted file which size is increased than
			 * what it should be:
			 * cksum_mismatch.sh in
			 * regress/lib/libgfarm/gfarm/gfs_pio_open/
			 */
			e = gfs_pio_view_fstat(gf, &gst);
			if (e == GFARM_ERR_NO_ERROR) {
				/*
				 * should not call gfs_stat_free(),
				 * because gfs_pio_view_fstat()
				 * doesn't set gst->st_user/st_group.
				 */
				filesize = gst.st_size;
			}
		}

		e = (*gf->ops->view_close)(gf);
		if (e == GFARM_ERR_GFMD_FAILED_OVER) {
			gflog_info(GFARM_MSG_1003268,
			    "ignore %s error at pio close operation",
			    gfarm_error_string(e));
			gfarm_filesystem_set_failover_detected(fs, 1);
			e = GFARM_ERR_NO_ERROR;
		}
		if (e_save == GFARM_ERR_NO_ERROR)
			e_save = e;
	}

	gfs_pio_file_list_remove(gfarm_filesystem_opened_file_list(fs), gf);

#ifdef GFARM_USE_OPENSSL
	if ((gf->mode &
	    (GFS_FILE_MODE_DIGEST_CALC|GFS_FILE_MODE_DIGEST_FINISH)) ==
	    (GFS_FILE_MODE_DIGEST_CALC) && gf->md_offset == filesize) {
		e = gfs_pio_md_finish(gf);
		if (e_save == GFARM_ERR_NO_ERROR)
			e_save = e;

		/*
		 * the reason why `gf->md.filesize' is compared instead
		 * of `filesize' is because the comparison effectively
		 * invalidates incorrect checksum calculation.
		 * although this comparison became unnecessary since r9425
		 * for SF.net #814, we leave this comparision as is
		 * because it's cheap and no harm.
		 */
		if (e == GFARM_ERR_NO_ERROR &&
		    (gf->mode &
		     (GFS_FILE_MODE_WRITE|GFS_FILE_MODE_DIGEST_CALC|
		      GFS_FILE_MODE_DIGEST_FINISH)) ==
		     (GFS_FILE_MODE_WRITE|GFS_FILE_MODE_DIGEST_CALC|
		      GFS_FILE_MODE_DIGEST_FINISH) &&
		    ((gf->mode & GFS_FILE_MODE_MODIFIED) != 0 ||
		     (gf->mode & GFS_FILE_MODE_DIGEST_AVAIL) == 0) &&
		    gf->md_offset == gf->md.filesize) {
			cip = &gf->md;
			if (!is_local)
				cip->cksum_set_flags =
				    GFM_PROTO_CKSUM_SET_REPORT_ONLY;
		}
	}

	if (gf->md.cksum_type != NULL &&
	    (gf->mode & GFS_FILE_MODE_DIGEST_FINISH) == 0) {
		unsigned char md_value[EVP_MAX_MD_SIZE];

		/* not calculated, but need to do this to avoid memory leak */
		gfarm_msgdigest_free(gf->md_ctx, md_value);
	}
#endif /* GFARM_USE_OPENSSL */

	/*
	 * even if gfsd detectes gfmd failover,
	 * gfm_connection is possibily still alive in client.
	 *
	 * retrying gfm_close_fd is not necessary because fd is
	 * closed in gfmd when the connection is closed.
	 */
	if (gf->fd >= 0) {
		e = gfm_close_fd(gf->gfm_server, gf->fd, igenp, cip);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_debug(GFARM_MSG_1004220,
				    "gfs_pio_close: close gfmd: %s",
				    gfarm_error_string(e));
		/*
		 * NOTE:
		 * Because a successful failover shouldn't return
		 * a connection error, we ignore such error here.
		 * Note: unlike other protocols, retrying the close
		 *  protocol at a failover is meaningless.
		 * Ignoring connection errors is not a serious problem,
		 * because the file will be automatically closed by gfmd
		 * due to the connection error.
		 */
		if (e_save == GFARM_ERR_NO_ERROR &&
		    !gfm_client_is_connection_error(e))
			e_save = e;
	}

	gfs_pio_mutex_unlock(&gf->mutex, __func__);

	gfm_client_connection_free(gf->gfm_server);
	gfs_file_free(gf);

	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(staticp->close_time += gfarm_timerval_sub(&t2, &t1));
	gfs_profile(staticp->close_count++);

	if (e_save != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001299,
			"close operation on pio failed: %s",
			gfarm_error_string(e_save));
	}

	return (e_save);
}
gfarm_error_t
gfs_pio_close(GFS_File gf)
{
	return (gfs_pio_close_getgen(gf, NULL));
}

static gfarm_error_t
gfs_pio_purge(GFS_File gf)
{
	gf->offset += gf->p;
	gf->p = gf->length = 0;
	return (GFARM_ERR_NO_ERROR);
}

#define CHECK_WRITABLE(gf) { \
	if (((gf)->mode & GFS_FILE_MODE_WRITE) == 0) \
		return (gfarm_errno_to_error(EBADF)); \
	else if ((gf)->error == GFARM_ERRMSG_GFS_PIO_IS_EOF) \
		(gf)->error = GFARM_ERR_NO_ERROR; \
}
#define CHECK_WRITABLE_LOCKED(gf) { \
	if (((gf)->mode & GFS_FILE_MODE_WRITE) == 0) {\
		gfs_pio_mutex_unlock(&(gf)->mutex, __func__); \
		return (gfarm_errno_to_error(EBADF)); \
	} else if ((gf)->error == GFARM_ERRMSG_GFS_PIO_IS_EOF) \
		(gf)->error = GFARM_ERR_NO_ERROR; \
}
/*
 * we check this against gf->open_flags rather than gf->mode,
 * because we may set GFARM_FILE_MODE_READ even if write-only case.
 */
#define CHECK_READABLE(gf) { \
	if (((gf)->open_flags & GFARM_FILE_ACCMODE) == GFARM_FILE_WRONLY) \
		return (gfarm_errno_to_error(EBADF)); \
}

#define CHECK_READABLE_EOF(gf) { \
	if (((gf)->open_flags & GFARM_FILE_ACCMODE) == GFARM_FILE_WRONLY) \
		return (EOF); \
}

#define CHECK_READABLE_LOCKED(gf) { \
	if (((gf)->open_flags & GFARM_FILE_ACCMODE) == GFARM_FILE_WRONLY) { \
		gfs_pio_mutex_unlock(&(gf)->mutex, __func__); \
		return (gfarm_errno_to_error(EBADF)); \
	} \
}

#define CHECK_READABLE_EOF_LOCKED(gf) { \
	if (((gf)->open_flags & GFARM_FILE_ACCMODE) == GFARM_FILE_WRONLY) { \
		gfs_pio_mutex_unlock(&(gf)->mutex, __func__); \
		return (EOF); \
	} \
}

static gfarm_error_t
gfs_pio_fillbuf(GFS_File gf, size_t size)
{
	gfarm_error_t e;
	size_t len;
	int nretries = GFS_FAILOVER_RETRY_COUNT;

	CHECK_READABLE(gf);

	if (gf->error != GFARM_ERR_NO_ERROR) { /* error or EOF? */
		if (GFS_PIO_ERROR(gf) != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001300,
				"CHECK_READABLE failed: %s",
				gfarm_error_string(GFS_PIO_ERROR(gf)));
		}
		return (GFS_PIO_ERROR(gf));
	}
	/* UNBUFFERED case */
	if (gf->buffer == NULL) {
		gf->error = GFARM_ERR_NO_BUFFER_SPACE_AVAILABLE;
		return (gf->error);
	}
	if (gf->p < gf->length)
		return (GFARM_ERR_NO_ERROR);

	if ((gf->mode & GFS_FILE_MODE_BUFFER_DIRTY) != 0) {
		e = flush_internal(gf);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001301,
				"gfs_pio_flush() failed: %s",
				gfarm_error_string(e));
			return (e);
		}
	} else
		gfs_pio_purge(gf);

	if (gf->io_offset != gf->offset) {
		gf->io_offset = gf->offset;
	}

	do {
		e = (*gf->ops->view_pread)(gf, gf->buffer, size, gf->io_offset,
		    &len);
	} while (e != GFARM_ERR_NO_ERROR && --nretries >= 0 &&
	    gfs_pio_failover_check_retry(gf, &e));
	if (e != GFARM_ERR_NO_ERROR) {
		gf->error = e;
		gflog_debug(GFARM_MSG_1001302,
			"view_pread() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	gf->length = len;
	gf->io_offset += len;
	if (len == 0)
		gf->error = GFARM_ERRMSG_GFS_PIO_IS_EOF;
	return (GFARM_ERR_NO_ERROR);
}

/* unlike other functions, this returns `*writtenp' even if an error happens */
static gfarm_error_t
do_write(GFS_File gf, const char *buffer, size_t length,
	size_t *writtenp)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	size_t written, len;
	int nretries;

	if (length == 0) {
		*writtenp = 0;
		return (GFARM_ERR_NO_ERROR);
	}
	assert(buffer);
	if (gf->io_offset != gf->offset) {
		gf->io_offset = gf->offset;
	}
	for (written = 0; written < length; written += len) {
		/* in case of GFARM_FILE_APPEND, io_offset is ignored */
		nretries = GFS_FAILOVER_RETRY_COUNT;
		do {
			e = (*gf->ops->view_pwrite)(gf,
			    buffer + written, length - written, gf->io_offset,
			    &len);
		} while (e != GFARM_ERR_NO_ERROR && --nretries >= 0 &&
		    gfs_pio_failover_check_retry(gf, &e));
		if (e != GFARM_ERR_NO_ERROR) {
			gf->error = e;
			gflog_debug(GFARM_MSG_1001303,
				"view_pwrite() failed: %s",
				gfarm_error_string(e));
			break;
		}
		gf->io_offset += len;
	}
	*writtenp = written;

	return (e);
}

static gfarm_error_t
flush_internal(GFS_File gf)
{
	gfarm_error_t e;
	size_t written;

	CHECK_WRITABLE(gf);

	if ((gf->mode & GFS_FILE_MODE_BUFFER_DIRTY) != 0) {
		e = gfs_pio_check_view_default(gf);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001304,
			    "gfs_pio_check_view_default() failed: %s",
			    gfarm_error_string(e));
			return (e);
		}
		e = do_write(gf, gf->buffer, gf->length, &written);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001305,
				"do_write() failed: %s",
				gfarm_error_string(e));
			return (e);
		}
		gf->mode &= ~GFS_FILE_MODE_BUFFER_DIRTY;
	}
	if (gf->p >= gf->length)
		gfs_pio_purge(gf);

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_pio_flush(GFS_File gf)
{
	gfarm_error_t e;
	gfs_pio_mutex_lock(&gf->mutex, __func__);
	e = flush_internal(gf);
	gfs_pio_mutex_unlock(&gf->mutex, __func__);
	return (e);
}

static gfarm_error_t
seek_internal(GFS_File gf, gfarm_off_t offset, int whence,
	gfarm_off_t *resultp)
{
	gfarm_error_t e;
	gfarm_off_t where;
	gfarm_timerval_t t1, t2;

	GFARM_KERNEL_UNUSE2(t1, t2);
	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	e = gfs_pio_check_view_default(gf);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001306,
			"gfs_pio_check_view_default() failed: %s",
			gfarm_error_string(e));
		goto finish;
	}

	if (gf->error == GFARM_ERRMSG_GFS_PIO_IS_EOF)
		gf->error = GFARM_ERR_NO_ERROR;

	switch (whence) {
	case GFARM_SEEK_SET:
		where = offset;
		break;
	case GFARM_SEEK_CUR:
		where = offset + gf->offset + gf->p;
		break;
	case GFARM_SEEK_END:
		/* XXX FIXME: ask the file size to gfsd. */
		e = gf->error = GFARM_ERR_FUNCTION_NOT_IMPLEMENTED;
		gflog_debug(GFARM_MSG_1001307,
			"GFARM_SEEK_END option is not supported: %s",
			gfarm_error_string(e));
		goto finish;
	default:
		e = gf->error = GFARM_ERR_INVALID_ARGUMENT;
		gflog_debug(GFARM_MSG_1001308,
			"invalid argument whence(%d): %s",
			whence,
			gfarm_error_string(e));
		goto finish;
	}
	if (where < 0) {
		e = gf->error = GFARM_ERR_INVALID_ARGUMENT;
		gflog_debug(GFARM_MSG_1001309,
			"invalid argument: %s",
			gfarm_error_string(e));
		goto finish;
	}

	/*
	 * This is the case that the file offset will be repositioned
	 * within the current io buffer.  In case of GFARM_FILE_APPEND,
	 * reposition is not allowed if the buffer is dirty.
	 */
	if (((gf->open_flags & GFARM_FILE_APPEND) == 0 ||
	     (gf->mode & GFS_FILE_MODE_BUFFER_DIRTY) == 0) &&
	    gf->offset <= where && where <= gf->offset + gf->length) {
		gf->p = where - gf->offset;
		if (resultp != NULL)
			*resultp = where;

		e = GFARM_ERR_NO_ERROR;
		goto finish;
	}

	if (gf->mode & GFS_FILE_MODE_BUFFER_DIRTY) {
		e = flush_internal(gf);
		if (e != GFARM_ERR_NO_ERROR) {
			gf->error = e;
			gflog_debug(GFARM_MSG_1001310,
				"gfs_pio_flush() failed: %s",
				gfarm_error_string(e));
			goto finish;
		}
	}

	e = gf->error = GFARM_ERR_NO_ERROR; /* purge EOF/error state */
	gfs_pio_purge(gf);
	gf->offset = gf->io_offset = where;
	if (resultp != NULL)
		*resultp = where;

 finish:
	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(staticp->seek_time += gfarm_timerval_sub(&t2, &t1));
	gfs_profile(staticp->seek_count++);

	return (e);
}

gfarm_error_t
gfs_pio_seek(GFS_File gf, gfarm_off_t offset, int whence, gfarm_off_t *resultp)
{
	gfarm_error_t e;
	gfs_pio_mutex_lock(&gf->mutex, __func__);
	e = seek_internal(gf, offset, whence, resultp);
	gfs_pio_mutex_unlock(&gf->mutex, __func__);
	return (e);
}

gfarm_error_t
gfs_pio_truncate(GFS_File gf, gfarm_off_t length)
{
	gfarm_error_t e;
	gfarm_timerval_t t1, t2;
	int nretries = GFS_FAILOVER_RETRY_COUNT;

	GFARM_KERNEL_UNUSE2(t1, t2);
	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	gfs_pio_mutex_lock(&gf->mutex, __func__);

	e = gfs_pio_check_view_default(gf);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001311,
			"gfs_pio_check_view_default() failed %s",
			gfarm_error_string(e));
		goto finish;
	}

	CHECK_WRITABLE_LOCKED(gf);

	if (gf->mode & GFS_FILE_MODE_BUFFER_DIRTY) {
		e = flush_internal(gf);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001312,
				"gfs_pio_flush(): %s",
				gfarm_error_string(e));
			goto finish;
		}
	}

	gf->error = GFARM_ERR_NO_ERROR; /* purge EOF/error state */
	gfs_pio_purge(gf);

	do {
		e = (*gf->ops->view_ftruncate)(gf, length);
	} while (e != GFARM_ERR_NO_ERROR && --nretries >= 0 &&
	    gfs_pio_failover_check_retry(gf, &e));
	if (e != GFARM_ERR_NO_ERROR)
		gf->error = e;
finish:
	gfs_pio_mutex_unlock(&gf->mutex, __func__);
	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(staticp->truncate_time += gfarm_timerval_sub(&t2, &t1));
	gfs_profile(staticp->truncate_count++);

	return (e);
}
static gfarm_error_t
gfs_pio_pread_unbuffer(GFS_File gf, void *buffer, int size,
	gfarm_off_t offset, int *np)
{
	gfarm_error_t e;
	char *p = buffer;
	int n = 0;
	size_t length;
	int nretries;

	while (size > 0) {
		nretries = GFS_FAILOVER_RETRY_COUNT;
		do {
			e = (*gf->ops->view_pread)(gf,
			    p, size, offset, &length);
		} while (e != GFARM_ERR_NO_ERROR && --nretries >= 0 &&
		    gfs_pio_failover_check_retry(gf, &e));
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1003944,
				"pread() failed: %s",
				gfarm_error_string(e));
			if (n == 0)
				goto finish;
			else
				break;
		}
		p += length;
		n += length;
		offset += length;
		size -= length;
		if (!length) {
			gf->error = GFARM_ERRMSG_GFS_PIO_IS_EOF;
			break;
		}
	}
	*np = n;
	e = GFARM_ERR_NO_ERROR;
 finish:
	return (e);
}

static gfarm_error_t
gfs_pio_pwrite_unbuffer(GFS_File gf, const void *buffer, int size,
	gfarm_off_t offset, int *np)
{
	gfarm_error_t e;
	const char *p = buffer;
	int n = 0;
	size_t length;
	int nretries;

	while (size > 0) {
		nretries = GFS_FAILOVER_RETRY_COUNT;
		do {
			e = (*gf->ops->view_pwrite)(gf,
			    p, size, offset, &length);
		} while (e != GFARM_ERR_NO_ERROR && --nretries >= 0 &&
		    gfs_pio_failover_check_retry(gf, &e));
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1003945,
				"pwrite() failed: %s",
				gfarm_error_string(e));
			if (n == 0)
				goto finish;
			else
				break;
		}
		p += length;
		n += length;
		offset += length;
		size -= length;
		if (!length)
			break;
	}
	*np = n;
	e = GFARM_ERR_NO_ERROR;
 finish:
	return (e);
}

static gfarm_error_t
read_internal(GFS_File gf, void *buffer, int size, int *np)
{
	gfarm_error_t e;
	char *p = buffer;
	int n = 0;
	int length;
	gfarm_timerval_t t1, t2;

	GFARM_KERNEL_UNUSE2(t1, t2);
	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	e = gfs_pio_check_view_default(gf);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001313,
			"Check view default for pio failed: %s",
			gfarm_error_string(e));
		return (e);
	}

	CHECK_READABLE(gf);

	/* UNBUFFERED case */
	if (gf->buffer == NULL) {
		gfarm_off_t result, offset = gf->offset + gf->p;

		e = gfs_pio_pread_unbuffer(gf, buffer, size, offset, np);
		if (e == GFARM_ERR_NO_ERROR) {
			seek_internal(gf,
			    offset + *np, GFARM_SEEK_SET, &result);
			n = *np;
		}
		goto finish;
	}
	if (size >= gf->bufsize + (gf->length - gf->p)) {
		if (gf->p < gf->length) {
			length = gf->length - gf->p;
			memcpy(p, gf->buffer + gf->p, length);
			p += length;
			n += length;
			size -= length;
			gf->p += length;
		}
		if ((gf->mode & GFS_FILE_MODE_BUFFER_DIRTY) != 0)
			e = flush_internal(gf); /* this does purge too */
		else
			e = gfs_pio_purge(gf);
		if (e == GFARM_ERR_NO_ERROR) {
			e = gfs_pio_pread_unbuffer(gf, p, size, gf->offset, np);
			if (e == GFARM_ERR_NO_ERROR) {
				gf->offset += *np;
				n += *np;
			}
		}
	} else
		while (size > 0) {
			if ((e = gfs_pio_fillbuf(gf, gf->bufsize))
			    != GFARM_ERR_NO_ERROR) {
				/* XXX call reconnect, when failover for writing
				 *     is supported
				 */
				if ((gf->mode & GFS_FILE_MODE_READ) == 0 ||
				    (gf->mode & GFS_FILE_MODE_WRITE) != 0 ||
				    !IS_CONNECTION_ERROR(e))
					break;
				if ((e = gfs_pio_reconnect(gf))
				    != GFARM_ERR_NO_ERROR)
					break;
				continue;
			}
			if (gf->error != GFARM_ERR_NO_ERROR) /* EOF or error */
				break;
			length = gf->length - gf->p;
			if (length > size)
				length = size;
			memcpy(p, gf->buffer + gf->p, length);
			p += length;
			n += length;
			size -= length;
			gf->p += length;
		}
	if (e != GFARM_ERR_NO_ERROR) {
		/*
		 * when n > 0, part of data is stored in the buffer,
		 * and the file position is changed.
		 */
		gflog_debug(GFARM_MSG_1003740, "gfs_pio_read: n=%d: %s",
		    n, gfarm_error_string(e));
		goto finish;
	}
	*np = n;
 finish:
	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(staticp->read_time += gfarm_timerval_sub(&t2, &t1));
	gfs_profile(staticp->read_size += n);
	gfs_profile(staticp->read_count++);

	return (e);
}

gfarm_error_t
gfs_pio_read(GFS_File gf, void *buffer, int size, int *np)
{
	gfarm_error_t e;
	gfs_pio_mutex_lock(&gf->mutex, __func__);
	e = read_internal(gf, buffer, size, np);
	gfs_pio_mutex_unlock(&gf->mutex, __func__);
	return (e);
}

gfarm_error_t
gfs_pio_pread_page(GFS_File gf, gfarm_off_t off, int size, int force,
		gfs_pageio_t cb, void *arg)
{
	gfarm_error_t e;
	gfarm_timerval_t t1, t2;
	int second = 0;

	GFARM_KERNEL_UNUSE2(t1, t2);
	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	gfs_pio_mutex_lock(&gf->mutex, __func__);

	e = gfs_pio_check_view_default(gf);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1004616,
			"Check view default for pio failed: %s",
			gfarm_error_string(e));
		gfs_pio_mutex_unlock(&gf->mutex, __func__);
		return (e);
	}

	CHECK_READABLE_LOCKED(gf);

	do {
		if (second || off < gf->offset
			|| off >= (gf->offset + gf->length)) {
			if (!force)
				break;
			flush_internal(gf);
			gfs_pio_purge(gf);
			gf->offset = off;

			if ((e = gfs_pio_fillbuf(gf, gf->bufsize))
			    != GFARM_ERR_NO_ERROR) {
				/* XXX call reconnect, when failover for writing
				 *     is supported
				 */
				if ((gf->mode & GFS_FILE_MODE_READ) == 0 ||
				    (gf->mode & GFS_FILE_MODE_WRITE) != 0 ||
				    !IS_CONNECTION_ERROR(e))
					break;
				if ((e = gfs_pio_reconnect(gf))
				    != GFARM_ERR_NO_ERROR)
					break;
				continue;
			}
			if (gf->error != GFARM_ERR_NO_ERROR) /* EOF or error */
				break;
		}
		off = cb(gf->buffer, gf->offset, gf->length, arg);
		second++;
	} while (force && off > 0);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1004617,
			"gfs_pio_fillbuf() failed: %s",
			gfarm_error_string(e));
		goto finish;
	}

	e = GFARM_ERR_NO_ERROR;
 finish:
	gfs_pio_mutex_unlock(&gf->mutex, __func__);
	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(staticp->read_time += gfarm_timerval_sub(&t2, &t1));

	return (e);
}

static gfarm_error_t
write_internal(GFS_File gf, const void *buffer, int size, int *np)
{
	gfarm_error_t e;
	size_t written = 0;
	gfarm_timerval_t t1, t2;

	GFARM_KERNEL_UNUSE2(t1, t2);
	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	e = gfs_pio_check_view_default(gf);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001315,
			"gfs_pio_check_view_default() failed: %s",
			gfarm_error_string(e));
		return (e);
	}

	CHECK_WRITABLE(gf);

	/* UNBUFFERED case */
	if (gf->buffer == NULL) {
		gfarm_off_t result, offset = gf->offset + gf->p;

		e = gfs_pio_pwrite_unbuffer(gf, buffer, size, offset, np);
		if (e == GFARM_ERR_NO_ERROR) {
			seek_internal(gf,
			    offset + *np, GFARM_SEEK_SET, &result);
			written = *np;
		}
		goto finish;
	}

	if (size + gf->p > gf->bufsize) {
		/*
		 * gf->buffer[gf->p .. gf->bufsize-1] will be overridden
		 * by buffer.
		 */
		gf->length = gf->p;
		e = flush_internal(gf); /* this does purge too */
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001316,
				"gfs_pio_flush() failed: %s",
				gfarm_error_string(e));
			goto finish;
		}
	}
	if (size >= gf->bufsize) {
		/* shortcut to avoid unnecessary memory copy */
		assert(gf->p == 0); /* gfs_pio_flush() was called above */
		gf->length = 0;
		gf->mode &= ~GFS_FILE_MODE_BUFFER_DIRTY;

		e = do_write(gf, buffer, size, &written);
		if (e != GFARM_ERR_NO_ERROR && written == 0) {
			gflog_debug(GFARM_MSG_1001317,
				"do_write() failed: %s",
				gfarm_error_string(e));
			goto finish;
		}
		gf->offset += written;
		*np = written; /* XXX - size_t vs int */

		e = GFARM_ERR_NO_ERROR;
		goto finish;
	}
	/* purge the buffer for reading to avoid unintended overwrite */
	if ((gf->mode & GFS_FILE_MODE_BUFFER_DIRTY) == 0)
		gfs_pio_purge(gf);
	gf->mode |= GFS_FILE_MODE_BUFFER_DIRTY;
	memcpy(gf->buffer + gf->p, buffer, size);
	gf->p += size;
	if (gf->p > gf->length)
		gf->length = gf->p;
	*np = written = size;
	e = GFARM_ERR_NO_ERROR;
	if (gf->p >= gf->bufsize)
		e = flush_internal(gf);
 finish:
	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(staticp->write_time += gfarm_timerval_sub(&t2, &t1));
	gfs_profile(staticp->write_size += written);
	gfs_profile(staticp->write_count++);

	return (e);
}

gfarm_error_t
gfs_pio_write(GFS_File gf, const void *buffer, int size, int *np)
{
	gfarm_error_t e;
	gfs_pio_mutex_lock(&gf->mutex, __func__);
	e = write_internal(gf, buffer, size, np);
	gfs_pio_mutex_unlock(&gf->mutex, __func__);
	return (e);
}

gfarm_error_t
gfs_pio_pread(GFS_File gf, void *buffer, int size, gfarm_off_t offset, int *np)
{
	gfarm_error_t e;

	gfs_pio_mutex_lock(&gf->mutex, __func__);
	if (!(e = seek_internal(gf, offset, GFARM_SEEK_SET, &offset)))
		e = read_internal(gf, buffer, size, np);
	gfs_pio_mutex_unlock(&gf->mutex, __func__);

	return (e);
}

gfarm_error_t
gfs_pio_pwrite(GFS_File gf,
		const void *buffer, int size, gfarm_off_t offset, int *np)
{
	gfarm_error_t e;

	gfs_pio_mutex_lock(&gf->mutex, __func__);
	if (!(e = seek_internal(gf, offset, GFARM_SEEK_SET, &offset)))
		e = write_internal(gf, buffer, size, np);
	gfs_pio_mutex_unlock(&gf->mutex, __func__);

	return (e);
}

gfarm_error_t
gfs_pio_append(GFS_File gf, void *buffer, int size, int *np,
		gfarm_off_t *offp, gfarm_off_t *fsizep)
{
	gfarm_error_t e;
	gfarm_timerval_t t1, t2;
	size_t length;
	int nretries = GFS_FAILOVER_RETRY_COUNT;

	GFARM_KERNEL_UNUSE2(t1, t2);
	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	gfs_pio_mutex_lock(&gf->mutex, __func__);

	e = gfs_pio_check_view_default(gf);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003948,
			"gfs_pio_check_view_default() failed: %s",
			gfarm_error_string(e));
		gfs_pio_mutex_unlock(&gf->mutex, __func__);
		return (e);
	}

	CHECK_WRITABLE_LOCKED(gf);

	do {
		e = (*gf->ops->view_write)(gf,
		    buffer, size, &length, offp, fsizep);
	} while (e != GFARM_ERR_NO_ERROR && --nretries >= 0 &&
	    gfs_pio_failover_check_retry(gf, &e));
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003949,
			"view_write() failed: %s",
			gfarm_error_string(e));
		gfs_pio_mutex_unlock(&gf->mutex, __func__);
		return (e);
	}
	*np = length;

	gfs_pio_mutex_unlock(&gf->mutex, __func__);

	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(staticp->write_time += gfarm_timerval_sub(&t2, &t1));

	return (e);
}
gfarm_error_t
gfs_pio_view_fd(GFS_File gf, int *fdp)
{
	gfarm_error_t e;

	*fdp = 0;
	e = gfs_pio_check_view_default(gf);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003950,
			"gfs_pio_check_view_default() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	*fdp = (*gf->ops->view_fd)(gf);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
sync_internal(GFS_File gf, int operation, double *time, unsigned long long *ct)
{
	gfarm_error_t e;
	int nretries = GFS_FAILOVER_RETRY_COUNT;
	gfarm_timerval_t t1, t2;

#ifdef __KERNEL__	/* may called at exit, fd passing refers tsk->files */
	if (!gfs_pio_is_view_set(gf)) /* view isn't set yet */
		return (GFARM_ERR_NO_ERROR);
#endif /* __KERNEL__ */

	GFARM_KERNEL_UNUSE2(t1, t2);
	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	e = gfs_pio_check_view_default(gf);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003951,
		    "gfs_pio_sync: %s", gfarm_error_string(e));
		return (e);
	}
	e = flush_internal(gf);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001318,
			"gfs_pio_flush() failed: %s",
			gfarm_error_string(e));
		goto finish;
	}

	do {
		e = (*gf->ops->view_fsync)(gf, operation);
	} while (e != GFARM_ERR_NO_ERROR && --nretries >= 0 &&
	    gfs_pio_failover_check_retry(gf, &e));
	if (e != GFARM_ERR_NO_ERROR) {
		gf->error = e;
		gflog_debug(GFARM_MSG_1001319,
			"view_fsync() failed: %s",
			gfarm_error_string(e));
	}
finish:
	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(*time += gfarm_timerval_sub(&t2, &t1));
	gfs_profile(*ct += 1);

	return (e);
}

gfarm_error_t
gfs_pio_sync(GFS_File gf)
{
	gfarm_error_t e;

	gfs_pio_mutex_lock(&gf->mutex, __func__);
	e = sync_internal(gf, GFS_PROTO_FSYNC_WITH_METADATA,
		    &staticp->sync_time, &staticp->sync_count);
	gfs_pio_mutex_unlock(&gf->mutex, __func__);
	return (e);
}

gfarm_error_t
gfs_pio_datasync(GFS_File gf)
{
	gfarm_error_t e;

	gfs_pio_mutex_lock(&gf->mutex, __func__);
	e = sync_internal(gf, GFS_PROTO_FSYNC_WITHOUT_METADATA,
		    &staticp->datasync_time, &staticp->datasync_count);
	gfs_pio_mutex_unlock(&gf->mutex, __func__);
	return (e);
}

static int
getc_internal(GFS_File gf)
{
	gfarm_error_t e;
	int c;
	gfarm_timerval_t t1, t2;

	GFARM_KERNEL_UNUSE2(t1, t2);
	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	e = gfs_pio_check_view_default(gf);
	if (e != GFARM_ERR_NO_ERROR) {
		gf->error = e;
		gflog_debug(GFARM_MSG_1001320,
			"gfs_pio_check_view_default() failed: %s",
			gfarm_error_string(e));
		return (EOF);
	}

	CHECK_READABLE_EOF(gf);

	if (gf->p >= gf->length) {
		if (gfs_pio_fillbuf(gf, gf->bufsize) != GFARM_ERR_NO_ERROR) {
			c = EOF; /* can get reason via gfs_pio_error() */
			gflog_debug(GFARM_MSG_1001321,
				"gfs_pio_fillbuf() failed: %s",
				gfarm_error_string(GFS_PIO_ERROR(gf)));
			goto finish;
		}
		if (gf->error != GFARM_ERR_NO_ERROR) {
			c = EOF;
			gflog_debug(GFARM_MSG_1001322,
				"gfs_pio_fillbuf() failed: %s",
				gfarm_error_string(gf->error));
			goto finish;
		}
	}
	assert(gf->buffer);
	c = ((unsigned char *)gf->buffer)[gf->p++];
 finish:
	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(staticp->getc_time += gfarm_timerval_sub(&t2, &t1));
	return (c);
}

int
gfs_pio_getc(GFS_File gf)
{
	int c;
	gfs_pio_mutex_lock(&gf->mutex, __func__);
	c = getc_internal(gf);
	gfs_pio_mutex_unlock(&gf->mutex, __func__);
	return (c);
}

static int
ungetc_internal(GFS_File gf, int c)
{
	gfarm_error_t e;

	e = gfs_pio_check_view_default(gf);
	if (e != GFARM_ERR_NO_ERROR) {
		gf->error = e;
		gflog_debug(GFARM_MSG_1001323,
			"gfs_pio_check_view_default() failed: %s",
			gfarm_error_string(e));
		return (EOF);
	}

	CHECK_READABLE_EOF(gf);

	if (c != EOF) {
		if (gf->p == 0) { /* cannot unget - XXX should permit this? */
			gf->error = GFARM_ERR_NO_SPACE;
			gflog_debug(GFARM_MSG_1001324,
				"gfs_pio_ungetc(): %s",
				gfarm_error_string(GFARM_ERR_NO_SPACE));
			return (EOF);
		}
		/* We do not mark this buffer dirty here. */
		assert(gf->buffer);
		gf->buffer[--gf->p] = c;
	}
	return (c);
}

int
gfs_pio_ungetc(GFS_File gf, int c)
{
	int c2;
	gfs_pio_mutex_lock(&gf->mutex, __func__);
	c2 = ungetc_internal(gf, c);
	gfs_pio_mutex_unlock(&gf->mutex, __func__);
	return (c2);
}

static gfarm_error_t
putc_internal(GFS_File gf, int c)
{
	gfarm_error_t e;
	gfarm_timerval_t t1, t2;

	GFARM_KERNEL_UNUSE2(t1, t2);
	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	e = gfs_pio_check_view_default(gf);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001325,
			"gfs_pio_check_view_default() failed: %s",
			gfarm_error_string(e));
		return (e);
	}

	CHECK_WRITABLE(gf);

	/* UNBUFFERED case */
	if (gf->buffer == NULL) {
		gf->error = GFARM_ERR_NO_BUFFER_SPACE_AVAILABLE;
		return (gf->error);
	}
	if (gf->p >= gf->bufsize) {
		gfarm_error_t e = flush_internal(gf); /* this does purge too */

		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001326,
				"gfs_pio_flush() failed: %s",
				gfarm_error_string(e));
			goto finish;
		}
	}
	/* purge the buffer for reading to avoid unintended overwrite */
	if ((gf->mode & GFS_FILE_MODE_BUFFER_DIRTY) == 0)
		gfs_pio_purge(gf);
	gf->mode |= GFS_FILE_MODE_BUFFER_DIRTY;
	gf->buffer[gf->p++] = c;
	if (gf->p > gf->length)
		gf->length = gf->p;
	if (gf->p >= gf->bufsize)
		e = flush_internal(gf);
 finish:
	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(staticp->putc_time += gfarm_timerval_sub(&t2, &t1));
	return (e);
}

gfarm_error_t
gfs_pio_putc(GFS_File gf, int c)
{
	gfarm_error_t e;
	gfs_pio_mutex_lock(&gf->mutex, __func__);
	e = putc_internal(gf, c);
	gfs_pio_mutex_unlock(&gf->mutex, __func__);
	return (e);
}

/* mostly compatible with fgets(3) */
static gfarm_error_t
puts_internal(GFS_File gf, const char *s)
{
	gfarm_error_t e;

	e = gfs_pio_check_view_default(gf);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001327,
			"gfs_pio_check_view_default() failed: %s",
			gfarm_error_string(e));
		return (e);
	}

	CHECK_WRITABLE(gf);

	while (*s != '\0') {
		gfarm_error_t e = putc_internal(gf, *(unsigned char *)s);

		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001328,
				"gfs_pio_putc() failed: %s",
				gfarm_error_string(e));
			return (e);
		}
		s++;
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_pio_puts(GFS_File gf, const char *s)
{
	gfarm_error_t e;
	gfs_pio_mutex_lock(&gf->mutex, __func__);
	e = puts_internal(gf, s);
	gfs_pio_mutex_unlock(&gf->mutex, __func__);
	return (e);
}

/* mostly compatible with fgets(3), but EOF check is done by *s == '\0' */
gfarm_error_t
gfs_pio_gets(GFS_File gf, char *s, size_t size)
{
	gfarm_error_t e;
	char *p = s;
	int c;
	gfarm_timerval_t t1, t2;

	gfs_pio_mutex_lock(&gf->mutex, __func__);

	e = gfs_pio_check_view_default(gf);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001329,
			"gfs_pio_check_view_default() failed: %s",
			gfarm_error_string(e));
		gfs_pio_mutex_unlock(&gf->mutex, __func__);
		return (e);
	}

	GFARM_KERNEL_UNUSE2(t1, t2);
	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

#ifdef __GNUC__ /* workaround gcc warning: unused variable */
	c = EOF;
#endif
	CHECK_READABLE_LOCKED(gf);

	if (size <= 1) {
		gf->error = GFARM_ERR_INVALID_ARGUMENT;
		gflog_debug(GFARM_MSG_1001330,
			"invalid argument, size (%d) <= 1: %s",
			(int)size,
			gfarm_error_string(gf->error));
		gfs_pio_mutex_unlock(&gf->mutex, __func__);
		return (gf->error);
	}
	--size; /* for '\0' */
	for (; size > 0 && (c = getc_internal(gf)) != EOF; --size) {
		*p++ = c;
		if (c == '\n')
			break;
	}
	*p++ = '\0';

	gfs_pio_mutex_unlock(&gf->mutex, __func__);

	gfs_profile(gfarm_gettimerval(&t2));
	/* XXX should introduce gfs_pio_gets_time??? */
	gfs_profile(staticp->getline_time += gfarm_timerval_sub(&t2, &t1));

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_pio_getline(GFS_File gf, char *s, size_t size, int *eofp)
{
	gfarm_error_t e;
	char *p = s;
	int c;
	gfarm_timerval_t t1, t2;

	gfs_pio_mutex_lock(&gf->mutex, __func__);

	e = gfs_pio_check_view_default(gf);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001331,
			"gfs_pio_check_view_default() failed: %s",
			gfarm_error_string(e));
		gfs_pio_mutex_unlock(&gf->mutex, __func__);
		return (e);
	}

	GFARM_KERNEL_UNUSE2(t1, t2);
	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

#ifdef __GNUC__ /* workaround gcc warning: unused variable */
	c = EOF;
#endif
	CHECK_READABLE_LOCKED(gf);

	if (size <= 1) {
		gf->error = GFARM_ERR_INVALID_ARGUMENT;
		gflog_debug(GFARM_MSG_1001332,
			"invalid argument, size(%d) <= 1: %s",
			(int)size,
			gfarm_error_string(gf->error));
		gfs_pio_mutex_unlock(&gf->mutex, __func__);
		return (gf->error);
	}
	--size; /* for '\0' */
	for (; size > 0 && (c = getc_internal(gf)) != EOF; --size) {
		if (c == '\n')
			break;
		*p++ = c;
	}
	*p++ = '\0';
	if (p == s + 1 && c == EOF) {
		*eofp = 1;
		gfs_pio_mutex_unlock(&gf->mutex, __func__);
		return (GFS_PIO_ERROR(gf));
	}
	*eofp = 0;

	gfs_pio_mutex_unlock(&gf->mutex, __func__);

	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(staticp->getline_time += gfarm_timerval_sub(&t2, &t1));

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_pio_putline(GFS_File gf, const char *s)
{
	gfarm_error_t e;

	gfs_pio_mutex_lock(&gf->mutex, __func__);

	e = gfs_pio_check_view_default(gf);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001333,
			"gfs_pio_check_view_default() failed: %s",
			gfarm_error_string(e));
		gfs_pio_mutex_unlock(&gf->mutex, __func__);
		return (e);
	}

	CHECK_WRITABLE_LOCKED(gf);

	e = puts_internal(gf, s);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001334,
			"gfs_pio_puts() failed: %s",
			gfarm_error_string(e));
		gfs_pio_mutex_unlock(&gf->mutex, __func__);
		return (e);
	}

	gfs_pio_mutex_unlock(&gf->mutex, __func__);

	return (putc_internal(gf, '\n'));
}

#define ALLOC_SIZE_INIT	220

/*
 * mostly compatible with getline(3) in glibc,
 * but there are the following differences:
 * 1. on EOF, *lenp == 0
 * 2. on error, *lenp isn't touched.
 */
gfarm_error_t
gfs_pio_readline(GFS_File gf, char **bufp, size_t *sizep, size_t *lenp)
{
	gfarm_error_t e;
	char *buf = *bufp, *p = NULL;
	size_t size = *sizep, len = 0;
	int c;
	size_t alloc_size;
	int overflow = 0;
	gfarm_timerval_t t1, t2;

	gfs_pio_mutex_lock(&gf->mutex, __func__);

	e = gfs_pio_check_view_default(gf);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001335,
			"gfs_pio_check_view_default() failed: %s",
			gfarm_error_string(e));
		gfs_pio_mutex_unlock(&gf->mutex, __func__);
		return (e);
	}

	GFARM_KERNEL_UNUSE2(t1, t2);
	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

#ifdef __GNUC__ /* workaround gcc warning: unused variable */
	c = EOF;
#endif
	CHECK_READABLE_LOCKED(gf);

	if (buf == NULL || size <= 1) {
		if (size <= 1)
			size = ALLOC_SIZE_INIT;
		GFARM_REALLOC_ARRAY(buf, buf, size);
		if (buf == NULL) {
			gflog_debug(GFARM_MSG_1001336,
				"allocation of buf for pio_getc failed: %s",
				gfarm_error_string(GFARM_ERR_NO_MEMORY));
			gfs_pio_mutex_unlock(&gf->mutex, __func__);
			return (GFARM_ERR_NO_MEMORY);
		}
	}
	for (;;) {
		c = getc_internal(gf);
		if (c == EOF)
			break;
		if (size <= len) {
			alloc_size = gfarm_size_add(&overflow, size, size);
			if (!overflow)
				GFARM_REALLOC_ARRAY(p, buf, alloc_size);
			if (overflow || p == NULL) {
				*bufp = buf;
				*sizep = size;
				gflog_debug(GFARM_MSG_1001337,
					"allocation of buf for pio_getc "
					"failed or size overflow: %s",
					gfarm_error_string(
						GFARM_ERR_NO_MEMORY));
				gfs_pio_mutex_unlock(&gf->mutex, __func__);
				return (GFARM_ERR_NO_MEMORY);
			}
			buf = p;
			size += size;
		}
		buf[len++] = c;
		if (c == '\n')
			break;
	}
	if (size <= len) {
		alloc_size = gfarm_size_add(&overflow, size, size);
		if (!overflow)
			GFARM_REALLOC_ARRAY(p, buf, alloc_size);
		if (overflow || p == NULL) {
			*bufp = buf;
			*sizep = size;
			gflog_debug(GFARM_MSG_1001338,
				"allocation of buf for pio_getc failed "
				"or size overflow: %s",
				gfarm_error_string(
					GFARM_ERR_NO_MEMORY));
			gfs_pio_mutex_unlock(&gf->mutex, __func__);
			return (GFARM_ERR_NO_MEMORY);
		}
		buf = p;
		size += size;
	}
	buf[len] = '\0';

	gfs_pio_mutex_unlock(&gf->mutex, __func__);

	gfs_profile(gfarm_gettimerval(&t2));
	/* XXX should introduce gfs_pio_readline_time??? */
	gfs_profile(staticp->getline_time += gfarm_timerval_sub(&t2, &t1));

	*bufp = buf;
	*sizep = size;
	*lenp = len;

	return (GFARM_ERR_NO_ERROR);
}

/*
 * mostly compatible with getdelim(3) in glibc,
 * but there are the following differences:
 * 1. on EOF, *lenp == 0
 * 2. on error, *lenp isn't touched.
 */
gfarm_error_t
gfs_pio_readdelim(GFS_File gf, char **bufp, size_t *sizep, size_t *lenp,
	const char *delim, size_t delimlen)
{
	gfarm_error_t e;
	char *buf = *bufp, *p = NULL;
	size_t size = *sizep, len = 0, alloc_size;
	int c, delimtail, overflow;
	static const char empty_line[] = "\n\n";
	gfarm_timerval_t t1, t2;

	gfs_pio_mutex_lock(&gf->mutex, __func__);

	e = gfs_pio_check_view_default(gf);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001339,
			"gfs_pio_check_view_default() failed: %s",
			gfarm_error_string(e));
		gfs_pio_mutex_unlock(&gf->mutex, __func__);
		return (e);
	}

	GFARM_KERNEL_UNUSE2(t1, t2);
	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

#ifdef __GNUC__ /* workaround gcc warning: unused variable */
	c = EOF;
#endif
	CHECK_READABLE_LOCKED(gf);

	if (delim == NULL) { /* special case 1 */
		delimtail = 0; /* workaround gcc warning */
	} else {
		if (delimlen == 0) { /* special case 2 */
			delim = empty_line;
			delimlen = 2;
		}
		delimtail = delim[delimlen - 1];
	}
	if (buf == NULL || size <= 1) {
		if (size <= 1)
			size = ALLOC_SIZE_INIT;
		GFARM_REALLOC_ARRAY(buf, buf, size);
		if (buf == NULL) {
			gflog_debug(GFARM_MSG_1001340,
				"allocation of buf for pio_getc failed: %s",
				gfarm_error_string(GFARM_ERR_NO_MEMORY));
			gfs_pio_mutex_unlock(&gf->mutex, __func__);
			return (GFARM_ERR_NO_MEMORY);
		}
	}
	for (;;) {
		c = getc_internal(gf);
		if (c == EOF)
			break;
		if (size <= len) {
			alloc_size = gfarm_size_add(&overflow, size, size);
			if (!overflow)
				GFARM_REALLOC_ARRAY(p, buf, alloc_size);
			if (overflow || p == NULL) {
				*bufp = buf;
				*sizep = size;
				gflog_debug(GFARM_MSG_1001341,
					"allocation of buf for pio_getc failed"
					" or size overflow: %s",
					gfarm_error_string(
						GFARM_ERR_NO_MEMORY));
				gfs_pio_mutex_unlock(&gf->mutex, __func__);
				return (GFARM_ERR_NO_MEMORY);
			}
			buf = p;
			size += size;
		}
		buf[len++] = c;
		if (delim == NULL) /* special case 1: no delimiter */
			continue;
		if (len >= delimlen && c == delimtail &&
		    memcmp(&buf[len - delimlen], delim, delimlen) == 0) {
			if (delim == empty_line) { /* special case 2 */
				for (;;) {
					c = getc_internal(gf);
					if (c == EOF)
						break;
					if (c != '\n') {
						ungetc_internal(gf, c);
						break;
					}
					if (size <= len) {
						alloc_size = gfarm_size_add(
						    &overflow, size, size);
						if (!overflow)
							GFARM_REALLOC_ARRAY(p,
							    buf, alloc_size);
						if (overflow || p == NULL) {
							*bufp = buf;
							*sizep = size;
							gflog_debug(
							  GFARM_MSG_1001342,
							  "allocation of buf "
							  "for pio_getc failed"
							  "or size overflow: "
							  "%s",
							  gfarm_error_string(
							    GFARM_ERR_NO_MEMORY
								));
							gfs_pio_mutex_unlock(
							  &gf->mutex,
							  __func__);
							return (
							  GFARM_ERR_NO_MEMORY);
						}
						buf = p;
						size += size;
					}
					buf[len++] = c;
				}
			}
			break;
		}
	}
	if (size <= len) {
		alloc_size = gfarm_size_add(&overflow, size, size);
		if (!overflow)
			GFARM_REALLOC_ARRAY(p, buf, alloc_size);
		if (overflow || p == NULL) {
			*bufp = buf;
			*sizep = size;
			gflog_debug(GFARM_MSG_1001343,
				"allocation of buf for pio_getc failed "
				"or size overflow: %s",
				gfarm_error_string(
					GFARM_ERR_NO_MEMORY));
			gfs_pio_mutex_unlock(&gf->mutex, __func__);
			return (GFARM_ERR_NO_MEMORY);
		}
		buf = p;
		size += size;
	}
	buf[len] = '\0';

	gfs_pio_mutex_unlock(&gf->mutex, __func__);

	gfs_profile(gfarm_gettimerval(&t2));
	/* XXX should introduce gfs_pio_readdelim_time??? */
	gfs_profile(staticp->getline_time += gfarm_timerval_sub(&t2, &t1));

	*bufp = buf;
	*sizep = size;
	*lenp = len;

	return (GFARM_ERR_NO_ERROR);
}

/*
 * fstat
 */

static gfarm_error_t
gfs_pio_view_fstat(GFS_File gf, struct gfs_stat *st)
{
	gfarm_error_t e;
	int nretries = GFS_FAILOVER_RETRY_COUNT;

	do {
		e = (*gf->ops->view_fstat)(gf, st);
	} while (e != GFARM_ERR_NO_ERROR && --nretries >= 0 &&
	    gfs_pio_failover_check_retry(gf, &e));
	return (e);
}

gfarm_error_t
gfs_pio_stat(GFS_File gf, struct gfs_stat *st)
{
	gfarm_error_t e;

	gfs_pio_mutex_lock(&gf->mutex, __func__);

	e = gfs_pio_check_view_default(gf);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003742,
		    "gfs_pio_stat: %s", gfarm_error_string(e));
		gfs_pio_mutex_unlock(&gf->mutex, __func__);
		return (e);
	}
	e = gfs_fstat(gf, st);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001344,
			"gfs_fstat() failed: %s",
			gfarm_error_string(e));
		gfs_pio_mutex_unlock(&gf->mutex, __func__);
		return (e);
	}

	if (gfs_pio_is_view_set(gf)) {
		if ((gf->mode & GFS_FILE_MODE_WRITE) != 0) {
			/* XXX call reconnect, when failover for writing
			 *     is supported
			 */
			if ((e = flush_internal(gf))
			    != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1002655,
				    "gfs_pio_flush() failed: %s",
				    gfarm_error_string(e));
			} else if ((e = gfs_pio_view_fstat(gf, st))
			    != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1002656,
				    "view_fstat() failed: %s",
				    gfarm_error_string(e));
			}
		} else if ((e = gfs_pio_view_fstat(gf, st))
		     != GFARM_ERR_NO_ERROR && IS_CONNECTION_ERROR(e)) {
			if ((e = gfs_pio_reconnect(gf)) != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1002657,
				    "gfs_pio_reconnect() failed: %s",
				    gfarm_error_string(e));
			} else if ((e = gfs_pio_view_fstat(gf, st))
				    != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1002658,
				    "view_stat() failed: %s",
				    gfarm_error_string(e));
			}
		} else if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001345,
				"view_fstat() failed: %s",
				gfarm_error_string(e));
			gf->error = e;
		}
	}
	gfs_pio_mutex_unlock(&gf->mutex, __func__);
	return (e);
}

gfarm_error_t
gfs_pio_cksum(GFS_File gf, const char *type, struct gfs_stat_cksum *cksum)
{
	gfarm_error_t e;
	int nretries = GFS_FAILOVER_RETRY_COUNT;

	gfs_pio_mutex_lock(&gf->mutex, __func__);

	e = gfs_pio_check_view_default(gf);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003743,
		    "gfs_pio_cksum: %s", gfarm_error_string(e));
		gfs_pio_mutex_unlock(&gf->mutex, __func__);
		return (e);
	}
	do {
		e = (*gf->ops->view_cksum)(gf, type, cksum);
	} while (e != GFARM_ERR_NO_ERROR && --nretries >= 0 &&
	    gfs_pio_failover_check_retry(gf, &e));

	gfs_pio_mutex_unlock(&gf->mutex, __func__);

	return (e);
}

/*
 * recvfile/sendfile
 */

gfarm_error_t
gfs_pio_recvfile(GFS_File r_gf, gfarm_off_t r_off,
	int w_fd, gfarm_off_t w_off,
	gfarm_off_t len, gfarm_off_t *recvp)
{
	gfarm_error_t e;
	int nretries = GFS_FAILOVER_RETRY_COUNT;
	gfarm_timerval_t t1, t2;

	GFARM_KERNEL_UNUSE2(t1, t2);
	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	gfs_pio_mutex_lock(&r_gf->mutex, __func__);

	e = gfs_pio_check_view_default(r_gf);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003741,
		    "gfs_pio_check_view_default() failed: %s",
		    gfarm_error_string(e));
		gfs_pio_mutex_unlock(&r_gf->mutex, __func__);
		return (e);
	}

	CHECK_READABLE_LOCKED(r_gf);

	do {
		e = (*r_gf->ops->view_recvfile)(r_gf, r_off, w_fd, w_off, len,
		    recvp);
	} while (e != GFARM_ERR_NO_ERROR && --nretries >= 0 &&
	    gfs_pio_failover_check_retry(r_gf, &e));

	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(staticp->read_time += gfarm_timerval_sub(&t2, &t1));

	gfs_pio_mutex_unlock(&r_gf->mutex, __func__);
	return (e);
}

gfarm_error_t
gfs_pio_sendfile(GFS_File w_gf, gfarm_off_t w_off,
	int r_fd, gfarm_off_t r_off,
	gfarm_off_t len, gfarm_off_t *sentp)
{
	gfarm_error_t e;
	int nretries = GFS_FAILOVER_RETRY_COUNT;
	gfarm_timerval_t t1, t2;

	GFARM_KERNEL_UNUSE2(t1, t2);
	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	gfs_pio_mutex_lock(&w_gf->mutex, __func__);

	e = gfs_pio_check_view_default(w_gf);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003952,
			"gfs_pio_check_view_default() failed: %s",
			gfarm_error_string(e));
		gfs_pio_mutex_unlock(&w_gf->mutex, __func__);
		return (e);
	}

	CHECK_WRITABLE_LOCKED(w_gf);

	do {
		e = (*w_gf->ops->view_sendfile)(w_gf, w_off, r_fd, r_off, len,
		    sentp);
	} while (e != GFARM_ERR_NO_ERROR && --nretries >= 0 &&
	    gfs_pio_failover_check_retry(w_gf, &e));

	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(staticp->write_time += gfarm_timerval_sub(&t2, &t1));

	gfs_pio_mutex_unlock(&w_gf->mutex, __func__);

	return (e);
}

/*
 * internal utility functions, mostly for failover handling
 */

#define GFS_FILE_LIST_MUTEX "gfs_file_list.mutex"

struct gfs_file_list *
gfs_pio_file_list_alloc()
{
	struct gfs_file_list *gfl;

	GFARM_MALLOC(gfl);
	if (gfl == NULL)
		return (NULL);
	GFARM_HCIRCLEQ_INIT(gfl->files, hcircleq);
	gfarm_mutex_init(&gfl->mutex, "gfs_pio_file_list_alloc",
	    GFS_FILE_LIST_MUTEX);

	return (gfl);
}

void
gfs_pio_file_list_free(struct gfs_file_list *gfl)
{
	if (gfl == NULL)
		return;
	gfarm_mutex_destroy(&gfl->mutex, "gfs_pio_file_list_free",
	    GFS_FILE_LIST_MUTEX);
	free(gfl);
}

void
gfs_pio_file_list_add(struct gfs_file_list *gfl, GFS_File gf)
{
	gfarm_mutex_lock(&gfl->mutex, "gfs_pio_file_list_add",
	    GFS_FILE_LIST_MUTEX);

	GFARM_HCIRCLEQ_INSERT_TAIL(gfl->files, gf, hcircleq);

	gfarm_mutex_unlock(&gfl->mutex, "gfs_pio_file_list_add",
	    GFS_FILE_LIST_MUTEX);
}

void
gfs_pio_file_list_remove(struct gfs_file_list *gfl, GFS_File gf)
{
	gfarm_mutex_lock(&gfl->mutex, "gfs_pio_file_list_remove",
	    GFS_FILE_LIST_MUTEX);

	GFARM_HCIRCLEQ_REMOVE(gf, hcircleq);

	gfarm_mutex_unlock(&gfl->mutex, "gfs_pio_file_list_remove",
	    GFS_FILE_LIST_MUTEX);
}

void
gfs_pio_file_list_foreach(struct gfs_file_list *gfl,
	int (*func)(struct gfs_file *, void *), void *closure)
{
	GFS_File gf;

	gfarm_mutex_lock(&gfl->mutex, "gfs_pio_file_list_foreach",
	    GFS_FILE_LIST_MUTEX);

	GFARM_HCIRCLEQ_FOREACH(gf, gfl->files, hcircleq) {
		if (func(gf, closure) == 0)
			break;
	}

	gfarm_mutex_unlock(&gfl->mutex, "gfs_pio_file_list_foreach",
	    GFS_FILE_LIST_MUTEX);
}

struct gfs_profile_list profile_items[] = {
	{ "create_time", "gfs_pio_create time  : %g sec", "%g", 'd',
	  offsetof(struct gfarm_gfs_pio_static, create_time) },
	{ "create_count", "gfs_pio_create count : %llu", "%llu", 'l',
	  offsetof(struct gfarm_gfs_pio_static, create_count) },
	{ "open_time", "gfs_pio_open time   : %g sec", "%g", 'd',
	  offsetof(struct gfarm_gfs_pio_static, open_time) },
	{ "open_count", "gfs_pio_open count  : %llu", "%llu", 'l',
	  offsetof(struct gfarm_gfs_pio_static, open_count) },
	{ "close_time", "gfs_pio_close time  : %g sec", "%g", 'd',
	  offsetof(struct gfarm_gfs_pio_static, close_time) },
	{ "close_count", "gfs_pio_close count : %llu", "%llu", 'l',
	  offsetof(struct gfarm_gfs_pio_static, close_count) },
	{ "seek_time", "gfs_pio_seek time   : %g sec", "%g", 'd',
	  offsetof(struct gfarm_gfs_pio_static, seek_time) },
	{ "seek_count", "gfs_pio_seek count  : %llu", "%llu", 'l',
	  offsetof(struct gfarm_gfs_pio_static, seek_count) },
	{ "truncate_time", "gfs_pio_truncate time  : %g sec", "%g", 'd',
	  offsetof(struct gfarm_gfs_pio_static, truncate_time) },
	{ "truncate_count", "gfs_pio_truncate count : %llu", "%llu", 'l',
	  offsetof(struct gfarm_gfs_pio_static, truncate_count) },
	{ "read_time", "gfs_pio_read time   : %g sec", "%g", 'd',
	  offsetof(struct gfarm_gfs_pio_static, read_time) },
	{ "read_size", "gfs_pio_read size   : %llu", "%llu", 'l',
	  offsetof(struct gfarm_gfs_pio_static, read_size) },
	{ "read_count", "gfs_pio_read count  : %llu", "%llu", 'l',
	  offsetof(struct gfarm_gfs_pio_static, read_count) },
	{ "write_time", "gfs_pio_write time  : %g sec", "%g", 'd',
	  offsetof(struct gfarm_gfs_pio_static, write_time) },
	{ "write_size", "gfs_pio_write size  : %llu", "%llu", 'l',
	  offsetof(struct gfarm_gfs_pio_static, write_size) },
	{ "write_count", "gfs_pio_write count : %llu", "%llu", 'l',
	  offsetof(struct gfarm_gfs_pio_static, write_count) },
	{ "sync_time", "gfs_pio_sync time   : %g sec", "%g", 'd',
	  offsetof(struct gfarm_gfs_pio_static, sync_time) },
	{ "sync_count", "gfs_pio_sync count  : %llu", "%llu", 'l',
	  offsetof(struct gfarm_gfs_pio_static, sync_count) },
	{ "datasync_time", "gfs_pio_datasync time  : %g sec", "%g", 'd',
	  offsetof(struct gfarm_gfs_pio_static, datasync_time) },
	{ "datasync_count", "gfs_pio_datasync count : %llu", "%llu", 'l',
	  offsetof(struct gfarm_gfs_pio_static, datasync_count) },
	{ "getline_time", "gfs_pio_getline time  : %g sec (this calls getc)",
	  "%g", 'd', offsetof(struct gfarm_gfs_pio_static, getline_time) },
	{ "getline_count", "gfs_pio_getline count : %llu (this calls getc)",
	  "%llu", 'l', offsetof(struct gfarm_gfs_pio_static, getline_count) },
	{ "getc_time", "gfs_pio_getc time  : %g sec", "%g", 'd',
	  offsetof(struct gfarm_gfs_pio_static, getc_time) },
	{ "getc_count", "gfs_pio_getc count : %llu", "%llu", 'l',
	  offsetof(struct gfarm_gfs_pio_static, getc_count) },
	{ "putc_time", "gfs_pio_putc time  : %g sec", "%g", 'd',
	  offsetof(struct gfarm_gfs_pio_static, putc_time) },
	{ "putc_count",  "gfs_pio_putc count : %llu", "%llu", 'l',
	  offsetof(struct gfarm_gfs_pio_static, putc_count) },
};

void
gfs_pio_display_timers(void)
{
	int n = GFARM_ARRAY_LENGTH(profile_items);

	gfs_profile_display_timers(n, profile_items, staticp);
}

gfarm_error_t
gfs_pio_profile_value(const char *name, char *v, size_t *sp)
{
	int n = GFARM_ARRAY_LENGTH(profile_items);

	return (gfs_profile_value(name, n, profile_items, staticp, v, sp));
}
