/*
 * $Id$
 */

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h> /* sprintf */
#include <ctype.h>
#include <time.h>
#include <sys/time.h>
#include <pthread.h>
#include <errno.h>

#define GFARM_INTERNAL_USE
#include <gfarm/gflog.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

#include "gflog_reduced.h"
#include "gfutil.h"
#include "nanosec.h"
#include "thrsubr.h"

#include "quota_info.h"
#include "config.h"
#include "context.h"
#include "timespec.h"
#include "patmatch.h"
#include "gfm_proto.h"
#include "gfs_proto.h"

#include "uint64_map.h"
#include "quota.h"
#include "subr.h"
#include "inum_string_list.h"
#include "db_access.h"
#include "tenant.h"
#include "host.h"
#include "user.h"
#include "group.h"
#include "dir.h"
#include "inode.h"
#include "file_copy.h"
#include "dead_file_copy.h"
#include "process.h" /* struct file_opening */
#include "xattr_info.h"
#include "back_channel.h"
#include "acl.h"
#include "xattr.h"
#include "fsngroup.h"
#include "dirset.h"
#include "quota_dir.h"
#include "replica_check.h"

#include "auth.h" /* for "peer.h" */
#include "peer.h" /* peer_reset_pending_new_generation() */
#include "gfmd.h" /* resuming_*() */

#define DIR_DEPTH_BUF_INIT			1024	/* == GFARM_PATH_MAX */

#define ROOT_INUMBER			2
#define INODE_TABLE_SIZE_INITIAL	1024
#define INODE_TABLE_SIZE_MULTIPLY	2

#define INODE_MODE_FREE			0	/* struct inode:i_mode */

struct file_copy {
	struct file_copy *host_next;
	struct host *host;
	int flags; /* 0, if there is ongoing replication about lastest gen */
#define FILE_COPY_VALID		1
#define FILE_COPY_BEING_REMOVED	2
};

#define FILE_COPY_IS_VALID(fc) \
		(((fc)->flags & FILE_COPY_VALID) != 0)
#define FILE_COPY_IS_BEING_REMOVED(fc) \
		(((fc)->flags & FILE_COPY_BEING_REMOVED) != 0)
/*
 * !FILE_COPY_IS_VALID() means either
 *	the replica is being created (incomplete).
 * or
 *	the replica was complete, but being removed.
 *
 * invariant:
 * 	if (FILE_COPY_IS_BEING_REMOVED(fc)) {
 *		assert(!FILE_COPY_IS_VALID(fc));
 *		assert(remove_replica_entity() is already called);
 *	}
 */

static const char xattr_md5[] = "gfarm.md5";
static const char xattr_ncopy[] = GFARM_EA_NCOPY;
static const char xattr_repattr[] = GFARM_EA_REPATTR;
static const char xattr_all[] = "*";

struct xattr_entry {
	struct xattr_entry *prev, *next;
	char *name;
	void *cached_attrvalue;
	int cached_attrsize;
};

struct xattrs {
	struct xattr_entry *head, *tail;
};

struct inode {
	gfarm_ino_t i_number;
	gfarm_uint64_t i_gen;
	gfarm_uint64_t i_nlink;
	gfarm_uint64_t i_nlink_ini; /* only used at gfmd startup */
	gfarm_off_t i_size;
	struct user *i_user;
	struct group *i_group;
	struct gfarm_timespec i_atimespec;
	gfarm_mode_t i_mode;
	struct gfarm_timespec i_mtimespec;
	struct gfarm_timespec i_ctimespec;
	struct xattrs i_xattrs, i_xmlattrs;

	union {
		struct inode_free_link {
			struct inode *prev, *next;
		} l;
		struct inode_common_data {
			union inode_type_specific_data {
				struct inode_file {
					struct file_copy *copies;
					struct checksum *cksum;
				} f;
				struct inode_dir {
					Dir entries;

					/* only used at gfmd startup */
					struct inode *parent_dir;
				} d;
				struct inode_symlink {
					char *source_path;
				} l;
			} s;
			struct inode_activity *activity;
		} c;
	} u;
};

struct checksum {
	char *type;
	size_t len;
	char sum[1];
};

struct inode_activity {
	struct file_opening openings; /* dummy header */

	struct dirset *tdirset;

	union inode_state_type_specific {
		struct inode_state_file {
			enum {
				EVENT_NONE,
				EVENT_GEN_UPDATED,
				EVENT_GEN_UPDATED_BY_COOKIE
			} event_type;
			struct event_waiter *event_waiters;
			struct peer *event_source;

			struct gfarm_timespec last_update;
			int writers, spool_writers;
			int replication_pending;

			struct inode_replicating_state *rstate;
		} f;
	} u;
};

struct inode_replicating_state {
	struct file_replicating replicating_hosts; /* dummy header */
};

static gfarm_error_t file_replicating_new(
	struct inode *, struct host *, struct host *, struct dead_file_copy *,
	struct dirset *tdirset,
	struct file_replicating **);
static void file_replicating_free(struct file_replicating *);
static void file_replicating_free_by_error_before_request(
	struct file_replicating *);

struct inode **inode_table = NULL;
gfarm_ino_t inode_table_size = 0;
gfarm_ino_t inode_free_index = ROOT_INUMBER;

static char TENANT_BASE_NAME[] = ".tenants"; /* for /.tenants/${TENANT_NAME} */

struct inode inode_free_list; /* dummy header of doubly linked circular list */
int inode_free_list_initialized = 0;

static gfarm_uint64_t total_num_inodes;
static pthread_mutex_t total_num_inodes_mutex = PTHREAD_MUTEX_INITIALIZER;
static const char total_num_inodes_diag[] = "total_num_inodes_mutex";

static char lost_found[] = "lost+found";

/* for gfs_profile */
static gfarm_uint64_t cumulative_replicated_files = 0;
static gfarm_uint64_t cumulative_replicated_bytes = 0;
static double cumulative_replicated_time = 0.0;

void
inode_for_each_file_copies(
	struct inode *inode,
	void (*func)(struct inode *inode, struct file_copy *copy,
		void *closure),
	void *closure)
{
	struct file_copy *copy;

	assert(inode_is_file(inode));
	for (copy = inode->u.c.s.f.copies; copy != NULL;
	    copy = copy->host_next) {
		(*func)(inode, copy, closure);
	}
}

void
inode_for_each_file_opening(
	struct inode *inode,
	void (*func)(int openflag, struct host *spool_host, void *closure),
	void *closure)
{
	struct file_opening *fo;
	struct inode_activity *ia = inode->u.c.activity;

	assert(inode_is_file(inode));
	if (ia == NULL)
		return;

	for (fo = ia->openings.opening_next;
	     fo != &ia->openings;
	     fo = fo->opening_next) {
		(*func)(fo->flag, fo->u.f.spool_host, closure);
	}
}

struct host *
file_copy_host(struct file_copy *file_copy)
{
	return (file_copy->host);
}

int
file_copy_is_valid(struct file_copy *file_copy)
{
	return (FILE_COPY_IS_VALID(file_copy));
}

int
file_copy_is_being_removed(struct file_copy *file_copy)
{
	return (FILE_COPY_IS_BEING_REMOVED(file_copy));
}

gfarm_uint64_t
inode_total_num(void)
{
	gfarm_uint64_t num_inodes;
	static const char diag[] = "inode_total_num";

	gfarm_mutex_lock(&total_num_inodes_mutex, diag, total_num_inodes_diag);
	num_inodes = total_num_inodes;
	gfarm_mutex_unlock(&total_num_inodes_mutex,
	    diag, total_num_inodes_diag);

	return (num_inodes);
}

void
inode_cksum_remove_in_cache(struct inode *inode)
{
	assert(inode_is_file(inode));
	free(inode->u.c.s.f.cksum);
	inode->u.c.s.f.cksum = NULL;
}

void
inode_cksum_remove(struct inode *inode)
{
	gfarm_error_t e;

	if (inode->u.c.s.f.cksum != NULL) {
		e = db_inode_cksum_remove(inode->i_number);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1000297,
			    "db_inode_cksum_remove(%lld): %s",
			    (unsigned long long)inode->i_number,
			    gfarm_error_string(e));
	}
	inode_cksum_remove_in_cache(inode);
}

void
inode_cksum_invalidate(struct file_opening *fo)
{
	struct inode_activity *ia = fo->inode->u.c.activity;
	struct file_opening *o;

	for (o = ia->openings.opening_next;
	    o != &ia->openings; o = o->opening_next) {
		if (o != fo)
			o->flag |= GFARM_FILE_CKSUM_INVALIDATED;
	}
}

void
inode_cksum_invalidate_all(struct inode *inode)
{
	struct inode_activity *ia = inode->u.c.activity;
	struct file_opening *o;

	if (ia == NULL)
		return;
	for (o = ia->openings.opening_next;
	    o != &ia->openings; o = o->opening_next) {
		o->flag |= GFARM_FILE_CKSUM_INVALIDATED;
	}
}

static gfarm_error_t
inode_cksum_set_internal(struct inode *inode,
	const char *cksum_type, size_t cksum_len, const char *cksum)
{
	struct checksum *cs;
	size_t size;
	int overflow = 0;

#ifdef __GNUC__ /* workaround gcc warning: might be used uninitialized */
	cs = NULL;
#endif
	size = gfarm_size_add(&overflow,
	    sizeof(*cs) - sizeof(cs->sum) + strlen(cksum_type) + 1, cksum_len);
	if (!overflow)
		cs = malloc(size);
	if (overflow || cs == NULL) {
		gflog_error(GFARM_MSG_1001712,
		    "cksum allocation failed: no memory");
		return (GFARM_ERR_NO_MEMORY);
	}
	cs->type = cs->sum + cksum_len;
	cs->len = cksum_len;
	memcpy(cs->sum, cksum, cksum_len);
	strcpy(cs->type, cksum_type);

	inode->u.c.s.f.cksum = cs;

	return(GFARM_ERR_NO_ERROR);
}

gfarm_error_t
inode_cksum_set_in_cache(struct inode *inode,
	const char *cksum_type, size_t cksum_len, const char *cksum)
{
	gfarm_error_t e;
	struct checksum *cs = inode->u.c.s.f.cksum;

	/* reduce memory reallocation */
	if (cs != NULL &&
	    strcmp(cksum_type, cs->type) == 0 && cksum_len == cs->len) {
		memcpy(cs->sum, cksum, cksum_len);
		return (GFARM_ERR_NO_ERROR);
	}
	inode_cksum_remove_in_cache(inode);

	e = inode_cksum_set_internal(inode, cksum_type, cksum_len, cksum);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1001716,
			"inode_cksum_set_internal() failed: %s",
			gfarm_error_string(e));
	return (e);
}

static int
cmp_cksum(struct checksum *c1,
	const char *cksum_type, size_t cksum_len, const char *cksum)
{
	return (strcmp(c1->type, cksum_type) != 0 || c1->len != cksum_len ||
	    memcmp(c1->sum, cksum, c1->len) != 0);
}

gfarm_error_t
inode_cksum_set(struct inode *inode,
	const char *cksum_type, size_t cksum_len, const char *cksum,
	gfarm_int32_t cksum_result_flags, int i_am_only_writer, int *is_setp)
{
	gfarm_error_t e;
	struct inode_activity *ia = inode->u.c.activity;
	struct checksum *cs;
	static const char diag[] = "inode_cksum_set";

	if (strlen(cksum_type) > GFM_PROTO_CKSUM_TYPE_MAXLEN) {
		gflog_debug(GFARM_MSG_1002429,
		    "too long cksum type: \"%s\"", cksum_type);
		return (GFARM_ERR_INVALID_ARGUMENT);
	}
	if (cksum_len > GFM_PROTO_CKSUM_MAXLEN) {
		gflog_debug(GFARM_MSG_1002430,
		    "too long cksum (type: \"%s\"): %d bytes",
		    cksum_type, (int)cksum_len);
		return (GFARM_ERR_INVALID_ARGUMENT);
	}
	cs = inode->u.c.s.f.cksum;
	if (cs != NULL) {
		if (cmp_cksum(cs, cksum_type, cksum_len, cksum) != 0) {
			e = GFARM_ERR_CHECKSUM_MISMATCH;
			if (ia != NULL && ia->u.f.writers >= 1 &&
			    !i_am_only_writer) {
				gflog_debug(GFARM_MSG_1003761,
				   "%s: (%llu:%llu) %s", diag,
				   (unsigned long long)inode_get_number(inode),
				   (unsigned long long)inode_get_gen(inode),
				   gfarm_error_string(e));
				if (is_setp != NULL)
					*is_setp = 0;
				return (GFARM_ERR_NO_ERROR);
			} else {
				/*
				 * return error even when
				 * GFM_PROTO_CKSUM_SET_REPORT_ONLY
				 */
				gflog_error(GFARM_MSG_1003762,
				   "%s: (%llu:%llu) %s", diag,
				   (unsigned long long)inode_get_number(inode),
				   (unsigned long long)inode_get_gen(inode),
				   gfarm_error_string(e));
				gflog_info(GFARM_MSG_1004321,
				    "cksum %s:<%.*s> vs %s:<%.*s>",
				    cs->type, (int)cs->len, cs->sum,
				    cksum_type, (int)cksum_len, cksum);
				return (e);
			}
		} else {
			e = GFARM_ERR_ALREADY_EXISTS;
			gflog_debug(GFARM_MSG_1003763, "%s: (%llu:%llu) %s",
			    diag, (unsigned long long)inode_get_number(inode),
			    (unsigned long long)inode_get_gen(inode),
			    gfarm_error_string(e));
			if (is_setp != NULL)
				*is_setp = 0;
			return (GFARM_ERR_NO_ERROR);
		}
	} else if ((cksum_result_flags & GFM_PROTO_CKSUM_SET_REPORT_ONLY)
	    != 0) {
		if (ia != NULL && ia->u.f.writers >= 1 && !i_am_only_writer) {
			gflog_debug(GFARM_MSG_1004206,
			   "%s: (%llu:%llu) client cksum report "
			    "about multiple writer case", diag,
			   (unsigned long long)inode_get_number(inode),
			   (unsigned long long)inode_get_gen(inode));
			if (is_setp != NULL)
				*is_setp = 0;
			return (GFARM_ERR_NO_ERROR);
		}
		if (inode_get_size(inode) > 0) {
			/*
			 * this report is sent from a client
			 * about remote access, and in that case,
			 * cksum should be set by gfsd already,
			 * but it's not really set.
			 */
			gflog_notice(GFARM_MSG_1004207,
			   "%s: (%llu:%llu): "
			   "cksum is incorrectly set by client", diag,
			   (unsigned long long)inode_get_number(inode),
			   (unsigned long long)inode_get_gen(inode));
		}
		/*
		 * don't report error (SF.net #813),
		 * because the way of cksum calculation is not exactly
		 * same between gfsd and libgfarm, especially about
		 * whether they call lseek(,SEEK_CUR,) or not.
		 */
		if (is_setp != NULL)
			*is_setp = 0;
		return (GFARM_ERR_NO_ERROR);

	}
	if (cs == NULL) {
		e = db_inode_cksum_add(inode->i_number,
		    cksum_type, cksum_len, cksum);
	} else { /* this condition will never be satisfied since r8972 */
		e = db_inode_cksum_modify(inode->i_number,
		    cksum_type, cksum_len, cksum);
	}
	/* XXX FIXME: shouldn't happen, but */
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1000298,
		    "db_inode_cksum_%s(%lld): %s",
		    cs == NULL ? "add" : "modify",
		    (unsigned long long)inode->i_number,
		    gfarm_error_string(e));
	/* abandon `e' here, since it shouldn't happen */

	/* inode_cksum_set_in_cache() calls gflog_error/gflog_debug */
	e = inode_cksum_set_in_cache(inode, cksum_type, cksum_len, cksum);
	if (e == GFARM_ERR_NO_ERROR && is_setp != NULL)
		*is_setp = 1;
	return (e);
}

/*
 * This function is called from GFM_PROTO_FHSET_CKSUM (i.e. write_verify gfsd).
 * Unlike GFM_PROTO_CKSUM_SET, this protocol never sets checksum,
 * if the file is opened for writing, and returns GFARM_ERR_FILE_BUSY
 * in that case.
 */
gfarm_error_t
inode_cksum_set_if_not_writing(struct inode *inode,
	const char *cksum_type, size_t cksum_len, const char *cksum,
	gfarm_int32_t cksum_result_flags)
{
	struct inode_activity *ia = inode->u.c.activity;

	if (ia != NULL && ia->u.f.writers > 0)
		return (GFARM_ERR_FILE_BUSY);
	return (inode_cksum_set(inode,
	    cksum_type, cksum_len, cksum, cksum_result_flags, 0, NULL));
}

gfarm_error_t
file_opening_cksum_set(struct file_opening *fo,
	const char *cksum_type, size_t cksum_len, const char *cksum,
	gfarm_int32_t flags, struct gfarm_timespec *mtime)
{
	gfarm_error_t e;
	struct inode *inode = fo->inode;
	struct inode_activity *ia = inode->u.c.activity;

	assert(ia != NULL);

	if ((fo->flag & GFARM_FILE_CKSUM_INVALIDATED) != 0) {
		gflog_debug(GFARM_MSG_1001714, "file checksum is invalidated");
		return (GFARM_ERR_EXPIRED);
	}
	e = inode_cksum_set(fo->inode, cksum_type, cksum_len, cksum, flags,
	    ia->u.f.writers == 1 && (accmode_to_op(fo->flag) & GFS_W_OK) != 0,
	    NULL);
	if (e != GFARM_ERR_NO_ERROR)
		return (e); /* inode_cksum_set() calls gflog_error/debug */

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
file_opening_cksum_get(struct file_opening *fo,
	char **cksum_typep, size_t *cksum_lenp, char **cksump,
	gfarm_int32_t *flagsp)
{
	struct inode_activity *ia = fo->inode->u.c.activity;
	struct checksum *cs;
	gfarm_int32_t flags = 0;

	assert(ia != NULL);

	if (!inode_is_file(fo->inode)) {
		gflog_debug(GFARM_MSG_1001717,
			"inode type is not file");
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	}
	if (ia->u.f.writers > 1 ||
	    (ia->u.f.writers == 1 &&
	     (accmode_to_op(fo->flag) & GFS_W_OK) == 0))
		flags |= GFM_PROTO_CKSUM_GET_MAYBE_EXPIRED;
	if (fo->flag & GFARM_FILE_CKSUM_INVALIDATED)
		flags |= GFM_PROTO_CKSUM_GET_EXPIRED;

	cs = fo->inode->u.c.s.f.cksum;
	if (cs == NULL) {
		*cksum_typep = gfarm_digest;
		*cksum_lenp = 0;
		*cksump = NULL;
		*flagsp = flags;
		return (GFARM_ERR_NO_ERROR);
	}
	/* NOTE: These values shoundn't be referered without giant_lock */
	*cksum_typep = cs->type;
	*cksum_lenp = cs->len;
	*cksump = cs->sum;
	*flagsp = flags;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
inode_cksum_get_on_host(struct inode *inode, struct host *spool_host,
	char **cksum_typep, size_t *cksum_lenp, char **cksump,
	gfarm_int32_t *flagsp)
{
	struct inode_activity *ia = inode->u.c.activity;
	struct checksum *cs;
	gfarm_int32_t flags = 0;

	if (!inode_is_file(inode)) {
		gflog_debug(GFARM_MSG_1004327,
			"inode type is not file");
		return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
	}

	/*
	 * already removed, or (once removed and) during replication.
	 * if it's during replication, write_verify will be requested again
	 * after the completion of the replication.
	 */
	if (!inode_has_replica(inode, spool_host))
		return (GFARM_ERR_NO_SUCH_OBJECT);

	/* opened for writing? */
	if (ia != NULL && ia->u.f.writers > 0)
		flags |= GFM_PROTO_CKSUM_GET_MAYBE_EXPIRED;

	/*
	 * NOTE: We don't/can't check GFM_PROTO_CKSUM_GET_EXPIRED here, but
	 * the caller side should have already checked it by using inode gen.
	 */

	cs = inode->u.c.s.f.cksum;
	if (cs == NULL) {
		*cksum_typep = gfarm_digest;
		*cksum_lenp = 0;
		*cksump = NULL;
		*flagsp = flags;
		return (GFARM_ERR_NO_ERROR);
	}
	/* NOTE: These values shoundn't be referered without giant_lock */
	*cksum_typep = cs->type;
	*cksum_lenp = cs->len;
	*cksump = cs->sum;
	*flagsp = flags;
	return (GFARM_ERR_NO_ERROR);
}

static void
xattrs_init(struct xattrs *xattrs)
{
	xattrs->head = xattrs->tail =NULL;
}

static void
xattrs_free_entries(struct xattrs *xattrs)
{
	struct xattr_entry *entry = xattrs->head, *next;
	while (entry != NULL) {
		free(entry->name);
		if (entry->cached_attrvalue != NULL)
			free(entry->cached_attrvalue);
		next = entry->next;
		free(entry);
		entry = next;
	}
	xattrs->head = NULL;
	xattrs->tail = NULL;
}

static void
inode_xattrs_init(struct inode *inode)
{
	xattrs_init(&inode->i_xattrs);
	xattrs_init(&inode->i_xmlattrs);
}

void
inode_xattrs_clear(struct inode *inode)
{
	xattrs_free_entries(&inode->i_xattrs);
	xattrs_free_entries(&inode->i_xmlattrs);
}

static void
remove_all_xattrs(struct inode *inode, int xmlMode)
{
	gfarm_error_t e;
	struct xattrs *xattrs = xmlMode ?
		&inode->i_xmlattrs : &inode->i_xattrs;
	struct xattr_entry *entry = NULL;

	if (xattrs->head == NULL)
		return;

	e = db_xattr_removeall(xmlMode, inode->i_number);
	if (e != GFARM_ERR_OPERATION_NOT_SUPPORTED) {
		if (e != GFARM_ERR_NO_ERROR)
			gflog_debug(GFARM_MSG_1001718,
			    "db_xattr_removeall() failed: %s",
			    gfarm_error_string(e));
		return;
	}
	entry = xattrs->head;
	while (entry != NULL) {
		e = db_xattr_remove(xmlMode, inode->i_number, entry->name);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_warning(GFARM_MSG_1000299, "remove xattr: %s",
				gfarm_error_string(e));
		entry = entry->next;
	}
}

static void
inode_remove_all_xattrs(struct inode *inode)
{
	remove_all_xattrs(inode, 0);
#ifdef ENABLE_XMLATTR
	remove_all_xattrs(inode, 1);
#endif
}

static struct inode_activity *
inode_activity_alloc(struct dirset *tdirset)
{
	struct inode_activity *ia;

	GFARM_MALLOC(ia);
	if (ia == NULL) {
		gflog_error(GFARM_MSG_1004328, "no memory");
		return (NULL);
	}
	/* make circular list `openings' empty */
	ia->openings.opening_prev =
	ia->openings.opening_next = &ia->openings;

	ia->tdirset = tdirset;
	if (tdirset != TDIRSET_IS_UNKNOWN && tdirset != TDIRSET_IS_NOT_SET)
		dirset_add_ref(tdirset);

	ia->u.f.writers = 0;
	ia->u.f.spool_writers = 0;
	ia->u.f.replication_pending = 0;

	ia->u.f.event_waiters = NULL;
	ia->u.f.event_source = NULL;
	ia->u.f.event_type = EVENT_NONE;

	ia->u.f.last_update.tv_sec = 0;
	ia->u.f.last_update.tv_nsec = 0;

	ia->u.f.rstate = NULL;

	return (ia);
}

static struct inode_activity *
inode_activity_alloc_or_update(
	struct inode_activity **iap, struct dirset *tdirset)
{
	struct inode_activity *ia = *iap;

	if (ia == NULL) {
		ia = inode_activity_alloc(tdirset);
		if (ia == NULL)
			return (NULL);
		*iap = ia;
	} else {
		if (ia->tdirset == TDIRSET_IS_UNKNOWN &&
		    tdirset != TDIRSET_IS_UNKNOWN) {
			ia->tdirset = tdirset;
			if (tdirset != TDIRSET_IS_NOT_SET)
				dirset_add_ref(tdirset);
		}
	}
	return (ia);
}

static void
inode_activity_free(struct inode_activity *ia)
{
	assert(ia->openings.opening_next == &ia->openings);
	if (ia->tdirset != TDIRSET_IS_UNKNOWN &&
	    ia->tdirset != TDIRSET_IS_NOT_SET)
		dirset_del_ref(ia->tdirset);
	free(ia);
}

int
inode_activity_free_try(struct inode *inode)
{
	struct inode_activity *ia = inode->u.c.activity;

	if (ia->openings.opening_next == &ia->openings &&
	    ia->u.f.event_type == EVENT_NONE &&
	    ia->u.f.rstate == NULL) {
		inode_activity_free(ia);
		inode->u.c.activity = NULL;
		return (1);
	}
	return (0);
}

void
inode_free_list_init(void)
{
	inode_free_list.u.l.prev =
	inode_free_list.u.l.next = &inode_free_list;
	inode_free_list_initialized = 1;
}

struct inode *
inode_alloc_num(gfarm_ino_t inum)
{
	gfarm_ino_t i;
	struct inode *inode;
	static const char diag[] = "inode_alloc_num";

	if (inum < ROOT_INUMBER)
		return (NULL); /* we don't use 0 and 1 as i_number */
	if (inode_table_size <= inum) {
		gfarm_ino_t new_table_size;
		struct inode **p;

		new_table_size = inode_table_size * INODE_TABLE_SIZE_MULTIPLY;
		if (new_table_size <= inum)
			new_table_size = inum * INODE_TABLE_SIZE_MULTIPLY;
		if (new_table_size < INODE_TABLE_SIZE_INITIAL)
			new_table_size = INODE_TABLE_SIZE_INITIAL;
		GFARM_REALLOC_ARRAY(p, inode_table, new_table_size);
		if (p == NULL) {
			gflog_error(GFARM_MSG_1004329, "%s: no memory", diag);
			return (NULL); /* no memory */
		}
		inode_table = p;

		for (i = inode_table_size; i < new_table_size; i++)
			inode_table[i] = NULL;
		inode_table_size = new_table_size;
	}
	if ((inode = inode_table[inum]) == NULL) {
		GFARM_MALLOC(inode);
		if (inode == NULL) {
			gflog_error(GFARM_MSG_1004330, "%s: no memory", diag);
			return (NULL); /* no memory */
		}
		inode_xattrs_init(inode);

		inode->i_number = inum;
		inode->i_gen = 0;
		inode_table[inum] = inode;

		/* update inode_free_index */
		if (inum == inode_free_index) { /* always true for now */
			while (++inode_free_index < inode_table_size) {
				/* the following is always true for now */
				if (inode_table[inode_free_index] == NULL)
					break;
			}
		}
	} else if (inode->i_mode != INODE_MODE_FREE) {
		assert(0);
		return (NULL); /* the inode is not free */
	} else {
		if (inode->i_number != inum) {
			/* should be gflog_fatal() */
			gflog_error(GFARM_MSG_1004331,
			    "alloc inode(%lld): unexpected inode(%lld:%lld)",
			    (long long)inum,
			    (long long)inode->i_number,
			    (long long)inode->i_gen);
		}
		/* remove from the inode_free_list */
		inode->u.l.next->u.l.prev = inode->u.l.prev;
		inode->u.l.prev->u.l.next = inode->u.l.next;
		inode->i_gen++; /* see inode_undo_alloc() */
	}
	inode->i_nlink_ini = 0;
	inode->u.c.activity = NULL;
	gfarm_mutex_lock(&total_num_inodes_mutex, diag, total_num_inodes_diag);
	++total_num_inodes;
	gfarm_mutex_unlock(&total_num_inodes_mutex,
	    diag, total_num_inodes_diag);
	return (inode);
}

struct inode *
inode_alloc(void)
{
	if (!inode_free_list_initialized)
		inode_free_list_init();

	if (inode_free_list.u.l.next != &inode_free_list)
		return (inode_alloc_num(inode_free_list.u.l.next->i_number));
	else
		return (inode_alloc_num(inode_free_index));
}

static void
inode_clear(struct inode *inode)
{
	static const char diag[] = "inode_clear";

	inode->i_mode = INODE_MODE_FREE;
	inode->i_nlink = inode->i_nlink_ini = 0;
	/* add to the inode_free_list */
	inode->u.l.prev = &inode_free_list;
	inode->u.l.next = inode_free_list.u.l.next;
	inode->u.l.next->u.l.prev = inode;
	inode_free_list.u.l.next = inode;
	inode_xattrs_clear(inode);
	gfarm_mutex_lock(&total_num_inodes_mutex, diag, total_num_inodes_diag);
	--total_num_inodes;
	gfarm_mutex_unlock(&total_num_inodes_mutex,
	    diag, total_num_inodes_diag);
}

static void
inode_undo_alloc(struct inode *inode)
{
	/*
	 * to make inode_db_init() happy.
	 * inode->i_gen++ will be done at next inode_alloc_num(),
	 * and if we don't do inode->i_gen-- here,
	 * the inode->i_gen == 0 check in inode_db_init() won't work correctly.
	 * see SF.net #936
	 */
	inode->i_gen--;

	/*
	 * either inode_clear(inode) or inode_free(inode) should be done
	 * just after inode_undo_alloc()
	 */
}

static void inode_free(struct inode *);

static void
inode_release(struct inode *inode)
{
	gfarm_error_t e;

	inode_free(inode);

	e = db_inode_nlink_modify(inode->i_number, inode->i_nlink);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1000300,
		    "db_inode_nlink_modify(%lld): %s",
		    (unsigned long long)inode->i_number,
		    gfarm_error_string(e));
	e = db_inode_mode_modify(inode->i_number, inode->i_mode);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1000301,
		    "db_inode_mode_modify(%lld): %s",
		    (unsigned long long)inode->i_number,
		    gfarm_error_string(e));
}

static void
inode_tdirset_check(struct inode *inode, struct dirset *tdirset,
	const char *diag)
{
	if (tdirset == TDIRSET_IS_UNKNOWN) {
		gflog_notice(GFARM_MSG_1004666,
		    "%s: inode %lld: unknown dirset, scheduling quota_check",
		    diag, (long long)inode_get_number(inode));
		gfarm_log_backtrace_symbols();
		dirquota_check_schedule();
	}
}

#define SAME_WARNING_TRIGGER	10	/* check reduced mode */
#define SAME_WARNING_THRESHOLD	30	/* more than this -> reduced mode */
#define SAME_WARNING_DURATION	600	/* seconds to measure the limit */
#define SAME_WARNING_INTERVAL	60	/* seconds: interval of reduced log */

static struct gflog_reduced_state rep_removing_retry_state =
	GFLOG_REDUCED_STATE_INITIALIZER(
		SAME_WARNING_TRIGGER,
		SAME_WARNING_THRESHOLD,
		SAME_WARNING_DURATION,
		SAME_WARNING_INTERVAL);

static struct gflog_reduced_state rep_unsatisfied_state =
	GFLOG_REDUCED_STATE_INITIALIZER(
		SAME_WARNING_TRIGGER,
		SAME_WARNING_THRESHOLD,
		SAME_WARNING_DURATION,
		SAME_WARNING_INTERVAL);

static struct gflog_reduced_state rep_rtunavail_state =
	GFLOG_REDUCED_STATE_INITIALIZER(
		SAME_WARNING_TRIGGER,
		SAME_WARNING_THRESHOLD,
		SAME_WARNING_DURATION,
		SAME_WARNING_INTERVAL);

static struct gflog_reduced_state rep_quota_exceeded_state =
	GFLOG_REDUCED_STATE_INITIALIZER(
		SAME_WARNING_TRIGGER,
		SAME_WARNING_THRESHOLD,
		SAME_WARNING_DURATION,
		SAME_WARNING_INTERVAL);

static struct gflog_reduced_state rep_reqfailed_state =
	GFLOG_REDUCED_STATE_INITIALIZER(
		SAME_WARNING_TRIGGER,
		SAME_WARNING_THRESHOLD,
		SAME_WARNING_DURATION,
		SAME_WARNING_INTERVAL);

static struct gflog_reduced_state rep_fewer_state =
	GFLOG_REDUCED_STATE_INITIALIZER(
		SAME_WARNING_TRIGGER,
		SAME_WARNING_THRESHOLD,
		SAME_WARNING_DURATION,
		SAME_WARNING_INTERVAL);

static struct gflog_reduced_state rep_fixed_state =
	GFLOG_REDUCED_STATE_INITIALIZER(
		SAME_WARNING_TRIGGER,
		SAME_WARNING_THRESHOLD,
		SAME_WARNING_DURATION,
		SAME_WARNING_INTERVAL);

/*
 * this function breaks *n_scopep and scope[], and they cannot be used later.
 * this function modifies *n_existingp, existing[], *n_being_removedp
 * and being_removed[] but they may be abled to be used later.
 *
 * srcs[] must be different from existing[].
 */
gfarm_error_t
inode_schedule_replication_within_scope(
	struct inode *inode, struct dirset *tdirset, int n_desired,
	int n_srcs, struct host **srcs, int *next_src_indexp,
	int *n_scopep, struct hostset *scope,
	int *n_existingp, struct hostset *existing, gfarm_time_t grace,
	int *n_being_removedp, struct hostset *being_removed, const char *diag,
	int *req_ok_nump)
{
	gfarm_error_t e, save_e = GFARM_ERR_NO_ERROR;
	struct host **targets, *src, *dst;
	int busy = 0, n_success = 0, n_targets, i, n_valid, shortage;
	struct file_replicating *fr;
	gfarm_off_t necessary_space;

	necessary_space = inode_get_size(inode);
	e = hostset_schedule_n_except(scope, existing, grace, being_removed,
	    host_is_not_busy_and_disk_available_filter, &necessary_space,
	    n_desired, &n_targets, &targets, &n_valid);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_warning(GFARM_MSG_1003693,
		    "%s: inode %lld:%lld: cannot create replicas: "
		    "desired=%d/scope=%d/existing=%d/being_removed=%d: %s",
		    diag, (long long)inode_get_number(inode),
		    (long long)inode_get_gen(inode),
		    n_desired, *n_scopep, *n_existingp, *n_being_removedp,
		    gfarm_error_string(e));
		return (e);
	}

	shortage = n_desired - n_valid;
	if (shortage <= 0)
		return (GFARM_ERR_NO_ERROR); /* sufficient */

	if (shortage > n_targets) {
		if (*n_being_removedp >= shortage - n_targets) {
			/*
			 * #674 - automatic replication may fail
			 * when many replicas of a file are being removed
			 */
			gflog_reduced_info(GFARM_MSG_1005096,
			    &rep_removing_retry_state, "%s: inode %lld:%lld: "
			    "many replicas are being removed: "
			    "desired=%d/scope=%d/existing=%d/being_removed=%d/"
			    "valid=%d/target=%d",
			    diag, (long long)inode_get_number(inode),
			    (long long)inode_get_gen(inode),
			    n_desired, *n_scopep, *n_existingp,
			    *n_being_removedp, n_valid, n_targets);
			save_e = GFARM_ERR_FILE_BUSY; /* retry */
		} else {
			gflog_reduced_info(GFARM_MSG_1005097,
			    &rep_unsatisfied_state, "%s: inode %lld:%lld: "
			    "could not satisfy number of desired replicas: "
			    "desired=%d/scope=%d/existing=%d/being_removed=%d/"
			    "valid=%d/target=%d",
			    diag, (long long)inode_get_number(inode),
			    (long long)inode_get_gen(inode),
			    n_desired, *n_scopep, *n_existingp,
			    *n_being_removedp, n_valid, n_targets);
		}
	}
	/* but, retry is unnecessary when n_desired is too large */

	for (i = 0; i < n_targets; i++) {
		if (*next_src_indexp >= n_srcs)
			*next_src_indexp = 0;
		src = srcs[*next_src_indexp];
		dst = targets[i];

		e = file_replicating_new(inode, dst, src, NULL, tdirset, &fr);
		if (e == GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE) {
			busy = 1;
			gflog_reduced_debug(
			    GFARM_MSG_1003645, &rep_rtunavail_state,
			    "%s: %lld:%lld:%s@%s: file_replicating_new:"
			    " %s", diag,
			    (long long)inode_get_number(inode),
			    (long long)inode_get_gen(inode),
			    user_tenant_name(inode_get_user(inode)),
			    host_name(dst),
			    gfarm_error_string(e));
		} else if (e == GFARM_ERR_DISK_QUOTA_EXCEEDED) {
			gflog_reduced_info(
			    GFARM_MSG_1004332, &rep_quota_exceeded_state,
			    "%s: %lld:%lld:%s@%s: replication failed:"
			    " %s", diag,
			    (long long)inode_get_number(inode),
			    (long long)inode_get_gen(inode),
			    user_tenant_name(inode_get_user(inode)),
			    host_name(dst),
			    gfarm_error_string(e));
			save_e = e;
			break;
		} else if (e != GFARM_ERR_NO_ERROR) {
			gflog_reduced_warning(
			    GFARM_MSG_1003705, &rep_reqfailed_state,
			    "%s: %lld:%lld:%s@%s: replication failed:"
			    " %s", diag,
			    (long long)inode_get_number(inode),
			    (long long)inode_get_gen(inode),
			    user_tenant_name(inode_get_user(inode)),
			    host_name(dst),
			    gfarm_error_string(e));

			/* prefer GFARM_ERR_NO_MEMORY to the first error */
			if (save_e == GFARM_ERR_NO_ERROR ||
			    e == GFARM_ERR_NO_MEMORY)
				save_e = e;
		} else if ((e = inode_replication_request(inode, fr,
		    diag)) != GFARM_ERR_NO_ERROR) {
			/* this case, inode_replication_request() may sleep */

			/* prefer GFARM_ERR_NO_MEMORY to the first error */
			if (save_e == GFARM_ERR_NO_ERROR ||
			    e == GFARM_ERR_NO_MEMORY)
				save_e = e;
		} else if ((e = hostset_add_host(existing, dst)) !=
		    GFARM_ERR_NO_ERROR) {
			/* prefer GFARM_ERR_NO_MEMORY to the first error */
			if (save_e == GFARM_ERR_NO_ERROR ||
			    e == GFARM_ERR_NO_MEMORY)
				save_e = e;
		} else {
			(*n_existingp)++;
			n_success++;
		}
	}
	free(targets);

	*req_ok_nump += n_success;

	if (busy) /* retry immediately in replica_check */
		return (GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE);

	if (shortage > n_success)
		gflog_reduced_notice(GFARM_MSG_1003694, &rep_fewer_state,
		    "%s: %lld:%lld:%s: fewer replicas, "
		    "increase=%d/before=%d/desire=%d", diag,
		    (long long)inode_get_number(inode),
		    (long long)inode_get_gen(inode),
		    user_tenant_name(inode_get_user(inode)),
		    n_success, n_valid, n_desired);
	else
		gflog_reduced_debug(GFARM_MSG_1003695, &rep_fixed_state,
		    "%s: %lld:%lld:%s: will be fixed, increase=%d/desire=%d",
		    diag, (long long)inode_get_number(inode),
		    (long long)inode_get_gen(inode),
		    user_tenant_name(inode_get_user(inode)),
		    n_success, n_desired);

	return (save_e);
}

/*
 * this function modifies *n_existingp, existing[], *n_being_removedp
 * and being_removed[] but they may be abled to be used later.
 *
 * srcs[] must be different from existing[].
 */
static gfarm_error_t
inode_schedule_replication_from_all(
	struct inode *inode, struct dirset *tdirset, int n_desired,
	int n_srcs, struct host **srcs,
	int *n_existingp, struct hostset *existing, gfarm_time_t grace,
	int *n_being_removedp, struct hostset *being_removed, const char *diag,
	int *req_ok_nump)
{
	gfarm_error_t e;
	int n_all_hosts, next_src_index;
	struct hostset *all_hosts;

	all_hosts = hostset_of_all_hosts_alloc(&n_all_hosts);
	if (all_hosts == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_warning(GFARM_MSG_1003646, "%s: inode %lld:%lld: "
		    "cannot create %d replicas except %d: %s",
		    diag,
		    (long long)inode_get_number(inode),
		    (long long)inode_get_gen(inode),
		    n_desired, *n_existingp, gfarm_error_string(e));
		return (e);
	}
	next_src_index = host_select_one(n_srcs, srcs, diag);
	e = inode_schedule_replication_within_scope(
	    inode, tdirset, n_desired, n_srcs, srcs, &next_src_index,
	    &n_all_hosts, all_hosts, n_existingp, existing, grace,
	    n_being_removedp, being_removed, diag, req_ok_nump);
	hostset_free(all_hosts);
	return (e);
}

/*
 * this function modifies *n_existingp, existing[], *n_being_removedp
 * and being_removed[] but they may be abled to be used later.
 *
 * srcs[] must be different from existing[].
 */
gfarm_error_t
inode_schedule_replication(
	struct inode *inode, struct dirset *tdirset, int is_replica_check,
	int n_desired, const char *repattr,
	int n_srcs, struct host **srcs,
	int *n_existingp, struct hostset *existing, gfarm_time_t grace,
	int *n_being_removedp, struct hostset *being_removed, const char *diag,
	int *req_ok_nump)
{
	gfarm_error_t e;
	int total_repattr;

	if (repattr != NULL) {
		if (debug_mode)
			gflog_debug(GFARM_MSG_1004014,
			    "%s: about to schedule "
			    "repattr-based replication for inode "
			    "%lld:%lld@%s.", diag,
			    (long long)inode_get_number(inode),
			    (long long)inode_get_gen(inode),
			    host_name(srcs[0]));
		e = fsngroup_schedule_replication(
		    inode, tdirset, repattr, n_srcs, srcs,
		    n_existingp, existing, grace,
		    n_being_removedp, being_removed, diag, &total_repattr,
		    req_ok_nump);
		if (is_replica_check &&
		    e == GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE) {
			/* retry in replica_check */
			return (e);
		}

		/* gfarm.ncopy vs gfarm.replicainfo */
		if (n_desired < total_repattr)
			n_desired = total_repattr;
	}
	gflog_debug(GFARM_MSG_1003648,
	    "%s: about to schedule "
	    "ncopy-based replication for inode %lld:%lld@%s. "
	    "number = %d (= %d - %d + %d)", diag,
	    (long long)inode_get_number(inode),
	    (long long)inode_get_gen(inode),
	    host_name(srcs[0]),
	    n_desired - *n_existingp,
	    n_desired, *n_existingp + *n_being_removedp,
	    *n_being_removedp);
	e = inode_schedule_replication_from_all(
	    inode, tdirset, n_desired,
	    n_srcs, srcs, n_existingp, existing, grace,
	    n_being_removedp, being_removed, diag, req_ok_nump);

	return (e);
}

/*
 * NOTE: excluding_list may or may not include spool_host.
 *	it doesn't if this is called from update_replicas(), but
 *	it does if this this is called from inode_check_pending_replication().
 */
static gfarm_error_t
make_replicas_except(struct inode *inode, struct dirset *tdirset,
	struct host *spool_host, int desired_replica_number, char *repattr,
	struct file_copy *exception_list)
{
	/* make CPPFLAGS='-DDEBUG_REPLICA_CHECK_ENQUEUE' */
#ifdef DEBUG_REPLICA_CHECK_ENQUEUE
	gflog_warning(GFARM_MSG_UNFIXED,
	    "DEBUG_REPLICA_CHECK_ENQUEUE enabled");
	return (GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE);
#else /* DEBUG_REPLICA_CHECK_ENQUEUE */
	gfarm_error_t e;
	struct hostset *existing, *being_removed;
	int n_existing = 1; /* +1 is for spool_host */
	int n_being_removed = 0;
	struct host *srcs[1];
	struct file_copy *copy;
	int req_ok_num = 0;
	static const char diag[] = "make_replicas_except";

	existing = hostset_empty_alloc();
	if (existing == NULL) {
		gflog_error(GFARM_MSG_1005061,
		    "%s: no memory to schedule replicas: existing hostset",
		    diag);
		return (GFARM_ERR_NO_MEMORY);
	}
	being_removed = hostset_empty_alloc();
	if (being_removed == NULL) {
		hostset_free(existing);
		gflog_error(GFARM_MSG_1005062,
		    "%s: no memory to schedule replicas: "
		    "being_removed hostset",
		    diag);
		return (GFARM_ERR_NO_MEMORY);
	}

	e = hostset_add_host(existing, spool_host);
	if (e != GFARM_ERR_NO_ERROR) {
		hostset_free(existing);
		hostset_free(being_removed);
		return (e);
	}

	/*
	 * this is usually faster than calling hostset_alloc_by(), because
	 * number of replicas is usually far fewer than number of hosts
	 */
	for (copy = exception_list; copy != NULL; copy = copy->host_next) {
		if (copy->host != spool_host) {
			if (FILE_COPY_IS_BEING_REMOVED(copy)) {
				++n_being_removed;
				e = hostset_add_host(being_removed,
				    copy->host);
			} else {
				++n_existing; /* include replicating */
				e = hostset_add_host(existing, copy->host);
			}
			if (e != GFARM_ERR_NO_ERROR) {
				hostset_free(existing);
				hostset_free(being_removed);
				return (e);
			}
		}
	}

	srcs[0] = spool_host;
	e = inode_schedule_replication(
	    inode, tdirset, 0, desired_replica_number, repattr,
	    1, srcs, &n_existing, existing, 0,
	    &n_being_removed, being_removed, diag, &req_ok_num);

	hostset_free(existing);
	hostset_free(being_removed);

	return (e);
#endif /* DEBUG_REPLICA_CHECK_ENQUEUE */
}

static gfarm_error_t
remove_replica_entity(struct inode *, gfarm_int64_t, struct host *,
	int, struct dirset *, struct dead_file_copy **);

/* spool_host may be NULL, if GFARM_FILE_TRUNC_PENDING */
static void
update_replicas(struct inode *inode, struct host *spool_host,
	gfarm_int64_t old_gen, int start_replication,
	int desired_replica_number, char *repattr, const char *diag)
{
	struct file_copy **copyp, *copy, *next, *to_be_excluded = NULL;
	struct dirset *tdirset = inode_get_tdirset(inode);
	struct dead_file_copy *deferred_cleanup;
	struct file_replicating *fr;
	gfarm_error_t e, save_e = GFARM_ERR_NO_ERROR;

	/*
	 * First of all, about each and every host having replica of
	 * the inode:
	 *
	 *	If replication is required, count up how many replicas
	 *	we have at this moment and make a list of them.
	 *
	 *	If replication is not required, remove existing
	 *	replica(s) if it is not yet removed.
	 */
	for (copyp = &inode->u.c.s.f.copies; (copy = *copyp) != NULL; ) {
		if (copy->host == spool_host) {
			copyp = &copy->host_next;
			continue;
		}
		*copyp = copy->host_next;

		if (start_replication && spool_host != NULL) {
			/*
			 * since file_replicating_new() changes
			 * inode->u.c.s.f.copies via inode_add_replica(),
			 * it has to be called at outside of this loop.
			 */
			copy->host_next = to_be_excluded;
			to_be_excluded = copy;
		} else {
			if (FILE_COPY_IS_VALID(copy) &&
			    !FILE_COPY_IS_BEING_REMOVED(copy)) {
				e = remove_replica_entity(
				    inode, old_gen, copy->host,
				    FILE_COPY_IS_VALID(copy), tdirset, NULL);
				/* abandon `e' */
			} else { /* dead_file_copy must be already created */
				assert(!FILE_COPY_IS_VALID(copy));
			}
			free(copy);
		}
	}
	/*
	 * For now the to_be_excluded contains hosts which have to be
	 * excluded from the replication destination candidate list
	 * since the hosts in the to_be_excluded had a replica of the
	 * old contents and the specification says:
	 *
	 *	If a replica is created on a host, the host must be
	 *	kept having the replica of the file ever after.
	 *
	 * So we have to avoid to schedule creation of a replica on
	 * them, at this moment.
	 *
	 * NOTE:
	 * this must be called after the loop above, because
	 * the above loop obsoletes all replicas except one on the spool_host.
	 *
	 */
	if (start_replication && spool_host != NULL)
		save_e = make_replicas_except(inode, tdirset, spool_host,
		    desired_replica_number, repattr, to_be_excluded);

	/*
	 * After scheduling replica creation to new hosts, start
	 * updation of the existing replicas on the hosts in the
	 * to_be_excluded VERY HERE.
	 */
	for (copy = to_be_excluded; copy != NULL; copy = next) {
		/*
		 * if there is ongoing replication, don't start new one
		 *
		 * XXX FIXME: well, it's better to schedule new replication
		 * even in that case unless it's currently being removed.
		 */
		if (!FILE_COPY_IS_VALID(copy) ||
		    !host_is_up(copy->host) ||
		    !host_supports_async_protocols(copy->host)) {
			if (FILE_COPY_IS_VALID(copy) &&
			    !FILE_COPY_IS_BEING_REMOVED(copy)) {
				e = remove_replica_entity(
				    inode, old_gen, copy->host,
				    FILE_COPY_IS_VALID(copy), tdirset, NULL);
				/* abandon `e' */
			}
		} else if (!FILE_COPY_IS_BEING_REMOVED(copy)) {
			assert(FILE_COPY_IS_VALID(copy));
			e = remove_replica_entity(inode, old_gen, copy->host,
			    FILE_COPY_IS_VALID(copy), tdirset,
			    &deferred_cleanup);
			/* abandon `e' */

			e = file_replicating_new(inode, copy->host, spool_host,
			    deferred_cleanup, tdirset, &fr);
			if (e != GFARM_ERR_NO_ERROR) {
				gflog_notice(GFARM_MSG_1002245,
				    "replication before removal: host %s: %s",
				    host_name(copy->host),
				    gfarm_error_string(e));
				/*
				 * Give up the replication and remove
				 * the old one
				 */
				removal_pendingq_enqueue(deferred_cleanup);

				/*
				 * prefer GFARM_ERR_NO_MEMORY to the
				 * first error
				 */
				if (save_e == GFARM_ERR_NO_ERROR ||
				    e == GFARM_ERR_NO_MEMORY)
					save_e = e;
			} else if ((e = inode_replication_request(
			    inode, fr, diag)) != GFARM_ERR_NO_ERROR) {
				/*
				 * in this case,
				 * inode_replication_request() may sleep,
				 * and it frees `fr' internally.
				 */

				/*
				 * prefer GFARM_ERR_NO_MEMORY to the
				 * first error
				 */
				if (save_e == GFARM_ERR_NO_ERROR ||
				    e == GFARM_ERR_NO_MEMORY)
					save_e = e;
			}
		}

		next = copy->host_next;
		free(copy);
	}

	/*
	 * #464 - retry automatic replication after
	 * GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE
	 *
	 * #673 - retry automatic replication when a request of
	 * replication is failure
	 */
	if (IS_REPLICA_CHECK_REQUIRED(save_e)) {
		replica_check_enqueue(inode, tdirset,
		    desired_replica_number, repattr, diag);
		/*
		 * Starts replica_check thread even though there is no
		 * need to check all files.
		 */
		replica_check_start_rep_request_failed();
	}
}

static void
inode_remove(struct inode *inode, struct dirset *tdirset)
{
	int dfc_needs_free = 0;

	if (gfarm_read_only_mode()) {
		gflog_warning(GFARM_MSG_1005163, "inode %llu:%llu: "
		    "cannot remove unreferenced inode due to read_only",
		    (long long)inode_get_number(inode),
		    (long long)inode_get_gen(inode));
		return;
	}

	inode_remove_all_xattrs(inode);

	if (inode->u.c.activity != NULL)
		gflog_fatal(GFARM_MSG_1000302, "inode_remove: still opened");

	if (inode_is_file(inode)) {
		struct file_copy *copy, *cn;

		for (copy = inode->u.c.s.f.copies; copy != NULL; copy = cn) {
			if (FILE_COPY_IS_VALID(copy) &&
			    !FILE_COPY_IS_BEING_REMOVED(copy)) {
				(void)remove_replica_entity(inode,
				    inode->i_gen, copy->host,
				    FILE_COPY_IS_VALID(copy), tdirset, NULL);
				/* abandon error */
			}
			cn = copy->host_next;
			free(copy);
		}
		inode->u.c.s.f.copies = NULL; /* ncopy == 0 */
		inode_cksum_remove(inode);
		dfc_needs_free = 1;
	} else if (inode_is_dir(inode) || inode_is_symlink(inode)) {
		/*
		 * inode_release() will call inode_free(), and
		 * inode_free() will do enough job.
		 */
	} else {
		gflog_fatal(GFARM_MSG_1002800,
		    "inode_unlink(%llu): unknown inode type: 0%o",
		    (unsigned long long)inode->i_number, inode->i_mode);
		/*NOTREACHED*/
	}
	quota_update_file_remove(inode, tdirset);
	inode_release(inode);

	if (dfc_needs_free)
		dead_file_copy_inode_status_changed(inode->i_number);
}

static int
inode_remove_try(struct inode *inode, struct dirset *tdirset)
{
	if (inode->i_nlink == 0 && inode->u.c.activity == NULL) {
		/* this file is not currently used, i.e. removable */
		inode_remove(inode, tdirset);
		return (1);
	}
	return (0);
}

static gfarm_error_t
inode_init_dir_internal(struct inode *inode)
{
	/* if this function is modified, update inode_free() too */

	inode->u.c.s.d.entries = dir_alloc();
	if (inode->u.c.s.d.entries == NULL) {
		gflog_debug(GFARM_MSG_1001722,
			"inode entries is NULL");
		return (GFARM_ERR_NO_MEMORY);
	}
	inode->u.c.s.d.parent_dir = NULL;

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
inode_init_dir(struct inode *inode, struct inode *parent)
{
	gfarm_error_t e;
	DirEntry entry;

	e = inode_init_dir_internal(inode);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001723,
			"inode_init_dir_internal() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	/*
	 * We won't do db_direntry_add() here to make LDAP happy.
	 * See the comment in inode_lookup_basename().
	 */

	entry = dir_enter(inode->u.c.s.d.entries, DOT, DOT_LEN, NULL);
	if (entry == NULL) {
		dir_free(inode->u.c.s.d.entries);
		gflog_debug(GFARM_MSG_1001724,
			"inode entries is NULL");
		return (GFARM_ERR_NO_MEMORY);
	}
	dir_entry_set_inode(entry, inode);

	entry = dir_enter(inode->u.c.s.d.entries, DOTDOT, DOTDOT_LEN, NULL);
	if (entry == NULL) {
		dir_free(inode->u.c.s.d.entries);
		gflog_debug(GFARM_MSG_1001725,
			"inode entries is NULL");
		return (GFARM_ERR_NO_MEMORY);
	}
	dir_entry_set_inode(entry, parent);

	inode->i_nlink = 2;
	inode->i_mode = GFARM_S_IFDIR;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
inode_init_file(struct inode *inode)
{
	/* if this function is modified, update inode_free() too */

	inode->i_nlink = 1;
	inode->i_mode = GFARM_S_IFREG;
	inode->u.c.s.f.copies = NULL;
	inode->u.c.s.f.cksum = NULL;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
inode_init_symlink(struct inode *inode, char *source_path)
{
	/* if this function is modified, update inode_clear_symlink() too */

	static const char diag[] = "inode_init_symlink";

	if (source_path != NULL) {
		source_path = strdup_log(source_path, diag);
		if (source_path == NULL)
			return (GFARM_ERR_NO_MEMORY);
	}
	inode->i_nlink = 1;
	inode->i_mode = GFARM_S_IFLNK;
	inode->u.c.s.l.source_path = source_path;
	return (GFARM_ERR_NO_ERROR);
}

void
inode_clear_symlink(struct inode *inode)
{
	/* should be consistent with inode_init_symlink() */

	free(inode->u.c.s.l.source_path);
	inode->u.c.s.l.source_path = NULL;
}

static void
inode_free(struct inode *inode)
{
	switch (GFARM_S_IFMT & inode->i_mode) {
	case GFARM_S_IFDIR:
		/* should be consistent with inode_init_dir_internal() */
		dir_free(inode->u.c.s.d.entries);
		inode->u.c.s.d.entries = NULL;
		break;
	case GFARM_S_IFREG:
		/* should be consistent with inode_init_file() */
		/* must be no file_copy */
		break;
	case GFARM_S_IFLNK:
		inode_clear_symlink(inode);
		break;
	}

	inode_clear(inode);
}

gfarm_error_t
inode_check_file(struct inode *inode)
{
	if (inode_is_file(inode))
		return (GFARM_ERR_NO_ERROR);
	else if (inode_is_dir(inode))
		return (GFARM_ERR_IS_A_DIRECTORY);
	else if (inode_is_symlink(inode))
		return (GFARM_ERR_IS_A_SYMBOLIC_LINK);
	else
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
}

gfarm_ino_t
inode_root_number()
{
	return (ROOT_INUMBER);
}

gfarm_ino_t
inode_table_current_size()
{
	return (inode_table_size);
}

struct inode *
inode_lookup(gfarm_ino_t inum)
{
	struct inode *inode;

	if (inum >= inode_table_size)
		return (NULL);
	inode = inode_table[inum];
	if (inode == NULL)
		return (NULL);
	if (inode->i_mode == INODE_MODE_FREE)
		return (NULL);
	return (inode);
}

void
inode_lookup_all(void *closure, void (*callback)(void *, struct inode *))
{
	gfarm_ino_t i;

	for (i = ROOT_INUMBER; i < inode_table_size; i++) {
		if (inode_table[i] != NULL &&
		    inode_table[i]->i_mode != INODE_MODE_FREE)
			callback(closure, inode_table[i]);
	}
}

int
inode_is_dir(struct inode *inode)
{
	return (GFARM_S_ISDIR(inode->i_mode));
}

int
inode_is_file(struct inode *inode)
{
	return (GFARM_S_ISREG(inode->i_mode));
}

int
inode_is_symlink(struct inode *inode)
{
	return (GFARM_S_ISLNK(inode->i_mode));
}

gfarm_ino_t
inode_get_number(struct inode *inode)
{
	return (inode->i_number);
}

gfarm_int64_t
inode_get_gen(struct inode *inode)
{
	return (inode->i_gen);
}

void
inode_increment_gen(struct inode *inode)
{
	gfarm_error_t e;
	int xmlmode;

	++inode->i_gen;

	e = db_inode_gen_modify(inode->i_number, inode->i_gen);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1002246,
		    "db_inode_gen_modify(%lld, %lld): %s",
		    (unsigned long long)inode->i_number,
		    (unsigned long long)inode->i_gen,
		    gfarm_error_string(e));

	/* remove gfarm.md5 */
	xmlmode = 0;
	e = inode_xattr_remove(inode, xmlmode, xattr_md5);
	if (e == GFARM_ERR_NO_ERROR)
		db_xattr_remove(xmlmode, inode_get_number(inode), xattr_md5);
}

gfarm_int64_t
inode_get_nlink(struct inode *inode)
{
	return (inode->i_nlink);
}

static gfarm_int64_t
inode_get_nlink_ini(struct inode *inode)
{
	return (inode->i_nlink_ini);
}

static void
inode_increment_nlink_ini(struct inode *inode)
{
	++inode->i_nlink_ini;
}

static void
inode_decrement_nlink_ini(struct inode *inode)
{
	--inode->i_nlink_ini;
}

struct user *
inode_get_user(struct inode *inode)
{
	return (inode->i_user);
}

struct group *
inode_get_group(struct inode *inode)
{
	return (inode->i_group);
}

int
inode_has_no_replica(struct inode *inode)
{
	struct file_copy *copy;

	for (copy = inode->u.c.s.f.copies; copy != NULL;
	    copy = copy->host_next) {
		if (FILE_COPY_IS_VALID(copy))
			return (0);
	}
	return (1);
}

/* if is_valid == 0, FILE_COPY_BEING_REMOVED copies are included */
static gfarm_int64_t
inode_get_ncopy_common(struct inode *inode, int is_valid, int is_up)
{
	struct file_copy *copy;
	gfarm_int64_t n = 0;

	if (!inode_is_file(inode)) {
		gflog_error(GFARM_MSG_1004333, "internal error: not a file");
		return (0);
	}
	for (copy = inode->u.c.s.f.copies; copy != NULL;
	    copy = copy->host_next) {
		if ((is_valid ? FILE_COPY_IS_VALID(copy) : 1) &&
		    (is_up ? host_is_up(copy->host) : 1))
			n++;
	}
	return (n);
}

gfarm_int64_t
inode_get_ncopy(struct inode *inode)
{
	return (inode_get_ncopy_common(inode, 1, 1));
}

gfarm_int64_t
inode_get_ncopy_with_dead_host(struct inode *inode)
{
	return (inode_get_ncopy_common(inode, 1, 0));
}

/* if valid_only == 0, incomplete replicas are included */
int
inode_count_replicas_within_scope(
	struct inode *inode, int valid_only, int up_only,
	gfarm_time_t host_down_grace, struct hostset *scope)
{
	int count = 0;
	struct file_copy *copy;

	/*
	 * this is usually faster than calling hostset_alloc_by(host_is_up,)
	 * then calling hostset_intersect() and hostset_count_hosts(), because
	 * number of replicas is usually far fewer than number of hosts.
	 */
	for (copy = inode->u.c.s.f.copies;
	    copy != NULL; copy = copy->host_next) {
		if ((valid_only ?
		     FILE_COPY_IS_VALID(copy) :
		     !FILE_COPY_IS_BEING_REMOVED(copy))
		    &&
		    (!up_only ||
		     host_is_up_with_grace(copy->host, host_down_grace))
		    &&
		    hostset_has_host(scope, copy->host))
			++count;
	}
	return (count);
}

/* if scope != NULL, only hosts in scope is selected, otherwise don't care */
static gfarm_error_t
inode_alloc_file_copy_hosts_within_scope(struct inode *inode,
	int (*filter)(struct file_copy *, void *), void *closure,
	struct hostset *scope,
	gfarm_int32_t *np, struct host ***hostsp)
{
	struct file_copy *copy;
	int i, nhosts;
	struct host **hosts;

	assert(inode_is_file(inode));

	nhosts = 0;
	for (copy = inode->u.c.s.f.copies; copy != NULL;
	    copy = copy->host_next)
		nhosts++; /* include !host_is_up() */
	if (nhosts == 0) {
		*np = 0;
		*hostsp = NULL;
		return (GFARM_ERR_NO_ERROR);
	}

	/*
	 * need to remember the hosts, because results of
	 * (*filter)() (i.e. host_is_up()/host_is_disk_available(), ...)
	 * may change even while the giant lock is held.
	 */
	GFARM_MALLOC_ARRAY(hosts, nhosts);
	if (hosts == NULL) {
		gflog_error(GFARM_MSG_1004335, "no memory");
		return (GFARM_ERR_NO_MEMORY);
	}
	i = 0;
	for (copy = inode->u.c.s.f.copies; copy != NULL;
	    copy = copy->host_next) {
		if (i >= nhosts)
			break;
		if ((scope == NULL || hostset_has_host(scope, copy->host)) &&
		    (*filter)(copy, closure))
			hosts[i++] = copy->host;
	}
	*np = i; /* NOTE: this may be 0 */
	*hostsp = hosts;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
inode_alloc_file_copy_hosts(struct inode *inode,
	int (*filter)(struct file_copy *, void *), void *closure,
	gfarm_int32_t *np, struct host ***hostsp)
{
	return (inode_alloc_file_copy_hosts_within_scope(
	    inode, filter, closure, NULL, np, hostsp));
}

static gfarm_error_t
hostset_of_file_copy_alloc(struct inode *inode,
	int (*filter)(struct file_copy *, void *), void *closure,
	const char *diag,
	struct hostset **hsp)
{
	gfarm_error_t e;
	struct hostset *hs = hostset_empty_alloc();
	struct file_copy *copy;

	if (hs == NULL) {
		gflog_error(GFARM_MSG_1005063,
		    "%s: no memory of file_copy hostset", diag);
		return (GFARM_ERR_NO_MEMORY);
	}
	for (copy = inode->u.c.s.f.copies; copy != NULL;
	    copy = copy->host_next) {
		if ((*filter)(copy, closure)) {
			e = hostset_add_host(hs, copy->host);
			if (e != GFARM_ERR_NO_ERROR) {
				gflog_error(GFARM_MSG_1005064,
				    "%s: add host(%s): %s",
				    diag, host_name(copy->host),
				    gfarm_error_string(e));
				hostset_free(hs);
				return (e);
			}
		}
	}
	*hsp = hs;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
hostset_of_file_opening_alloc(struct inode *inode,
	int (*filter)(struct host *, void *), void *closure, const char *diag,
	struct hostset **hsp)
{
	gfarm_error_t e;
	struct hostset *hs = hostset_empty_alloc();
	struct inode_activity *ia = inode->u.c.activity;
	struct file_opening *fo;

	if (hs == NULL) {
		gflog_error(GFARM_MSG_1005065,
		    "%s: no memory of file opening hostset", diag);
		return (GFARM_ERR_NO_MEMORY);
	}

	if (ia == NULL) {
		*hsp = hs; /* empty hostset */
		return (GFARM_ERR_NO_ERROR);
	}

	fo = ia->openings.opening_next;
	if (fo == &ia->openings) {
		*hsp = hs; /* empty hostset */
		return (GFARM_ERR_NO_ERROR);
	}

	for (; fo != &ia->openings; fo = fo->opening_next) {
		if (fo->u.f.spool_host != NULL &&
		    (*filter)(fo->u.f.spool_host, closure)) {
			e = hostset_add_host(hs, fo->u.f.spool_host);
			if (e != GFARM_ERR_NO_ERROR) {
				gflog_error(GFARM_MSG_1005066,
				    "%s: add host(%s): %s",
				    diag, host_name(fo->u.f.spool_host),
				    gfarm_error_string(e));
				hostset_free(hs);
				return (e);
			}
		}
	}
	*hsp = hs;
	return (GFARM_ERR_NO_ERROR);
}

/* NOTE: `existing' includes incomplete replicas as well */
gfarm_error_t
inode_replica_hostset(
	struct inode *inode,
	gfarm_int32_t *n_existingp, struct hostset **existingp,
	gfarm_int32_t *n_removingp, struct hostset **removingp)
{
	gfarm_error_t e;
	struct file_copy *copy;
	int n_existing, n_removing;
	struct hostset *existing, *removing;

	assert(inode_is_file(inode));

	existing = hostset_empty_alloc();
	if (existing == NULL) {
		gflog_error(GFARM_MSG_1004336, "no memory");
		return (GFARM_ERR_NO_MEMORY);
	}
	removing = hostset_empty_alloc();
	if (removing == NULL) {
		hostset_free(existing);
		gflog_error(GFARM_MSG_1004337, "no memory");
		return (GFARM_ERR_NO_MEMORY);
	}

	n_existing = 0;
	n_removing = 0;
	for (copy = inode->u.c.s.f.copies; copy != NULL;
	    copy = copy->host_next) {
		if (FILE_COPY_IS_BEING_REMOVED(copy)) {
			e = hostset_add_host(removing, copy->host);
			n_removing++;
		} else {
			e = hostset_add_host(existing, copy->host);
			n_existing++;
		}
		if (e != GFARM_ERR_NO_ERROR) {
			hostset_free(removing);
			hostset_free(existing);
			gflog_error(GFARM_MSG_1005067,
			    "inode_replica_hostset: %s",
			    gfarm_error_string(e));
			return (e);
		}
	}
	*n_existingp = n_existing;
	*n_removingp = n_removing;
	*existingp = existing;
	*removingp = removing;
	return (GFARM_ERR_NO_ERROR);
}

static int
file_copy_is_valid_and_up(struct file_copy *copy, void *closure)
{
	return (FILE_COPY_IS_VALID(copy) && host_is_up(copy->host));
}

static int
file_copy_is_valid_and_disk_available(struct file_copy *copy, void *closure)
{
	gfarm_off_t *sizep = closure;

	return (FILE_COPY_IS_VALID(copy) &&
	    host_is_disk_available(copy->host, *sizep));
}

static int
file_copy_is_invalid(struct file_copy *copy, void *closure)
{
	return (!FILE_COPY_IS_VALID(copy));
}


gfarm_mode_t
inode_get_mode(struct inode *inode)
{
	return (inode->i_mode);
}

/*
 * NOTE:
 * only db_journal_apply_inode_mode_modify() is allowed to call this function.
 */
void
inode_set_mode_in_cache(struct inode *inode, gfarm_mode_t mode)
{
	if (mode == INODE_MODE_FREE) {
		quota_update_file_remove(inode, TDIRSET_IS_UNKNOWN);
		inode_free(inode);
		return;
	}

	if (inode->i_mode != INODE_MODE_FREE &&
	    (mode & GFARM_S_IFMT) != (inode->i_mode & GFARM_S_IFMT)) {
		gflog_warning(GFARM_MSG_1005164,
		    "inode %llu:%llu: unexpected inode type change: 0%o->0%o",
		    (long long)inode_get_number(inode),
		    (long long)inode_get_gen(inode),
		    (int)inode->i_mode, (int)mode);
	}

	inode->i_mode = mode;
}

gfarm_error_t
inode_set_mode(struct inode *inode, gfarm_mode_t mode)
{
	gfarm_error_t e;

	if ((mode & GFARM_S_IFMT) != 0) {
		gflog_debug(GFARM_MSG_1001726,
			"argument 'mode' is invalid");
		return (GFARM_ERR_INVALID_ARGUMENT);
	}
	inode->i_mode = (inode->i_mode & GFARM_S_IFMT) |
	    (mode & GFARM_S_ALLPERM);

	inode_status_changed(inode);
	e = db_inode_mode_modify(inode->i_number, inode->i_mode);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1000304,
		    "db_inode_mode_modify(%lld): %s",
		    (unsigned long long)inode->i_number,
		    gfarm_error_string(e));

	return (GFARM_ERR_NO_ERROR);
}

gfarm_off_t
inode_get_size(struct inode *inode)
{
	return (inode->i_size);
}

static void
inode_set_size_internal(struct inode *inode, struct dirset *tdirset,
	gfarm_off_t size)
{
	/* inode is file */
	quota_update_file_resize(inode, tdirset, size);

	inode->i_size = size;
}

void
inode_set_size_in_cache(struct inode *inode, gfarm_off_t size)
{
	/*
	 * TDIRSET_IS_UNKNOWN is OK,
	 * because dirquota_check will be done after becoming master
	 */
	inode_set_size_internal(inode, TDIRSET_IS_UNKNOWN, size);
}

static void
inode_set_size(struct inode *inode, struct dirset *tdirset, gfarm_off_t size)
{
	gfarm_error_t e;

	inode_set_size_internal(inode, tdirset, size);

	e = db_inode_size_modify(inode->i_number, inode->i_size);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1000305,
		    "db_inode_size_modify(%lld): %s",
		    (unsigned long long)inode->i_number,
		    gfarm_error_string(e));
}

void
inode_set_nlink_in_cache(struct inode *inode, gfarm_uint64_t nlink)
{
	inode->i_nlink = nlink;
}

void
inode_set_gen_in_cache(struct inode *inode, gfarm_uint64_t gen)
{
	inode->i_gen = gen;
}

static void
inode_set_user_by_name(struct inode *inode, const char *username)
{
	inode->i_user = user_tenant_lookup_or_enter_invalid(username);
}

static void
inode_set_group_by_name(struct inode *inode, const char *groupname)
{
	inode->i_group = group_tenant_lookup_or_enter_invalid(groupname);
}

void
inode_set_user_by_name_in_cache(struct inode *inode, const char *username)
{
	/* chown won't change dirquota, so passing TDIRSET_IS_NOT_SET is OK */
	quota_update_file_remove(inode, TDIRSET_IS_NOT_SET);

	inode_set_user_by_name(inode, username);

	/* chown won't change dirquota, so passing TDIRSET_IS_NOT_SET is OK */
	quota_update_file_add(inode, TDIRSET_IS_NOT_SET);
}

void
inode_set_group_by_name_in_cache(struct inode *inode, const char *groupname)
{
	/* chown won't change dirquota, so passing TDIRSET_IS_NOT_SET is OK */
	quota_update_file_remove(inode, TDIRSET_IS_NOT_SET);

	inode_set_group_by_name(inode, groupname);

	/* chown won't change dirquota, so passing TDIRSET_IS_NOT_SET is OK */
	quota_update_file_add(inode, TDIRSET_IS_NOT_SET);
}

gfarm_error_t
inode_set_owner(struct inode *inode, struct user *user, struct group *group)
{
	gfarm_error_t e;
	int change_user = 0, change_group = 0;
	gfarm_int64_t ncopy = 0;

	if (user == NULL && group == NULL)
		return (GFARM_ERR_NO_ERROR); /* shortcut */

	if (user != NULL && user != inode_get_user(inode))
		change_user = 1;
	if (group != NULL && group != inode_get_group(inode))
		change_group = 1;
	if (inode_is_file(inode))
		ncopy = inode_get_ncopy_with_dead_host(inode);
	/* chown won't change dirquota, so passing tdirset==NULL is OK here */
	e = quota_limit_check(
	    change_user ? user : NULL, change_group ? group : NULL, NULL,
	    1, ncopy, inode_get_size(inode));
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1004338,
		    "inode=%lld, quota_limit_check(%s, %s, 1, %lld, %lld): %s",
		    (long long)inode_get_number(inode),
		    user ? user_tenant_name(user) : "NULL",
		    group ? group_tenant_name(group) : "NULL",
		    (long long)ncopy,
		    (long long)inode_get_size(inode),
		    gfarm_error_string(e));
		return (e);
	}

	/* chown won't change dirquota, so passing TDIRSET_IS_NOT_SET is OK */
	quota_update_file_remove(inode, TDIRSET_IS_NOT_SET);
	if (change_user) {
		inode->i_user = user;

		e = db_inode_user_modify(inode->i_number,
		    user_tenant_name(user));
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1000306,
			    "db_inode_user_modify(%lld): %s",
			    (unsigned long long)inode->i_number,
			    gfarm_error_string(e));
	}
	if (change_group) {
		inode->i_group = group;

		e = db_inode_group_modify(inode->i_number,
		    group_tenant_name(group));
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1000307,
			    "db_inode_group_modify(%lld): %s",
			    (unsigned long long)inode->i_number,
			    gfarm_error_string(e));
	}
	if (user != NULL || group != NULL) {
		gfarm_mode_t mode = inode->i_mode;

		if (GFARM_S_ISREG(mode) && (mode & 0111) != 0) {
			/*
			 * Solaris doesn't drop the setuid/setgid bits,
			 * if the user is root.  But we follow
			 * the linux behavior which drops the bits always.
			 */
			mode &= ~(GFARM_S_ISUID|GFARM_S_ISGID);
			/* inode_set_mode() implies inode_status_changed() */
			inode_set_mode(inode, mode & ~GFARM_S_IFMT);
		} else {
			inode_status_changed(inode);
		}
	}
	/* chown won't change dirquota, so passing TDIRSET_IS_NOT_SET is OK */
	quota_update_file_add(inode, TDIRSET_IS_NOT_SET);

	return (GFARM_ERR_NO_ERROR);
}

struct gfarm_timespec *
inode_get_atime(struct inode *inode)
{
	return (&inode->i_atimespec);
}

struct gfarm_timespec *
inode_get_mtime(struct inode *inode)
{
	return (&inode->i_mtimespec);
}

struct gfarm_timespec *
inode_get_ctime(struct inode *inode)
{
	return (&inode->i_ctimespec);
}

void
inode_set_atime_in_cache(struct inode *inode, struct gfarm_timespec *atime)
{
	inode->i_atimespec = *atime;
}

static void
inode_set_atime_main(struct inode *inode, struct gfarm_timespec *atime)
{
	gfarm_error_t e;

	if (atime == NULL)
		return;

	if (gfarm_timespec_cmp(&inode->i_atimespec, atime) == 0)
		return; /* not necessary to change */

	if (gfarm_read_only_mode())
		return;

	inode_set_atime_in_cache(inode, atime);

	e = db_inode_atime_modify(inode->i_number, &inode->i_atimespec);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1000308,
		    "db_inode_atime_modify(%lld): %s",
		    (unsigned long long)inode->i_number,
		    gfarm_error_string(e));
}

static void
inode_set_atime_nop(struct inode *inode, struct gfarm_timespec *atime)
{
}

static void inode_set_atime_switch(struct inode *, struct gfarm_timespec *);
void (*inode_set_atime)(struct inode *, struct gfarm_timespec *) =
	inode_set_atime_switch;

/* initalized only once */
static void
inode_set_atime_switch(struct inode *inode, struct gfarm_timespec *atime)
{
	if (gfarm_atime_type_get() == GFARM_ATIME_DISABLE)
		inode_set_atime = inode_set_atime_nop;
	else
		inode_set_atime = inode_set_atime_main;
	inode_set_atime(inode, atime);
}

static void
inode_set_relatime_main(struct inode *inode, struct gfarm_timespec *atime)
{
	struct gfarm_timespec sub;
	static struct gfarm_timespec a_day
		= { .tv_sec = 24 * 60 * 60, .tv_nsec = 0 };

	if (atime == NULL)
		return;

	sub = *atime;
	gfarm_timespec_sub(&sub, &inode->i_atimespec);
	if (gfarm_timespec_cmp(&sub, &a_day) <= 0 &&
	    gfarm_timespec_cmp(&inode->i_atimespec, &inode->i_ctimespec) > 0 &&
	    gfarm_timespec_cmp(&inode->i_atimespec, &inode->i_mtimespec) > 0)
		return;

	inode_set_atime(inode, atime);
}

static void inode_set_relatime_switch(struct inode *, struct gfarm_timespec *);
void (*inode_set_relatime)(struct inode *, struct gfarm_timespec *) =
	inode_set_relatime_switch;

/* initalized only once */
static void
inode_set_relatime_switch(struct inode *inode, struct gfarm_timespec *atime)
{
	switch (gfarm_atime_type_get()) {
	case GFARM_ATIME_DISABLE:
		inode_set_relatime = inode_set_atime_nop;
		break;
	case GFARM_ATIME_RELATIVE:
		inode_set_relatime = inode_set_relatime_main;
		break;
	case GFARM_ATIME_STRICT:
	default:
		inode_set_relatime = inode_set_atime_main;
		break;
	}
	inode_set_relatime(inode, atime);
}

void
inode_set_mtime_in_cache(struct inode *inode, struct gfarm_timespec *mtime)
{
	inode->i_mtimespec = *mtime;
}

void
inode_set_mtime(struct inode *inode, struct gfarm_timespec *mtime)
{
	gfarm_error_t e;

	if (gfarm_timespec_cmp(&inode->i_mtimespec, mtime) == 0)
		return; /* not necessary to change */

	inode_set_mtime_in_cache(inode, mtime);

	e = db_inode_mtime_modify(inode->i_number, inode_get_mtime(inode));
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1000309,
		    "db_inode_mtime_modify(%lld): %s",
		    (unsigned long long)inode->i_number,
		    gfarm_error_string(e));
}

void
inode_set_ctime_in_cache(struct inode *inode, struct gfarm_timespec *ctime)
{
	inode->i_ctimespec = *ctime;
}

void
inode_set_ctime(struct inode *inode, struct gfarm_timespec *ctime)
{
	gfarm_error_t e;

	if (gfarm_timespec_cmp(&inode->i_ctimespec, ctime) == 0)
		return; /* not necessary to change */

	inode_set_ctime_in_cache(inode, ctime);

	e = db_inode_ctime_modify(inode->i_number, &inode->i_ctimespec);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1000310,
		    "db_inode_ctime_modify(%lld): %s",
		    (unsigned long long)inode->i_number,
		    gfarm_error_string(e));
}

static void
touch(struct gfarm_timespec *tsp)
{
	struct timespec ts;

	gfarm_gettime(&ts);
	tsp->tv_sec = ts.tv_sec;
	tsp->tv_nsec = ts.tv_nsec;
}

void
inode_accessed(struct inode *inode)
{
	struct gfarm_timespec ts;

	touch(&ts);
	inode_set_relatime(inode, &ts);
}

void
inode_modified(struct inode *inode)
{
	struct gfarm_timespec ts;

	touch(&ts);
	inode_set_mtime(inode, &ts);
}

void
inode_status_changed(struct inode *inode)
{
	struct gfarm_timespec ts;

	touch(&ts);
	inode_set_ctime(inode, &ts);
}

void
inode_created(struct inode *inode)
{
	touch(&inode->i_ctimespec);
	inode->i_atimespec = inode->i_mtimespec = inode->i_ctimespec;
}

Dir
inode_get_dir(struct inode *inode)
{
	if (!inode_is_dir(inode))
		return (NULL);
	return (inode->u.c.s.d.entries);
}

char *
inode_get_symlink(struct inode *inode)
{
	if (!inode_is_symlink(inode))
		return (NULL);
	return (inode->u.c.s.l.source_path);
}

int
inode_dir_is_empty(struct inode *inode)
{
	assert(inode_is_dir(inode));
	return (inode->i_nlink <= 2 && dir_is_empty(inode->u.c.s.d.entries));
}

int
inode_desired_dead_file_copy(gfarm_ino_t inum)
{
	struct inode *inode = inode_lookup(inum);

	if (inode == NULL) /* already removed */
		return (0);
	if (!inode_is_file(inode)) /* already removed */
		return (0);
	if (inode_get_nlink(inode) == 0) /* waiting for being removed */
		return (0);
	if (inode_get_size(inode) == 0) /* can access latest contents? */
		return (0);
	if (inode_get_ncopy(inode) > 0) /* can access latest contents? */
		return (0);

	return (1); /* XXX FIXME: use xattr gfarm:ncopy */
}

int
inode_new_generation_is_pending(struct inode *inode)
{
	struct inode_activity *ia = inode->u.c.activity;
	static const char diag[] = "inode_new_generation_is_pending";

	if (ia == NULL) {
		gflog_debug(GFARM_MSG_1002247, "%s: not opened", diag);
		return (0);
	}
	return (ia->u.f.event_type != EVENT_NONE);
}

void
inode_new_generation_by_fd_start(struct inode *inode, struct peer *peer)
{
	struct inode_activity *ia = inode->u.c.activity;

	assert(ia != NULL);
	ia->u.f.event_type = EVENT_GEN_UPDATED;
	ia->u.f.event_source = peer;
}

gfarm_error_t
inode_new_generation_by_cookie_start(struct inode *inode,
	struct peer *peer, gfarm_uint64_t cookie)
{
	/*
	 * EVENT_GEN_UPDATED_BY_COOKIE is one of special cases:
	 * inode_activity is allocated without file_opening.
	 */
	struct inode_activity *ia = inode_activity_alloc_or_update(
	    &inode->u.c.activity, TDIRSET_IS_UNKNOWN);

	if (ia == NULL) {
		gflog_error(GFARM_MSG_1004019,
		    "unable to track inode generation");
		return (GFARM_ERR_NO_MEMORY);
	}
	ia->u.f.event_type = EVENT_GEN_UPDATED_BY_COOKIE;
	ia->u.f.event_source = peer;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
inode_new_generation_finish_precondition(struct inode *inode, const char *diag)
{
	if (!inode_is_file(inode)) {
		gflog_error(GFARM_MSG_1002249, "%s: not a file", diag);
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	}
	if (inode->u.c.activity == NULL) {
		gflog_error(GFARM_MSG_1002250, "%s: not opened", diag);
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	}
	return (GFARM_ERR_NO_ERROR);
}

static void
inode_new_generation_finish_event_post(struct inode *inode)
{
	struct inode_activity *ia = inode->u.c.activity;
	struct event_waiter *waiter, *next;

	waiter = ia->u.f.event_waiters;
	for (; waiter != NULL; waiter = next) {
		next = waiter->next;
		resuming_enqueue(waiter);
	}
	ia->u.f.event_type = EVENT_NONE;
	ia->u.f.event_source = NULL;
	ia->u.f.event_waiters = NULL;
}

static void inode_metadata_update(struct inode *, gfarm_off_t,
	struct gfarm_timespec *, struct gfarm_timespec *);
static void inode_generation_updated(struct inode *,
	struct host *, int, char *, const char *);

gfarm_error_t
inode_new_generation_by_fd_finish(struct inode *inode, struct peer *peer,
	enum inode_close_mode close_mode, gfarm_error_t result,
	int desired_replica_number, char *repattr,
	gfarm_off_t size,
	struct gfarm_timespec *atime, struct gfarm_timespec *mtime,
	char *username, const char *diag)
{
	gfarm_error_t e;
	struct inode_activity *ia;

	if ((e = inode_new_generation_finish_precondition(inode, diag)) !=
	    GFARM_ERR_NO_ERROR)
		return (e);

	ia = inode->u.c.activity;
	if (ia->u.f.event_type != EVENT_GEN_UPDATED) {
		gflog_warning(GFARM_MSG_1004020,
		    "%s: not pending generation update: %d",
		    diag, ia->u.f.event_type);
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	}
	assert(ia->u.f.event_source != NULL);
	if (peer == NULL) {
		peer = ia->u.f.event_source;
	} else if (peer != ia->u.f.event_source) {
		gflog_warning(GFARM_MSG_1002252, "%s: different peer", diag);
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	}

	if (close_mode == INODE_CLOSE_V2_8) {
		if (gfarm_read_only_mode()) {
			gflog_error(GFARM_MSG_1005469,
			    "inode %llu:%llu host %s user %s "
			    "gfmd became read_only during generation update, "
			    "metadata update size %llu is lost",
			    (long long)inode_get_number(inode),
			    (long long)inode_get_gen(inode),
			    peer_get_hostname(peer),
			    username, (long long)size);
		} else {
			inode_metadata_update(inode, size, atime, mtime);
		}
	}
	inode_del_ref_spool_writers(inode);
	/*
	 * XXX
	 * if result != GFARM_ERR_NO_ERROR,
	 * maybe it's better to keep old replica in update_replicas()?
	 */
	if (gfarm_read_only_mode()) {
		gflog_error(GFARM_MSG_1005470,
		    "inode %llu:%llu host %s user %s "
		    "gfmd became read_only during generation update, "
		    "replica status update is lost",
		    (long long)inode_get_number(inode),
		    (long long)inode_get_gen(inode),
		    peer_get_hostname(peer),
		    username);
	} else {
		inode_generation_updated(inode, peer_get_host(peer),
		    desired_replica_number, repattr, diag);
	}

	inode_new_generation_finish_event_post(inode);
	assert(ia->openings.opening_next != &ia->openings);

	return (GFARM_ERR_NO_ERROR);
}

/*
 * NOTE:
 * - caller of this function should acquire giant_lock as well
 * - caller of this function SHOULD call db_begin()/db_end() around this
 */
gfarm_error_t
inode_new_generation_by_cookie_finish(struct inode *inode,
	struct peer *peer, gfarm_uint64_t cookie,
	enum inode_close_mode close_mode, gfarm_error_t result,
	gfarm_off_t size,
	struct gfarm_timespec *atime, struct gfarm_timespec *mtime,
	char *username)
{
	gfarm_error_t e;
	struct inode_activity *ia;
	struct dirset *tdirset;
	gfarm_ino_t inum;
	static const char diag[] = "inode_new_generation_by_cookie_finish";

	if ((e = inode_new_generation_finish_precondition(inode, diag)) !=
	    GFARM_ERR_NO_ERROR)
		return (e);

	ia = inode->u.c.activity;
	if (ia->u.f.event_type != EVENT_GEN_UPDATED_BY_COOKIE) {
		gflog_warning(GFARM_MSG_1004021,
		    "%s: not pending generation update by cookie: %d",
		    diag, ia->u.f.event_type);
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	}
	assert(ia->u.f.event_source != NULL);
	if (peer == NULL) {
		peer = ia->u.f.event_source;
	} else if (peer != ia->u.f.event_source) {
		gflog_warning(GFARM_MSG_1002252, "%s: different peer", diag);
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	}

	/* giant_lock should prevent resuming threads from running */
	inode_new_generation_finish_event_post(inode);

	tdirset = inode_get_tdirset(inode);
	/* inode_activity_free_try() may free tdirset, so we need protection */
	if (tdirset != TDIRSET_IS_UNKNOWN && tdirset != TDIRSET_IS_NOT_SET)
		dirset_add_ref(tdirset);
	inum = inode_get_number(inode);

	if (close_mode == INODE_CLOSE_V2_4) {
		if (size != inode_get_size(inode)) {
			if (gfarm_read_only_mode()) {
				gflog_warning(GFARM_MSG_1005165,
				    "GFM_PROTO_GENERATION_UPDATED_BY_COOKIE: "
				    "inode %llu:%llu conflict detected, "
				    "but reverting size %llu to %llu skipped "
				    "due to read_only",
				    (long long)inode_get_number(inode),
				    (long long)inode_get_gen(inode),
				    (long long)inode_get_size(inode),
				    (long long)size);
			} else {
				inode_set_size(inode, tdirset, size);
			}
		}
	} else if (close_mode == INODE_CLOSE_V2_8) {
		if (gfarm_read_only_mode()) {
			gflog_error(GFARM_MSG_1005471,
			    "GFM_PROTO_GENERATION_UPDATED_BY_COOKIE_V2_8: "
			    "inode %llu:%llu host %s user %s: "
			    "gfmd became read_only during generation update, "
			    "metadata update size %llu is lost",
			    (long long)inode_get_number(inode),
			    (long long)inode_get_gen(inode),
			    peer_get_hostname(peer),
			    username, (long long)size);
		} else {
			inode_metadata_update(inode, size, atime, mtime);
		}
	} else { /* shouldn't happen */
		gflog_fatal(GFARM_MSG_1005472,
		    "invalid close_mode %d", close_mode);
	}
	/*
	 * XXX
	 * if result != GFARM_ERR_NO_ERROR,
	 * maybe it's better to keep old replica in update_replicas()?
	 */
	if (gfarm_read_only_mode()) {
		gflog_error(GFARM_MSG_1005473,
		    "inode %llu:%llu host %s user %s: "
		    "gfmd became read_only during generation update, "
		    "replica status update is lost",
		    (long long)inode_get_number(inode),
		    (long long)inode_get_gen(inode),
		    peer_get_hostname(peer), username);
	} else {
		inode_generation_updated(inode, peer_get_host(peer),
		    /* desired_file_number is unknown */ 1,
		    /* repattr is unknown */ NULL, diag);
	}

	if (inode_activity_free_try(inode))
		inode_remove_try(inode, tdirset);

	if (tdirset != TDIRSET_IS_UNKNOWN && tdirset != TDIRSET_IS_NOT_SET)
		dirset_del_ref(tdirset);
	if (tdirset == TDIRSET_IS_UNKNOWN) {
		gflog_notice(GFARM_MSG_1004668,
		    "inode %lld: unknown dirset, scheduling quota_check",
		    (long long)inum);
		dirquota_check_schedule();
	}

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
inode_new_generation_wait(struct inode *inode, struct peer *peer,
	gfarm_error_t (*action)(struct peer *, void *, int *), void *arg)
{
	struct inode_activity *ia;
	struct event_waiter *waiter;
	static const char diag[] = "inode_new_generation_wait";

	if (!inode_is_file(inode)) {
		gflog_error(GFARM_MSG_1002253, "%s: not a file", diag);
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	}
	ia = inode->u.c.activity;
	if (ia == NULL) {
		gflog_error(GFARM_MSG_UNUSED, "%s: no activity in inode %lld",
		    diag, (long long)inode_get_number(inode));
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	}
	if (ia->u.f.event_type == EVENT_NONE) {
		gflog_warning(GFARM_MSG_1002255, "%s: not pending", diag);
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	}

	GFARM_MALLOC(waiter);
	if (waiter == NULL) {
		gflog_warning(GFARM_MSG_1002256, "%s: no memory", diag);
		return (GFARM_ERR_NO_MEMORY);
	}

	waiter->peer = peer;
	waiter->action = action;
	waiter->arg = arg;

	waiter->next = ia->u.f.event_waiters;
	ia->u.f.event_waiters = waiter;

	return (GFARM_ERR_NO_ERROR);
}

#define check_mode(mode, mask) ((mode & mask) == mask ? \
			GFARM_ERR_NO_ERROR : GFARM_ERR_PERMISSION_DENIED)

gfarm_error_t
inode_access(struct inode *inode, struct tenant *tenant, struct user *user,
	int op)
{
	gfarm_error_t e;
	gfarm_mode_t mask = 0;

	if (inode->i_user == user) {
		if (op & GFS_X_OK)
			mask |= 0100;
		if (op & GFS_W_OK)
			mask |= 0200;
		if (op & GFS_R_OK)
			mask |= 0400;

		if (check_mode(inode->i_mode, mask) == GFARM_ERR_NO_ERROR)
			return (GFARM_ERR_NO_ERROR);
		else if (user_is_root_for_inode(user, inode))
			return (GFARM_ERR_NO_ERROR);
		return (GFARM_ERR_PERMISSION_DENIED);
	}

	if (user_is_root_for_inode(user, inode))
		return (GFARM_ERR_NO_ERROR);

	e = acl_access(inode, tenant, user, op);
	if (e != GFARM_ERR_NO_SUCH_OBJECT) {
		if (e != GFARM_ERR_NO_ERROR)
			gflog_debug(GFARM_MSG_1002801,
				    "acl_access() failed: %s",
				    gfarm_error_string(e));
		return (e);
	}

	/* this inode does not have ACL */
	if (user_in_group(user, inode->i_group)) {
		if (op & GFS_X_OK)
			mask |= 0010;
		if (op & GFS_W_OK)
			mask |= 0020;
		if (op & GFS_R_OK)
			mask |= 0040;
	} else {
		if (op & GFS_X_OK)
			mask |= 0001;
		if (op & GFS_W_OK)
			mask |= 0002;
		if (op & GFS_R_OK)
			mask |= 0004;
	}

	return (check_mode(inode->i_mode, mask));
}

static int
inode_access_flags(struct inode *inode,
	struct tenant *tenant, struct user *user)
{
	char flags = 0;

	if (inode_access(inode, tenant, user, GFS_R_OK) == GFARM_ERR_NO_ERROR)
		flags |= GFS_R_OK;
	if (inode_access(inode, tenant, user, GFS_W_OK) == GFARM_ERR_NO_ERROR)
		flags |= GFS_W_OK;
	if (inode_access(inode, tenant, user, GFS_X_OK) == GFARM_ERR_NO_ERROR)
		flags |= GFS_X_OK;
	return (flags);
}

static gfarm_error_t
xattr_list_set_by_inode_access(struct xattr_list *entry, const char *name,
	struct inode *inode, struct tenant *tenant, struct user *user,
	const char *diag)
{
	size_t size = 1;
	char *value;

	GFARM_MALLOC_ARRAY(value, size);
	if (value == NULL)
		return (GFARM_ERR_NO_MEMORY);
	*value = inode_access_flags(inode, tenant, user);
	if (name != NULL)
		entry->name = strdup_log(name, diag);
	else
		entry->name = NULL;
	entry->value = value;
	entry->size = size;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
inode_add_xattr_common(struct inode *ino, const char *name,
		       const void *value, size_t size)
{
	/* XXX UNCONST */
	gfarm_error_t e = db_xattr_add(0, ino->i_number, (char *)name,
				       (char *)value, size, NULL);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1002802,
			    "db_xattr_add(%lld, %s): %s",
			    (unsigned long long)ino->i_number, name,
			    gfarm_error_string(e));
	return (e);
}

static void
inode_db_init(struct inode *inode)
{
	gfarm_error_t e;
	struct gfs_stat st;

	st.st_ino = inode->i_number;
	st.st_gen = inode->i_gen;
	st.st_mode = inode->i_mode;
	st.st_nlink = inode->i_nlink;
	st.st_user = user_tenant_name_even_invalid(inode->i_user);
	st.st_group = group_tenant_name_even_invalid(inode->i_group);
	st.st_size = inode->i_size;
	st.st_ncopy = 0;
	st.st_atimespec = inode->i_atimespec;
	st.st_mtimespec = inode->i_mtimespec;
	st.st_ctimespec = inode->i_ctimespec;
	if (inode->i_gen == 0) /* see inode_undo_alloc() */
		e = db_inode_add(&st);
	else
		e = db_inode_modify(&st);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1000314,
		    "db_inode_%s(%lld): %s",
		    inode->i_gen == 0 ? "add" : "modify",
		    (unsigned long long)inode->i_number,
		    gfarm_error_string(e));
}

static int
is_removable_in_sticky_dir(struct inode *dir, struct inode *entry,
	struct user *user,
	const char *diag, const char *name, int len)
{
	if (user == inode_get_user(dir) ||
	    user == inode_get_user(entry) ||
	    user_is_root_for_inode(user, dir) ||
	    user_is_root_for_inode(user, entry)) {
		return (1);
	} else {
		gflog_debug(GFARM_MSG_1004022,
		    "%s is disallowed due to sticky dir: "
		    "dir %lld:%lld name %.*s user %s", diag,
		    (long long)inode_get_number(dir),
		    (long long)inode_get_gen(dir),
		    len, name, user_tenant_name(user));
		return (0);
	}
}

enum gfarm_inode_lookup_op {
	INODE_LOOKUP,
	INODE_CREATE,
	INODE_CREATE_EXCLUSIVE,
	INODE_REMOVE,
	INODE_LINK,
};

/*
 * if (op == INODE_LINK)
 *	(*inp) is an input parameter instead of output.
 *	db_inode_nlink_modify() should be done by the caller.
 * if (op == INODE_REMOVE)
 *	db_inode_nlink_modify() should be done by the caller.
 * if (op == INODE_CREATE)
 *	createdp must be passed, otherwise it's ignored.
 * if (op != INODE_CREATE && op != INODE_CREATE_EXCLUSIVE)
 *	expcted_type may be GFS_DT_UNKNOWN, and that means "don't care".
 * if (op == INODE_CREATE || op == INODE_CREATE_EXCLUSIVE)
 *	new_mode must be passed, otherwise it's ignored.
 * if ((op == INODE_CREATE || op == INODE_CREATE_EXCLUSIVE) &&
 *     expected_type == GFS_DT_LNK)
 *	symlink_src must be passed, otherwise it's ignored.
 */
static gfarm_error_t
inode_lookup_basename(struct inode *parent, const char *name, int len,
	int expected_type, enum gfarm_inode_lookup_op op,
	struct tenant *tenant, struct user *user,
	gfarm_mode_t new_mode, char *symlink_src,
	struct inode **inp, int *createdp)
{
	gfarm_error_t e;
	DirEntry entry;
	int created, mode_updated = 0;
	struct inode *n;
	struct dirset *parent_tdirset = TDIRSET_IS_UNKNOWN;
	void *acl_acc = NULL, *acl_def = NULL;
	size_t acl_acc_size, acl_def_size;
	void *root_user = NULL, *root_group = NULL;
	size_t root_user_size, root_group_size;
	gfarm_mode_t new_mode_from_default_acl;

	if (len == 0) {
		if (op != INODE_LOOKUP) {
			gflog_debug(GFARM_MSG_1001727,
				"argument 'op' is invalid");
			return (GFARM_ERR_INVALID_ARGUMENT);
		}
		*inp = parent;
		return (GFARM_ERR_NO_ERROR);
	} else if (len == 1 && name[0] == '.') {
		if (op != INODE_LOOKUP) {
			gflog_debug(GFARM_MSG_1001728,
				"argument 'op' is invalid");
			return (GFARM_ERR_INVALID_ARGUMENT);
		}
		*inp = parent;
		return (GFARM_ERR_NO_ERROR);
	} else if (memchr(name, '/', len) != NULL) {
		gflog_debug(GFARM_MSG_1001729,
			"argument 'name' is invalid");
		return (GFARM_ERR_INVALID_ARGUMENT);
	}
	if (len > GFS_MAXNAMLEN) {
		gflog_debug(GFARM_MSG_1002431,
		    "op %d: too long file name: \"%s\"", op, name);
		return (GFARM_ERR_FILE_NAME_TOO_LONG);
	}
	if (op != INODE_CREATE && op != INODE_CREATE_EXCLUSIVE &&
	    op != INODE_LINK) {
		entry = dir_lookup(parent->u.c.s.d.entries, name, len);
		if (entry == NULL) {
			gflog_debug(GFARM_MSG_1001730,
				"dir_lookup() failed");
			return (GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY);
		}
		if (op == INODE_LOOKUP) {
			*inp = dir_entry_get_inode(entry);
			return (GFARM_ERR_NO_ERROR);
		}

		assert(op == INODE_REMOVE);
		/* GFS_X_OK is already checked by inode_lookup_relative() */
		if ((e = inode_access(parent, tenant, user, GFS_W_OK)) !=
		    GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001731,
				"inode_access() failed: %s",
				gfarm_error_string(e));
			return (e);
		}
		if ((parent->i_mode & GFARM_S_ISTXT) != 0 &&
		    !is_removable_in_sticky_dir(parent,
		    dir_entry_get_inode(entry), user, "remove op", name, len))
			return (GFARM_ERR_OPERATION_NOT_PERMITTED);

		*inp = dir_entry_get_inode(entry);
		(*inp)->i_nlink--;
		dir_remove_entry(parent->u.c.s.d.entries, name, len);
		inode_modified(parent);

		e = db_direntry_remove(parent->i_number, name, len);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1000311,
			    "db_direntry_remove(%lld, %.*s): %s",
			    (unsigned long long)parent->i_number, len, name,
			    gfarm_error_string(e));

		return (GFARM_ERR_NO_ERROR);
	}

	if (op == INODE_LINK) {
		struct dirset *target_tdirset;

		n = *inp;
		target_tdirset = inode_get_tdirset(n);
		parent_tdirset = inode_get_tdirset(parent);
		if (parent_tdirset == TDIRSET_IS_UNKNOWN)
			parent_tdirset = inode_search_tdirset(parent);
		if (target_tdirset == TDIRSET_IS_UNKNOWN ||
		    parent_tdirset == TDIRSET_IS_UNKNOWN) {
			gflog_notice(GFARM_MSG_1004669,
			    "inode_lookup_basename: unknown dirset: %p vs %p",
			    target_tdirset, parent_tdirset);
			return (GFARM_ERR_INTERNAL_ERROR);
		}
		if (target_tdirset != parent_tdirset)
			return (GFARM_ERR_CROSS_DEVICE_LINK);
	}

	entry = dir_enter(parent->u.c.s.d.entries, name, len, &created);
	if (entry == NULL) {
		gflog_debug(GFARM_MSG_1001732,
			"dir_enter() failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	if (!created) {
		if (op == INODE_CREATE_EXCLUSIVE || op == INODE_LINK) {
			gflog_debug(GFARM_MSG_1001733,
				"inode already exists");
			return (GFARM_ERR_ALREADY_EXISTS);
		}
		assert(op == INODE_CREATE);
		*inp = dir_entry_get_inode(entry);
		if (createdp)
			*createdp = 0;
		return (GFARM_ERR_NO_ERROR);
	}
	if ((e = inode_access(parent, tenant, user, GFS_W_OK))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001734, "inode_access() failed: %s",
			gfarm_error_string(e));
		dir_remove_entry(parent->u.c.s.d.entries, name, len);
		return (e);
	}
	if (op == INODE_LINK) {
		n = *inp;
		n->i_nlink++;
		dir_entry_set_inode(entry, n);
		inode_status_changed(n);
		inode_modified(parent);

		e = db_direntry_add(parent->i_number, name, len, n->i_number);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1000313,
			    "db_direntry_add(%lld, %lld): %s",
			    (unsigned long long)parent->i_number,
			    (unsigned long long)n->i_number,
			    gfarm_error_string(e));

		return (GFARM_ERR_NO_ERROR);
	}
	assert((op == INODE_CREATE || op == INODE_CREATE_EXCLUSIVE) &&
	    expected_type != GFS_DT_UNKNOWN);

	if (parent_tdirset == TDIRSET_IS_UNKNOWN) {
		parent_tdirset = inode_get_tdirset(parent);
		if (parent_tdirset == TDIRSET_IS_UNKNOWN)
			parent_tdirset = inode_search_tdirset(parent);
	}

	n = inode_alloc();
	if (n == NULL) {
		dir_remove_entry(parent->u.c.s.d.entries, name, len);
		gflog_debug(GFARM_MSG_1001735,
			"inode_alloc() failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	switch (expected_type) {
	case GFS_DT_DIR:
		e = quota_limit_check(user, parent->i_group, parent_tdirset,
		    1, 0, 0);
		if (e == GFARM_ERR_NO_ERROR)
			e = inode_init_dir(n, parent);
		break;
	case GFS_DT_REG:
		e = quota_limit_check(user, parent->i_group, parent_tdirset,
		    1, 1, 0);
		if (e == GFARM_ERR_NO_ERROR)
			e = inode_init_file(n);
		break;
	case GFS_DT_LNK:
		e = quota_limit_check(user, parent->i_group, parent_tdirset,
		    1, 0, 0);
		if (e == GFARM_ERR_NO_ERROR)
			e = inode_init_symlink(n, symlink_src);
		break;
	default:
		assert(0);
		e = GFARM_ERR_UNKNOWN;
		break;
	}
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001736,
			"error occurred during process: %s",
			gfarm_error_string(e));
		dir_remove_entry(parent->u.c.s.d.entries, name, len);
		inode_undo_alloc(n);
		inode_clear(n);
		return (e);
	}
	n->i_mode |= new_mode;
	n->i_user = user;
	n->i_group = parent->i_group;
	n->i_size = 0;
	inode_created(n);
	dir_entry_set_inode(entry, n);
	inode_modified(parent);

	e = xattr_inherit(parent, n, tenant,
	    &acl_def, &acl_def_size,
	    &acl_acc, &acl_acc_size,
	    &new_mode_from_default_acl, &mode_updated,
	    &root_user, &root_user_size,
	    &root_group, &root_group_size);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002803, "xattr_inherit() failed: %s",
			    gfarm_error_string(e));
		dir_remove_entry(parent->u.c.s.d.entries, name, len);
		inode_undo_alloc(n);
		/* if inode_is_dir(n), "." and ".." are freed automatically */
		inode_free(n);
		return (e);
	}
	if (mode_updated)
		n->i_mode = (n->i_mode \
		    & (GFARM_S_IFMT|GFARM_S_ISUID|GFARM_S_ISGID|GFARM_S_ISTXT))
		    | (new_mode_from_default_acl & GFARM_S_ALLPERM);

	inode_db_init(n);
	quota_update_file_add(n, parent_tdirset);

	if (acl_def != NULL) {
		assert(inode_is_dir(n));
		inode_add_xattr_common(n, GFARM_ACL_EA_DEFAULT,
				       acl_def, acl_def_size);
		free(acl_def);
	}
	if (acl_acc != NULL) {
		assert(!inode_is_symlink(n));
		inode_add_xattr_common(n, GFARM_ACL_EA_ACCESS,
				       acl_acc, acl_acc_size);
		free(acl_acc);
	}
	if (root_user != NULL) {
		assert(!inode_is_symlink(n));
		inode_add_xattr_common(n, GFARM_ROOT_EA_USER,
				       root_user, root_user_size);
		free(root_user);
	}
	if (root_group != NULL) {
		assert(!inode_is_symlink(n));
		inode_add_xattr_common(n, GFARM_ROOT_EA_GROUP,
				       root_group, root_group_size);
		free(root_group);
	}

	/*
	 * We do db_direntry_add() here to make LDAP happy.
	 * Because inode must be created before DirEntry
	 * due to LDAP DN hierarchy.
	 * See the comment in inode_init_dir() too.
	 */
	if (expected_type == GFS_DT_DIR) {
		e = db_direntry_add(n->i_number, DOT, DOT_LEN, n->i_number);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1000315,
			    "db_direntry_add(%lld, \".\", %lld): %s",
			    (unsigned long long)parent->i_number,
			    (unsigned long long)n->i_number,
			    gfarm_error_string(e));
		e = db_direntry_add(
			n->i_number, DOTDOT, DOTDOT_LEN, parent->i_number);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1000316,
			    "db_direntry_add(%lld, \"..\", %lld): %s",
			    (unsigned long long)parent->i_number,
			    (unsigned long long)n->i_number,
			    gfarm_error_string(e));
		parent->i_nlink++;
		e = db_inode_nlink_modify(parent->i_number, parent->i_nlink);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1002804,
			    "db_inode_nlink_modify(%llu, %llu): %s",
			    (unsigned long long)parent->i_number,
			    (unsigned long long)parent->i_nlink,
			    gfarm_error_string(e));
	} else if (expected_type == GFS_DT_LNK) {
		e = db_symlink_add(n->i_number, symlink_src);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1000317,
			    "db_symlink_add(%lld, \"%s\"): %s",
			    (unsigned long long)n->i_number, symlink_src,
			    gfarm_error_string(e));
	}
	e = db_direntry_add(parent->i_number, name, len, n->i_number);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1000318,
		    "db_direntry_add(%lld, %lld): %s",
		    (unsigned long long)parent->i_number,
		    (unsigned long long)n->i_number,
		    gfarm_error_string(e));

	*inp = n;
	if (createdp)
		*createdp = 1;
	return (GFARM_ERR_NO_ERROR);
}

/* XXX TODO: namei cache */
/*
 * lookup relative pathname without permission check
 * NOTE: inode_lookup_by_name() should be used if permission has to be checked
 *
 * NOTE: this doesn't handle chroot environment correctly,
 * inode_lookup_relative() should be used to handle chroot
 *
 * if (op == INODE_LINK)
 *	(*inp) is an input parameter instead of output.
 * if (op == INODE_CREATE)
 *	createdp must be passed, otherwise it's ignored.
 * if (op != INODE_CREATE && op != INODE_CREATE_EXCLUSIVE)
 *	expcted_type may be GFS_DT_UNKNOWN, and that means "don't care".
 * if (op == INODE_CREATE || op == INODE_CREATE_EXCLUSIVE)
 *	new_mode must be passed, otherwise it's ignored.
 * if ((op == INODE_CREATE || op == INODE_CREATE_EXCLUSIVE) &&
 *     expected_type == GFS_DT_LNK)
 *	symlink_src must be passed, otherwise it's ignored.
 */
static gfarm_error_t
inode_lookup_relative_internal(struct inode *n, const char *name,
	int expected_type, enum gfarm_inode_lookup_op op,
	struct tenant *tenant, struct user *user,
	gfarm_mode_t new_mode, char *symlink_src,
	struct inode **inp, int *createdp)
{
	gfarm_error_t e;
	int len = strlen(name);
	struct inode *nn;

#ifdef __GNUC__ /* workaround gcc warning: may be used uninitialized */
	nn = NULL;
#endif
	if (!inode_is_dir(n)) {
		gflog_debug(GFARM_MSG_1001737,
			"inode is not directory");
		return (GFARM_ERR_NOT_A_DIRECTORY);
	}
	if ((e = inode_access(n, tenant, user, GFS_X_OK))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001738,
			"inode_access() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (op == INODE_LINK)
		nn = *inp;
	e = inode_lookup_basename(n, name, len,
	    expected_type, op, tenant, user, new_mode, symlink_src,
	    &nn, createdp);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001739,
			"inode_lookup_basename() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (expected_type != GFS_DT_UNKNOWN &&
	    gfs_mode_to_type(nn->i_mode) != expected_type) {
		assert(op != INODE_CREATE_EXCLUSIVE &&
		       (op != INODE_CREATE || !*createdp));
		return (
		    expected_type == GFS_DT_DIR ? GFARM_ERR_NOT_A_DIRECTORY :
		    GFARM_S_ISDIR(nn->i_mode) ?  GFARM_ERR_IS_A_DIRECTORY :
		    GFARM_ERR_OPERATION_NOT_SUPPORTED);
	}
	*inp = nn;
	return (GFARM_ERR_NO_ERROR);
}

/*
 * lookup relative pathname without permission check
 * NOTE: inode_lookup_by_name() should be used if permission has to be checked
 *
 * unlike inode_lookup_relative_internal(), this handles chroot.
 */
static gfarm_error_t
inode_lookup_relative(struct inode *n, const char *name,
	int expected_type, enum gfarm_inode_lookup_op op,
	struct process *process,
	gfarm_mode_t new_mode, char *symlink_src,
	struct inode **inp, int *createdp)
{
	gfarm_ino_t root_inum;

	if (strcmp(name, DOTDOT) == 0 &&
	    n->i_number == (root_inum = process_get_root_inum(process))) {
		if (op != INODE_LOOKUP)
			return (GFARM_ERR_OPERATION_NOT_PERMITTED);
		if (n->i_gen != process_get_root_igen(process)) {
			/*
			 * chroot directory was removed,
			 * and same inode number was reused for this inode.
			 * this will never happen.
			 */
			return (GFARM_ERR_STALE_FILE_HANDLE);
		}
		if (expected_type != GFS_DT_UNKNOWN &&
		    expected_type != GFS_DT_DIR) {
			return (GFARM_ERR_IS_A_DIRECTORY);
		}
		*inp = n;
		if (createdp != NULL)
			*createdp = 0;
		return (GFARM_ERR_NO_ERROR);
	}

	return (inode_lookup_relative_internal(n, name, expected_type, op,
	    process_get_tenant(process),
	    process_get_user(process), new_mode, symlink_src, inp, createdp));
}

gfarm_error_t
inode_lookup_root(struct process *process, int op, struct inode **inp)
{
	gfarm_error_t e;
	struct inode *inode = inode_lookup(process_get_root_inum(process));

	if (inode == NULL) {
		gflog_debug(GFARM_MSG_1001740, "inode_lookup() failed");
		return (GFARM_ERR_STALE_FILE_HANDLE); /* XXX never happen */
	}
	if (inode->i_gen != process_get_root_igen(process)) {
		gflog_debug(GFARM_MSG_1005474, "inode_lookup_root(): "
		    "user %s's root inode %llu expects gen %llu, but %llu",
		    user_tenant_name(process_get_user(process)),
		    (long long)process_get_root_inum(process),
		    (long long)process_get_root_igen(process),
		    (long long)inode->i_gen);
		return (GFARM_ERR_STALE_FILE_HANDLE); /* directory removed */
	}
	e = inode_access(inode,
	    process_get_tenant(process), process_get_user(process), op);
	if (e == GFARM_ERR_NO_ERROR)
		*inp = inode;
	return (e);
}

gfarm_error_t
inode_lookup_parent(struct inode *base, struct process *process, int op,
	struct dirset **tdirsetp, struct inode **inp)
{
	struct inode *inode;
	struct user *user = process_get_user(process);
	gfarm_error_t e = inode_lookup_relative(base, DOTDOT, GFS_DT_DIR,
	    INODE_LOOKUP, process, 0, NULL, &inode, NULL);
	struct dirset *tdirset;

	if (e == GFARM_ERR_NO_ERROR &&
	    (e = inode_access(inode, process_get_tenant(process), user, op))
	    == GFARM_ERR_NO_ERROR) {

		/* do not allow write-open anyway */
		if ((op & GFS_W_OK) != 0) {
			gflog_debug(GFARM_MSG_1004670, "dotdot is directory");
			return (GFARM_ERR_IS_A_DIRECTORY);
		}

		tdirset = inode_get_tdirset(base);
		if (tdirset == TDIRSET_IS_NOT_SET)
			*tdirsetp = TDIRSET_IS_NOT_SET;
		else if (tdirset == TDIRSET_IS_UNKNOWN) {
			tdirset = inode_search_tdirset(inode);
			*tdirsetp = tdirset;
		} else if (quota_dir_get_dirset_by_inum(inode_get_number(base))
		    == tdirset)
			*tdirsetp = TDIRSET_IS_NOT_SET;
		else
			*tdirsetp = tdirset;

		*inp = inode;
	}
	return (e);
}

/*
 * similar to inode_looukp_by_name(),
 * but checks write-open against a directory and does quota handling
 *
 * *tdirsetp;	input/output parameter
 * *inp;	output parameter
 */
gfarm_error_t
inode_lookup_for_open(struct inode *base, const char *name,
	struct process *process, int op,
	struct dirset **tdirsetp, struct inode **inp)
{
	struct inode *inode;
	struct user *user = process_get_user(process);
	gfarm_error_t e;
	struct dirset *tdirset;

	if (strcmp(name, DOTDOT) == 0)
		return (inode_lookup_parent(base, process, op, tdirsetp, inp));

	e = inode_lookup_relative(base, name, GFS_DT_UNKNOWN,
	    INODE_LOOKUP, process, 0, NULL, &inode, NULL);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	if ((op & GFS_W_OK) != 0 && inode_is_dir(inode)) {
		gflog_debug(GFARM_MSG_1001741,
			"inode is directory");
		return (GFARM_ERR_IS_A_DIRECTORY);
	}

	e = inode_access(inode, process_get_tenant(process), user, op);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	tdirset = *tdirsetp;
	if (inode_is_dir(inode)) {
		if (tdirset == TDIRSET_IS_UNKNOWN) {
			tdirset = inode_search_tdirset(inode);
		} else if (tdirset == TDIRSET_IS_NOT_SET) {
			tdirset = quota_dir_get_dirset_by_inum(
			    inode_get_number(inode));
			if (tdirset == NULL)
				tdirset = TDIRSET_IS_NOT_SET;
		}
	} else if (tdirset == TDIRSET_IS_UNKNOWN)
		tdirset = inode_search_tdirset(inode);

	if (inode_is_file(inode) && (op & GFS_W_OK) != 0) {
		e = quota_limit_check(inode_get_user(inode),
		    inode_get_group(inode), tdirset, 0, 0, 0);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
	}

	*inp = inode;
	*tdirsetp = tdirset;
	return (GFARM_ERR_NO_ERROR);
}

/*
 * lookup relative pathname with permission check against the inode itself
 */
static gfarm_error_t
inode_lookup_by_name(struct inode *base, const char *name,
	struct process *process, struct inode **inp)
{
	struct inode *inode;
	struct user *user = process_get_user(process);
	gfarm_error_t e = inode_lookup_relative(base, name, GFS_DT_UNKNOWN,
	    INODE_LOOKUP, process, 0, NULL, &inode, NULL);

	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	/*
	 * this inode_access() always succeeds with current ACL feature,
	 * but perhaps may fail with some ACL extension in future?
	 */
	e = inode_access(inode, process_get_tenant(process), user, 0);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	*inp = inode;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
inode_lookup_user_root(struct user *u, struct inode **inodep)
{
	gfarm_error_t e;
	/* do not check inode_access() here */
	struct inode *root_inode = inode_lookup(ROOT_INUMBER);

	if (user_needs_chroot(u)) {
		const char *tenant_name = user_get_tenant_name(u);
		struct inode *tenant_base_inode, *tenant_root_inode;

		e = inode_lookup_relative_internal(
		    root_inode, TENANT_BASE_NAME,
		    GFS_DT_DIR, INODE_LOOKUP,
		    tenant_default(), &filesystem_superuser, 0, NULL,
		    &tenant_base_inode, NULL);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
		e = inode_lookup_relative_internal(
		    tenant_base_inode, tenant_name,
		    GFS_DT_DIR, INODE_LOOKUP,
		    tenant_default(), &filesystem_superuser, 0, NULL,
		    &tenant_root_inode, NULL);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
		if (!inode_is_dir(tenant_root_inode))
			return (GFARM_ERR_NOT_A_DIRECTORY);
		root_inode = tenant_root_inode;
	}
	*inodep = root_inode;
	return (GFARM_ERR_NO_ERROR);
}

static struct inode *
inode_lookup_lost_found(void)
{
	static gfarm_ino_t inum_lost_found = 0;
	static gfarm_uint64_t gen_lost_found = 0;
	struct inode *root, *inode;
	struct user *admin;
	int created;
	gfarm_error_t e;

	if (inum_lost_found != 0) {
		inode = inode_lookup(inum_lost_found);
		if (inode != NULL && inode->i_gen == gen_lost_found)
			return (inode);
	}
	root = inode_lookup(ROOT_INUMBER);
	if (root == NULL) {
		gflog_error(GFARM_MSG_1002480, "no root directory");
		return (NULL);
	}
	admin = user_tenant_lookup_including_invalid(ADMIN_USER_NAME);
	if (admin == NULL) {
		gflog_error(GFARM_MSG_1002481, "no admin user");
		return (NULL);
	}
	e = inode_lookup_relative_internal(root, lost_found, GFS_DT_DIR,
	    INODE_CREATE, tenant_default(), admin, 0700, NULL,
	    &inode, &created);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1002482, "no /%s directory", lost_found);
		return (NULL);
	}
	if (created) {
		root->i_nlink_ini++;
		inode->i_nlink_ini = inode->i_nlink;
		inode->u.c.s.d.parent_dir = root;
		gflog_info(GFARM_MSG_1002483, "create /%s directory",
		    lost_found);
	}
	inum_lost_found = inode->i_number;
	gen_lost_found = inode->i_gen;
	return (inode);
}

gfarm_error_t
inode_create_file(struct inode *base, char *name,
	struct process *process, int op, gfarm_mode_t mode, int exclusive,
	struct inode **inp, int *createdp)
{
	struct inode *inode;
	int created;
	struct user *user = process_get_user(process);
	gfarm_error_t e = inode_lookup_relative(base, name, GFS_DT_REG,
	    exclusive ? INODE_CREATE_EXCLUSIVE : INODE_CREATE,
	    process, mode, NULL, &inode, &created);

	if (e == GFARM_ERR_NO_ERROR) {
		if (!created)
			e = inode_access(inode,
			    process_get_tenant(process), user, op);
		if (e == GFARM_ERR_NO_ERROR) {
			*inp = inode;
			*createdp = created;
		}
	}
	return (e);
}

gfarm_error_t
inode_create_dir(struct inode *base, char *name,
	struct process *process, gfarm_mode_t mode)
{
	struct inode *inode;

	return (inode_lookup_relative(base, name, GFS_DT_DIR,
	    INODE_CREATE_EXCLUSIVE, process, mode, NULL,
	    &inode, NULL));
}

gfarm_error_t
inode_create_symlink(struct inode *base, char *name,
	struct process *process, char *source_path,
	struct inode_trace_log_info *inodetp)
{
	gfarm_error_t e;
	struct inode *inode;

	if (strlen(source_path) > GFARM_PATH_MAX) {
		gflog_debug(GFARM_MSG_1002432,
		    "create symlink \"%s\"  \"%s\": too long source path",
		    source_path, name);
		return (GFARM_ERR_FILE_NAME_TOO_LONG);
	}

	e = inode_lookup_relative(base, name, GFS_DT_LNK,
	    INODE_CREATE_EXCLUSIVE, process,
	    0777, source_path, &inode, NULL);
	if (gfarm_ctxp->file_trace && e == GFARM_ERR_NO_ERROR &&
	    inodetp != NULL) {
		inodetp->inum = inode_get_number(inode);
		inodetp->igen = inode_get_gen(inode);
		inodetp->imode = inode_get_mode(inode);
	}
	return (e);
}

static gfarm_error_t
inode_create_link_internal(struct inode *base, const char *name,
	struct user *user, struct inode *inode)
{
	return (inode_lookup_relative_internal(base, name, GFS_DT_UNKNOWN,
	    INODE_LINK, tenant_default(), user, 0, NULL, &inode, NULL));
}

static gfarm_error_t
inode_create_link_only(struct inode *base, const char *name,
	struct process *process, struct inode *inode)
{
	return (inode_lookup_relative(base, name, GFS_DT_UNKNOWN,
	    INODE_LINK, process, 0, NULL, &inode, NULL));
}

gfarm_error_t
inode_create_link(struct inode *base, char *name,
	struct process *process, struct inode *inode)
{
	gfarm_error_t e;

	e = inode_create_link_only(base, name, process, inode);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	e = db_inode_nlink_modify(inode->i_number, inode->i_nlink);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1000312,
		    "db_inode_nlink_modify(%lld): %s",
		    (unsigned long long)inode->i_number,
		    gfarm_error_string(e));
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
inode_dir_reparent(struct inode *dir_inode,
	struct inode *old_parent, struct inode *new_parent)
{
	gfarm_error_t e;
	DirEntry entry;

	if (dir_remove_entry(dir_inode->u.c.s.d.entries, DOTDOT, DOTDOT_LEN)) {
		e = db_direntry_remove(inode_get_number(dir_inode),
		    DOTDOT, DOTDOT_LEN);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1002805,
			    "reparent: db_direntry_remove(%llu, %.*s): %s",
			    (unsigned long long)inode_get_number(dir_inode),
			    (int)DOTDOT_LEN, DOTDOT, gfarm_error_string(e));

		assert(old_parent != NULL);
		old_parent->i_nlink--;
		e = db_inode_nlink_modify(
		    inode_get_number(old_parent), old_parent->i_nlink);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1002806,
			    "decrement for reparent(%llu): "
			    "db_inode_nlink_modify(%llu): %s",
			    (unsigned long long)inode_get_number(dir_inode),
			    (unsigned long long)inode_get_number(old_parent),
			    gfarm_error_string(e));
	}

	entry = dir_enter(dir_inode->u.c.s.d.entries, DOTDOT, DOTDOT_LEN, NULL);
	if (entry == NULL)
		return (GFARM_ERR_NO_MEMORY);
	dir_entry_set_inode(entry, new_parent);
	e = db_direntry_add(inode_get_number(dir_inode),
	    DOTDOT, DOTDOT_LEN, inode_get_number(new_parent));
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1002807,
		    "reparent: db_direntry_add(%llu, %llu): %s",
		    (unsigned long long)inode_get_number(dir_inode),
		    (unsigned long long)inode_get_number(new_parent),
		    gfarm_error_string(e));

	new_parent->i_nlink++;
	e = db_inode_nlink_modify(inode_get_number(new_parent),
	    new_parent->i_nlink);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1002808,
		    "increment for reparent(%llu): "
		    "db_inode_nlink_modify(%llu): %s",
		    (unsigned long long)inode_get_number(dir_inode),
		    (unsigned long long)inode_get_number(new_parent),
		    gfarm_error_string(e));
	return (GFARM_ERR_NO_ERROR);
}

/* similar to inode_dir_reparent(), but use i_nlink_ini instead of i_nlink */
static void
inode_dir_reparent_ini(struct inode *dir_inode,
	struct inode *old_parent, struct inode *new_parent)
{
	gfarm_error_t e;
	DirEntry entry;

	if (dir_remove_entry(dir_inode->u.c.s.d.entries, DOTDOT, DOTDOT_LEN)) {
		e = db_direntry_remove(inode_get_number(dir_inode),
		    DOTDOT, DOTDOT_LEN);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1002809,
			    "reparent_ini: db_direntry_remove(%llu, %.*s): %s",
			    (unsigned long long)inode_get_number(dir_inode),
			    (int)DOTDOT_LEN, DOTDOT, gfarm_error_string(e));

		assert(old_parent != NULL);
		inode_decrement_nlink_ini(old_parent);
	}

	entry = dir_enter(dir_inode->u.c.s.d.entries,
	    DOTDOT, DOTDOT_LEN, NULL);
	if (entry == NULL) {
		if (old_parent == NULL)
			gflog_error(GFARM_MSG_1002810,
			    "reparent_ini %llu to %llu: no memory for dotdot",
			    (unsigned long long)inode_get_number(dir_inode),
			    (unsigned long long)inode_get_number(new_parent));
		else
			gflog_error(GFARM_MSG_1002811,
			    "reparent_ini %llu from %llu to %llu: "
			    "no memory for dotdot",
			    (unsigned long long)inode_get_number(dir_inode),
			    (unsigned long long)inode_get_number(old_parent),
			    (unsigned long long)inode_get_number(new_parent));
		return;
	}
	dir_entry_set_inode(entry, new_parent);
	e = db_direntry_add(inode_get_number(dir_inode), DOTDOT, DOTDOT_LEN,
	    inode_get_number(new_parent));
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1002812,
		    "reparent_ini: db_direntry_add(%llu, %llu): %s",
		    (unsigned long long)inode_get_number(dir_inode),
		    (unsigned long long)inode_get_number(new_parent),
		    gfarm_error_string(e));

	inode_increment_nlink_ini(new_parent);
}

static void
inode_dir_check_and_repair_dotdot(struct inode *dir_inode,
	struct inode *new_parent)
{
	DirEntry entry = dir_lookup(dir_inode->u.c.s.d.entries,
	    DOTDOT, DOTDOT_LEN);

	if (entry == NULL) {
		inode_dir_reparent_ini(dir_inode, NULL, new_parent);
		gflog_warning(GFARM_MSG_1002813,
		    "inode %llu: dotdot didn't exist: fixed to %llu",
		    (unsigned long long)inode_get_number(dir_inode),
		    (unsigned long long)inode_get_number(new_parent));
	} else {
		struct inode *old_parent = dir_entry_get_inode(entry);

		assert(old_parent != NULL);
		if (old_parent != new_parent) {
			inode_dir_reparent_ini(dir_inode,
			    old_parent, new_parent);
			gflog_warning(GFARM_MSG_1002814,
			    "inode %llu: dotdot pointed %llu: fixed to %llu",
			    (unsigned long long)inode_get_number(dir_inode),
			    (unsigned long long)inode_get_number(old_parent),
			    (unsigned long long)inode_get_number(new_parent));
		}
	}
}

static gfarm_error_t
inode_create_link_orphan_inode(struct inode *base, const char *name,
	struct user *user, struct inode *inode, struct dirset *tdirset)
{
	gfarm_error_t e;
	struct inode_activity *ia;

	ia = inode_activity_alloc_or_update(&inode->u.c.activity, tdirset);
	if (ia == NULL) {
		gflog_debug(GFARM_MSG_1004750,
			"inode_activity_alloc() failed");
		return (GFARM_ERR_NO_MEMORY);
	}

	e = inode_create_link_internal(base, name, user, inode);

	/* inode_activity_free_try() may free tdirset, so we need protection */
	if (tdirset != TDIRSET_IS_UNKNOWN && tdirset != TDIRSET_IS_NOT_SET)
		dirset_add_ref(tdirset);

	if (inode_activity_free_try(inode))
		inode_remove_try(inode, tdirset);

	if (tdirset != TDIRSET_IS_UNKNOWN && tdirset != TDIRSET_IS_NOT_SET)
		dirset_del_ref(tdirset);

	return (e);
}

/*
 * NOTE: it's disallowed to call this from outside of inode_check_and_repair()
 */
static gfarm_error_t
inode_link_to_lost_found(struct inode *inode)
{
	gfarm_error_t e;
	struct inode *base;
	struct user *admin;
	static char name[16 + 16 + 1];
	int name_len;

	base = inode_lookup_lost_found();
	if (base == NULL)
		return (GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY);
	admin = user_tenant_lookup_including_invalid(ADMIN_USER_NAME);
	if (admin == NULL)
		return (GFARM_ERR_NO_SUCH_USER);

	name_len = sizeof(name);
	snprintf(name, name_len, "%016llX%016llX",
	    (unsigned long long)inode_get_number(inode),
	    (unsigned long long)inode_get_gen(inode));
	/*
	 * this function is only called from inode_check_and_repair(), and
	 * inode_check_and_repair() is called before the first invocation of
	 * dirquota_check_main() which is called from quota_check_init().
	 * thus, TDIRSET_IS_NOT_SET is OK in this phase.
	 */
	e = inode_create_link_orphan_inode(base, name, admin, inode,
	    TDIRSET_IS_NOT_SET);
	if (e == GFARM_ERR_NO_ERROR) {
		inode->i_nlink_ini++;
		if (inode_is_dir(inode)) {
			inode_dir_check_and_repair_dotdot(inode, base);
			inode->u.c.s.d.parent_dir = base;
		}
	}

	return (e);
}

gfarm_error_t
inode_create_file_in_lost_found(
	struct host *host,
	gfarm_ino_t inum_old, gfarm_uint64_t gen_old, gfarm_off_t size,
	struct gfarm_timespec *mtime, struct inode **inodep)
{
	gfarm_error_t e;
	struct inode *lf, *n;
	struct user *admin;
	char fname[16 + 16 + GFARM_HOST_NAME_MAX + 2];

	lf = inode_lookup_lost_found();
	if (lf == NULL)
		return (GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY);
	admin = user_tenant_lookup_including_invalid(ADMIN_USER_NAME);
	if (admin == NULL)
		return (GFARM_ERR_NO_SUCH_USER);

	n = inode_alloc();
	if (n == NULL)
		return (GFARM_ERR_NO_MEMORY);
	inode_init_file(n);
	n->i_nlink = 0;
	n->i_mode |= 0400;
	n->i_user = admin;
	n->i_group = lf->i_group;
	n->i_size = size;
	inode_created(n);
	inode_set_mtime_in_cache(n, mtime);
	inode_db_init(n);

	snprintf(fname, (int)sizeof(fname), "%016llX%016llX-%s",
	    (unsigned long long)inum_old,
	    (unsigned long long)gen_old, host_name(host));


	/*
	 * protect `n' from being incorrectly removed by inode_remove_try() in
	 * inode_create_link_orphan_inode()
	 */
	n->i_nlink++;

	/* `n' is logically a new file, thus TDIRSET_IS_NOT_SET is OK */
	e = inode_create_link_orphan_inode(lf, fname, admin, n,
	    TDIRSET_IS_NOT_SET); /* i_nlink++ */

	n->i_nlink--; /* undo the protection above */

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1003477,
		    "inode %lld:%lld on %s -> %lld:%lld: "
		    "cannot create /%s/%s: %s",
		    (long long)inum_old, (long long)gen_old, host_name(host),
		    (long long)inode_get_number(n),
		    (long long)inode_get_gen(n),
		    lost_found, fname, gfarm_error_string(e));
		inode_release(n);
		return (e);
	}
	e = db_inode_nlink_modify(inode_get_number(n), n->i_nlink);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1005166,
		    "db_inode_nlink_modify(%llu:%llu): %s",
		    (unsigned long long)inode_get_number(n),
		    (unsigned long long)inode_get_gen(n),
		    gfarm_error_string(e));
	/* abandon `e' */

	quota_update_file_add(n, inode_search_tdirset(lf));

	*inodep = n;
	return (GFARM_ERR_NO_ERROR);
}

static void
inode_link_to_lost_found_and_report(struct inode *inode)
{
	gfarm_error_t e;

	gflog_warning(GFARM_MSG_1005167,
	    "inode %llu:%llu is not referenced, moving to /%s",
	    (unsigned long long)inode_get_number(inode),
	    (unsigned long long)inode_get_gen(inode),
	    lost_found);
	/* move to the /lost+found directory */
	e = inode_link_to_lost_found(inode);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1005168,
		    "failed to link inode %llu:%llu in /%s: %s",
		    (unsigned long long)inode_get_number(inode),
		    (unsigned long long)inode_get_gen(inode),
		    lost_found, gfarm_error_string(e));
	}
}

#if 0 /* disable recursive call, because thread stack is relatively smaller */

/* recursive version */
static int
inode_foreach_in_subtree(struct inode *inode,
	void *closure, int (*callback)(void *, struct inode *))
{
	int interrupted;
	Dir dir;
	DirCursor cursor;

	interrupted = (*callback)(closure, inode);
	if (interrupted || !inode_is_dir(inode))
		return (interrupted);

	dir = inode_get_dir(inode);
	if (dir == NULL)
		return (0);
	if (!dir_cursor_set_pos(dir, 0, &cursor))
		return (0);
	for (;;) {
		const char *name;
		int namelen;
		DirEntry entry = dir_cursor_get_entry(dir, &cursor);

		assert(entry != NULL);
		name = dir_entry_get_name(entry, &namelen);
		if (!name_is_dot_or_dotdot(name, namelen)) {
			if (inode_foreach_in_subtree(
			    dir_entry_get_inode(entry), closure, callback))
				return (1); /* stop traversing */
		}
		if (!dir_cursor_next(dir, &cursor))
			break;
	}
	return (0);
}
#else /* non-recursive version */
static int
inode_foreach_in_subtree(struct inode *inode,
	void *closure, int (*callback)(void *, struct inode *))
{
	int interrupted;
	static const char diag[] = "inode_foreach_in_subtree";

	struct {
		Dir dir;
		DirCursor cursor;
	} *dirs, *tmp_dirs;
	int depth = 0, max_depth = DIR_DEPTH_BUF_INIT;

	interrupted = (*callback)(closure, inode);
	if (interrupted || !inode_is_dir(inode))
		return (interrupted);

	GFARM_MALLOC_ARRAY(dirs, max_depth);
	if (dirs == NULL) {
		gflog_error(GFARM_MSG_1004671,
		    "%s: no memory for %d depth dir %lld:%lld",
		    diag, max_depth,
		    (long long)inode_get_number(inode),
		    (long long)inode_get_gen(inode));
		return (0);
	}

	dirs[depth].dir = inode_get_dir(inode);
	if (dirs[depth].dir == NULL) {
		free(dirs);
		return (0);
	}
	if (!dir_cursor_set_pos(dirs[depth].dir, 0, &dirs[depth].cursor)) {
		free(dirs);
		return (0);
	}
	for (;;) {
		const char *name;
		int namelen;
		DirEntry entry = dir_cursor_get_entry(
		    dirs[depth].dir, &dirs[depth].cursor);

		assert(entry != NULL);
		name = dir_entry_get_name(entry, &namelen);
		if (!name_is_dot_or_dotdot(name, namelen)) {

			inode = dir_entry_get_inode(entry);
			if ((*callback)(closure, inode)) {
				interrupted = 1;
				break;
			}

			if (inode_is_dir(inode)) {
				++depth;
				if (depth >= max_depth) {
					int tmp_depth = max_depth + max_depth;

					GFARM_REALLOC_ARRAY(
					    tmp_dirs, dirs, tmp_depth);
					if (tmp_dirs == NULL) {
						gflog_error(GFARM_MSG_1004672,
						    "%s: no memory for %d "
						    "depth dir %lld:%lld:",
						    diag, tmp_depth,
						    (long long)
						    inode_get_number(inode),
						    (long long)
						    inode_get_gen(inode));
					} else {
						dirs = tmp_dirs;
						max_depth = tmp_depth;
					}
				}
				if (depth < max_depth) {
					dirs[depth].dir = inode_get_dir(inode);
					if (dirs[depth].dir != NULL &&
					    dir_cursor_set_pos(dirs[depth].dir,
					    0, &dirs[depth].cursor)) {
						/* one-level deeper */
						continue;
					}
				}
				/* failed to traverse subdir */
				--depth;
			}
		}
		while (!dir_cursor_next(dirs[depth].dir,
		    &dirs[depth].cursor)) {
			if (depth <= 0)
				goto completed;
			--depth;
		}
	}
completed:
	free(dirs);
	return (interrupted);
}
#endif /* non-recursive version */

/* returns 1, if stopped, or tree is modified during giant_lock release */
int
inode_foreach_in_subtree_interruptible(struct inode *inode, void *closure,
	enum inode_scan_choice (*callback)(void *, struct inode *),
	int (*interval)(void *))
{
	Dir dir;
	gfarm_off_t pos;
	DirCursor cursor;
	enum inode_scan_choice scan_choice;
	int interrupted = 0;
	static const char diag[] = "inode_foreach_in_subtree_interruptible";

	struct {
		gfarm_ino_t dir_inum;
		gfarm_uint64_t dir_igen;
		gfarm_off_t cursor_pos;
	} *dirs, *tmp_dirs;
	int depth = 0, max_depth = DIR_DEPTH_BUF_INIT;

	scan_choice = (*callback)(closure, inode);
	if (scan_choice == INODE_SCAN_INTERRUPT)
		return (1); /* interrupted */
	if (scan_choice == INODE_SCAN_RELEASE_GIANT_LOCK) {
		gfarm_ino_t inum = inode_get_number(inode);
		gfarm_uint64_t igen = inode_get_gen(inode);

		giant_unlock();
		if (interval != NULL)
			interrupted = (*interval)(closure);
		giant_lock();
		if (interrupted)
			return (1); /* interrupted */

		inode = inode_lookup(inum);
		if (inode == NULL || inode_get_gen(inode) != igen)
			return (1); /* interrupted */
	}
	if (!inode_is_dir(inode))
		return (0);

	GFARM_MALLOC_ARRAY(dirs, max_depth);
	if (dirs == NULL) {
		gflog_error(GFARM_MSG_1004673,
		    "%s: no memory for %d depth dir %lld:%lld",
		    diag, max_depth,
		    (long long)inode_get_number(inode),
		    (long long)inode_get_gen(inode));
		return (0);
	}

	dirs[depth].dir_inum = inode_get_number(inode);
	dirs[depth].dir_igen = inode_get_gen(inode);
	dirs[depth].cursor_pos = 0; /* this is not used */

	dir = inode_get_dir(inode);
	if (dir == NULL) {
		free(dirs);
		return (0);
	}
	pos = 0;
	if (!dir_cursor_set_pos(dir, pos, &cursor)) {
		free(dirs);
		return (0);
	}
	for (;;) {
		const char *name;
		int namelen;
		DirEntry entry = dir_cursor_get_entry(dir, &cursor);

		assert(entry != NULL);
		name = dir_entry_get_name(entry, &namelen);
		if (!name_is_dot_or_dotdot(name, namelen)) {
			inode = dir_entry_get_inode(entry);
			scan_choice = (*callback)(closure, inode);
			if (scan_choice == INODE_SCAN_INTERRUPT) {
				interrupted = 1;
				break;
			}
			if (scan_choice == INODE_SCAN_RELEASE_GIANT_LOCK) {
				gfarm_ino_t inum = inode_get_number(inode);
				gfarm_uint64_t igen = inode_get_gen(inode);

				giant_unlock();
				if (interval != NULL)
					interrupted = (*interval)(closure);
				giant_lock();
				if (interrupted)
					break;

				inode = inode_lookup(dirs[depth].dir_inum);
				if (inode == NULL ||
				    inode_get_gen(inode) !=
				    dirs[depth].dir_igen ||
				    (dir = inode_get_dir(inode)) == NULL ||
				    !dir_cursor_set_pos(dir, pos, &cursor) ||
				    (entry = dir_cursor_get_entry(dir, &cursor)
				    ) == NULL ||
				    (inode = dir_entry_get_inode(entry))
				    == NULL ||
				    inode_get_number(inode) != inum ||
				    inode_get_gen(inode) != igen) {
					interrupted = 1;
					break;
				}
			}

			if (inode_is_dir(inode)) {
				++depth;
				if (depth >= max_depth) {
					int tmp_depth = max_depth + max_depth;

					GFARM_REALLOC_ARRAY(
					    tmp_dirs, dirs, tmp_depth);
					if (tmp_dirs == NULL) {
						gflog_error(GFARM_MSG_1004674,
						    "%s: no memory for %d "
						    "depth dir %lld:%lld:",
						    diag, tmp_depth,
						    (long long)
						    inode_get_number(inode),
						    (long long)
						    inode_get_gen(inode));
					} else {
						dirs = tmp_dirs;
						max_depth = tmp_depth;
					}
				}
				if (depth < max_depth) {
					Dir tmp_dir = inode_get_dir(inode);

					if (tmp_dir != NULL &&
					    dir_cursor_set_pos(tmp_dir, 0,
					    &cursor)) {
						/* one-level deeper */
						dirs[depth].dir_inum =
						    inode_get_number(inode);
						dirs[depth].dir_igen =
						    inode_get_gen(inode);
						dirs[depth].cursor_pos = pos;

						dir = tmp_dir;
						pos = 0;
						continue;
					}
				}
				/* failed to traverse subdir */
				--depth;
			}
		}
		while (!dir_cursor_next(dir, &cursor)) {
			if (depth <= 0)
				goto completed;

			pos = dirs[depth].cursor_pos;
			--depth;

			inode = inode_lookup(dirs[depth].dir_inum);
			if (inode == NULL ||
			    inode_get_gen(inode) != dirs[depth].dir_igen ||
			    (dir = inode_get_dir(inode)) == NULL ||
			    !dir_cursor_set_pos(dir, pos, &cursor) ||
			    (entry = dir_cursor_get_entry(dir, &cursor))
			    == NULL ||
			    (inode = dir_entry_get_inode(entry)) == NULL ||
			    inode_get_number(inode) !=
			    dirs[depth + 1].dir_inum ||
			    inode_get_gen(inode) != dirs[depth + 1].dir_igen) {
				interrupted = 1;
				goto completed;
			}
		}
		/* currently, `pos' is the sequence number in the directory */
		++pos;
	}
completed:
	free(dirs);
	return (interrupted);
}


static int
is_all_hardlinks_within_subtree_per_inode(
	void *closure, struct uint64_to_uint64_map_entry *entry)
{
	gfarm_ino_t inum = uint64_to_uint64_map_entry_key(entry);
	struct inode *inode = inode_lookup(inum);
	static const char diag[] = "is_all_hardlinks_within_subtree_per_inode";

	if (inode == NULL) { /* shouldn't happen */
		gflog_error(GFARM_MSG_1004675, "%s: inum %lld: not found",
		    diag, (long long)inum);
		return (1); /* interrupted */
	}
	/* number of directory entries in subtree does not match nlink */
	if (uint64_to_uint64_map_entry_value(entry) != inode_get_nlink(inode))
		return (1); /* interrupted */

	return (0); /* OK */
}

static int
is_all_hardlinks_within_subtree(struct uint64_to_uint64_map *hardlink_counters)
{
	return (!uint64_to_uint64_map_foreach(hardlink_counters,
	    NULL, is_all_hardlinks_within_subtree_per_inode));
}

struct is_ok_to_change_dirset_from_state {
	struct uint64_to_uint64_map *hardlink_counters;
	struct dirset *src_tdirset;
};

static int
is_ok_to_change_dirset_from_per_inode(void *closure, struct inode *inode)
{
	struct is_ok_to_change_dirset_from_state *state = closure;

	/*
	 * if src_tdirset == TDIRSET_IS_NOT_SET, dst_tdirset must be set.
	 * check if the result of this rename causes nested quota_dir
	 */
	if (state->src_tdirset == TDIRSET_IS_NOT_SET && inode_is_dir(inode)) {
		struct dirset *ds =
		    quota_dir_get_dirset_by_inum(inode_get_number(inode));

		/* disallow nested quota_dir, even if gfarmroot */
		if (ds != NULL)
			return (1); /* interrupted */
	}

	if (inode_is_file(inode) && inode_get_nlink(inode) > 1) {
		if (!uint64_to_uint64_map_inc_value(state->hardlink_counters,
		    inode_get_number(inode), NULL)) {
			gflog_error(GFARM_MSG_1004676,
			    "dirquota_check: no memory for %lld hardlinks",
			    (long long)uint64_to_uint64_map_size(
			    state->hardlink_counters));
			return (1); /* interrupted */
		}
	}
	return (0);
}

static int
is_ok_to_change_dirset_from(struct inode *movee, struct dirset *src_tdirset)
{
	struct is_ok_to_change_dirset_from_state state;
	int ok;

	state.hardlink_counters = uint64_to_uint64_map_new();
	if (state.hardlink_counters == NULL) {
		gflog_error(GFARM_MSG_1004677,
		    "inode_rename: no memory for hardlink counter");
		return (0);
	}
	state.src_tdirset = src_tdirset;

	ok = !inode_foreach_in_subtree(movee, &state,
	    is_ok_to_change_dirset_from_per_inode);
	if (ok)
		ok = is_all_hardlinks_within_subtree(state.hardlink_counters);

	uint64_to_uint64_map_free(state.hardlink_counters);
	return (ok);
}

int
inode_is_ok_to_set_dirset(struct inode *inode)
{
	return (is_ok_to_change_dirset_from(inode, TDIRSET_IS_NOT_SET));
}

static int
inode_subtree_fixup_tdirset_per_inode(void *closure, struct inode *inode)
{
	struct dirset *tdirset = closure;
	struct inode_activity *ia = inode->u.c.activity;

	if (ia != NULL) {
		/* maintain refcount */
		if (tdirset != TDIRSET_IS_UNKNOWN &&
		    tdirset != TDIRSET_IS_NOT_SET)
			dirset_add_ref(tdirset);
		/* add_ref first, then del_ref */
		if (ia->tdirset != TDIRSET_IS_UNKNOWN &&
		    ia->tdirset != TDIRSET_IS_NOT_SET)
			dirset_del_ref(ia->tdirset);

		ia->tdirset = tdirset;
	}
	return (0);
}

void
inode_subtree_fixup_tdirset(struct inode *inode, struct dirset *tdirset)
{
	inode_foreach_in_subtree(inode, tdirset,
	    inode_subtree_fixup_tdirset_per_inode);
}

static int
is_ok_to_move_to(struct inode *movee, struct inode *dst_dir)
{
	DirEntry entry;
	int depth = 0;
	struct inode *dir = dst_dir;

	for (;;) {
		if (movee == dir) /* movee is an ancestor of dir */
			return (0);

		if (!inode_is_dir(dir)) /* something is going wrong */
			return (0);

		/* do not stop at tenant root, check until real root */
		if (inode_get_number(dir) == ROOT_INUMBER)
			return (1);

		entry = dir_lookup(dir->u.c.s.d.entries, DOTDOT, DOTDOT_LEN);
		if (entry == NULL)
			return (0);
		dir = dir_entry_get_inode(entry);

		/* this check is not strictly necessary, but... */
		if (++depth >= gfarm_max_directory_depth) {
			gflog_notice(GFARM_MSG_1004759,
			    "moving inode %lld:%lld to directory %lld:%lld "
			    "is requested, but the dir depth reaches %d",
			    (long long)movee->i_number,
			    (long long)movee->i_gen,
			    (long long)dst_dir->i_number,
			    (long long)dst_dir->i_gen,
			    gfarm_max_directory_depth);

			return (0);
		}
	}
}

gfarm_error_t
inode_rename(
	struct inode *sdir, const char *sname,
	struct inode *ddir, const char *dname,
	struct process *process, struct peer *peer,
	struct inode_trace_log_info *srctp,
	struct inode_trace_log_info *dsttp,
	int *dst_removed, int *hlink_removed, /* for gfarm_file_trace */
	const char *diag)
{
	gfarm_error_t e;
	struct user *user = process_get_user(process);
	struct inode *src, *dst;
	struct dirset *src_tdirset, *dst_tdirset;
	int dirquota_adjust = 0, dirquota_root_adjust = 0;
	struct file_opening *fo = NULL;

	if (user == NULL) {
		gflog_debug(GFARM_MSG_1001742, "process_get_user() failed");
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	}

	/* can remove src? */
	if ((e = inode_access(sdir, process_get_tenant(process), user,
	    GFS_X_OK|GFS_W_OK)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001743,
			"inode_access() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if ((e = inode_lookup_by_name(sdir, sname, process, &src))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001744,
			"inode_lookup_by_name() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if ((inode_get_mode(sdir) & GFARM_S_ISTXT) != 0 &&
	    !is_removable_in_sticky_dir(sdir, src, user,
	    "rename op", sname, strlen(sname)))
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);

	if (gfarm_ctxp->file_trace && srctp != NULL) {
		srctp->inum = inode_get_number(src);
		srctp->igen = inode_get_gen(src);
		srctp->imode = inode_get_mode(src);
	}

	if (strchr(sname, '/') != NULL) {  /* sname should't have '/' */
		gflog_debug(GFARM_MSG_1001745,
			"argument 'sname' is invalid");
		return (GFARM_ERR_INVALID_ARGUMENT);
	}
	if (strchr(dname, '/') != NULL) { /* dname should't have '/' */
		gflog_debug(GFARM_MSG_1001746,
			"argument 'dname' is invalid");
		return (GFARM_ERR_INVALID_ARGUMENT);
	}

	if (!is_ok_to_move_to(src, ddir)) {
		gflog_debug(GFARM_MSG_1002815,
		    "rename(%llu:%s, %llu:%s): "
		    "the former is ancestor of the directory of latter",
		    (unsigned long long)inode_get_number(sdir), sname,
		    (unsigned long long)inode_get_number(ddir), dname);
		return (GFARM_ERR_INVALID_ARGUMENT);
	}

	/* NOTE: sname is NOT ".." here, is_ok_to_move_to() does check that */
	src_tdirset = inode_get_tdirset(sdir);
	if (src_tdirset == TDIRSET_IS_UNKNOWN)
		src_tdirset = inode_search_tdirset(sdir);
	dst_tdirset = inode_get_tdirset(ddir);
	if (dst_tdirset == TDIRSET_IS_UNKNOWN)
		dst_tdirset = inode_search_tdirset(ddir);
	if (src_tdirset == TDIRSET_IS_UNKNOWN ||
	    dst_tdirset == TDIRSET_IS_UNKNOWN) {
		gflog_notice(GFARM_MSG_1004678,
		    "%s: unknown dirset: %lld:%lld (%p) vs %lld:%lld (%p)",
		    diag,
		    (long long)inode_get_number(sdir),
		    (long long)inode_get_gen(sdir), src_tdirset,
		    (long long)inode_get_number(ddir),
		    (long long)inode_get_gen(ddir), dst_tdirset);
		return (GFARM_ERR_INTERNAL_ERROR);
	}

	if (src_tdirset == TDIRSET_IS_NOT_SET && inode_is_dir(src)) {
		struct dirset *ds =
		    quota_dir_get_dirset_by_inum(inode_get_number(src));

		if (ds != NULL) {
			/* disallow nested quota_dir, even if gfarmroot */
			if (dst_tdirset != TDIRSET_IS_NOT_SET)
				return (GFARM_ERR_OPERATION_NOT_SUPPORTED);

			src_tdirset = ds;
		}
	}

	if (src_tdirset != dst_tdirset) {
		gfarm_uint64_t ncopy = 0;

		if (inode_is_dir(src)) {
			/*
			 * use user_is_super_root() instead of
			 * user_is_root_for_inode() here,
			 * because dirquota_root_adjust may take giant_lock
			 * too long period,
			 * and only real gfarmroot is allowed to do such thing
			 * usually during maintenance.
			 */
			if (!user_is_super_root(process_get_user(process))) {
				/*
				 * We don't return GFARM_ERR_CROSS_DEVICE_LINK,
				 * to avoid mv(1) from automatically doing
				 *"cp -r" + "rm -r"
				 */
				return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
			}
			if (!is_ok_to_change_dirset_from(src, src_tdirset))
				return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
			/* we dont care limit_check, because it's gfarmroot */
			dirquota_root_adjust = 1;
		} else if (inode_is_file(src) && inode_get_nlink(src) > 1) {
			/*
			 * hardlinked file cannot be moved even by gfarmroot.
			 * state.hardlink_counters above does same check
			 * for hardlinked file inside the src directory case.
			 */
			return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
		} else {
			if (inode_is_file(src))
				ncopy = inode_get_ncopy_with_dead_host(src);
			e = dirquota_limit_check(dst_tdirset,
			    1, ncopy, inode_get_size(src));
			if (e != GFARM_ERR_NO_ERROR)
				return (e);
			dirquota_adjust = 1;
		}
	}

	/*
	 * make sure that src has inode_activity,
	 * otherwise inode_create_link_only() may fail
	 */
	fo = file_opening_alloc(src, peer, NULL, GFARM_FILE_LOOKUP);
	if (fo == NULL)
		return (GFARM_ERR_NO_MEMORY);
	e = inode_open(fo, src_tdirset);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1004679,
		    "inode_open() for rename: %s",
		    gfarm_error_string(e));
		file_opening_free(fo, inode_get_mode(src));
	}

	e = inode_lookup_by_name(ddir, dname, process, &dst);
	if (e == GFARM_ERR_NO_ERROR) {
		if (src == dst) {
			inode_close(fo, NULL, diag);
			file_opening_free(fo, inode_get_mode(src));
			return (GFARM_ERR_NO_ERROR);
		}

		if (gfarm_ctxp->file_trace && dsttp != NULL) {
			dsttp->inum = inode_get_number(dst);
			dsttp->igen = inode_get_gen(dst);
			dsttp->imode = inode_get_mode(dst);
		}
		if (GFARM_S_ISDIR(inode_get_mode(src)) ==
		    GFARM_S_ISDIR(inode_get_mode(dst))) {
			e = inode_unlink(ddir, dname, process,
				NULL, hlink_removed);
			if (e != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1001747,
					"inode_unlink() failed: %s",
					gfarm_error_string(e));
				inode_close(fo, NULL, diag);
				file_opening_free(fo, inode_get_mode(src));
				return (e);
			} else {
				*dst_removed = 1;
			}
		} else if (GFARM_S_ISDIR(inode_get_mode(src))) {
			gflog_debug(GFARM_MSG_1001748,
				"inode 'inode_get_mode(src)' "
				"is not a directory");
			inode_close(fo, NULL, diag);
			file_opening_free(fo, inode_get_mode(src));
			return (GFARM_ERR_NOT_A_DIRECTORY);
		} else {
			gflog_debug(GFARM_MSG_1001749,
				"inode 'inode_get_mode(src)' is directory");
			inode_close(fo, NULL, diag);
			file_opening_free(fo, inode_get_mode(src));
			return (GFARM_ERR_IS_A_DIRECTORY);
		}
	} else if (e != GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY) {
		inode_close(fo, NULL, diag);
		file_opening_free(fo, inode_get_mode(src));
		return (e);
	}

	if (dirquota_adjust || dirquota_root_adjust) {
		/*
		 * fabricate src tdirset to avoid GFARM_ERR_CROSS_DEVICE_LINK
		 */

		/* maintain refcount */
		if (dst_tdirset != TDIRSET_IS_UNKNOWN &&
		    dst_tdirset != TDIRSET_IS_NOT_SET)
			dirset_add_ref(dst_tdirset);
		/* add_ref first, then del_ref */
		if (src->u.c.activity->tdirset != TDIRSET_IS_UNKNOWN &&
		    src->u.c.activity->tdirset != TDIRSET_IS_NOT_SET)
			dirset_del_ref(src->u.c.activity->tdirset);

		src->u.c.activity->tdirset = dst_tdirset;
	}
	e = inode_create_link_only(ddir, dname, process, src);
	if (e != GFARM_ERR_NO_ERROR) {
		/* undo src tdirset change */
		if (dirquota_adjust || dirquota_root_adjust) {

			/* maintain refcount */
			if (src_tdirset != TDIRSET_IS_UNKNOWN &&
			    src_tdirset != TDIRSET_IS_NOT_SET)
				dirset_add_ref(src_tdirset);
			/* add_ref first, then del_ref */
			if (src->u.c.activity->tdirset != TDIRSET_IS_UNKNOWN &&
			    src->u.c.activity->tdirset != TDIRSET_IS_NOT_SET)
				dirset_del_ref(src->u.c.activity->tdirset);

			src->u.c.activity->tdirset = src_tdirset;
		}

		/*
		 * this may fail (e.g. GFARM_ERR_PERMISSION_DENIED),
		 * but doesn't have to undo the inode_unlink() operation above,
		 * because the above inode_unlink() should already fail
		 * in such cases.
		 */
		gflog_debug(GFARM_MSG_1000319,
		    "rename(%s, %s): failed to link: %s",
		    sname, dname, gfarm_error_string(e));
		inode_close(fo, NULL, diag);
		file_opening_free(fo, inode_get_mode(src));
		return (e);
	}

	inode_close(fo, NULL, diag);
	file_opening_free(fo, inode_get_mode(src));
	if (dirquota_adjust) {
		/*
		 * leave src->u.c.activity->tdirset as is, because it's correct
		 * now, and we have to tell the new tdirset to simultaneously
		 * opening processes.
		 */
		dirquota_update_file_add(src, dst_tdirset);
		tdirset_notify_changed(dst_tdirset); /* may delay */
	} else if (dirquota_root_adjust) {
		inode_subtree_fixup_tdirset(src, dst_tdirset);
		dirquota_invalidate(dst_tdirset);
		tdirset_notify_changed(dst_tdirset); /* may delay */
	}

	e = inode_lookup_relative(sdir, sname, GFS_DT_UNKNOWN, INODE_REMOVE,
	    process, 0, NULL, &src, NULL);
	if (e != GFARM_ERR_NO_ERROR) /* shouldn't happen */
		gflog_error(GFARM_MSG_1000320,
		    "rename(%s, %s): failed to unlink: %s",
		    sname, dname, gfarm_error_string(e));
	if (e == GFARM_ERR_NO_ERROR) {
		int num;

		if (inode_is_dir(src)) {
			e = inode_dir_reparent(src, sdir, ddir);
			if (e != GFARM_ERR_NO_ERROR) /* shouldn't happen */
				gflog_error(GFARM_MSG_1002816,
				    "rename(%s, %s): failed to reparent: %s",
				    sname, dname, gfarm_error_string(e));
		}
		if (dirquota_adjust) {
			dirquota_update_file_remove(src, src_tdirset);
			tdirset_notify_changed(src_tdirset); /* may delay */
		} else if (dirquota_root_adjust) {
			dirquota_invalidate(src_tdirset);
			dirquota_fixup_schedule();
			tdirset_notify_changed(src_tdirset); /* may delay */
		}

		if (sdir != ddir && (inode_is_dir(src) || inode_is_file(src))
		    && (!inode_has_desired_number(src, &num) &&
			!inode_has_repattr(src, NULL)))
			replica_check_start_move();
	}
	/* db_inode_nlink_modify() is not necessary, because it's unchanged */
	return (e);
}

gfarm_error_t
inode_unlink(struct inode *base, const char *name, struct process *process,
	struct inode_trace_log_info *inodetp,
	int *hlink_removed) /* for gfarm_file_trace */
{
	struct inode *inode;
	gfarm_error_t e = inode_lookup_by_name(base, name, process, &inode);
	struct dirset *dirset = NULL;

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001750,
			"inode_lookup_by_name() failed: %s",
			gfarm_error_string(e));
		return (e);
	}

	if (gfarm_ctxp->file_trace && inodetp != NULL) {
		inodetp->inum = inode_get_number(inode);
		inodetp->igen = inode_get_gen(inode);
		inodetp->imode = inode_get_mode(inode);
	}

	if (inode_is_file(inode)) {
		e = inode_lookup_relative(base, name, GFS_DT_REG, INODE_REMOVE,
		    process, 0, NULL, &inode, NULL);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001751,
				"inode_lookup_relative() failed: %s",
				gfarm_error_string(e));
			return (e);
		}
		if (inode->i_nlink > 0) {
			e = db_inode_nlink_modify(inode->i_number,
			    inode->i_nlink);
			if (e != GFARM_ERR_NO_ERROR)
				gflog_error(GFARM_MSG_1000321,
				    "db_inode_nlink_modify(%lld): %s",
				    (unsigned long long)inode->i_number,
				    gfarm_error_string(e));
			*hlink_removed = 1;
			return (GFARM_ERR_NO_ERROR);
		}
	} else if (inode_is_dir(inode)) {
		if (!inode_dir_is_empty(inode)) {
			gflog_debug(GFARM_MSG_1001752,
				"directory is not empty");
			return (GFARM_ERR_DIRECTORY_NOT_EMPTY);
		} else if (string_is_dot_or_dotdot(name)) {
			gflog_debug(GFARM_MSG_1001753,
				"argument 'name' is invalid");
			return (GFARM_ERR_INVALID_ARGUMENT);
		}

		e = inode_lookup_relative(base, name, GFS_DT_DIR, INODE_REMOVE,
		    process, 0, NULL, &inode, NULL);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001754,
				"inode_lookup_relative() failed: %s",
				gfarm_error_string(e));
			return (e);
		}

		dirset = quota_dir_get_dirset_by_inum(inode->i_number);
		/* if this is a top-directory of dirquota, dirset != NULL */

		e = db_direntry_remove(inode->i_number, DOT, DOT_LEN);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1000322,
			    "db_direntry_remove(%lld, %s): %s",
			    (unsigned long long)inode->i_number, DOT,
			    gfarm_error_string(e));

		/*
		 * this i_nlink change will be written to db at the end of
		 * this function.
		 */
		inode->i_nlink--;

		e = db_direntry_remove(inode->i_number, DOTDOT, DOTDOT_LEN);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1000323,
			    "db_direntry_remove(%lld, %s): %s",
			    (unsigned long long)inode->i_number, DOTDOT,
			    gfarm_error_string(e));

		base->i_nlink--;
		e = db_inode_nlink_modify(base->i_number, base->i_nlink);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1002817,
			    "db_inode_nlink_modify(%llu): %s",
			    (unsigned long long)base->i_number,
			    gfarm_error_string(e));

		if (dirset != NULL) {
			/* may fail, if dirset is set by an ancestor */
			(void)quota_dir_remove(inode->i_number);
		}

	} else if (inode_is_symlink(inode)) {
		e = inode_lookup_relative(base, name, GFS_DT_LNK, INODE_REMOVE,
		    process, 0, NULL, &inode, NULL);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001755,
				"inode_lookup_relative() failed: %s",
				gfarm_error_string(e));
			return (e);
		}
		assert(inode->i_nlink == 0);
		e = db_symlink_remove(inode->i_number);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1000324,
			    "db_symlink_remove(%lld): %s",
			    (unsigned long long)inode->i_number,
			    gfarm_error_string(e));
	} else {
		gflog_fatal(GFARM_MSG_1000325,
		    "inode_unlink: unknown inode type");
		/*NOTREACHED*/
		return (GFARM_ERR_UNKNOWN);
	}
	if (!inode_remove_try(inode,
	    dirset != NULL ? dirset : inode_search_tdirset(base))) {
		/* there are some processes which open this file */
		/* leave this inode until closed */

		e = db_inode_nlink_modify(inode->i_number, inode->i_nlink);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1000326,
			    "db_inode_nlink_modify(%lld): %s",
			    (unsigned long long)inode->i_number,
			    gfarm_error_string(e));

	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
inode_open(struct file_opening *fo, struct dirset *tdirset)
{
	struct inode *inode = fo->inode;
	struct inode_activity *ia =
	    inode_activity_alloc_or_update(&inode->u.c.activity, tdirset);

	if (ia == NULL) {
		gflog_debug(GFARM_MSG_1001756,
			"inode_activity_alloc() failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	if ((accmode_to_op(fo->flag) & GFS_W_OK) != 0) {
		if (inode_get_size(inode) > 0 &&
		    (fo->flag & GFARM_FILE_TRUNC) == 0 &&
		    !inode_has_no_replica(inode) &&
		    !inode_has_writable_replica(inode)) {
			gflog_debug(GFARM_MSG_1005081,
			    "inode_open: NO_SPACE or READ_ONLY_FILE_SYSTEM");
			return (GFARM_ERR_NO_SPACE);
		}
		/*
		 * else...
		 *   inode_get_size(inode) == 0
		 *   || (fo->flag & GFARM_FILE_TRUNC) != 0
		 *   || inode_has_no_replica(inode) != 0
		 *   || inode_has_writable_replica(inode)) != 0
		 * related function: inode_schedule_file_default()
		 */
		++ia->u.f.writers;
	}
	if ((fo->flag & GFARM_FILE_TRUNC) != 0) {
		/* do not change the metadata for close-to-open consistency */
		fo->flag |= GFARM_FILE_TRUNC_PENDING;
	}

	fo->opening_prev = &ia->openings;
	fo->opening_next = ia->openings.opening_next;
	ia->openings.opening_next = fo;
	fo->opening_next->opening_prev = fo;
	return (GFARM_ERR_NO_ERROR);
}

struct dirset *
inode_get_tdirset(struct inode *inode)
{
	struct inode_activity *ia = inode->u.c.activity;

	if (ia == NULL)
		return (NULL); /* unknown */
	return (ia->tdirset);
}

void
inode_close(struct file_opening *fo, char **trace_logp, const char *diag)
{
	inode_close_read(fo, NULL, trace_logp, diag);
}

void
inode_close_read(struct file_opening *fo, struct gfarm_timespec *atime,
	char **trace_logp, const char *diag)
{
	struct inode *inode = fo->inode;
	struct inode_activity *ia = inode->u.c.activity;
	struct dirset *tdirset = inode_get_tdirset(inode);
	int read_only = gfarm_read_only_mode();

	if ((accmode_to_op(fo->flag) & GFS_W_OK) != 0)
		--ia->u.f.writers;
	if ((fo->flag & GFARM_FILE_TRUNC_PENDING) != 0 &&
	    ia->u.f.writers == 0) {
		/*
		 * In this case, there will be no file replica since reopen
		 * was not called or failed.  The reason why we exclude the
		 * case of writers > 0 is "lost all replica" happens if
		 * some client already opened this in write mode and the final
		 * file size is not zero.  XXX - this means a successful call
		 * of open(O_TRUNC) is ignored in this case.
		 * see SF.net #472 and #441.
		 */
		if (read_only) {
			gflog_warning(GFARM_MSG_1005169,
			    "inode %llu:%llu: GFARM_FILE_TRUNC is "
				      "not performed due to read_only",
			    (long long)inode_get_number(inode),
			    (long long)inode_get_gen(inode));
		} else {
			inode_file_update(fo, INODE_CLOSE_V2_0, 0,
			    atime, &inode->i_mtimespec,
			    NULL, NULL, trace_logp, diag);
		}
	} else {
		/* if read_only, atime update will be ignored */
		if (atime != NULL && !read_only)
			inode_set_relatime(inode, atime);
	}

	/* inode_activity_free_try() may free tdirset, so we need protection */
	if (tdirset != TDIRSET_IS_UNKNOWN && tdirset != TDIRSET_IS_NOT_SET)
		dirset_add_ref(tdirset);

	fo->opening_prev->opening_next = fo->opening_next;
	fo->opening_next->opening_prev = fo->opening_prev;
	if (ia->openings.opening_next == &ia->openings) { /* all closed */
		/*
		 * NOTE:
		 * It's possible that this client (== gfsd, in this case)
		 * failed to notify a generation_updated(_by_cookie) event.
		 * in that case, the event will be freed by
		 * peer_unset_pending_new_generation_for_process() called via
		 * peer_free() or peer_unset_process()
		 */
		inode_activity_free_try(inode);
	}

	inode_remove_try(inode, tdirset);

	if (tdirset != TDIRSET_IS_UNKNOWN && tdirset != TDIRSET_IS_NOT_SET)
		dirset_del_ref(tdirset);
}

gfarm_error_t
inode_fhclose_read(struct inode *inode, struct gfarm_timespec *atime)
{
	static const char diag[] = "inode_fhclose_read";

	if (!inode_is_file(inode)) {
		gflog_error(GFARM_MSG_1003283, "%s: not a file", diag);
		return (GFARM_ERR_STALE_FILE_HANDLE);
	}
	if (atime != NULL && !gfarm_read_only_mode())
		inode_set_relatime(inode, atime);

	return (GFARM_ERR_NO_ERROR);
}

void
inode_add_ref_spool_writers(struct inode *inode)
{
	struct inode_activity *ia = inode->u.c.activity;

	assert(ia != NULL);
	++ia->u.f.spool_writers;
}

void
inode_del_ref_spool_writers(struct inode *inode)
{
	struct inode_activity *ia = inode->u.c.activity;

	assert(ia != NULL);
	--ia->u.f.spool_writers;
}

void
inode_check_pending_replication(struct file_opening *fo)
{
	gfarm_error_t e;
	struct inode *inode = fo->inode;
	struct host *spool_host = fo->u.f.spool_host;
	struct inode_activity *ia = inode->u.c.activity;
	static const char diag[] = "inode_check_pending_replication";

	assert(spool_host != NULL && ia != NULL);

	if (host_supports_async_protocols(spool_host) &&
	    ia->u.f.spool_writers == 0 && ia->u.f.replication_pending) {
		ia->u.f.replication_pending = 0;
		if (gfarm_read_only_mode())
			e = GFARM_ERR_READ_ONLY_FILE_SYSTEM;
		else
			e = make_replicas_except(
			    inode, ia->tdirset, spool_host,
			    fo->u.f.replica_spec.desired_number,
			    fo->u.f.replica_spec.repattr,
			    inode->u.c.s.f.copies);
		/*
		 * #464 - retry automatic replication after
		 * GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE
		 *
		 * #673 - retry automatic replication when a request of
		 * replication is failure
		 */
		if (IS_REPLICA_CHECK_REQUIRED(e)) {
			replica_check_enqueue(inode, ia->tdirset,
			    fo->u.f.replica_spec.desired_number,
			    fo->u.f.replica_spec.repattr, diag);
			/*
			 * Starts replica_check thread even though
			 * there is no need to check all files.
			 */
			replica_check_start_rep_request_failed();
		}
	}
}

static void
inode_metadata_update(struct inode *inode, gfarm_off_t size,
	struct gfarm_timespec *atime, struct gfarm_timespec *mtime)
{
	struct inode_activity *ia = inode->u.c.activity;
	struct dirset *tdirset = inode_get_tdirset(inode);

	inode_set_size(inode, tdirset, size);
	if (tdirset == TDIRSET_IS_UNKNOWN) {
		gflog_notice(GFARM_MSG_1004680,
		    "inode %lld: unknown dirset, "
		    "scheduling quota_check",
		    (long long)inode_get_number(inode));
		dirquota_check_schedule();
	}
	inode_set_atime(inode, atime);
	if (ia == NULL ||
	    /* to avoid that mtime and ctime move backward */
	    gfarm_timespec_cmp(mtime, &ia->u.f.last_update) >= 0) {
		inode_set_mtime(inode, mtime);
		inode_set_ctime(inode, mtime);
		if (ia != NULL)
			ia->u.f.last_update = *mtime;
	}
}

/*
 * returns TRUE, if generation number is updated.
 *
 * spool_host may be NULL, if GFARM_FILE_TRUNC_PENDING.
 *
 * if inode_close_mode == INODE_CLOSE_V2_0, i.e. GFM_PROTO_CLOSE_WRITE:
 *	atime and mtime are all not NULL,
 *	old_genp, new_genp, trace_logp are all NULL.
 * if inode_close_mode == INODE_CLOSE_V2_4, i.e. GFM_PROTO_CLOSE_WRITE_V2_4
 *	atime and mtime are all not NULL,
 *	old_genp, new_genp, trace_logp are all not NULL.
 * if inode_close_mode == INODE_CLOSE_V2_8, i.e. GFM_PROTO_CLOSE_WRITE_V2_8
 *	size == 0, atime and mtime are all NULL,
 *	old_genp, new_genp, trace_logp are all not NULL.
 */
static int
inode_file_update_common(struct inode *inode, enum inode_close_mode close_mode,
	gfarm_off_t size,
	struct gfarm_timespec *atime, struct gfarm_timespec *mtime,
	struct host *spool_host, int desired_replica_number, char *repattr,
	gfarm_int64_t *old_genp, gfarm_int64_t *new_genp,
	char **trace_logp, const char *diag)
{
	gfarm_int64_t old_gen;
	int generation_updated = 0;

	if (close_mode < INODE_CLOSE_V2_8) {
		/*
		 * if the RPC is V2_8 or later,
		 * the following will be done at generation_updated RPC
		 */
		inode_metadata_update(inode, size, atime, mtime);
	}

	old_gen = inode->i_gen;

	if (close_mode == INODE_CLOSE_V2_0) {
		/*
		 * if the RPC is V2_4 or later,
		 * the following will be done at generation_updated RPC
		 */
		update_replicas(inode, spool_host, old_gen,
		    0, desired_replica_number, repattr, diag);
	} else {
		struct timeval tv;
		char tmp_str[4096];

		/* update generation number */
		if (old_genp != NULL)
			*old_genp = inode->i_gen;
		inode_increment_gen(inode);
		if (new_genp != NULL)
			*new_genp = inode->i_gen;
		generation_updated = 1;

		if (gfarm_ctxp->file_trace && trace_logp != NULL) {
			gettimeofday(&tv, NULL);
			snprintf(tmp_str, sizeof(tmp_str),
			    "%lld/%010ld.%06ld////"
			    "UPDATEGEN/%s/%d//%lld/%lld/%lld//////",
			    (long long int)trace_log_get_sequence_number(),
			    (long int)tv.tv_sec, (long int)tv.tv_usec,
			    gfarm_host_get_self_name(),
			    gfmd_port,
			    (long long int)inode_get_number(inode),
			    (long long int)old_gen,
			    (long long int)inode->i_gen);
			*trace_logp = strdup(tmp_str);
		}
	}

	return (generation_updated);
}

static void
inode_generation_updated(struct inode *inode,
	struct host *spool_host, int desired_replica_number, char *repattr,
	const char *diag)
{
	struct inode_activity *ia = inode->u.c.activity;
	int start_replication = 0;

	/* if there is no other writing process */
	if (ia == NULL) {
		start_replication = 1;
	} else if (ia->u.f.spool_writers == 0) {
		start_replication = 1;
		ia->u.f.replication_pending = 0;
	} else {
		ia->u.f.replication_pending = 1;
	}

	/* old_gen must be current gen - 1 at generation_updated RPC */
	update_replicas(inode, spool_host, inode->i_gen - 1,
	    start_replication, desired_replica_number, repattr, diag);
}

/*
 * returns TRUE, if generation number is updated.
 *
 * spool_host may be NULL, if GFARM_FILE_TRUNC_PENDING.
 */
int
inode_file_update(struct file_opening *fo, enum inode_close_mode close_mode,
	gfarm_off_t size,
	struct gfarm_timespec *atime, struct gfarm_timespec *mtime,
	gfarm_int64_t *old_genp, gfarm_int64_t *new_genp,
	char **trace_logp, const char *diag)
{
	struct inode *inode = fo->inode;
	int updated;

	inode_cksum_invalidate(fo);
	inode_cksum_remove(inode);

	if (close_mode == INODE_CLOSE_V2_0) {
		/*
		 * if the RPC is V2_4 or later,
		 * the following will be done at generation_updated RPC
		 */
		inode_del_ref_spool_writers(inode);
	}

	if ((updated = inode_file_update_common(inode, close_mode,
	    size, atime, mtime, fo->u.f.spool_host,
	    fo->u.f.replica_spec.desired_number, fo->u.f.replica_spec.repattr,
	    old_genp, (gfarm_int64_t *)&fo->gen, trace_logp, diag))) {
		if (new_genp)
			*new_genp = fo->gen;
	}
	return (updated);
}

/* returns TRUE, if generation number is updated. */
gfarm_error_t
inode_file_handle_update(struct inode *inode, enum inode_close_mode close_mode,
	gfarm_off_t size,
	struct gfarm_timespec *atime, struct gfarm_timespec *mtime,
	struct host *spool_host,
	gfarm_int64_t *old_genp, gfarm_int64_t *new_genp, int *gen_updatedp,
	char **trace_logp, const char *diag)
{
	struct host *writing_spool_host;

	if (!inode_has_replica(inode, spool_host)) {
		/* this replica became obsolete during gfmd failover */
		gflog_error(GFARM_MSG_1004023,
		    "inode_file_handle_update: "
		    "inode %lld:%lld modification on %s "
		    "is lost during gfmd failover, current generation %lld",
		    (long long)inode_get_number(inode), (long long)*old_genp,
		    host_name(spool_host), (long long)inode_get_gen(inode));
		return (GFARM_ERR_STALE_FILE_HANDLE);
	}

	if ((writing_spool_host = inode_writing_spool_host(inode)) != NULL &&
	    spool_host != writing_spool_host) {
		/* conflict. another replica was opened for writing */
		gflog_error(GFARM_MSG_1004024,
		    "inode_file_handle_update: "
		    "inode %lld:%lld modification on %s "
		    "conflicts with current generation %lld on %s",
		    (long long)inode_get_number(inode), (long long)*old_genp,
		    host_name(spool_host), (long long)inode_get_gen(inode),
		    host_name(writing_spool_host));
		return (GFARM_ERR_STALE_FILE_HANDLE);
	}

	inode_cksum_invalidate_all(inode);
	inode_cksum_remove(inode);

	/* replica_check will fix the unknown desired_file_number and repattr */
	*gen_updatedp = inode_file_update_common(inode, close_mode,
	    size, atime, mtime, spool_host,
	    /* desired_file_number is unknown */ 1,
	    /* repattr is unknown */ NULL,
	    old_genp, new_genp, trace_logp, diag);
	return (GFARM_ERR_NO_ERROR);
}

int
inode_is_opened_for_writing(struct inode *inode)
{
	struct inode_activity *ia = inode->u.c.activity;

	return (ia != NULL && ia->u.f.writers > 0);
}

int
inode_is_opened_for_spool_writing(struct inode *inode)
{
	struct inode_activity *ia = inode->u.c.activity;

	return (ia != NULL && ia->u.f.spool_writers > 0);
}

int
inode_is_opened_on(struct inode *inode, struct host *spool_host)
{
	struct inode_activity *ia = inode->u.c.activity;
	struct file_opening *fo;

	if (!inode_is_file(inode))
		return (0);

	if (ia != NULL &&
	    (fo = ia->openings.opening_next) != &ia->openings) {
		for (; fo != &ia->openings; fo = fo->opening_next) {
			if (spool_host == fo->u.f.spool_host)
				return (1);
		}
	}
	return (0);
}

gfarm_uint64_t
inode_get_open_status_by_host(struct inode *inode, struct host *spool_host)
{
	struct inode_activity *ia = inode->u.c.activity;
	struct file_opening *fo;
	gfarm_uint64_t opening = 0;
	int op;

	if (!inode_is_file(inode))
		return (0);

	if (ia != NULL &&
	    (fo = ia->openings.opening_next) != &ia->openings) {
		for (; fo != &ia->openings; fo = fo->opening_next) {
			if (spool_host == fo->u.f.spool_host) {
				op = accmode_to_op(fo->flag);
				if ((op & GFS_W_OK) != 0)
					opening |=
					    GFM_PROTO_REPLICA_OPENED_WRITE;
				if ((op & GFS_R_OK) != 0)
					opening |=
					    GFM_PROTO_REPLICA_OPENED_READ;
			}
		}
	}
	return (opening);
}

struct file_copy *
inode_get_file_copy(struct inode *inode, struct host *spool_host)
{
	struct file_copy *copy;

	if (!inode_is_file(inode))
		gflog_fatal(GFARM_MSG_1000330,
		    "inode_has_replica: not a file");
	for (copy = inode->u.c.s.f.copies; copy != NULL;
	    copy = copy->host_next) {
		if (copy->host == spool_host)
			return (copy);
	}
	return (NULL);
}

int
inode_has_file_copy(struct inode *inode, struct host *spool_host)
{
	if (inode_get_file_copy(inode, spool_host) == NULL)
		return (0);
	return (1); /* include !FILE_COPY_VALID and FILE_COPY_BEING_REMOVED */
}

int
inode_has_replica(struct inode *inode, struct host *spool_host)
{
	struct file_copy *copy = inode_get_file_copy(inode, spool_host);

	if (copy == NULL)
		return (0);
	return (FILE_COPY_IS_VALID(copy));
}

int
inode_has_writable_replica(struct inode *inode)
{
	struct file_copy *copy;

	for (copy = inode->u.c.s.f.copies; copy != NULL;
	    copy = copy->host_next) {
		if (FILE_COPY_IS_VALID(copy) &&
		    host_is_disk_available(copy->host, 0))
			return (1);
	}
	return (0);
}

gfarm_error_t
inode_getdirpath(struct inode *inode, struct process *process, char **namep)
{
	gfarm_error_t e;
	struct inode *parent, *dei;
	int ok;
	struct tenant *tenant = process_get_tenant(process);
	struct user *user = process_get_user(process);
	struct inode *root;
	Dir dir;
	DirEntry entry;
	DirCursor cursor;
	char *s, *name, **names;
	int i, namelen, depth = 0, max_depth = DIR_DEPTH_BUF_INIT;
	size_t totallen = 0;
	int overflow = 0;
	static const char diag[] = "inode_getdirpath";

	e = inode_lookup_root(process, 0, &root);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	GFARM_MALLOC_ARRAY(names, max_depth);
	if (names == NULL) {
		gflog_error(GFARM_MSG_1004760,
		    "%s: no memory for %d depth dir %lld:%lld",
		    diag, max_depth,
		    (long long)inode_get_number(inode),
		    (long long)inode_get_gen(inode));
		return (GFARM_ERR_NO_MEMORY);
	}

	for (; inode != root; inode = parent) {
		e = inode_lookup_relative(inode, DOTDOT, GFS_DT_DIR,
		    INODE_LOOKUP, process, 0, NULL, &parent, NULL);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001757,
				"inode_lookup_relative() failed: %s",
				gfarm_error_string(e));
			goto error_exit;
		}
		e = inode_access(parent, tenant, user, GFS_R_OK|GFS_X_OK);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001758,
				"inode_access() failed: %s",
				gfarm_error_string(e));
			goto error_exit;
		}
		/* search the inode in the parent directory. */
		/* XXX this is slow. should we create a reverse index? */
		dir = inode_get_dir(parent);
		ok = dir_cursor_set_pos(dir, 0, &cursor);
		assert(ok);
		for (;;) {
			entry = dir_cursor_get_entry(dir, &cursor);
			assert(entry != NULL);
			dei = dir_entry_get_inode(entry);
			assert(dei != NULL);
			if (dei == inode)
				break;
			ok = dir_cursor_next(dir, &cursor);
			/*
			 * For now, we won't remove a directory
			 * while it's opened
			 */
			assert(ok);
		}
		name = dir_entry_get_name(entry, &namelen);
		GFARM_MALLOC_ARRAY(s, namelen + 1);
		if (depth >= max_depth || s == NULL) {
			if (s != NULL) { /* i.e. depth >= max_depth */
				int tmp_depth = max_depth + max_depth;
				char **tmp_names;

				GFARM_REALLOC_ARRAY(tmp_names,
				    names, tmp_depth);
				if (tmp_names == NULL) {
					/* directory too deep */
					gflog_error(GFARM_MSG_1004761,
					    "%s: no memory for %d "
					    "depth dir %lld:%lld:",
					    diag, tmp_depth,
					    (long long)
					    inode_get_number(inode),
					    (long long)
					    inode_get_gen(inode));
				} else {
					names = tmp_names;
					max_depth = tmp_depth;
				}
			}
			if (depth >= max_depth || s == NULL) {
				if (s == NULL)
					gflog_error(GFARM_MSG_1004339,
					    "no memory");
				free(s);
				e = GFARM_ERR_NO_MEMORY;
				goto error_exit;
			}
		}
		memcpy(s, name, namelen);
		s[namelen] = '\0';
		names[depth++] = s;
		totallen = gfarm_size_add(&overflow, totallen, namelen);
	}
	if (depth == 0)
		GFARM_MALLOC_ARRAY(s, 1 + 1);
	else {
		s = NULL;
		totallen = gfarm_size_add(&overflow, totallen, depth + 1);
		if (!overflow)
			GFARM_MALLOC_ARRAY(s, totallen);

	}
	if (s == NULL) {
		gflog_error(GFARM_MSG_1004340, "no memory");
		e = GFARM_ERR_NO_MEMORY;
	} else if (depth == 0) {
		strcpy(s, "/");
		*namep = s;
		e = GFARM_ERR_NO_ERROR;
	} else if (overflow) {
		assert(s == NULL);
		gflog_error(GFARM_MSG_1004762,
		    "%s: pathname length %zu too long for dir %lld:%lld:",
		    diag, totallen,
		    (long long)inode_get_number(inode),
		    (long long)inode_get_gen(inode));
		e = GFARM_ERR_NO_MEMORY;
	} else {
		totallen = 0;
		for (i = depth - 1; i >= 0; --i) {
			namelen = strlen(names[i]);
			sprintf(s + totallen, "/%s", names[i]);
			totallen += namelen + 1;
		}
		*namep = s;
		e = GFARM_ERR_NO_ERROR;
	}
error_exit:
	for (i = 0; i < depth; i++)
		free(names[i]);
	free(names);
	return (e);
}

struct host *
inode_writing_spool_host(struct inode *inode)
{
	struct inode_activity *ia = inode->u.c.activity;
	struct file_opening *fo;

	if (!inode_is_file(inode)) {
		gflog_debug(GFARM_MSG_1001761,
			"not a file");
		return (NULL); /* not a file */
	}
	if (ia != NULL &&
	    (fo = ia->openings.opening_next) != &ia->openings) {
		for (; fo != &ia->openings; fo = fo->opening_next) {
			if ((accmode_to_op(fo->flag) & GFS_W_OK) != 0 &&
			    fo->u.f.spool_host != NULL)
				return (fo->u.f.spool_host);
		}
	}
	return (NULL);
}

gfarm_error_t
inode_schedule_confirm_for_write(struct file_opening *opening,
	struct host *spool_host, int *to_createp)
{
	struct inode *inode = opening->inode;
	struct host *writing_spool_host;
	struct file_copy *copy;

	if (!inode_is_file(inode))
		gflog_fatal(GFARM_MSG_1000331,
		    "inode_schedule_confirm_for_write: not a file");

	if (inode_has_no_replica(inode)) {
		/*
		 * the caller of this function already ensures the following
		 * assertion.  If it was not true, the caller replied
		 * GFARM_ERR_STALE_FILE_HANDLE to a client.
		 */
		assert((opening->flag & GFARM_FILE_TRUNC) != 0 ||
		    inode_get_size(inode) == 0);

		if (!host_is_disk_available(spool_host, 0))
			return (GFARM_ERR_NO_SPACE);

		/*
		 * process_reopen_file() adds a replica,
		 * just after the call of inode_schedule_confirm_for_write().
		 * thus, it won't happen that two different clients are
		 * simultaneously creating a file.
		 */
		*to_createp = 1;
		return (GFARM_ERR_NO_ERROR);
	}

	if ((writing_spool_host = inode_writing_spool_host(inode)) != NULL) {
		/* this replica must be valid */
		if (spool_host == writing_spool_host)
			return (GFARM_ERR_NO_ERROR);
		else
			return (GFARM_ERR_FILE_MIGRATED);
	}

	/* not opened for writing */
	copy = inode_get_file_copy(inode, spool_host);
	if (copy != NULL) {
		/*
		 * if a replication is ongoing, don't allow to create new one,
		 * because it incurs a race.
		 */
		if (FILE_COPY_IS_VALID(copy) /* == inode_has_replica() */) {
			if (host_is_disk_available(spool_host, 0))
				return (GFARM_ERR_NO_ERROR);
			else
				return (GFARM_ERR_NO_SPACE);
		} else
			return (GFARM_ERR_FILE_MIGRATED); /* XXX ? */
	}
	if (((opening->flag & GFARM_FILE_TRUNC) != 0 ||
	     inode_get_size(inode) == 0)) {
		if (!host_is_disk_available(spool_host, 0))
			return (GFARM_ERR_NO_SPACE);
		/*
		 * http://sourceforge.net/apps/trac/gfarm/ticket/68
		 * (measures against disk full for a file overwriting case)
		 */
		*to_createp = 1;
		return (GFARM_ERR_NO_ERROR);
	}
	return (GFARM_ERR_FILE_MIGRATED);
}

static gfarm_error_t
inode_schedule_new_file(struct peer *peer, gfarm_int32_t *np,
			struct host ***hostsp)
{
	gfarm_off_t necessary_space = 0; /* i.e. use default value */

	return (host_from_all(host_is_disk_available_filter, &necessary_space,
		np, hostsp));
}

/* this interface is exported for a use from a private extension */
gfarm_error_t
inode_schedule_file_default(struct file_opening *opening,
	struct peer *peer, gfarm_int32_t *np, struct host ***hostsp)
{
	gfarm_error_t e;
	struct inode_activity *ia = opening->inode->u.c.activity;
	struct file_opening *fo;
	int n, nhosts, write_mode, truncate_flag;
	struct host **hosts;
	gfarm_off_t necessary_space = 0; /* i.e. use default value */
	static const char diag[] = "inode_schedule_file_default";

	/* XXX FIXME too long giant lock */

	assert(inode_is_file(opening->inode));

	truncate_flag = (opening->flag & GFARM_FILE_TRUNC) != 0;
	write_mode = (accmode_to_op(opening->flag) & GFS_W_OK) != 0;
	if (inode_has_no_replica(opening->inode)) {
		/*
		 * even if a file is opened in read only mode, return
		 * all available hosts when the size is zero, since
		 * the following gfs_pio_read() call needs to connect
		 * a gfsd to read 0-byte file.
		 */
		if (inode_get_size(opening->inode) == 0 || truncate_flag)
			return (inode_schedule_new_file(peer, np, hostsp));
		gflog_error(GFARM_MSG_1003479,
		    "(%llu:%llu, %llu): lost all replicas",
		    (unsigned long long)inode_get_number(opening->inode),
		    (unsigned long long)inode_get_gen(opening->inode),
		    (unsigned long long)inode_get_size(opening->inode));
		return (GFARM_ERR_STALE_FILE_HANDLE);
	}
	if ((write_mode || truncate_flag) && ia != NULL &&
	    (fo = ia->openings.opening_next) != &ia->openings) {

		/* try to choose already opened replicas */
		n = 0;
		for (; fo != &ia->openings; fo = fo->opening_next) {
			if ((accmode_to_op(fo->flag) & GFS_W_OK) != 0 &&
			    fo->u.f.spool_host != NULL) {
				/*
				 * already opened for writing.
				 * only that replica is allowed in this case.
				 * we do not check host_is_disk_available(),
				 * because there is no other choice here.
				 * the replica must be valid.
				 */
				GFARM_MALLOC_ARRAY(hosts, 1);
				if (hosts == NULL) {
					gflog_error(GFARM_MSG_1004341,
					    "no memory");
					return (GFARM_ERR_NO_MEMORY);
				}
				hosts[0] = fo->u.f.spool_host;
				*np = 1;
				*hostsp = hosts;
				return (GFARM_ERR_NO_ERROR);
			}
			if (fo->u.f.spool_host != NULL)
				n++;
		}
		if (n > 0) {
			/*
			 * already opened for reading.
			 * try to return the opened replicas in this case,
			 * to give a chance to a reader to see the changes
			 * made by this writer.
			 */

			/*
			 * need to remember the hosts, because results of
			 * host_is_up()/host_is_disk_available() may change
			 * even while the giant lock is held.
			 */
			struct hostset *opening_hostset;

			e = hostset_of_file_opening_alloc(opening->inode,
			    host_is_disk_available_filter, &necessary_space,
			    diag, &opening_hostset);
			if (e != GFARM_ERR_NO_ERROR)
				return (e);

			e = inode_alloc_file_copy_hosts_within_scope(
			    opening->inode,
			    file_copy_is_valid_and_disk_available,
			    &necessary_space, opening_hostset,
			    &nhosts, &hosts);
			hostset_free(opening_hostset);
			if (e != GFARM_ERR_NO_ERROR)
				return (e);
			if (nhosts > 0) {
				*np = nhosts;
				*hostsp = hosts;
				return (GFARM_ERR_NO_ERROR);
			}
			free(hosts);
			/* try other filesystem nodes, then */
		}
	}
	if (write_mode) {
		/* all replicas are candidates */
		e = inode_alloc_file_copy_hosts(opening->inode,
		    file_copy_is_valid_and_disk_available, &necessary_space,
		    &nhosts, &hosts);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
		if (nhosts > 0) {
			*np = nhosts;
			*hostsp = hosts;
			return (GFARM_ERR_NO_ERROR);
		}
		free(hosts);

		/*
		 * all file system nodes having the replica are down,
		 * or do not have enough capacity
		 */
		if (truncate_flag || inode_get_size(opening->inode) == 0) {
			/*
			 * If there is no other writer and the size of the file
			 * is zero, it's ok to choose any host unless
			 * a replication is ongoing on the host.
			 * cf. http://sourceforge.net/apps/trac/gfarm/ticket/68
			 * (measures against disk full for a file overwriting)
			 */
			struct hostset *incomplete_hostset;

			/* exclude hosts which are during a replication */
			e = hostset_of_file_copy_alloc(opening->inode,
			    file_copy_is_invalid, NULL, diag,
			    &incomplete_hostset);
			if (e != GFARM_ERR_NO_ERROR)
				return (e);

			e = host_from_all_except(
			    host_is_disk_available_filter, &necessary_space,
			    incomplete_hostset, np, hostsp);
			hostset_free(incomplete_hostset);
			return (e);
		}
	}

	/* all replicas are candidates */
	e = inode_alloc_file_copy_hosts(opening->inode,
	    file_copy_is_valid_and_up, NULL, &nhosts, &hosts);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	*np = nhosts;  /* NOTE: this may be 0 */
	*hostsp = hosts;
	return(GFARM_ERR_NO_ERROR);
}

/* this interface is made as a hook for a private extension */
gfarm_error_t (*inode_schedule_file)(struct file_opening *, struct peer *,
	gfarm_int32_t *, struct host ***) = inode_schedule_file_default;

/* remove file_copy metadata on an already removed filesystem node */
void
inode_remove_file_copy_for_invalid_host(gfarm_ino_t inum)
{
	struct inode *inode = inode_lookup(inum);
	struct file_copy **copyp, *copy, **nextp;

	if (inode == NULL)
		return;
	if (!inode_is_file(inode))
		return;

	for (copyp = &inode->u.c.s.f.copies; (copy = *copyp) != NULL;
	    copyp = nextp) {
		nextp = &copy->host_next;
		if (!host_is_valid(copy->host)) {
			if (FILE_COPY_IS_VALID(copy)) {
				/*
				 * called from file_copy_by_host_remove(), so
				 * no way to know relevant dirset
				 */
				dirquota_check_schedule();

				quota_update_replica_remove(
				    inode, TDIRSET_IS_UNKNOWN);
			}

			*copyp = copy->host_next;
			nextp = copyp;
			free(copy);
		}
	}
}

static gfarm_error_t
inode_remove_file_copy(struct inode *inode, struct host *spool_host)
{
	struct file_copy **copyp, *copy, **foundp = NULL;

	assert(inode && inode_is_file(inode));

	for (copyp = &inode->u.c.s.f.copies; (copy = *copyp) != NULL;
	    copyp = &copy->host_next) {
		if (copy->host == spool_host) {
			foundp = copyp;
			break;
		}
	}
	if (foundp == NULL) {
		gflog_notice(GFARM_MSG_1002818,
		    "(%lld, %lld, %s) : %s",
		    (long long)inode_get_number(inode),
		    (long long)inode_get_gen(inode), host_name(spool_host),
		    gfarm_error_string(GFARM_ERR_NO_SUCH_OBJECT));
		return (GFARM_ERR_NO_SUCH_OBJECT);
	}
	copy = *foundp;
	*foundp = copy->host_next;
	free(copy);
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
inode_remove_replica_in_cache(struct inode *inode, struct host *spool_host)
{
	gfarm_error_t e = inode_remove_file_copy(inode, spool_host);

	if (e == GFARM_ERR_NO_ERROR) {
		/*
		 * TDIRSET_IS_UNKNOWN is OK,
		 * because dirquota_check will be done after becoming master
		 */
		quota_update_replica_remove(inode, TDIRSET_IS_UNKNOWN);
	}

	/* host_status_update_disk_usage() is unnecessary for slave gfmd */

	return (e);
}

static unsigned long long file_replicating_count = 0;

void
replication_info(void)
{
	unsigned long long count;
	gfarm_uint64_t files;
	gfarm_uint64_t bytes;
	double t;

	giant_lock();
	count = file_replicating_count;
	files = cumulative_replicated_files;
	bytes = cumulative_replicated_bytes;
	t = cumulative_replicated_time;
	giant_unlock();
	gflog_info(GFARM_MSG_1005044, "number of ongoing replications: %llu",
	    count);
	gflog_info(GFARM_MSG_1005098, "cumulative replication progress: "
	    "%llu bytes / %llu files %g seconds",
	    (unsigned long long)bytes,
	    (unsigned long long)files, t);
}

/*
 * PREREQUISITE: giant_lock
 */
static gfarm_error_t
file_replicating_new(struct inode *inode, struct host *dst, struct host *src,
	struct dead_file_copy *deferred_cleanup, struct dirset *tdirset,
	struct file_replicating **frp)
{
	gfarm_error_t e;
	struct file_replicating *fr;
	struct inode_activity *ia;
	struct inode_replicating_state *irs;
	int ia_alloc_tried;

	if (!host_is_disk_available(dst, inode_get_size(inode)))
		return (GFARM_ERR_NO_SPACE);

	ia_alloc_tried = (inode->u.c.activity == NULL);
	ia = inode_activity_alloc_or_update(&inode->u.c.activity, tdirset);
	if (ia == NULL)
		return (GFARM_ERR_NO_MEMORY);

	if ((e = inode_add_replica(inode, dst, 0)) != GFARM_ERR_NO_ERROR) {
		if (ia_alloc_tried)
			inode_activity_free_try(inode);
		return (e);
	}
	if (frp == NULL) { /* client initiated replication case */
		if (ia_alloc_tried)
			inode_activity_free_try(inode);
		return (GFARM_ERR_NO_ERROR);
	}

	if ((e = host_replicating_new(dst, &fr)) != GFARM_ERR_NO_ERROR) {
		(void)inode_remove_file_copy(inode, dst);
		/* abandon error */
		if (ia_alloc_tried)
			inode_activity_free_try(inode);
		return (e);
	}

	irs = ia->u.f.rstate;
	if (irs == NULL) {
		GFARM_MALLOC(irs);
		if (irs == NULL) {
			gflog_error(GFARM_MSG_1004343, "no memory");
			peer_replicating_free(fr);
			(void)inode_remove_file_copy(inode, dst);
			/* abandon error */
			if (ia_alloc_tried)
				inode_activity_free_try(inode);
			return (GFARM_ERR_NO_MEMORY);
		}
		/* make circular list `replicating_hosts' empty */
		irs->replicating_hosts.prev_host =
		irs->replicating_hosts.next_host = &irs->replicating_hosts;

		ia->u.f.rstate = irs;
	}
	fr->prev_host = &irs->replicating_hosts;
	fr->next_host = irs->replicating_hosts.next_host;
	irs->replicating_hosts.next_host = fr;
	fr->next_host->prev_host = fr;

	fr->src = src;
	fr->inode = inode;
	fr->igen = inode_get_gen(inode);
	fr->cleanup = deferred_cleanup;

	/* record this always, because gfarm_ctxp->profile may be changed */
	gfarm_gettime(&fr->queue_time);

	++file_replicating_count;

	*frp = fr;
	return (GFARM_ERR_NO_ERROR);
}

/*
 * PREREQUISITE: giant_lock
 */
static void
file_replicating_free(struct file_replicating *fr)
{
	struct inode *inode = fr->inode;
	struct inode_activity *ia = inode->u.c.activity;
	struct inode_replicating_state *irs;
	struct dirset *tdirset;

	assert(inode_is_file(inode));
	assert(ia != NULL);
	irs = ia->u.f.rstate;
	fr->prev_host->next_host = fr->next_host;
	fr->next_host->prev_host = fr->prev_host;
	if (irs->replicating_hosts.next_host == &irs->replicating_hosts) {
		/* all done */
		free(irs);
		ia->u.f.rstate = NULL;
	}
	peer_replicating_free(fr);

	tdirset = inode_get_tdirset(inode);
	/* inode_activity_free_try() may free tdirset, so we need protection */
	if (tdirset != TDIRSET_IS_UNKNOWN && tdirset != TDIRSET_IS_NOT_SET)
		dirset_add_ref(tdirset);

	if (inode_activity_free_try(inode))
		inode_remove_try(inode, tdirset);

	if (tdirset != TDIRSET_IS_UNKNOWN && tdirset != TDIRSET_IS_NOT_SET)
		dirset_del_ref(tdirset);

	--file_replicating_count;
}

/*
 * this does not generate dead_file_copy, thus, only can be used
 * for an error at async_back_channel_replication_request().
 */
static void
file_replicating_free_by_error_before_request(struct file_replicating *fr)
{
	/*
	 * cannot use inode_remove_file_copy() directly,
	 * because it's possible that the caller released giant_lock at once,
	 * thus, inode generation might be updated.
	 * e.g. gfm_server_replicate_file_from_to() before r9139 did that.
	 */
	inode_remove_replica_completed(fr->inode->i_number, fr->igen, fr->dst);

	file_replicating_free(fr);
}

gfarm_int64_t
file_replicating_get_gen(struct file_replicating *fr)
{
	return (fr->igen);
}

/*
 * NOTE:
 * - caller of this function should acquire giant_lock as well
 * - caller of this function should NOT call db_begin()/db_end() around this
 */
gfarm_error_t
inode_replicated(struct file_replicating *fr,
	gfarm_int32_t src_errcode, gfarm_int32_t dst_errcode, gfarm_off_t size,
	int cksum_enabled, gfarm_int32_t cksum_request_flags,
	char *cksum_type, size_t cksum_len, char *cksum,
	gfarm_int32_t cksum_result_flags)
{
	struct inode *inode = fr->inode;
	int transaction = 0;
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	static const char diag[] = "inode_replicated";

	/* log even if generation updated */
	if (src_errcode == GFARM_ERR_NO_ERROR &&
	    dst_errcode == GFARM_ERR_NO_ERROR) {
		struct timespec now;
		double t, src_t, dst_t;
		gfarm_uint64_t src_files, dst_files;
		gfarm_uint64_t src_bytes, dst_bytes;

		/*
		 * record these values always,
		 * because gfarm_ctxp->profile may be changed
		 */

		gfarm_gettime(&now);
		t = now.tv_sec - fr->queue_time.tv_sec +
		    (double)(now.tv_nsec - fr->queue_time.tv_nsec)
		    / GFARM_SECOND_BY_NANOSEC;

		host_profile_add_sent(fr->src, size, t,
		    &src_files, &src_bytes, &src_t);
		host_profile_add_received(fr->dst, size, t,
		    &dst_files, &dst_bytes, &dst_t);
		cumulative_replicated_files++;
		cumulative_replicated_bytes += size;
		cumulative_replicated_time += t;

		if (gfarm_ctxp->profile) {
			gflog_info(GFARM_MSG_1005099,
			    "inode %lld:%lld from %s to %s: "
			    "size %lld time %g second speed %g byte/s, "
			    "cumulative since boot: "
			    "src %llu bytes / %llu files %g second, "
			    "dst %llu bytes / %llu files %g second, "
			    "total %llu bytes / %llu files %g second",
			    (long long)inode_get_number(inode),
			    (long long)fr->igen,
			    host_name(fr->src), host_name(fr->dst),
			    (long long)size,
			     t, t != 0 ? size / t : 0.0,
			    (unsigned long long)src_bytes,
			    (unsigned long long)src_files, src_t,
			    (unsigned long long)dst_bytes,
			    (unsigned long long)dst_files, dst_t,
			    (unsigned long long)cumulative_replicated_bytes,
			    (unsigned long long)cumulative_replicated_files,
			    cumulative_replicated_time
			);
		}
	}

	if (db_begin(diag) == GFARM_ERR_NO_ERROR)
		transaction = 1;

	if (src_errcode == GFARM_ERR_NO_ERROR &&
	    dst_errcode == GFARM_ERR_NO_ERROR &&
	    inode_is_file(inode) &&
	    size == inode_get_size(inode) &&
	    fr->igen == inode_get_gen(inode)) {
		if (cksum_enabled &&
		    (cksum_request_flags &
		    GFS_PROTO_REPLICATION_CKSUM_REQFLAG_INTERNAL_SUM_AVAIL
		    ) == 0 && cksum_type != NULL && *cksum_type != '\0' &&
		    cksum_len > 0) {
			int cksum_is_set;

			/*
			 * calling inode_cksum_set() without checking
			 * `cs == NULL' is OK, since r8972.
			 */
			e = inode_cksum_set(inode, cksum_type, cksum_len,
			    cksum, cksum_result_flags, 0, &cksum_is_set);
			if (e != GFARM_ERR_NO_ERROR)
				gflog_notice(GFARM_MSG_1004223,
				    "checksum error during replication of "
				    "inode %lld:%lld to %s: %s",
				    (long long)inode_get_number(inode),
				    (long long)fr->igen, host_name(fr->dst),
				    gfarm_error_string(e));
			else if (cksum_is_set)
				gflog_notice(GFARM_MSG_1004344,
				    "%s: inode %lld:%lld: checksum set to "
				    "<%s>:<%.*s> by replication to %s",
				    diag, (long long)inode_get_number(inode),
				    (long long)fr->igen,
				    cksum_type, (int)cksum_len, cksum,
				    host_name(fr->dst));
		}
		if (e == GFARM_ERR_NO_ERROR) {
			e = inode_add_replica(inode, fr->dst, 1);
			if (e != GFARM_ERR_NO_ERROR) {
				/* possibly quota check failure */
				gflog_notice(GFARM_MSG_1004224,
				    "replication of inode %lld:%lld to %s "
				    "completed, but: %s",
				    (long long)inode_get_number(inode),
				    (long long)fr->igen, host_name(fr->dst),
				    gfarm_error_string(e));
			}
		}
	} else {
		if (inode->i_mode == INODE_MODE_FREE ||
		    fr->igen != inode_get_gen(inode) ||
		    inode_is_opened_for_writing(inode)) {
			/* gflog_debug() should be enough, but to be sure */
			gflog_info(GFARM_MSG_1004736,
			    "canceled - "
			    "%lld:%lld (size:%lld) replication to %s: "
			    "mode:0o%o gen:%lld size:%lld writing=%d: "
			    "src=<%s> dst=<%s>",
			    (long long)inode_get_number(inode),
			    (long long)fr->igen, (long long)size,
			    host_name(fr->dst),
			    inode->i_mode, (long long)inode_get_gen(inode),
			    (long long)inode_get_size(inode),
			    inode_is_opened_for_writing(inode),
			    gfarm_error_string(src_errcode),
			    gfarm_error_string(dst_errcode));
		} else if (
		     src_errcode == GFARM_ERR_CHECKSUM_MISMATCH ||
		     dst_errcode == GFARM_ERR_CHECKSUM_MISMATCH) {
			/* checksum error happened, but it shouldn't */
			gflog_warning(GFARM_MSG_1004731,
			    "checksum error "
			    "at %lld:%lld replication to %s: "
			    "src=<%s> dst=<%s>",
			    (long long)inode_get_number(inode),
			    (long long)fr->igen,
			    host_name(fr->dst),
			    gfarm_error_string(src_errcode),
			    gfarm_error_string(dst_errcode));
		} else {
			gflog_notice(GFARM_MSG_1002257,
			    "temporary error "
			    "at %lld:%lld replication to %s: "
			    "src=%d dst=%d",
			    (long long)inode_get_number(inode),
			    (long long)fr->igen,
			    host_name(fr->dst), src_errcode,
			    dst_errcode);
		}
		e = GFARM_ERR_INVALID_FILE_REPLICA;
	}
	if (e != GFARM_ERR_NO_ERROR)
		inode_remove_replica_incomplete(inode, fr->dst, fr->igen);

	if (fr->cleanup != NULL) {
		if (dead_file_copy_is_removable(fr->cleanup))
			removal_pendingq_enqueue(fr->cleanup);
		else {
			/*
			 * if there is not enough number of replicas
			 * even including the obsolete one, keep it
			 */
			dead_file_copy_mark_deferred(fr->cleanup);
		}
	} else if (e == GFARM_ERR_NO_ERROR) {
		/* try to sweep deferred queue */
		dead_file_copy_replica_status_changed(inode_get_number(inode),
		    fr->dst);
	}

	file_replicating_free(fr);

	if (transaction)
		db_end(diag);

	if (e == GFARM_ERR_INVALID_FILE_REPLICA &&
	    src_errcode != GFARM_ERR_INVALID_FILE_REPLICA &&
	    src_errcode != GFARM_ERR_CHECKSUM_MISMATCH) {
		/*
		 * XXX - src_errcode check is a workaround to avoid
		 * infinite loop of replica check until #836 is fixed
		 *
		 * #647 - workaround for #646 - retry replication when
		 * a result of replication is failure
		 */
		replica_check_start_rep_result_failed();
	}

	return (e);
}

static gfarm_error_t
inode_add_replica_internal(struct inode *inode, struct host *spool_host,
	int flags, int update_quota, int in_cache)
{
	struct file_copy *copy;
	struct dirset *tdirset = TDIRSET_IS_UNKNOWN;
	static const char diag[] = "inode_add_replica_internal";

	for (copy = inode->u.c.s.f.copies; copy != NULL;
	    copy = copy->host_next) {
		if (copy->host == spool_host) {
			if (FILE_COPY_IS_VALID(copy)) {
				gflog_warning(GFARM_MSG_1001765,
				    "inode_add_replica: already exists");
				return (GFARM_ERR_ALREADY_EXISTS);
			} else if ((flags & FILE_COPY_VALID) == 0) {
				gflog_warning(GFARM_MSG_1002484,
				    "inode_add_replica: %s",
				    FILE_COPY_IS_BEING_REMOVED(copy) ?
				    "replication while removal is ongoing" :
				    "replication is already in progress");
				return (FILE_COPY_IS_BEING_REMOVED(copy) ?
				    GFARM_ERR_DEVICE_BUSY /* dst is busy */ :
				    GFARM_ERR_OPERATION_ALREADY_IN_PROGRESS);
			} else if (FILE_COPY_IS_BEING_REMOVED(copy)) {
				gflog_error(GFARM_MSG_1002485,
				    "inode_add_replica: "
				    "replicated while removal is ongoing");
				return (GFARM_ERR_DEVICE_BUSY); /*dst is busy*/
			} else { /* replicated, file_copy becomes valid */
				assert(copy->flags == 0);
				copy->flags |= FILE_COPY_VALID;
				if (update_quota) {
					/*
					 * dirquota_check will be done after
					 * becoming master in case of in_cache
					 */
					tdirset = in_cache ?
					    TDIRSET_IS_UNKNOWN :
					    inode_get_tdirset(inode);
					quota_update_replica_add(
					    inode, tdirset);
					if (!in_cache)
						inode_tdirset_check(
						    inode, tdirset, diag);
				}
				return (GFARM_ERR_NO_ERROR);
			}
		}
	}
	/* not exist in u.c.s.f.copies : add new replica */
	if (update_quota && !in_cache) {
		/*
		 * dirquota_check will be done after becoming master
		 * in case of in_cache
		 */
		gfarm_error_t e;

		tdirset = inode_get_tdirset(inode);
		/* check limits of space and number of the replica */
		e = quota_limit_check(inode_get_user(inode),
		    inode_get_group(inode), tdirset, 0, 1,
		    inode_get_size(inode));
		inode_tdirset_check(inode, tdirset, diag);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001767,
				"checking of limits of the replica failed");
			return (e);
		}
	}

	GFARM_MALLOC(copy);
	if (copy == NULL) {
		gflog_error(GFARM_MSG_1004345, "no memory");
		return (GFARM_ERR_NO_MEMORY);
	}
	if (update_quota && (flags & FILE_COPY_VALID) != 0) {
		/*
		 * dirquota_check will be done after becoming master
		 * in case of in_cache
		 */
		quota_update_replica_add(inode, tdirset);
		if (!in_cache) {
			/* dirquota_check will be done after becoming master */
			inode_tdirset_check(inode, tdirset, diag);
		}
	}

	copy->host = spool_host;
	copy->flags = flags;
	copy->host_next = inode->u.c.s.f.copies;
	inode->u.c.s.f.copies = copy;
	return (GFARM_ERR_NO_ERROR);
}

void
inode_dead_file_copy_added(gfarm_ino_t inum, gfarm_int64_t igen,
	struct host *host)
{
	struct inode *inode = inode_lookup(inum);

	if (inode == NULL || !inode_is_file(inode))
		return;
	if (igen != inode->i_gen)
		return;

	inode_add_replica_internal(inode, host, FILE_COPY_BEING_REMOVED, 0, 0);
}

/*
 * 'valid == 0' means that the replica is not ready right now, and
 * going to be created or removed.
 */
gfarm_error_t
inode_add_replica(struct inode *inode, struct host *spool_host, int valid)
{
	gfarm_error_t e = inode_add_replica_internal(
		inode, spool_host, valid ? FILE_COPY_VALID : 0, 1, 0);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001769,
			"inode_add_replica_internal() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (!valid)
		return (GFARM_ERR_NO_ERROR);
	host_status_update_disk_usage(spool_host, inode_get_size(inode));
	e = db_filecopy_add(inode->i_number, host_name(spool_host));
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1000327,
		    "db_filecopy_add(%lld, %s): %s",
		    (unsigned long long)inode->i_number,
		    host_name(spool_host),
		    gfarm_error_string(e));
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
inode_add_file_copy_in_cache(struct inode *inode, struct host *host)
{
	return (
	    inode_add_replica_internal(inode, host, FILE_COPY_VALID, 1, 1));
}

static gfarm_error_t
remove_replica_metadata(struct inode *inode, struct host *spool_host,
	struct dirset *tdirset)
{
	gfarm_error_t e;
	static const char diag[] = "remove_replica_metadata";

	quota_update_replica_remove(inode, tdirset);
	inode_tdirset_check(inode, tdirset, diag);
	host_status_update_disk_usage(spool_host, -inode_get_size(inode));

	e = db_filecopy_remove(inode->i_number, host_name(spool_host));
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1000329,
		    "db_filecopy_remove(%lld, %s): %s",
		    (unsigned long long)inode->i_number, host_name(spool_host),
		    gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
remove_replica_entity(struct inode *inode, gfarm_int64_t gen,
	struct host *spool_host, int valid, struct dirset *tdirset,
	struct dead_file_copy **deferred_cleanupp)
{
	struct dead_file_copy *dfc;

	dfc = dead_file_copy_new(inode->i_number, gen, spool_host);
	if (dfc == NULL)
		gflog_error(GFARM_MSG_1002260,
		    "remove_replica_entity(%lld, %lld, %s): no memory",
		    (unsigned long long)inode->i_number,
		    (unsigned long long)gen, host_name(spool_host));
	else if (deferred_cleanupp == NULL)
		removal_pendingq_enqueue(dfc);
	else {
		dead_file_copy_mark_kept(dfc); /* prevent this from removed */
		*deferred_cleanupp = dfc;
	}

	if (valid) {
		(void)remove_replica_metadata(inode, spool_host, tdirset);
		/* abandon error */
	}
	return (dfc == NULL ? GFARM_ERR_NO_MEMORY : GFARM_ERR_NO_ERROR);
}

void
inode_remove_replica_completed(gfarm_ino_t inum, gfarm_int64_t igen,
	struct host *host)
{
	struct inode *inode = inode_lookup(inum);

	if (inode == NULL || !inode_is_file(inode))
		return;
	if (igen != inode->i_gen)
		return;

	(void)inode_remove_file_copy(inode, host);
	/* abandon error */
}

static gfarm_error_t
inode_replica_is_removable(struct inode *inode, struct file_copy *copy,
	struct replica_spec *replica_spec, int current_num, int readonly_num,
	int up_only)
{
	int not_readonly_num;

	if (current_num <= 1)
		return (GFARM_ERR_CANNOT_REMOVE_LAST_REPLICA);

	if (host_is_readonly(copy->host))
		return (GFARM_ERR_READ_ONLY_FILE_SYSTEM);

	/* replicas on readonly-host are not spare */
	not_readonly_num = current_num - readonly_num;
	if (not_readonly_num <= replica_spec->desired_number)
		return (GFARM_ERR_INSUFFICIENT_NUMBER_OF_FILE_REPLICAS);

	if (replica_spec->repattr != NULL) {
		return (fsngroup_has_spare_for_repattr(inode, not_readonly_num,
		    host_fsngroup(copy->host), replica_spec->repattr,
		    up_only));
	}

	return (GFARM_ERR_NO_ERROR); /* removable */
}

static gfarm_error_t
inode_remove_replica_internal(struct inode *inode, struct host *spool_host,
	gfarm_int64_t gen, struct replica_spec *replica_spec,
	struct dirset *tdirset,
	int invalid_is_removable, int replica_lost, int metadata_only,
	struct dead_file_copy **deferred_cleanupp)
{
	struct file_copy **copyp, *copy, **foundp = NULL;
	gfarm_error_t e;
	int num_up = 0, num_up_ro = 0, num_incomplete = 0;

	if (gen == inode->i_gen) {
		for (copyp = &inode->u.c.s.f.copies; (copy = *copyp) != NULL;
		    copyp = &copy->host_next) {
			if (copy->host == spool_host)
				foundp = copyp;
			if (FILE_COPY_IS_VALID(copy)) {
				/* replicas on readonly-host are not spare */
				if (host_is_up(copy->host)) {
					++num_up; /* available replicas */
					if (host_is_readonly(copy->host))
						++num_up_ro;
				}
			} else if (!FILE_COPY_IS_BEING_REMOVED(copy))
				++num_incomplete;
			/* else: FILE_COPY_IS_BEING_REMOVED */
		}
		if (foundp == NULL) {
			gflog_debug(GFARM_MSG_1001770,
				"replica to remove not found");
			return (GFARM_ERR_NO_SUCH_OBJECT);
		}
		copy = *foundp;
		if (replica_spec != NULL && FILE_COPY_IS_VALID(copy)) {
			e = inode_replica_is_removable(inode, copy,
			    replica_spec, num_up, num_up_ro, 1);
			if (e != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1003698,
				    "replica_is_removable: %s",
				    gfarm_error_string(e));
				return (e);
			}
		}
		if (!metadata_only) {
			if (FILE_COPY_IS_BEING_REMOVED(copy) ||
			    (!invalid_is_removable &&
			     !FILE_COPY_IS_VALID(copy))) {
				gflog_debug(GFARM_MSG_1002486,
				    "remove_replica(%lld, %lld, %s): %s",
				    (unsigned long long)inode->i_number,
				    (unsigned long long)gen,
				    host_name(spool_host),
				    FILE_COPY_IS_BEING_REMOVED(copy) ?
				    "being removed" : "invalid");
				e = GFARM_ERR_NO_SUCH_OBJECT;
			} else if (FILE_COPY_IS_VALID(copy) &&
				   num_incomplete > 0) {
				/* the replica may be used for replication */
				gflog_debug(GFARM_MSG_1003706,
				    "remove_replica(%lld:%lld, %s): "
				    "being replicated",
				    (long long)inode->i_number, (long long)gen,
				    host_name(spool_host));
				e = GFARM_ERR_FILE_BUSY;
			} else if (gfarm_read_only_mode()) {
				gflog_warning(GFARM_MSG_1005170,
				    "inode %llu:%llu @ %s: replica "
				    "entity must be removed, "
				    "but currently read_only",
				    (unsigned long long)inode->i_number,
				    (unsigned long long)gen,
				    host_name(spool_host));
				e = GFARM_ERR_READ_ONLY_FILE_SYSTEM;
			} else {
				/*
				 * XXX FIXME invalid_is_removable case is wrong
				 * see PROBLEM-4 of SourceForge #407
				 */
				e = remove_replica_entity(inode, gen,
				    copy->host, FILE_COPY_IS_VALID(copy),
				    tdirset, deferred_cleanupp);
				if (e == GFARM_ERR_NO_ERROR) {
					copy->flags &= ~FILE_COPY_VALID;
					copy->flags |= FILE_COPY_BEING_REMOVED;
				} else {
					*foundp = copy->host_next;
					free(copy);
				}
			}
		} else {
			if (FILE_COPY_IS_VALID(copy)) {
				if (gfarm_read_only_mode()) {
					gflog_warning(GFARM_MSG_1005171,
					    "inode %llu:%llu @ %s: replica "
					    "metadata must be removed, "
					    "but currently read_only",
					    (unsigned long long)
					    inode->i_number,
					    (unsigned long long)gen,
					    host_name(spool_host));
					e = GFARM_ERR_READ_ONLY_FILE_SYSTEM;
				} else if ((e = remove_replica_metadata(
				    inode, copy->host, tdirset))
				    != GFARM_ERR_NO_ERROR) {
					gflog_error(GFARM_MSG_1003701,
					    "remove_replica_metadata("
					    "%lld, %lld, %s): %s",
					    (unsigned long long)inode->i_number,
					    (unsigned long long)gen,
					    host_name(spool_host),
					    gfarm_error_string(e));
					/* in-core metadata IS removed */
					e = GFARM_ERR_NO_ERROR;
				}
				*foundp = copy->host_next;
				free(copy);
			} else if (replica_lost &&
			    FILE_COPY_IS_BEING_REMOVED(copy)) {
				/*
				 * SF.net #913
				 * this file_copy will be removed at
				 * dead_file_copy_free(), when it calls
				 * inode_remove_replica_completed().
				 */
				e = dead_file_copy_mark_lost(
				    inode->i_number, gen, spool_host);
			} else {
				gflog_debug(GFARM_MSG_1002487,
				    "remove_replica_metadata(%lld, %lld, %s): "
				    "invalid",
				    (unsigned long long)inode->i_number,
				    (unsigned long long)gen,
				    host_name(spool_host));
				e = GFARM_ERR_NO_SUCH_OBJECT;
			}
		}
	} else if (!metadata_only) {
		if (gfarm_read_only_mode()) {
			gflog_warning(GFARM_MSG_1005172,
			    "inode %llu:%llu @ %s: obsolete replica "
			    "entity must be removed, but currently read_only",
			    (unsigned long long)inode->i_number,
			    (unsigned long long)gen,
			    host_name(spool_host));
			e = GFARM_ERR_READ_ONLY_FILE_SYSTEM;
		} else {
			/*
			 * tdirset is not needed,
			 * because this is obsolete replica
			 */
			e = remove_replica_entity(
			    inode, gen, spool_host, 0, tdirset,
			    deferred_cleanupp);
		}
	} else {
		/* remove_replica_entity() must be already called */
		gflog_debug(GFARM_MSG_1002488,
		    "remove_replica%s(%lld, %lld, %s): old, current=%lld",
		    metadata_only ? "_metadata" : "",
		    (unsigned long long)inode->i_number,
		    (unsigned long long)gen, host_name(spool_host),
		    (unsigned long long)inode->i_gen);
		e = GFARM_ERR_NO_SUCH_OBJECT;
	}
	return (e);
}

gfarm_error_t
inode_remove_replica_lost(
	struct inode *inode, struct host *spool_host, gfarm_int64_t gen)
{
	/*
	 * no way to know relevant tdirset
	 * because this is called from GFM_PROTO_REPLICA_LOST
	 */
	return (inode_remove_replica_internal(inode, spool_host, gen,
	    NULL, NULL, 0, 1, 1, NULL));
}

gfarm_error_t
inode_remove_replica_protected(struct inode *inode, struct host *spool_host,
	struct replica_spec *replica_spec, struct dirset *tdirset)
{
	assert(replica_spec != NULL);
	return (inode_remove_replica_internal(inode, spool_host,
	    inode_get_gen(inode), replica_spec, tdirset, 0, 0, 0, NULL));
}

gfarm_error_t
inode_remove_replica_orphan(struct inode *inode, struct host *spool_host)
{
	/*
	 * no need to pass tdirset, because this is only called from
	 * boot-time fsck,
	 * i.e. inode_remove_orphan() via file_copy_db_remove_one_orphan()
	 */
	return (inode_remove_replica_internal(inode, spool_host,
	    inode_get_gen(inode), NULL, NULL, 1, 0, 1, NULL));
}

/* remove an incomplete replica, when a replication fails */
void
inode_remove_replica_incomplete(struct inode *inode, struct host *spool_host,
	gfarm_int64_t gen)
{
	gfarm_error_t e;

	/*
	 * no need to pass tdirset, because this is only called at
	 * replication failure.
	 */
	e = inode_remove_replica_internal(inode, spool_host, gen,
	    NULL, NULL, 1, 0, 0, NULL);
	if (e == GFARM_ERR_NO_SUCH_OBJECT) {
		gflog_info(GFARM_MSG_1002433,
		    "cannot remove an incomplete replica (%s, %lld:%lld): "
		    "probably already removed",
		    host_name(spool_host),
		    (long long)inode_get_number(inode), (long long)gen);
	} else if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1002434,
		    "cannot remove an incomplete replica (%s, %lld:%lld): %s",
		    host_name(spool_host),
		    (long long)inode_get_number(inode), (long long)gen,
		    gfarm_error_string(e));
	}
}

gfarm_error_t
inode_prepare_to_replicate(struct inode *inode, struct process *process,
	struct host *src, struct host *dst, gfarm_int32_t flags,
	struct file_replicating **file_replicating_p)
{
	gfarm_error_t e;
	struct file_copy *copy;
	struct file_replicating *fr, **frp = &fr;

	if (file_replicating_p == NULL) /* client initiated replication */
		frp = NULL;

	if ((flags & ~GFS_REPLICATE_FILE_FORCE) != 0)
		return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);

	if ((e = inode_check_file(inode)) != GFARM_ERR_NO_ERROR)
		return (e);

	/* have enough privilege? i.e. can read the file? */
	if ((e = inode_access(inode,
	    process_get_tenant(process), process_get_user(process), GFS_R_OK))
	    != GFARM_ERR_NO_ERROR)
		return (e);
	if (!inode_has_replica(inode, src))
		return (GFARM_ERR_NO_SUCH_OBJECT);
	if ((copy = inode_get_file_copy(inode, dst)) != NULL) {
		if (FILE_COPY_IS_VALID(copy)) /* == inode_has_replica() */
			return (GFARM_ERR_ALREADY_EXISTS);
		else if (FILE_COPY_IS_BEING_REMOVED(copy))
			return (GFARM_ERR_DEVICE_BUSY); /* dst is busy */
		else
			return (GFARM_ERR_OPERATION_ALREADY_IN_PROGRESS);
	}
	if ((flags & GFS_REPLICATE_FILE_FORCE) == 0 &&
	    inode_is_opened_for_writing(inode))
		return (GFARM_ERR_FILE_BUSY); /* src is busy */
	else if ((e = file_replicating_new(inode, dst, src, NULL,
	    inode_get_tdirset(inode), frp)) != GFARM_ERR_NO_ERROR)
		return (e);

	if (file_replicating_p != NULL)
		*file_replicating_p = *frp;
	return (GFARM_ERR_NO_ERROR);
}

/*
 * the contents pointed by *cksum_type and *cksump should be copied
 * before releasing giant_lock
 */
void
inode_replication_get_cksum_mode(struct inode *inode, struct host *src,
	char **cksum_typep, size_t *cksum_lenp, char **cksump,
	gfarm_int32_t *cksum_request_flagsp)
{
	struct inode_activity *ia = inode->u.c.activity;
	struct checksum *cs = inode->u.c.s.f.cksum;
	gfarm_int32_t cksum_request_flags = 0;
	size_t cksum_len;
	char *cksum_type, *cksum;
	int src_supports = host_supports_cksum_protocols(src);

	if (ia != NULL && ia->u.f.writers > 0)
		cksum_request_flags |=
		    GFS_PROTO_REPLICATION_CKSUM_REQFLAG_MAYBE_EXPIRED;
	if (src_supports)
		cksum_request_flags |=
		    GFS_PROTO_REPLICATION_CKSUM_REQFLAG_SRC_SUPPORTS;
	if (cs != NULL) {
		cksum_request_flags |=
		    GFS_PROTO_REPLICATION_CKSUM_REQFLAG_INTERNAL_SUM_AVAIL;
		cksum_type = cs->type;
		cksum_len = cs->len;
		cksum = cs->sum;
	} else {
		if (!src_supports) {
			/* We don't trust destination calculated cksum */
			cksum_type = "";
		} else {
			cksum_type = gfarm_digest != NULL ? gfarm_digest : "";
		}
		cksum_len = 0;
		cksum = NULL;
	}
	/*
	 * cksum handling is enabled in this protocol sequence?
	 * this is a gfmd internal flag, and isn't passed via protocol.
	 */
	cksum_request_flags |=
	    GFS_PROTO_REPLICATION_CKSUM_REQFLAG_INTERNAL_ENABLED;

	*cksum_typep = cksum_type;
	*cksum_lenp = cksum_len;
	*cksump = cksum;
	*cksum_request_flagsp = cksum_request_flags;
}

/*
 * if dst-gfsd is gfarm-2.5 or older,
 * or cksum is not set and src-gfsd is gfarm-2.5 or older:
 *	gfmd issues GFS_PROTO_REPLICATION_REQUEST, and
 *	dst-gfsd issues GFS_PROTO_REPLICA_RECV
 * otherwise, i.e.
 * if dst-gfsd is gfarm-2.6 or newer,
 * and cksum is set or src-gfsd is gfarm-2.6 or newer:
 *	gfmd issues GFS_PROTO_REPLICATION_CKSUM_REQUEST
 *	if src-gfsd is gfarm-2.5 or older (cksum must be set in this case),
 *		dst-gfsd issues GFS_PROTO_REPLICA_RECV,
 *		dst-gfsd compares cksum
 *	otherwise,
 *	i.e. if src-gfsd is gfarm-2.6 or newer:
 *		dst-gfsd issues GFS_PROTO_REPLICA_RECV_CKSUM
 *		if cksum is set:
 *			src-gfsd compares cksum, and fails if it doesn't match
 *		otherwise, i.e. if cksum is not set:
 *			src-gfsd calculates cksum, and gfmd stores the cksum
 *
 * NOTE: the memory owner of `fr' is changed to this callee function.
 */
gfarm_error_t
inode_replication_request(
	struct inode *inode, struct file_replicating *fr, const char *diag)
{
	gfarm_error_t e;
	struct checksum *cs = inode->u.c.s.f.cksum;
	struct host *src = fr->src;
	struct host *dst = fr->dst;

	if (!host_supports_cksum_protocols(dst) ||
	    (cs == NULL && !host_supports_cksum_protocols(src))) {
		/*
		 * re: cs == NULL && !host_supports_cksum_protocols(src)) case:
		 * We don't trust destination calculated cksum
		 */
		e = async_back_channel_replication_request(
		    host_name(src), host_port(src),
		    dst, inode->i_number, inode->i_gen, fr);
	} else {
		size_t cksum_len;
		char *cksum_type, *cksum, *cksumbuf = NULL;
		gfarm_int32_t cksum_request_flags;

		inode_replication_get_cksum_mode(inode, src,
		    &cksum_type, &cksum_len, &cksum, &cksum_request_flags);

		if ((cksum_type = strdup_log(cksum_type, diag)) == NULL) {
			e = GFARM_ERR_NO_MEMORY;
		} else if (cksum_len == 0) {
			e = GFARM_ERR_NO_ERROR;
		} else {
			GFARM_MALLOC_ARRAY(cksumbuf, cksum_len);
			if (cksumbuf == NULL) {
				gflog_error(GFARM_MSG_1004346, "no memory");
				e = GFARM_ERR_NO_MEMORY;
			} else {
				e = GFARM_ERR_NO_ERROR;
				memcpy(cksumbuf, cksum, cksum_len);
			}
		}
		if (e == GFARM_ERR_NO_ERROR) {
			file_replicating_set_cksum_request_flags(fr,
			    cksum_request_flags);
			e = async_back_channel_replication_cksum_request(
			    host_name(src), host_port(src),
			    dst, inode->i_number, inode->i_gen, inode->i_size,
			    cksum_type, cksum_len, cksumbuf,
			    cksum_request_flags, fr);
		}
		if (e != GFARM_ERR_NO_ERROR) {
			free(cksum_type);
			free(cksumbuf);
		}
	}
	if (e != GFARM_ERR_NO_ERROR) /* may sleep in this case */
		file_replicating_free_by_error_before_request(fr);
	return (e);

}

int
inode_is_updated(struct inode *inode, struct gfarm_timespec *mtime)
{
	struct inode_activity *ia = inode->u.c.activity;

	/*
	 * ia->u.f.last_update is necessary,
	 * becasuse i_mtimespec may be modified by GFM_PROTO_FUTIMES.
	 */
	return (ia != NULL &&
	    gfarm_timespec_cmp(mtime, &ia->u.f.last_update) >= 0);
}

gfarm_error_t
inode_replica_hosts_valid(
	struct inode *inode, gfarm_int32_t *np, struct host ***hostsp)
{
	return (inode_alloc_file_copy_hosts(
	    inode, file_copy_is_valid_and_up, NULL, np, hostsp));
}

static gfarm_error_t
inode_replica_list_by_name_common(struct inode *inode,
	int is_up, gfarm_int32_t *np, char ***hostsp)
{
	struct file_copy *copy;
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	int n, i;
	char **hosts;
	static const char diag[] = "inode_replica_list_by_name";

	if (inode_is_dir(inode)) {
		gflog_debug(GFARM_MSG_1001771,
			"inode is a directory");
		return (GFARM_ERR_IS_A_DIRECTORY);
	} else if (!inode_is_file(inode)) {
		gflog_debug(GFARM_MSG_1001772,
			"node is not a file");
		return (GFARM_ERR_INVALID_ARGUMENT);
	}

	n = inode_get_ncopy_common(inode, 1, 0); /* include !host_is_up() */

	/* host_is_up() may change even while the giant lock is held. */
	GFARM_MALLOC_ARRAY(hosts, n);
	if (hosts == NULL) {
		gflog_error(GFARM_MSG_1004347, "no memory");
		return (GFARM_ERR_NO_MEMORY);
	}

	i = 0;
	for (copy = inode->u.c.s.f.copies; copy != NULL && i < n;
	    copy = copy->host_next) {
		if (FILE_COPY_IS_VALID(copy) &&
		    (is_up == 0 || host_is_up(copy->host))) {
			hosts[i] = strdup_log(host_name(copy->host), diag);
			if (hosts[i] == NULL) {
				gflog_debug(GFARM_MSG_1001774,
					"hosts[%d] is null", i);
				e = GFARM_ERR_NO_MEMORY;
				break;
			}
			++i;
		}
	}
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001775,
			"error occurred during process: %s",
			gfarm_error_string(e));
		while (--i >= 0)
			free(hosts[i]);
		free(hosts);
	} else {
		*np = i;
		*hostsp = hosts;
	}
	return (e);
}

gfarm_error_t
inode_replica_list_by_name(struct inode *inode,
	gfarm_int32_t *np, char ***hostsp)
{
	return (inode_replica_list_by_name_common(inode, 1, np, hostsp));
}

gfarm_error_t
inode_replica_list_by_name_with_dead_host(struct inode *inode,
	gfarm_int32_t *np, char ***hostsp)
{
	return (inode_replica_list_by_name_common(inode, 0, np, hostsp));
}

gfarm_error_t
inode_replica_info_get(struct inode *inode, gfarm_int32_t iflags,
	gfarm_int32_t *np,
	char ***hostsp, gfarm_int64_t **gensp, gfarm_int32_t **oflagsp)
{
	struct file_copy *copy;
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	int n, nlatest, ndead, i;
	char **hosts;
	gfarm_int64_t *gens, latest_gen;
	gfarm_int32_t *oflags;
	int show_incomplete =
	    (iflags & GFS_REPLICA_INFO_INCLUDING_INCOMPLETE_COPY) != 0;
	int show_down =
	    (iflags & GFS_REPLICA_INFO_INCLUDING_DEAD_HOST) != 0;
	int show_obsolete =
	    (iflags & GFS_REPLICA_INFO_INCLUDING_DEAD_COPY) != 0;
	static const char diag[] = "inode_replica_info_get";

	if ((e = inode_check_file(inode)) != GFARM_ERR_NO_ERROR)
		return (e);

	latest_gen = inode_get_gen(inode);

	/* include !host_is_up() */
	nlatest = inode_get_ncopy_common(inode, !show_incomplete, 0);

	if (show_obsolete)
		ndead = dead_file_copy_count_by_inode(inode_get_number(inode),
		    latest_gen, 0); /* include !host_is_up() */
	else
		ndead = 0;

	/* host_is_up() may change even while the giant lock is held. */
	n = nlatest + ndead;
	GFARM_MALLOC_ARRAY(hosts, n);
	GFARM_MALLOC_ARRAY(gens, n);
	GFARM_MALLOC_ARRAY(oflags, n);
	if (hosts == NULL || gens == NULL || oflags == NULL) {
		free(hosts);
		free(gens);
		free(oflags);
		gflog_error(GFARM_MSG_1004348, "no memory");
		return (GFARM_ERR_NO_MEMORY);
	}

	i = 0;
	for (copy = inode->u.c.s.f.copies; copy != NULL && i < n;
	    copy = copy->host_next) {
		enum { file_is_valid, file_is_incomplete,
		    file_is_being_removed } state;

		if (!show_down && !host_is_up(copy->host))
			continue;
		state = FILE_COPY_IS_VALID(copy) ? file_is_valid :
		    FILE_COPY_IS_BEING_REMOVED(copy) ? file_is_being_removed :
		    file_is_incomplete;
		if (state == file_is_valid ||
		    (show_incomplete && state == file_is_incomplete) ||
		    (show_obsolete && state == file_is_being_removed)) {
			hosts[i] = strdup_log(host_name(copy->host), diag);
			gens[i] = latest_gen;
			oflags[i] =
			    (show_incomplete && state == file_is_incomplete ?
			     GFM_PROTO_REPLICA_FLAG_INCOMPLETE : 0) |
			    (!host_is_up(copy->host) ?
			     GFM_PROTO_REPLICA_FLAG_DEAD_HOST : 0) |
			    (show_obsolete && state == file_is_being_removed ?
			     GFM_PROTO_REPLICA_FLAG_DEAD_COPY : 0);
			if (hosts[i] == NULL) {
				e = GFARM_ERR_NO_MEMORY;
				break;
			}
			++i;
		}
	}
	if (e == GFARM_ERR_NO_ERROR && show_obsolete)
		e = dead_file_copy_info_by_inode(
		     inode_get_number(inode), latest_gen,
		     !show_down, &ndead, &hosts[i], &gens[i], &oflags[i]);

	if (e != GFARM_ERR_NO_ERROR) {
		while (--i >= 0)
			free(hosts[i]);
		free(oflags);
		free(gens);
		free(hosts);
	} else {
		*np = i + ndead;
		*hostsp = hosts;
		*gensp = gens;
		*oflagsp = oflags;
	}
	return (e);
}

/*
 * removing orphan data.
 * We cannot remove the data at the load time, because backend implementations
 * of *_load() ops don't allow such thing.
 * Thus, defer removal after the *_load() calls.
 */

struct inum_list_entry {
	struct inum_list_entry *next;

	gfarm_ino_t inum;
};

static int
inum_list_add(struct inum_list_entry **listp, gfarm_ino_t inum)
{
	struct inum_list_entry *entry;

	GFARM_MALLOC(entry);
	if (entry == NULL) {
		gflog_error(GFARM_MSG_1004349, "no memory");
		return (0);
	}
	entry->inum = inum;

	entry->next = *listp;
	*listp = entry;
	return (1);
}

static void
inum_list_free(struct inum_list_entry **listp)
{
	struct inum_list_entry *entry, *next;

	for (entry = *listp; entry != NULL; entry = next) {
		next = entry->next;
		free(entry);
	}
	*listp = NULL;
}

static void
inum_list_foreach(struct inum_list_entry *list,
	gfarm_error_t (*op)(gfarm_ino_t), const char *name)
{
	gfarm_error_t e;
	struct inum_list_entry *entry, *next;

	for (entry = list; entry != NULL; entry = next) {
		next = entry->next;
		e = (*op)(entry->inum);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1002819,
			    "orphan %s %llu removal: %s",
			    name, (unsigned long long)entry->inum,
			    gfarm_error_string(e));
	}
}

static struct inum_list_entry *inode_cksum_removal_list = NULL;

static void
inode_cksum_defer_db_removal(gfarm_ino_t inum,
	char *type, size_t len, char *sum)
{
	if (!inum_list_add(&inode_cksum_removal_list, inum))
		gflog_error(GFARM_MSG_1002820,
		    "inode_cksum %llu type:%s len:%d: "
		    "no memory to record for removal",
		     (unsigned long long)inum, type, (int)len);
	else
		gflog_warning(GFARM_MSG_1002821,
		    "inode_cksum %llu type:%s len:%d: "
		    "removing orphan data",
		     (unsigned long long)inum, type, (int)len);

	free(type);
	free(sum);
}

static void
inode_cksum_db_remove_orphan(void)
{
	inum_list_foreach(inode_cksum_removal_list,
	    db_inode_cksum_remove, "inode_cksum");
}

static void
inode_cksum_free_orphan(void)
{
	inum_list_free(&inode_cksum_removal_list);
}

static struct inum_list_entry *symlink_removal_list = NULL;

static void
symlink_defer_db_removal(gfarm_ino_t inum, char *source_path)
{
	if (!inum_list_add(&symlink_removal_list, inum))
		gflog_error(GFARM_MSG_1002822,
		    "symlink %llu (%s): no memory to record for removal",
		    (unsigned long long)inum, source_path);
	else
		gflog_warning(GFARM_MSG_1002823,
		    "symlink %llu (%s): removing orphan data",
		    (unsigned long long)inum, source_path);
}

static void
symlink_db_remove_orphan(void)
{
	inum_list_foreach(symlink_removal_list, db_symlink_remove, "symlink");
}

static void
symlink_free_orphan(void)
{
	inum_list_free(&symlink_removal_list);
}

static struct dir_entry_removal_todo {
	struct dir_entry_removal_todo *next;

	gfarm_ino_t dir_inum;
	char *entry_name;
	int entry_len;
} *dir_entry_removal_list = NULL;

static void
dir_entry_free_orphan(void)
{
	struct dir_entry_removal_todo *entry, *next;

	for (entry = dir_entry_removal_list; entry != NULL; entry = next) {
		next = entry->next;
		free(entry->entry_name);
		free(entry);
	}
	dir_entry_removal_list = NULL;
}

static void
dir_entry_defer_db_removal(gfarm_ino_t dir_inum,
	char *entry_name, int entry_len, gfarm_ino_t entry_inum)
{
	struct dir_entry_removal_todo *entry;
	char *ename;

	GFARM_MALLOC(entry);
	GFARM_MALLOC_ARRAY(ename, entry_len);
	if (entry == NULL || ename == NULL) {
		free(entry);
		free(ename);
		gflog_error(GFARM_MSG_1002827,
		    "dir_entry (%llu name:%.*s) (%llu): "
		    "no memory to record for removal",
		    (unsigned long long)dir_inum, entry_len, entry_name,
		    (unsigned long long)entry_inum);
		return;
	}
	gflog_warning(GFARM_MSG_1002828,
	    "dir_entry (%llu name:%.*s) (%llu): removing orphan data",
	    (unsigned long long)dir_inum, entry_len, entry_name,
	    (unsigned long long)entry_inum);

	memcpy(ename, entry_name, entry_len);

	entry->dir_inum = dir_inum;
	entry->entry_name = ename;
	entry->entry_len = entry_len;

	entry->next = dir_entry_removal_list;
	dir_entry_removal_list = entry;
}

static void
dir_entry_db_remove_orphan(void)
{
	gfarm_error_t e;
	struct dir_entry_removal_todo *entry, *next;

	for (entry = dir_entry_removal_list; entry != NULL; entry = next) {
		next = entry->next;
		e = db_direntry_remove(entry->dir_inum,
		    entry->entry_name, entry->entry_len);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_1002829,
			    "orphan dir_entry (%llu name:%.*s) removal: %s",
			    (unsigned long long)entry->dir_inum,
			    entry->entry_len, entry->entry_name,
			    gfarm_error_string(e));
		}
	}
}

static struct inum_string_list_entry *xattr_removal_list = NULL;
static struct inum_string_list_entry *xml_removal_list = NULL;

/*
 * Unlike other *_db_removal() functions, xattr_defer_db_removal() doesn't
 * own the memory of `info' for now.  See xattr_add_one() too.
 */
static void
xattr_xml_defer_db_removal(struct inum_string_list_entry **listp,
	const char *name, struct xattr_info *info)
{
	char *attrname = strdup_log(info->attrname, "xattr_defer_db_removal");

	if (attrname == NULL ||
	    !inum_string_list_add(listp, info->inum, attrname)) {
		gflog_error(GFARM_MSG_1002830,
		    "%s (%llu name:%s) (size:%d): no memory for removal", name,
		    (unsigned long long)info->inum,
		    info->attrname, info->attrsize);
		free(attrname);
	} else {
		gflog_warning(GFARM_MSG_1002831,
		    "%s (%llu name:%s) (size:%d): removing orphan data", name,
		    (unsigned long long)info->inum,
		    info->attrname, info->attrsize);
	}
}

static void
xattr_defer_db_removal(struct xattr_info *info)
{
	xattr_xml_defer_db_removal(&xattr_removal_list, "xattr", info);
}

static void
xml_defer_db_removal(struct xattr_info *info)
{
	xattr_xml_defer_db_removal(&xml_removal_list, "xml", info);
}

static void
db_xattr_remove_one_orphan(void *closure,
	gfarm_ino_t inum, const char *attrname)
{
	gfarm_error_t e;

	e = db_xattr_remove(0, inum, (char *)attrname); /* XXX UNCONST */
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1002824,
		    "orphan xattr %llu attrname:%s removal: %s",
		    (unsigned long long)inum, attrname, gfarm_error_string(e));
}

static void
db_xml_remove_one_orphan(void *closure, gfarm_ino_t inum, const char *attrname)
{
	gfarm_error_t e;

	e = db_xattr_remove(1, inum, (char *)attrname); /* XXX UNCONST */
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1004269,
		    "orphan xml %llu attrname:%s removal: %s",
		    (unsigned long long)inum, attrname, gfarm_error_string(e));
}

static void
xattr_db_remove_orphan(void)
{
	inum_string_list_foreach(xattr_removal_list,
	    db_xattr_remove_one_orphan, NULL);
}

static void
xattr_free_orphan(void)
{
	inum_string_list_free(&xattr_removal_list);
}

static void
xml_db_remove_orphan(void)
{
	inum_string_list_foreach(xml_removal_list,
	    db_xml_remove_one_orphan, NULL);
}

static void
xml_free_orphan(void)
{
	inum_string_list_free(&xml_removal_list);
}

void
inode_remove_orphan(void)
{
	inode_cksum_db_remove_orphan();
	symlink_db_remove_orphan();
	file_copy_db_remove_orphan();
	dir_entry_db_remove_orphan();
	xattr_db_remove_orphan();
	xml_db_remove_orphan();
}

void
inode_free_orphan(void)
{
	inode_cksum_free_orphan();
	symlink_free_orphan();
	file_copy_free_orphan();
	dir_entry_free_orphan();
	xattr_free_orphan();
	xml_free_orphan();
}

/*
 * loading metadata from persistent storage.
 */

static void
inode_modify(struct inode *inode, struct gfs_stat *st)
{
	inode->i_gen = st->st_gen;
	inode->i_nlink = st->st_nlink;
	inode->i_size = st->st_size;
	inode->i_mode = st->st_mode;
	inode_set_user_by_name(inode, st->st_user);
	inode_set_group_by_name(inode, st->st_group);
	inode->i_atimespec = st->st_atimespec;
	inode->i_mtimespec = st->st_mtimespec;
	inode->i_ctimespec = st->st_ctimespec;

	if (st->st_mode != INODE_MODE_FREE) {
		/* dirquota_check will be done after becoming master */
		quota_update_file_add(inode, TDIRSET_IS_UNKNOWN);
	}
}

/* The memory owner of `*st' is changed to inode.c */
static gfarm_error_t
inode_add(struct gfs_stat *st, struct inode **inodep)
{
	gfarm_error_t e;
	struct inode *inode;

	inode = inode_alloc_num(st->st_ino);
	if (inode == NULL) {
		e = GFARM_ERR_UNKNOWN;
	} else if (GFARM_S_ISDIR(st->st_mode)) {
		e = inode_init_dir_internal(inode);
	} else if (GFARM_S_ISREG(st->st_mode)) {
		e = inode_init_file(inode);
	} else if (GFARM_S_ISLNK(st->st_mode)) {
		e = inode_init_symlink(inode, NULL);
	} else if (st->st_mode == INODE_MODE_FREE) {
		inode_clear(inode);
		e = GFARM_ERR_NO_ERROR;
	} else {
		gflog_error(GFARM_MSG_1000335,
		    "unknown inode type %lld, mode 0%o",
		    (unsigned long long)st->st_ino, st->st_mode);
		e = GFARM_ERR_UNKNOWN;
		assert(0);
	}
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1000336, "inode %lld: %s",
		    (unsigned long long)st->st_ino, gfarm_error_string(e));
		gfs_stat_free(st);
		return (e);
	}

	inode_modify(inode, st);

	gfs_stat_free(st);
	if (inodep)
		*inodep = inode;
	return (GFARM_ERR_NO_ERROR);
}

/* The memory owner of `*st' is changed to inode.c */
static void
inode_add_one(void *closure, struct gfs_stat *st)
{
	(void)inode_add(st, NULL);
	/* abandon error */
}

gfarm_error_t
inode_add_or_modify_in_cache(struct gfs_stat *st, struct inode **inodep)
{
	struct inode *n = inode_lookup(st->st_ino);


	if (n != NULL) {
		if (n->i_mode != INODE_MODE_FREE) {
			/*
			 * TDIRSET_IS_UNKNOWN is OK, because
			 * dirquota_check will be done after becoming master
			 */
			quota_update_file_remove(n, TDIRSET_IS_UNKNOWN);

			/*
			 * quota_update_file_add(inode, TDIRSET_IS_UNKNOWN);
			 * will be called by inode_modify() or inode_add()
			 * below.
			 */
		}
		if ((GFARM_S_IFMT & st->st_mode) ==
		    (GFARM_S_IFMT & n->i_mode)) {
			inode_modify(n, st);
			gfs_stat_free(st);
			*inodep = n;
			return (GFARM_ERR_NO_ERROR);
		}
		inode_free(n);
	}
	return (inode_add(st, inodep));
}


/* The memory owner of `type' and `sum' is changed to inode.c */
static void
inode_cksum_add_one(void *closure,
	gfarm_ino_t inum, char *type, size_t len, char *sum)
{
	struct inode *inode = inode_lookup(inum);

	if (inode == NULL) {
		gflog_error(GFARM_MSG_1000337,
		    "inode_cksum_add_one: no inode %lld",
		    (unsigned long long)inum);
		inode_cksum_defer_db_removal(inum, type, len, sum);
		return;
	} else if (!inode_is_file(inode)) {
		gflog_error(GFARM_MSG_1000338,
		    "inode_cksum_add_one: not file %lld",
		    (unsigned long long)inum);
		inode_cksum_defer_db_removal(inum, type, len, sum);
		return;
	} else if (inode->u.c.s.f.cksum != NULL) {
		gflog_error(GFARM_MSG_1000339,
		    "inode_cksum_add_one: dup cksum %lld",
		    (unsigned long long)inum);
		inode_cksum_defer_db_removal(inum, type, len, sum);
		return;
	} else {
		inode_cksum_set_internal(inode, type, len, sum);
	}
	free(type);
	free(sum);
}

/* The memory owner of `source_path' is changed to inode.c */
static void
symlink_add_one(void *closure, gfarm_ino_t inum, char *source_path)
{
	(void)symlink_add(inum, source_path);
	/* abandon error */

	/* symlink_defer_db_removal() doesn't defer access to source_path */
	free(source_path);
}

gfarm_error_t
symlink_add(gfarm_ino_t inum, char *source_path)
{
	gfarm_error_t e;
	struct inode *inode = inode_lookup(inum);
	char *s;

	if (inode == NULL) {
		gflog_error(GFARM_MSG_1000340,
		    "symlink_add_one: no inode %lld",
		    (unsigned long long)inum);
		symlink_defer_db_removal(inum, source_path);
		e = GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY;
	} else if (!inode_is_symlink(inode)) {
		gflog_error(GFARM_MSG_1000341,
		    "symlink_add_one: not symlink %lld",
		    (unsigned long long)inum);
		symlink_defer_db_removal(inum, source_path);
		e = GFARM_ERR_NOT_A_SYMBOLIC_LINK;
	} else if (inode->u.c.s.l.source_path != NULL) {
		gflog_error(GFARM_MSG_1000342,
		    "symlink_add_one: dup symlink %lld",
		    (unsigned long long)inum);
		symlink_defer_db_removal(inum, source_path);
		e = GFARM_ERR_INVALID_ARGUMENT;
	} else if ((s = strdup(source_path)) == NULL) {
		gflog_error(GFARM_MSG_1000342,
		    "symlink_add_one: no memory %lld -> \"%s\"",
		    (unsigned long long)inum, source_path);
		e = GFARM_ERR_NO_MEMORY;
		/* leave inode->u.c.s.l.source_path NULL: readlink()->EINVAL */
	} else {
		inode->u.c.s.l.source_path = s;
		e = GFARM_ERR_NO_ERROR;
	}
	return (e);
}

/* The memory owner of `entry_name' is changed to inode.c */
static void
dir_entry_add_one(void *closure,
	gfarm_ino_t dir_inum, char *entry_name, int entry_len,
	gfarm_ino_t entry_inum)
{
	dir_entry_add(dir_inum, entry_name, entry_len, entry_inum);

	/* abandon error */
	free(entry_name);
}

gfarm_error_t
dir_entry_add(gfarm_ino_t dir_inum, char *entry_name, int entry_len,
	gfarm_ino_t entry_inum)
{
	gfarm_error_t e;
	struct inode *dir_inode = inode_lookup(dir_inum);
	struct inode *entry_inode = inode_lookup(entry_inum);
	DirEntry entry;
	int created;
	static const char diag[] = "dir_entry_add_one";

	if (dir_inode == NULL) {
		e = GFARM_ERR_NO_SUCH_OBJECT;
		gflog_error(GFARM_MSG_1000350,
		    "%s: directory inode %lld: %s", diag, (long long)dir_inum,
		    gfarm_error_string(e));
		dir_entry_defer_db_removal(dir_inum,
		    entry_name, entry_len, entry_inum);
	} else if (!inode_is_dir(dir_inode)) {
		e = GFARM_ERR_NOT_A_DIRECTORY;
		gflog_error(GFARM_MSG_1000351,
		    "%s: inode %lld: %s", diag, (long long)dir_inum,
		    gfarm_error_string(e));
		dir_entry_defer_db_removal(dir_inum,
		    entry_name, entry_len, entry_inum);
	} else if (entry_inode == NULL) {
		e = GFARM_ERR_NO_SUCH_OBJECT;
		gflog_error(GFARM_MSG_1000352,
		    "%s: entry inode %lld: %s", diag, (long long)entry_inum,
		    gfarm_error_string(e));
		dir_entry_defer_db_removal(dir_inum,
		    entry_name, entry_len, entry_inum);
	} else if ((entry = dir_enter(dir_inode->u.c.s.d.entries,
	    entry_name, entry_len, &created)) == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_error(GFARM_MSG_1000353, "%s: %s", diag,
		    gfarm_error_string(e));
	} else if (!created) {
		e = GFARM_ERR_ALREADY_EXISTS;
		gflog_error(GFARM_MSG_1000354,
		    "%s: directory inode %lld entry name \'%*s\': %s", diag,
		    (long long)dir_inum, entry_len, entry_name,
		    gfarm_error_string(e));
	} else {
		dir_entry_set_inode(entry, entry_inode);
		inode_increment_nlink_ini(entry_inode);
		if (inode_is_dir(entry_inode) &&
		    !name_is_dot_or_dotdot(entry_name, entry_len) &&
		    dir_inode != entry_inode /* avoid self reference */) {
			/* XXX should avoid loop too */
			/* remember parent */
			entry_inode->u.c.s.d.parent_dir = dir_inode;
		}
		e = GFARM_ERR_NO_ERROR;
	}
	return (e);
}

void
inode_init(void)
{
	gfarm_error_t e;

	if (!inode_free_list_initialized)
		inode_free_list_init();

	e = db_inode_load(NULL, inode_add_one);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1000355,
		    "loading inode: %s", gfarm_error_string(e));
	e = db_inode_cksum_load(NULL, inode_cksum_add_one);
	if (e != GFARM_ERR_NO_ERROR && e != GFARM_ERR_NO_SUCH_OBJECT /* XXX */)
		gflog_error(GFARM_MSG_1000356,
		    "loading inode cksum: %s", gfarm_error_string(e));
}

void
inode_initial_entry(void)
{
	gfarm_error_t e;
	struct inode *root;
	struct gfs_stat st;

	root = inode_lookup(ROOT_INUMBER);
	if (root != NULL)
		return;

	gflog_info(GFARM_MSG_1000357,
	    "root inode not found, creating filesystem");

	/* root inode */
	st.st_ino = ROOT_INUMBER;
	st.st_gen = 0;
	st.st_nlink = 2;
	st.st_size = 4;
	st.st_mode = GFARM_S_IFDIR | 0775;
	st.st_user = strdup_ck(ADMIN_USER_NAME, "inode_init");
	st.st_group = strdup_ck(ADMIN_GROUP_NAME, "inode_init");
	touch(&st.st_atimespec);
 	st.st_ctimespec = st.st_mtimespec = st.st_atimespec;
	/* inode_add_one will free(st). need to call db_inode_add before it */
	e = db_inode_add(&st);
	inode_add_one(NULL, &st);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1000358,
		    "failed to store root inode to storage: %s",
		    gfarm_error_string(e));

	/* root directory */
	dir_entry_add_one(NULL, ROOT_INUMBER,
	    strdup_ck(DOT, "inode_init: \".\""), DOT_LEN,
	    ROOT_INUMBER);
	dir_entry_add_one(NULL, ROOT_INUMBER,
	    strdup_ck(DOTDOT, "inode_init: \"..\""), DOTDOT_LEN,
	    ROOT_INUMBER);
	e = db_direntry_add(ROOT_INUMBER, DOT, DOT_LEN, ROOT_INUMBER);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1000359,
		    "failed to store '.' in root directory to storage: %s",
		    gfarm_error_string(e));
	e = db_direntry_add(ROOT_INUMBER, DOTDOT, DOTDOT_LEN, ROOT_INUMBER);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1000360,
		    "failed to store '..' in root directory to storage: %s",
		    gfarm_error_string(e));
}

static void
inode_dir_check_and_repair_dot(struct inode *dir_inode)
{
	gfarm_error_t e;
	DirEntry entry = dir_lookup(dir_inode->u.c.s.d.entries, DOT, DOT_LEN);
	int should_add_dot = 0;
	struct inode *old;

	if (entry == NULL) {
		should_add_dot = 1;
		old = NULL;
	} else if ((old = dir_entry_get_inode(entry)) != dir_inode) {
		should_add_dot = 1;
		assert(old != NULL);
		dir_remove_entry(dir_inode->u.c.s.d.entries, DOT, DOT_LEN);
		e = db_direntry_remove(
		    inode_get_number(dir_inode), DOT, DOT_LEN);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1002832,
			    "repair_dot: db_direntry_remove(%llu, %.*s): %s",
			    (unsigned long long)inode_get_number(dir_inode),
			    (int)DOT_LEN, DOT, gfarm_error_string(e));
		inode_decrement_nlink_ini(old);
	}
	if (should_add_dot) {
		entry = dir_enter(dir_inode->u.c.s.d.entries,
		    DOT, DOT_LEN, NULL);
		if (entry == NULL) {
			gflog_error(GFARM_MSG_1002833,
			    "repair_dot: dir_enter(%llu, %*.s, %llu): "
			    "no memory",
			    (unsigned long long)inode_get_number(dir_inode),
			    (int)DOT_LEN, DOT,
			    (unsigned long long)inode_get_number(dir_inode));
			return;
		}
		dir_entry_set_inode(entry, dir_inode);
		e = db_direntry_add(inode_get_number(dir_inode), DOT, DOT_LEN,
		    inode_get_number(dir_inode));
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1002834,
			    "repair_dot: db_direntry_add(%llu, %*.s, %llu): %s",
			    (unsigned long long)inode_get_number(dir_inode),
			    (int)DOT_LEN, DOT,
			    (unsigned long long)inode_get_number(dir_inode),
			    gfarm_error_string(e));
		inode_increment_nlink_ini(dir_inode);

		if (old == NULL)
			gflog_warning(GFARM_MSG_1002835,
			    "inode %llu: dot didn't exist: fixed",
			    (unsigned long long)inode_get_number(dir_inode));
		else
			gflog_warning(GFARM_MSG_1002836,
			    "inode %llu: dot pointed %llu: fixed",
			    (unsigned long long)inode_get_number(dir_inode),
			    (unsigned long long)inode_get_number(old));
	}
}

static void
inode_check_and_repair_dir(void *closure, struct inode *inode)
{
	int *lost_found_modifiedp = closure;
	struct inode *parent;

	if (!inode_is_dir(inode))
		return;

	inode_dir_check_and_repair_dot(inode);

	if ((parent = inode->u.c.s.d.parent_dir) != NULL) {
		inode_dir_check_and_repair_dotdot(inode, parent);
	} else {
		inode_link_to_lost_found_and_report(inode);
		*lost_found_modifiedp = 1;
	}
}

static void
inode_check_and_repair_dir_entries(void *closure, struct inode *inode)
{
	gfarm_error_t e;
	Dir dir;
	DirEntry entry;
	DirCursor cursor;
	char *entry_name;
	int entry_len;
	struct inode *entry_inode;

	if (!inode_is_dir(inode))
		return;

	dir = inode->u.c.s.d.entries;
	if (!dir_cursor_set_pos(dir, 0, &cursor)) {
		gflog_error(GFARM_MSG_1002837,
		    "inode_check_and_repair_dir_entries(%llu): "
		    "cannot get cursor",
		    (unsigned long long)inode_get_number(inode));
		abort();
	}
	for (;;) {
		entry = dir_cursor_get_entry(dir, &cursor);
		if (entry == NULL)
			return;

		entry_inode = dir_entry_get_inode(entry);
		entry_name = dir_entry_get_name(entry, &entry_len);
		if (inode_is_dir(entry_inode) &&
		    !name_is_dot_or_dotdot(entry_name, entry_len) &&
		    entry_inode->u.c.s.d.parent_dir != inode) {
			e = db_direntry_remove(inode_get_number(inode),
			    entry_name, entry_len);
			if (e != GFARM_ERR_NO_ERROR)
				gflog_error(GFARM_MSG_1002838,
				    "db_direntry_remove(%llu, %.*s): %s",
				    (unsigned long long)inode_get_number(inode),
				    entry_len, entry_name,
				    gfarm_error_string(e));
			inode_decrement_nlink_ini(entry_inode);

			gflog_warning(GFARM_MSG_1002839,
			    "dir %llu: multiple parents - "
			    "%llu and %llu (name %.*s): the latter is removed",
			    (unsigned long long)inode_get_number(entry_inode),
			    (unsigned long long)inode_get_number(
			    entry_inode->u.c.s.d.parent_dir),
			    (unsigned long long)inode_get_number(inode),
			    entry_len, entry_name);

			/* must be done here, to access entry_name above */
			if (!dir_cursor_remove_entry(dir, &cursor))
				return;
		} else {
			if (!dir_cursor_next(dir, &cursor))
				return;
		}
	}
}

static void
inode_check_and_repair_nlink(void *closure, struct inode *inode)
{
	gfarm_error_t e;
	int nlink_modified = 0;
	int *lost_found_modifiedp = closure;
	static int dir_reported = 0;

	if (inode_get_nlink(inode) != inode_get_nlink_ini(inode)) {
		if (!inode_is_dir(inode) || !dir_reported) {
			gflog_warning(GFARM_MSG_1005173,
			    "inode %llu:%llu nlink %llu should be %llu: fixed",
			    (unsigned long long)inode_get_number(inode),
			    (unsigned long long)inode_get_gen(inode),
			    (unsigned long long)inode_get_nlink(inode),
			    (unsigned long long)inode_get_nlink_ini(inode));
			if (inode_is_dir(inode)) { /* too noisy */
				dir_reported = 1;
				gflog_warning(GFARM_MSG_1002840,
				    "suppress nlink reports "
				    "for other directories");
			}
		}

		inode->i_nlink = inode_get_nlink_ini(inode);
		nlink_modified = 1;
	}
	if (inode_get_nlink_ini(inode) == 0) {
		inode_link_to_lost_found_and_report(inode);
		nlink_modified = 1;
		*lost_found_modifiedp = 1;
	}

	if (!nlink_modified)
		return;
	e = db_inode_nlink_modify(
	    inode_get_number(inode), inode_get_nlink(inode));
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1002493,
		    "db_inode_nlink_modify(%llu): %s",
		    (unsigned long long)inode_get_number(inode),
		    gfarm_error_string(e));
}

void
inode_check_and_repair(void)
{
	gfarm_error_t e;
	int transaction = 0;
	int lost_found_modified = 0;
	struct inode *lost_found;
	static const char diag[] = "inode_check_and_repair";

	if (db_begin(diag) == GFARM_ERR_NO_ERROR) /* to make things faster */
		transaction = 1;

	inode_lookup_all(&lost_found_modified, inode_check_and_repair_dir);

	/*
	 * must be different pass from inode_check_and_repair_dir,
	 * since this assumes that inode->u.c.s.d.parent_dir is set,
	 * and inode_check_and_repair_dir() may set it.
	 */
	inode_lookup_all(NULL, inode_check_and_repair_dir_entries);

	inode_lookup_all(&lost_found_modified, inode_check_and_repair_nlink);

	if (lost_found_modified) {
		lost_found = inode_lookup_lost_found();
		if (lost_found == NULL) {
			gflog_error(GFARM_MSG_1002841,
			    "lost+found: cannot update st_mtime");
		} else {
			e = db_inode_nlink_modify(inode_get_number(lost_found),
			    lost_found->i_nlink);
			if (e != GFARM_ERR_NO_ERROR)
				gflog_error(GFARM_MSG_1005174,
				    "db_inode_nlink_modify(%llu:%llu): %s",
				    (unsigned long long)
				    inode_get_number(lost_found),
				    (unsigned long long)
				    inode_get_gen(lost_found),
				    gfarm_error_string(e));
			inode_modified(lost_found);
		}
	}

	inode_lookup_all(NULL, inode_check_and_repair_dir);

	if (transaction)
		db_end(diag);
}

void
dir_entry_init(void)
{
	gfarm_error_t e;

	e = db_direntry_load(NULL, dir_entry_add_one);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1000363,
		    "loading direntry: %s", gfarm_error_string(e));
}

void
dir_entry_initial_entry(void)
{
	struct inode *root;

	/* setup root->u.c.s.d.parent_dir */
	root = inode_lookup(ROOT_INUMBER);
	if (root == NULL) {
		gflog_error(GFARM_MSG_1002843,
		    "dir_entry_init: no root directory");
		return;
	}
	root->u.c.s.d.parent_dir = root;
}

void
symlink_init(void)
{
	gfarm_error_t e;

	e = db_symlink_load(NULL, symlink_add_one);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1000364,
		    "loading symlink: %s", gfarm_error_string(e));
}

/* implemented here to refer dot and dotdot */
int
dir_is_empty(Dir dir)
{
	DirEntry entry;
	DirCursor cursor;
	char *name;
	int namelen;

	if (!dir_cursor_set_pos(dir, 0, &cursor)) {
		gflog_error(GFARM_MSG_1000365,
		    "dir_emptry: cannot get cursor");
		abort();
	}
	for (;;) {
		entry = dir_cursor_get_entry(dir, &cursor);
		if (entry == NULL)
			return (1);
		name = dir_entry_get_name(entry, &namelen);
		if (!name_is_dot_or_dotdot(name, namelen))
			return (0);
		if (!dir_cursor_next(dir, &cursor))
			return (1);
	}
}

static struct xattr_entry *
xattr_entry_alloc(const char *attrname)
{
	struct xattr_entry *entry;
	static const char diag[] = "xattr_entry_alloc";

	GFARM_CALLOC_ARRAY(entry, 1);
	if (entry == NULL) {
		gflog_error(GFARM_MSG_1001777, "no memory");
		return (NULL);
	}
	if ((entry->name = strdup_log(attrname, diag)) == NULL) {
		free(entry);
		return (NULL);
	}
	entry->cached_attrvalue = NULL;
	entry->cached_attrsize = 0;
	return (entry);
}

static void
xattr_entry_free(struct xattr_entry *entry)
{
	if (entry != NULL) {
		free(entry->name);
		if (entry->cached_attrvalue != NULL)
			free(entry->cached_attrvalue);
		free(entry);
	}
}

static struct xattr_entry *
xattr_add(struct xattrs *xattrs, int xmlMode, const char *attrname,
	const void *value, int size)
{
	struct xattr_entry *entry, *tail;

	entry = xattr_entry_alloc(attrname);
	if (entry == NULL) {
		gflog_debug(GFARM_MSG_1001778,
			"allocation of 'xattr_entry' failure");
		return NULL;
	}
	if (!xmlMode && gfarm_xattr_caching(attrname) && value != NULL) {
		/* since malloc(0) is not portable */
		entry->cached_attrvalue = malloc(size == 0 ? 1 : size);
		if (entry->cached_attrvalue == NULL) {
			gflog_warning(GFARM_MSG_1002494,
			    "trying to cache %d bytes for attr %s: no memory",
			    size, attrname);
		} else {
			memcpy(entry->cached_attrvalue, value, size);
			entry->cached_attrsize = size;
		}
	}

	if (xattrs->head == NULL) {
		xattrs->head = xattrs->tail = entry;
		entry->prev = NULL;
	} else {
		tail = xattrs->tail;
		entry->prev = tail;
		tail->next = entry;
		xattrs->tail = entry;
	}
	return entry;
}

/*
 * The memory owner is NOT changed to inode.c for now.
 * If you'll change that, check xattr_defer_db_removal() too.
 */
void
xattr_add_one(void *closure, struct xattr_info *info)
{
	struct inode *inode = inode_lookup(info->inum);
	struct xattrs *xattrs;
	int xmlMode = (closure != NULL) ? *(int*)closure : 0;

	if (inode == NULL) {
		gflog_error(GFARM_MSG_1000366,
		    "xattr_add_one: no file %lld",
			(unsigned long long)info->inum);
		if (xmlMode)
			xml_defer_db_removal(info);
		else
			xattr_defer_db_removal(info);
	} else {
		xattrs = (xmlMode ? &inode->i_xmlattrs : &inode->i_xattrs);
		if (xattr_add(xattrs, xmlMode, info->attrname,
		    info->attrvalue, info->attrsize) == NULL)
			gflog_error(GFARM_MSG_1000367, "xattr_add_one: "
				"cannot add attrname %s to %lld",
				info->attrname, (unsigned long long)info->inum);
	}
}

void
xattr_init_cache_all(void)
{
	if (!gfarm_xattr_caching(xattr_all))
		gfarm_xattr_caching_pattern_add(xattr_all);
}

void
xattr_init(void)
{
	gfarm_error_t e;
	int xmlMode;

	if (!gfarm_xattr_caching(GFARM_ACL_EA_ACCESS))
		gfarm_xattr_caching_pattern_add(GFARM_ACL_EA_ACCESS);
	if (!gfarm_xattr_caching(GFARM_ACL_EA_DEFAULT))
		gfarm_xattr_caching_pattern_add(GFARM_ACL_EA_DEFAULT);

	if (!gfarm_xattr_caching(GFARM_ROOT_EA_USER))
		gfarm_xattr_caching_pattern_add(GFARM_ROOT_EA_USER);
	if (!gfarm_xattr_caching(GFARM_ROOT_EA_GROUP))
		gfarm_xattr_caching_pattern_add(GFARM_ROOT_EA_GROUP);

	if (!gfarm_xattr_caching(xattr_ncopy))
		gfarm_xattr_caching_pattern_add(xattr_ncopy);
	if (!gfarm_xattr_caching(xattr_md5))
		gfarm_xattr_caching_pattern_add(xattr_md5);
	if (!gfarm_xattr_caching(xattr_repattr))
		gfarm_xattr_caching_pattern_add(xattr_repattr);

	xmlMode = 0;
	e = db_xattr_load(&xmlMode, xattr_add_one);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1000368,
		    "loading xattr: %s", gfarm_error_string(e));
#ifdef ENABLE_XMLATTR
	xmlMode = 1;
	e = db_xattr_load(&xmlMode, xattr_add_one);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1000369,
		    "loading xmlattr: %s", gfarm_error_string(e));
#endif
}

static struct xattr_entry *
xattr_find(struct xattrs *xattrs, const char *attrname)
{
	struct xattr_entry *entry;

	entry = xattrs->head;
	while (entry != NULL) {
		if (strcmp(entry->name, attrname) == 0) {
			return entry;
		}
		entry = entry->next;
	}
	return NULL;
}

int
inode_xattr_has_attr(struct inode *inode, int xmlMode, const char *attrname)
{
	struct xattrs *xattrs = xmlMode ? &inode->i_xmlattrs : &inode->i_xattrs;

	return (xattr_find(xattrs, attrname) != NULL);
}

gfarm_error_t
inode_xattr_add(struct inode *inode, int xmlMode, const char *attrname,
	void *value, size_t size)
{
	gfarm_error_t e;
	struct xattrs *xattrs =
		xmlMode ? &inode->i_xmlattrs : &inode->i_xattrs;

	if (xattr_find(xattrs, attrname) != NULL) {
		gflog_debug(GFARM_MSG_1001779,
			"xattr of inode already exists: %s", attrname);
		e = GFARM_ERR_ALREADY_EXISTS;
	} else if (xattr_add(xattrs, xmlMode, attrname, value, size) != NULL) {
		e = GFARM_ERR_NO_ERROR;
	} else {
		gflog_debug(GFARM_MSG_1001780,
			"xattr_add() failed : %s", attrname);
		e = GFARM_ERR_NO_MEMORY;
	}
	return (e);
}

gfarm_error_t
inode_xattr_modify(struct inode *inode, int xmlMode, const char *attrname,
	void *value, size_t size)
{
	struct xattrs *xattrs =
		xmlMode ? &inode->i_xmlattrs : &inode->i_xattrs;
	struct xattr_entry *entry = xattr_find(xattrs, attrname);

	if (entry == NULL)
		return (GFARM_ERR_NO_SUCH_OBJECT);

	if (entry->cached_attrvalue != NULL) {
		free(entry->cached_attrvalue);
		entry->cached_attrvalue = NULL;
		entry->cached_attrsize = 0;
	}
	if (!xmlMode && gfarm_xattr_caching(attrname)) {
		entry->cached_attrvalue = malloc(size);
		if (entry->cached_attrvalue == NULL) {
			gflog_warning(GFARM_MSG_1002495,
			    "trying to cache %d bytes for attr %s: no memory",
			    (int)size, attrname);
			return (GFARM_ERR_NO_MEMORY);
		} else {
			memcpy(entry->cached_attrvalue, value, size);
			entry->cached_attrsize = size;
		}
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
inode_xattr_get_cache_with_process(struct inode *inode, int xmlMode,
	const char *attrname, void **cached_valuep, size_t *cached_sizep,
	struct process *process)
{
	struct xattrs *xattrs =
		xmlMode ? &inode->i_xmlattrs : &inode->i_xattrs;
	struct xattr_entry *entry;
	void *r;
	struct xattr_list l;
	gfarm_error_t e;
	static const char diag[] = "inode_xattr_get_cache";

	if (!xmlMode) {
		/* virtual extended attributes */
		if (strcmp(attrname, GFARM_EA_DIRECTORY_QUOTA) == 0) {
			struct dirset *tdirset;

			/*
			 * when this is called from gfm_server_getxattr(),
			 * inode is already opened.
			 */
			tdirset = inode_get_tdirset(inode);
			if (tdirset == TDIRSET_IS_UNKNOWN ||
			    tdirset == TDIRSET_IS_NOT_SET)
				return (GFARM_ERR_NO_SUCH_OBJECT);
			e = xattr_list_set_by_dirset(&l,
			    GFARM_EA_DIRECTORY_QUOTA, tdirset, process, diag);
			if (e != GFARM_ERR_NO_ERROR)
				return (e);
			*cached_valuep = l.value;
			*cached_sizep = l.size;
			free(l.name);
			return (GFARM_ERR_NO_ERROR);
		} else if (process &&
			   strcmp(attrname, GFARM_EA_EFFECTIVE_PERM) == 0) {
			struct tenant *tenant = process_get_tenant(process);
			struct user *user = process_get_user(process);

			e = xattr_list_set_by_inode_access(
			    &l, NULL, inode, tenant, user, diag);
			if (e != GFARM_ERR_NO_ERROR)
				return (e);
			*cached_valuep = l.value;
			*cached_sizep = 1;
			return (GFARM_ERR_NO_ERROR);
		}
	}
	entry = xattr_find(xattrs, attrname);
	if (entry == NULL)
		return (GFARM_ERR_NO_SUCH_OBJECT);

	if (entry->cached_attrvalue == NULL) {
		*cached_valuep = NULL;
		*cached_sizep = 0;
	} else if ((r = malloc(entry->cached_attrsize)) == NULL) {
		gflog_error(GFARM_MSG_1004350, "no memory");
		return (GFARM_ERR_NO_MEMORY);
	} else {
		memcpy(r, entry->cached_attrvalue, entry->cached_attrsize);
		*cached_valuep = r;
		*cached_sizep = entry->cached_attrsize;
	}

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
inode_xattr_get_cache(struct inode *inode, int xmlMode,
	const char *attrname, void **cached_valuep, size_t *cached_sizep)
{
	return (inode_xattr_get_cache_with_process(inode, xmlMode,
	    attrname, cached_valuep, cached_sizep, NULL));
}

int
inode_xattr_cache_is_same(struct inode *inode, int xmlMode,
	const char *attrname, const void *value, size_t size)
{
	struct xattrs *xattrs =
		xmlMode ? &inode->i_xmlattrs : &inode->i_xattrs;
	struct xattr_entry *entry = xattr_find(xattrs, attrname);

	if (entry == NULL || entry->cached_attrvalue == NULL) {
		if (size == 0)
			return (1);
		else
			return (0);
	}
	if (entry->cached_attrsize != size)
		return (0);
	if (entry->cached_attrsize == 0 && size == 0)
		return (1);

	return (!memcmp(entry->cached_attrvalue, value, size));
}

void
inode_xattr_list_free(struct xattr_list *list, size_t n)
{
	int i;

	for (i = 0; i < n; i++) {
		free(list[i].name);
		if (list[i].value != NULL)
			free(list[i].value);
	}
	free(list);
}

gfarm_error_t
inode_xattr_list_get_cached_by_patterns(gfarm_ino_t inum,
	struct process *process,
	char **patterns, int npattern, struct dirset *dirset,
	struct xattr_list **listp, size_t *np)
{
	struct inode *inode;
	struct tenant *tenant = process_get_tenant(process);
	struct user *user = process_get_user(process);
	size_t nxattrs;
	struct xattr_list *list;
	struct xattrs *xattrs;
	struct xattr_entry *entry;
	int i, j, directory_quota = 0, effective_perm = 0;
	static const char diag[] = "inode_xattr_list_get_cached_by_patterns";

	inode = inode_lookup(inum);
	if (inode == NULL)
		return (GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY);

	xattrs = &inode->i_xattrs;
	nxattrs = 0;

	for (j = 0; j < npattern; j++) {
		/* virtual extended attributes */
		if (strcmp(patterns[j], GFARM_EA_DIRECTORY_QUOTA) == 0) {
			if (dirset != NULL)
				directory_quota = 1;
			else if (inode_is_dir(inode) &&
			    (dirset = quota_dir_get_dirset_by_inum(inum))
			    != NULL)
				directory_quota = 1;
		} else if (strcmp(patterns[j], GFARM_EA_EFFECTIVE_PERM) == 0) {
			effective_perm = 1;
		}
		if (directory_quota && effective_perm)
			break;
	}
	if (directory_quota)
		++nxattrs;
	if (effective_perm)
		++nxattrs;

	for (entry = xattrs->head; entry != NULL; entry = entry->next) {
		for (j = 0; j < npattern; j++) {
			if (gfarm_pattern_match(patterns[j], entry->name, 0)) {
				++nxattrs;
				break;
			}
		}
	}
	if (nxattrs == 0) {
		*np = 0;
		*listp = NULL;
		return (GFARM_ERR_NO_ERROR);
	}
	GFARM_CALLOC_ARRAY(list, nxattrs);
	if (list == NULL) {
		gflog_error(GFARM_MSG_1004351, "no memory");
		return (GFARM_ERR_NO_MEMORY);
	}

	i = 0;
	if (directory_quota) { /* only true if dirset is set */
		if (xattr_list_set_by_dirset(&list[i],
		    GFARM_EA_DIRECTORY_QUOTA, dirset, process, diag)
		    != GFARM_ERR_NO_ERROR)
			nxattrs = i;
		else
			i++;
	}
	if (effective_perm) {
		if (xattr_list_set_by_inode_access(&list[i],
		    GFARM_EA_EFFECTIVE_PERM, inode, tenant, user, diag)
		    != GFARM_ERR_NO_ERROR)
			nxattrs = i;
		else
			i++;
	}
	for (entry = xattrs->head;
	     entry != NULL && i < nxattrs;
	     entry = entry->next) {
		for (j = 0; j < npattern; j++) {
			if (gfarm_pattern_match(patterns[j], entry->name, 0)) {
				list[i].name = strdup_log(entry->name, diag);
				if (list[i].name == NULL) {
					nxattrs = i;
					break;
				}
				if (entry->cached_attrvalue == NULL) {
					/* not cached */
					list[i].value = NULL;
					list[i].size = 0;
				} else { /* cached */
					list[i].value =
					    malloc(entry->cached_attrsize);
					if (list[i].value == NULL) {
						gflog_error(GFARM_MSG_1004352,
						    "no memory");
						list[i].size = 0;
					} else {
						memcpy(list[i].value,
						    entry->cached_attrvalue,
						    entry->cached_attrsize);
						list[i].size =
						    entry->cached_attrsize;
					}
				}
				i++;
				break;
			}
		}
	}
	*np = nxattrs;
	*listp = list;
	return (GFARM_ERR_NO_ERROR);
}

int
inode_xattr_has_xmlattrs(struct inode *inode)
{
#ifdef ENABLE_XMLATTR
	return (inode->i_xmlattrs.head != NULL);
#else
	return 0;
#endif
}

gfarm_error_t
inode_xattr_remove(struct inode *inode, int xmlMode, const char *attrname)
{
	struct xattrs *xattrs = xmlMode ? &inode->i_xmlattrs : &inode->i_xattrs;
	struct xattr_entry *entry, *prev, *next;

	entry = xattr_find(xattrs, attrname);
	if (entry != NULL) {
		prev = entry->prev; // NULL if entry is head
		next = entry->next; // NULL if entry is tail
		if (entry == xattrs->head)
			xattrs->head = next;
		else
			prev->next = next;
		if (entry == xattrs->tail)
			xattrs->tail = prev;
		else
			next->prev = prev;
		xattr_entry_free(entry);
		return GFARM_ERR_NO_ERROR;
	} else {
		gflog_debug(GFARM_MSG_1001781,
			"xattr of inode does not exist");
		return GFARM_ERR_NO_SUCH_OBJECT;
	}
}

gfarm_error_t
inode_xattr_list(struct inode *inode, int xmlMode, char **namesp, size_t *sizep)
{
	struct xattrs *xattrs = xmlMode ? &inode->i_xmlattrs : &inode->i_xattrs;
	struct xattr_entry *entry = NULL;
	char *names, *p;
	int size = 0, len;

	*namesp = NULL;
	*sizep = 0;

	entry = xattrs->head;
	while (entry != NULL) {
		size += (strlen(entry->name) + 1);
		entry = entry->next;
	}
	if (size == 0)
		return (GFARM_ERR_NO_ERROR);
	if (GFARM_MALLOC_ARRAY(names, size) == NULL) {
		gflog_error(GFARM_MSG_1004353, "no memory");
		return (GFARM_ERR_NO_MEMORY);
	}
	entry = xattrs->head;
	p = names;
	while (entry != NULL) {
		len = strlen(entry->name) + 1; // +1 is '\0'
		memcpy(p, entry->name, len);
		p += len;
		entry = entry->next;
	}
	*namesp = names;
	*sizep = size;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
inode_xattr_to_uint(
	const void *value, size_t size, unsigned int *retvp, int *all_digitp)
{
	unsigned int n, save;
	const unsigned char *s = value;

	n = 0;
	save = 0;
	for (; size > 0 && isdigit(*s); size--) {
		n = n * 10 + (*s - '0');
		if (n < save) { /* overflow */
			*all_digitp = 0;
			*retvp = save;
			return (GFARM_ERR_RESULT_OUT_OF_RANGE);
		}
		save = n;
		s++;
	}
	if (size == 0)
		*all_digitp = 1;
	else
		*all_digitp = 0;
	*retvp = n;
	return (GFARM_ERR_NO_ERROR);
}

static int
inode_xattr_convert_desired_number(
	const void *value, size_t size, int *desired_numberp)
{
	unsigned int n;
	int all_digit;

	if (inode_xattr_to_uint(value, size, &n, &all_digit)
	    != GFARM_ERR_NO_ERROR)
		return (0); /* ignore */
	*desired_numberp = n;
	if (*desired_numberp < 0) /* overflow */
		return (0); /* ignore */
	return (1);
}

/* This assumes that the "gfarm.ncopy" xattr is cached. */
int
inode_has_desired_number(struct inode *inode, int *desired_numberp)
{
	struct xattr_entry *ent = xattr_find(&inode->i_xattrs, xattr_ncopy);

	if (ent == NULL || ent->cached_attrvalue == NULL)
		return (0);
	return (inode_xattr_convert_desired_number(
	    ent->cached_attrvalue, ent->cached_attrsize, desired_numberp));
}

/* And also this assumes that the "gfarm.replicainfo" xattr is cached. */
int
inode_has_repattr(struct inode *inode, char **repattrp)
{
	void *repattr = NULL;
	size_t size = 0;

	if (inode_xattr_get_cache(inode, 0, GFARM_EA_REPATTR,
		&repattr, &size) != GFARM_ERR_NO_ERROR)
		return (0);

	if (repattr == NULL)
		return (0);

	/* The repattr is malloc'd in inode_xattr_get_cache(). */

	if (*(char *)repattr == '\0') { /* treat this as unspecified */
		free(repattr);
		return (0);
	}
	if (repattrp != NULL)
		*repattrp = (char *)repattr;
	else
		free(repattr);

	return (1);
}

/*
 * returns 1, if gfarm.replicainfo or gfarm.ncopy is found.
 * if gfarm.replicainfo is found, *repattrp != NULL, otherwise *repattr == NULL
 */
int
inode_get_replica_spec(struct inode *inode,
	char **repattrp, int *desired_numberp)
{
	int found = 0;

	if (inode_has_repattr(inode, repattrp))
		found = 1;
	else
		*repattrp = NULL;

	if (inode_has_desired_number(inode, desired_numberp))
		found = 1;
	else
		*desired_numberp = 0;

	return (found);
}

/*
 * do bottom-up search gfarm.replicainfo or gfarm.ncopy.
 *
 * returns 1, if gfarm.replicainfo or gfarm.ncopy is found.
 * if gfarm.replicainfo is found, *repattrp != NULL, otherwise *repattr == NULL
 */
int
inode_search_replica_spec(struct inode *dir,
	char **repattrp, int *desired_numberp)
{
	DirEntry entry;

	for (;;) {
		if (!inode_is_dir(dir))
			return (0);

		if (inode_get_replica_spec(dir, repattrp, desired_numberp))
			return (1);

		/* do not stop at tenant root, check until real root */
		if (inode_get_number(dir) == ROOT_INUMBER)
			return (0);

		entry = dir_lookup(dir->u.c.s.d.entries, DOTDOT, DOTDOT_LEN);
		if (entry == NULL)
			return (0);

		dir = dir_entry_get_inode(entry);
	}
}

struct dirset *
inode_search_tdirset(struct inode *dir)
{
	struct dirset *dirset;
	DirEntry entry;

	for (;;) {
		if (!inode_is_dir(dir))
			return (TDIRSET_IS_UNKNOWN);

		if ((dirset = quota_dir_get_dirset_by_inum(dir->i_number))
		    != NULL)
			return (dirset);

		/* do not stop at tenant root, check until real root */
		if (dir->i_number == ROOT_INUMBER)
			return (TDIRSET_IS_NOT_SET);

		entry = dir_lookup(dir->u.c.s.d.entries, DOTDOT, DOTDOT_LEN);
		if (entry == NULL)
			return (TDIRSET_IS_UNKNOWN);

		dir = dir_entry_get_inode(entry);
	}
}

#if 1 /* DEBUG */
void
dir_dump(gfarm_ino_t i_number)
{
	gfarm_error_t e;
	struct inode *inode = inode_lookup(i_number), *entry_inode;
	Dir dir;
	DirCursor cursor;
	int ok;
	char *name;

	if (inode == NULL) {
		gflog_info(GFARM_MSG_1000370,
		    "inode_lookup %lld failed", (unsigned long long)i_number);
		return;
	}
	dir = inode_get_dir(inode);
	if (dir == NULL) {
		gflog_info(GFARM_MSG_1000371,
		    "inode %lld is not a directory",
		    (unsigned long long)i_number);
		return;
	}
	ok = dir_cursor_set_pos(dir, 0, &cursor);
	if (!ok) {
		gflog_info(GFARM_MSG_1000372,
		    "dir inode %lld cannot seek to 0",
		    (unsigned long long)i_number);
		return;
	}
	gflog_info(GFARM_MSG_1000373,
	    "dir inode %lld dump start:", (unsigned long long)i_number);
	for (;;) {
		if ((e = dir_cursor_get_name_and_inode(dir, &cursor,
		    &name, &entry_inode)) != GFARM_ERR_NO_ERROR ||
		    name == NULL) {
			gflog_debug(GFARM_MSG_1001783,
				"dir_cursor_get_name_and_inode() failed: %s",
				gfarm_error_string(e));
			break;
		}
		gflog_info(GFARM_MSG_1000374,
		    "entry %s (len=%d) inum %lld",
		    name, (int)strlen(name),
		    (unsigned long long)inode_get_number(entry_inode));
		free(name);
		if (!dir_cursor_next(dir, &cursor))
			break;
	}
	gflog_info(GFARM_MSG_1000375,
	    "dir inode %lld dump end", (unsigned long long)i_number);
}

void
rootdir_dump(void)
{
	dir_dump(ROOT_INUMBER);
}

#endif /* DEBUG */
