/*
 * $Id$
 */

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h> /* sprintf */
#include <ctype.h>
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

#include "config.h"
#include "context.h"
#include "timespec.h"
#include "patmatch.h"
#include "gfm_proto.h"

#include "quota.h"
#include "subr.h"
#include "db_access.h"
#include "host.h"
#include "user.h"
#include "group.h"
#include "dir.h"
#include "inode.h"
#include "dead_file_copy.h"
#include "process.h" /* struct file_opening */
#include "xattr_info.h"
#include "back_channel.h"
#include "acl.h"
#include "xattr.h"
#include "repattr.h"
#include "fsngroup.h"
#include "replica_check.h"

#include "auth.h" /* for "peer.h" */
#include "peer.h" /* peer_reset_pending_new_generation() */
#include "gfmd.h" /* resuming_*() */

#define MAX_DIR_DEPTH			1024	/* == GFARM_PATH_MAX */

#define ROOT_INUMBER			2
#define INODE_TABLE_SIZE_INITIAL	1000
#define INODE_TABLE_SIZE_MULTIPLY	2

#define INODE_MODE_FREE			0	/* struct inode:i_mode */

#define GFS_MAX_DIR_DEPTH		256

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
					struct inode_replicating_state *rstate;
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
			struct inode_open_state *state;
		} c;
	} u;
};

struct checksum {
	char *type;
	size_t len;
	char sum[1];
};

struct inode_open_state {
	struct file_opening openings; /* dummy header */

	union inode_state_type_specific {
		struct inode_state_file {
			struct file_opening *cksum_owner;
			struct event_waiter *event_waiters;
			struct peer *event_source;
			struct gfarm_timespec last_update;
			int writers, spool_writers;
			int replication_pending;
		} f;
	} u;
};

struct inode_replicating_state {
	struct file_replicating replicating_hosts; /* dummy header */
};

struct inode **inode_table = NULL;
gfarm_ino_t inode_table_size = 0;
gfarm_ino_t inode_free_index = ROOT_INUMBER;

struct inode inode_free_list; /* dummy header of doubly linked circular list */
int inode_free_list_initialized = 0;

static gfarm_uint64_t total_num_inodes;
static pthread_mutex_t total_num_inodes_mutex = PTHREAD_MUTEX_INITIALIZER;
static const char total_num_inodes_diag[] = "total_num_inodes_mutex";

static char dot[] = ".";
static char dotdot[] = "..";

#define DOT_LEN		(sizeof(dot) - 1)
#define DOTDOT_LEN	(sizeof(dotdot) - 1)

static char lost_found[] = "lost+found";

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
	struct inode_open_state *ios = inode->u.c.state;

	assert(inode_is_file(inode));
	if (ios == NULL)
		return;

	for (fo = ios->openings.opening_next;
	     fo != &ios->openings;
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
inode_cksum_clear(struct inode *inode)
{
	struct inode_open_state *ios = inode->u.c.state;

	assert(inode_is_file(inode));
	if (ios != NULL && ios->u.f.cksum_owner != NULL)
		ios->u.f.cksum_owner = NULL;
	if (inode->u.c.s.f.cksum != NULL) {
		free(inode->u.c.s.f.cksum);
		inode->u.c.s.f.cksum = NULL;
	}
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
	inode_cksum_clear(inode);
}

void
inode_cksum_invalidate(struct file_opening *fo)
{
	struct inode_open_state *ios = fo->inode->u.c.state;
	struct file_opening *o;

	for (o = ios->openings.opening_next;
	    o != &ios->openings; o = o->opening_next) {
		if (o != fo)
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
		gflog_debug(GFARM_MSG_1001712,
			"allocation of 'size' failed or overflow");
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
inode_cksum_set(struct file_opening *fo,
	const char *cksum_type, size_t cksum_len, const char *cksum,
	gfarm_int32_t flags, struct gfarm_timespec *mtime)
{
	gfarm_error_t e;
	struct inode *inode = fo->inode;
	struct inode_open_state *ios = inode->u.c.state;
	struct checksum *cs;

	assert(ios != NULL);

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
	if (!inode_is_file(fo->inode)) {
		gflog_debug(GFARM_MSG_1001713,
			"inode type is not file");
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	}
	if ((fo->flag & GFARM_FILE_CKSUM_INVALIDATED) != 0) {
		gflog_debug(GFARM_MSG_1001714, "file checksum is invalidated");
		return (GFARM_ERR_EXPIRED);
	}
	/* writable descriptor has precedence over read-only one */
	if (ios->u.f.cksum_owner != NULL &&
	    (accmode_to_op(ios->u.f.cksum_owner->flag) & GFS_W_OK) != 0 &&
	    (accmode_to_op(fo->flag) & GFS_W_OK) == 0) {
		gflog_debug(GFARM_MSG_1001715,
			"writable descriptor has precedence over read-only "
			"one");
		return (GFARM_ERR_EXPIRED);
	}
	cs = inode->u.c.s.f.cksum;

	if (cs == NULL) {
		e = db_inode_cksum_add(inode->i_number,
		    cksum_type, cksum_len, cksum);
	} else {
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

	/* reduce memory reallocation */
	if (cs != NULL &&
	    strcmp(cksum_type, cs->type) == 0 && cksum_len == cs->len) {
		memcpy(cs->sum, cksum, cksum_len);
		return (GFARM_ERR_NO_ERROR);
	}
	inode_cksum_clear(inode);

	e = inode_cksum_set_internal(inode, cksum_type, cksum_len, cksum);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001716,
			"inode_cksum_set_internal() failed: %s",
			gfarm_error_string(e));
		return (e);
	}

	ios->u.f.cksum_owner = fo;

	if (flags & GFM_PROTO_CKSUM_SET_FILE_MODIFIED) {
		inode_set_mtime(inode, mtime);
		inode_cksum_invalidate(fo);
	}

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
inode_cksum_get(struct file_opening *fo,
	char **cksum_typep, size_t *cksum_lenp, char **cksump,
	gfarm_int32_t *flagsp)
{
	struct inode_open_state *ios = fo->inode->u.c.state;
	struct checksum *cs;
	gfarm_int32_t flags = 0;

	if (!inode_is_file(fo->inode)) {
		gflog_debug(GFARM_MSG_1001717,
			"inode type is not file");
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	}
	if (ios->u.f.writers > 1 ||
	    (ios->u.f.writers == 1 &&
	     (accmode_to_op(fo->flag) & GFS_W_OK) == 0))
		flags |= GFM_PROTO_CKSUM_GET_MAYBE_EXPIRED;
	if (fo->flag & GFARM_FILE_CKSUM_INVALIDATED)
		flags |= GFM_PROTO_CKSUM_GET_EXPIRED;

	cs = fo->inode->u.c.s.f.cksum;
	if (cs == NULL) {
		*cksum_typep = NULL;
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

struct inode_open_state *
inode_open_state_alloc(void)
{
	struct inode_open_state *ios;

	GFARM_MALLOC(ios);
	if (ios == NULL) {
		gflog_debug(GFARM_MSG_1001719,
			"allocation of 'inode_open_state' failed");
		return (NULL);
	}
	/* make circular list `openings' empty */
	ios->openings.opening_prev =
	ios->openings.opening_next = &ios->openings;
	ios->u.f.writers = 0;
	ios->u.f.spool_writers = 0;
	ios->u.f.replication_pending = 0;
	ios->u.f.event_waiters = NULL;
	ios->u.f.event_source = NULL;
	ios->u.f.last_update.tv_sec = 0;
	ios->u.f.last_update.tv_nsec = 0;
	ios->u.f.cksum_owner = NULL;
	return (ios);
}

void
inode_open_state_free(struct inode_open_state *ios)
{
	assert(ios->openings.opening_next == &ios->openings);
	free(ios);
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

		if (inum < INODE_TABLE_SIZE_INITIAL)
			new_table_size = INODE_TABLE_SIZE_INITIAL;
		else if (inum < inode_table_size * INODE_TABLE_SIZE_MULTIPLY)
			new_table_size =
			    inode_table_size * INODE_TABLE_SIZE_MULTIPLY;
		else
			new_table_size = inum * INODE_TABLE_SIZE_MULTIPLY;
		GFARM_REALLOC_ARRAY(p, inode_table, new_table_size);
		if (p == NULL) {
			gflog_debug(GFARM_MSG_1001720,
				"re-allocation of inode array failed");
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
			gflog_debug(GFARM_MSG_1001721,
				"allocation of 'inode' failed");
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
		/* remove from the inode_free_list */
		inode->u.l.next->u.l.prev = inode->u.l.prev;
		inode->u.l.prev->u.l.next = inode->u.l.next;
		inode->i_gen++;
	}
	inode->i_nlink_ini = 0;
	inode->u.c.state = NULL;
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

void
inode_free(struct inode *inode)
{
	gfarm_error_t e;

	inode_clear(inode);

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

#define SAME_WARNING_TRIGGER	10	/* check reduced mode */
#define SAME_WARNING_THRESHOLD	30	/* more than this -> reduced mode */
#define SAME_WARNING_DURATION	600	/* seconds to measure the limit */
#define SAME_WARNING_INTERVAL	60	/* seconds: interval of reduced log */

struct gflog_reduced_state rep_failure_state =
	GFLOG_REDUCED_STATE_INITIALIZER(
		SAME_WARNING_TRIGGER,
		SAME_WARNING_THRESHOLD,
		SAME_WARNING_DURATION,
		SAME_WARNING_INTERVAL);

struct gflog_reduced_state rep_success_state =
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
inode_schedule_replication(
	struct inode *inode, int retry, int n_desired,
	int n_srcs, struct host **srcs, int *next_src_indexp,
	int *n_scopep, struct host **scope,
	int *n_existingp, struct host **existing, gfarm_time_t grace,
	int *n_being_removedp, struct host **being_removed,
	const char *diag)
{
	gfarm_error_t e;
	struct host **targets, *src, *dst;
	int busy = 0, n_success = 0, n_targets, i, n_valid;
	struct file_replicating *fr;
	gfarm_off_t necessary_space;

	necessary_space = inode_get_size(inode);
	e = host_schedule_n_except(n_scopep, scope, n_existingp, existing,
	    grace, n_being_removedp, being_removed,
	    host_is_disk_available_filter, &necessary_space,
	    n_desired, &n_targets, &targets, &n_valid);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_warning(GFARM_MSG_UNFIXED,
		    "%s: inode %lld:%lld: cannot create replicas: "
		    "desired=%d/scope=%d/existing=%d/being_removed=%d: %s",
		    diag, (long long)inode_get_number(inode),
		    (long long)inode_get_gen(inode),
		    n_desired, *n_scopep, *n_existingp, *n_being_removedp,
		    gfarm_error_string(e));
		return (e);
	}
	if (n_valid >= n_desired)
		return (GFARM_ERR_NO_ERROR); /* sufficient */

	for (i = 0; i < n_targets; i++) {
		if (*next_src_indexp >= n_srcs)
			*next_src_indexp = 0;
		src = srcs[*next_src_indexp];
		dst = targets[i];

		/* GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE may occurs */
		e = file_replicating_new(inode, dst, retry, NULL, &fr);
		if (e != GFARM_ERR_NO_ERROR) {
			if (e == GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE)
				busy = 1;
			gflog_reduced_warning(GFARM_MSG_1003645,
			    &rep_failure_state,
			    "%s: file_replicating_new: (%s, %lld:%lld): %s",
			    diag, host_name(dst),
			    (long long)inode_get_number(inode),
			    (long long)inode_get_gen(inode),
			    gfarm_error_string(e));
		} else if ((e = async_back_channel_replication_request(
		    host_name(src), host_port(src),
		    dst, inode->i_number, inode->i_gen, fr))
		    != GFARM_ERR_NO_ERROR) {
			/* may sleep */
			file_replicating_free_by_error_before_request(fr);
		} else
			n_success++;
	}
	free(targets);

	if (n_desired - n_valid > n_success) {
		gflog_reduced_debug(GFARM_MSG_UNFIXED, &rep_failure_state,
		    "%s: %lld:%lld:%s: fewer replicas, "
		    "increase=%d/before=%d/desire=%d", diag,
		    (long long)inode_get_number(inode),
		    (long long)inode_get_gen(inode),
		    user_name(inode_get_user(inode)),
		    n_success, n_valid, n_desired);
	} else
		gflog_reduced_notice(GFARM_MSG_UNFIXED, &rep_success_state,
		    "%s: %lld:%lld:%s: will be fixed, increase=%d/desire=%d",
		    diag, (long long)inode_get_number(inode),
		    (long long)inode_get_gen(inode),
		    user_name(inode_get_user(inode)), n_success, n_desired);

	if (busy) /* retry immediately in replica_check */
		return (GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE);

	return (e);
}

/*
 * this function modifies *n_existingp, existing[], *n_being_removedp
 * and being_removed[] but they may be abled to be used later.
 */
gfarm_error_t
inode_schedule_replication_from_all(
	struct inode *inode, int retry, int n_desired,
	int n_srcs, struct host **srcs,
	int *n_existingp, struct host **existing, gfarm_time_t grace,
	int *n_being_removedp, struct host **being_removed,
	const char *diag)
{
	gfarm_error_t e;
	int nhosts, next_src_index = 0;
	struct host **hosts;

	e = host_array_alloc(&nhosts, &hosts);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_warning(GFARM_MSG_1003646, "%s: inode %lld:%lld: "
		    "cannot create %d replicas except %d: %s",
		    diag,
		    (long long)inode_get_number(inode),
		    (long long)inode_get_gen(inode),
		    n_desired, *n_existingp, gfarm_error_string(e));
		return (e);
	}
	e = inode_schedule_replication(
	    inode, retry, n_desired, n_srcs, srcs, &next_src_index,
	    &nhosts, hosts, n_existingp, existing, grace,
	    n_being_removedp, being_removed, diag);
	free(hosts);
	return (e);
}

/*
 * NOTE: excluding_list may or may not include spool_host.
 *	it doesn't if this is called from update_replicas(), but
 *	it does if this this is called from inode_check_pending_replication().
 */
static void
make_replicas_except(struct inode *inode, struct host *spool_host,
	int desired_replica_number, char *repattr,
	struct file_copy *exception_list)
{
	struct host **existing, **being_removed;
	int n_existing = 1; /* +1 is for spool_host */
	int n_being_removed = 0;
	struct host *srcs[1];
	struct file_copy *copy;
	static const char diag[] = "make_replicas_except";

	for (copy = exception_list; copy != NULL; copy = copy->host_next) {
		if (copy->host != spool_host) {
			if (FILE_COPY_IS_BEING_REMOVED(copy))
				++n_being_removed;
			else
				++n_existing; /* include replicating */
		}
	}
	GFARM_MALLOC_ARRAY(existing, n_existing);
	if (existing == NULL) {
		gflog_warning(GFARM_MSG_UNFIXED,
		    "%s: no memory to schedule replicas: existing %d hosts",
		    diag, n_existing);
		return;
	}
	if (n_being_removed > 0) {
		GFARM_MALLOC_ARRAY(being_removed, n_being_removed);
		if (being_removed == NULL) {
			free(existing);
			gflog_warning(GFARM_MSG_UNFIXED,
			    "%s: no memory to schedule replicas: "
			    "being_removed %d hosts",
			    diag, n_being_removed);
			return;
		}
	} else
		being_removed = NULL;

	n_existing = 0;
	n_being_removed = 0;
	existing[n_existing++] = spool_host;
	for (copy = exception_list; copy != NULL; copy = copy->host_next) {
		if (copy->host != spool_host) {
			if (FILE_COPY_IS_BEING_REMOVED(copy))
				being_removed[n_being_removed++] = copy->host;
			else
				existing[n_existing++] = copy->host;
		}
	}

	srcs[0] = spool_host;
	if (repattr != NULL) {
		gflog_debug(GFARM_MSG_1003648,
		    "%s: about to schedule "
		    "repattr-based replication for inode "
		    "%lld:%lld@%s.", diag,
		    (long long)inode_get_number(inode),
		    (long long)inode_get_gen(inode),
		    host_name(spool_host));
		(void)fsngroup_schedule_replication(
		    inode, 1, repattr, 1, srcs,
		    &n_existing, existing, 0,
		    &n_being_removed, being_removed, diag);
	} else if (n_existing < desired_replica_number) {
		gflog_debug(GFARM_MSG_1003648,
		    "%s: about to schedule "
		    "ncopy-based replication for inode %lld:%lld@%s. "
		    "number = %d (= %d - %d + %d)", diag,
		    (long long)inode_get_number(inode),
		    (long long)inode_get_gen(inode),
		    host_name(spool_host),
		    desired_replica_number - n_existing,
		    desired_replica_number, n_existing + n_being_removed,
		    n_being_removed);
		(void)inode_schedule_replication_from_all(
		    inode, 1, desired_replica_number,
		    1, srcs, &n_existing, existing, 0,
		    &n_being_removed, being_removed, diag);
	}
	free(existing);
	free(being_removed);
}

static gfarm_error_t
remove_replica_entity(struct inode *, gfarm_int64_t, struct host *,
	int, struct dead_file_copy **);

/* spool_host may be NULL, if GFARM_FILE_TRUNC_PENDING */
static void
update_replicas(struct inode *inode, struct host *spool_host,
	gfarm_int64_t old_gen, int start_replication,
	int desired_replica_number, char *repattr)
{
	struct file_copy **copyp, *copy, *next, *to_be_excluded = NULL;
	struct dead_file_copy *deferred_cleanup;
	struct file_replicating *fr;
	gfarm_error_t e;

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
					FILE_COPY_IS_VALID(copy), NULL);
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
		make_replicas_except(inode, spool_host,
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
					FILE_COPY_IS_VALID(copy), NULL);
				/* abandon `e' */
			}
		} else if (!FILE_COPY_IS_BEING_REMOVED(copy)) {
			assert(FILE_COPY_IS_VALID(copy));
			e = remove_replica_entity(inode, old_gen, copy->host,
			    FILE_COPY_IS_VALID(copy), &deferred_cleanup);
			/* abandon `e' */

			e = file_replicating_new(inode, copy->host, 1,
			    deferred_cleanup, &fr);
			if (e != GFARM_ERR_NO_ERROR) {
				gflog_warning(GFARM_MSG_1002245,
				    "replication before removal: host %s: %s",
				    host_name(copy->host),
				    gfarm_error_string(e));
				/*
				 * Give up the replication and remove
				 * the old one
				 */
				removal_pendingq_enqueue(deferred_cleanup);
			} else if ((e = async_back_channel_replication_request(
			    host_name(spool_host), host_port(spool_host),
			    copy->host, inode->i_number, inode->i_gen, fr))
			    != GFARM_ERR_NO_ERROR) {
				file_replicating_free_by_error_before_request(
				    fr); /* may sleep */
			}
		}

		next = copy->host_next;
		free(copy);
	}
}

void
inode_remove(struct inode *inode)
{
	int dfc_needs_free = 0;

	inode_remove_all_xattrs(inode);

	if (inode->u.c.state != NULL)
		gflog_fatal(GFARM_MSG_1000302, "inode_remove: still opened");
	if (inode_is_file(inode)) {
		struct file_copy *copy, *cn;

		for (copy = inode->u.c.s.f.copies; copy != NULL; copy = cn) {
			if (FILE_COPY_IS_VALID(copy) &&
			    !FILE_COPY_IS_BEING_REMOVED(copy)) {
				(void)remove_replica_entity(inode, inode->i_gen,
				    copy->host, FILE_COPY_IS_VALID(copy), NULL);
				/* abandon error */
			}
			cn = copy->host_next;
			free(copy);
		}
		inode->u.c.s.f.copies = NULL; /* ncopy == 0 */
		inode_cksum_remove(inode);
		quota_update_file_remove(inode);
		dfc_needs_free = 1;
	} else if (inode_is_dir(inode)) {
		dir_free(inode->u.c.s.d.entries);
	} else if (inode_is_symlink(inode)) {
		free(inode->u.c.s.l.source_path);
	} else {
		gflog_fatal(GFARM_MSG_1002800,
		    "inode_unlink(%llu): unknown inode type: 0%o",
		    (unsigned long long)inode->i_number, inode->i_mode);
		/*NOTREACHED*/
	}
	inode_free(inode);

	if (dfc_needs_free)
		dead_file_copy_inode_status_changed(inode->i_number);
}

static gfarm_error_t
inode_init_dir_internal(struct inode *inode)
{
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

	entry = dir_enter(inode->u.c.s.d.entries, dot, DOT_LEN, NULL);
	if (entry == NULL) {
		dir_free(inode->u.c.s.d.entries);
		gflog_debug(GFARM_MSG_1001724,
			"inode entries is NULL");
		return (GFARM_ERR_NO_MEMORY);
	}
	dir_entry_set_inode(entry, inode);

	entry = dir_enter(inode->u.c.s.d.entries, dotdot, DOTDOT_LEN,
	    NULL);
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
	inode->i_nlink = 1;
	inode->i_mode = GFARM_S_IFREG;
	inode->u.c.s.f.copies = NULL;
	inode->u.c.s.f.cksum = NULL;
	inode->u.c.s.f.rstate = NULL;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
inode_init_symlink(struct inode *inode, char *source_path)
{
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
	free(inode->u.c.s.l.source_path);
	inode->u.c.s.l.source_path = NULL;
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
	int i;

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

	++inode->i_gen;

	e = db_inode_gen_modify(inode->i_number, inode->i_gen);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1002246,
		    "db_inode_gen_modify(%lld, %lld): %s",
		    (unsigned long long)inode->i_number,
		    (unsigned long long)inode->i_gen,
		    gfarm_error_string(e));
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
	return (inode->u.c.s.f.copies == NULL);
}

/* if is_valid == 0, FILE_COPY_BEING_REMOVED copies are included */
static gfarm_int64_t
inode_get_ncopy_common(struct inode *inode, int is_valid, int is_up)
{
	struct file_copy *copy;
	gfarm_int64_t n = 0;

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

/* if is_valid == 0, FILE_COPY_BEING_REMOVED copies are excluded */
gfarm_error_t
inode_count_ncopy_with_grace(
	struct file_copy *copies, int is_valid, gfarm_time_t grace,
	int n_scope, struct host **scope, int *ncopyp)
{
	gfarm_error_t e;
	struct file_copy *copy;
	int count = 0, n_valid = 0, n_invalid = 0, n_before;
	struct host **valid, **invalid;

	for (copy = copies; copy != NULL; copy = copy->host_next)
		count++;

	GFARM_MALLOC_ARRAY(valid, count);
	GFARM_MALLOC_ARRAY(invalid, count);
	if (valid == NULL || invalid == NULL) {
		free(valid);
		free(invalid);
		return (GFARM_ERR_NO_MEMORY);
	}
	for (copy = copies; copy != NULL; copy = copy->host_next) {
		if ((is_valid ?
		     FILE_COPY_IS_VALID(copy) :
		     !FILE_COPY_IS_BEING_REMOVED(copy))
		    && host_is_up_with_grace(copy->host, grace))
			valid[n_valid++] = copy->host;
		else
			invalid[n_invalid++] = copy->host;
	}

	if (n_scope <= 0 || scope == NULL) {
		free(valid);
		free(invalid);

		*ncopyp = n_valid;
		return (GFARM_ERR_NO_ERROR);
	}

	e = host_except(&n_scope, scope, &n_invalid, invalid, NULL, NULL);
	if (e != GFARM_ERR_NO_ERROR) {
		free(valid);
		free(invalid);
		return (e);
	}

	n_before = n_scope;
	e = host_except(&n_scope, scope, &n_valid, valid, NULL, NULL);
	if (e != GFARM_ERR_NO_ERROR) {
		free(valid);
		free(invalid);
		return (e);
	}
	free(valid);
	free(invalid);

	*ncopyp = n_before - n_scope;
	return (GFARM_ERR_NO_ERROR);
}

/* if is_valid == 0, FILE_COPY_BEING_REMOVED copies are excluded */
gfarm_error_t
inode_get_ncopy_with_grace(
	struct inode *inode, int is_valid, gfarm_time_t grace,
	int nscope, struct host **scope, int *ncopyp)
{
	return (inode_count_ncopy_with_grace(
	    inode->u.c.s.f.copies, is_valid, grace, nscope, scope, ncopyp));
}

static gfarm_error_t
inode_alloc_file_copy_hosts(struct inode *inode,
	int (*filter)(struct file_copy *, void *), void *closure,
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
	if (hosts == NULL)
		return (GFARM_ERR_NO_MEMORY);

	i = 0;
	for (copy = inode->u.c.s.f.copies; copy != NULL;
	    copy = copy->host_next) {
		if (i >= nhosts)
			break;
		if ((*filter)(copy, closure))
			hosts[i++] = copy->host;
	}
	*np = i; /* NOTE: this may be 0 */
	*hostsp = hosts;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
inode_replica_hosts(
	struct inode *inode,
	gfarm_int32_t *n_existingp, struct host ***existingp,
	gfarm_int32_t *n_removingp, struct host ***removingp)
{
	struct file_copy *copy;
	int n_existing, n_removing;
	struct host **existing = NULL, **removing = NULL;

	assert(inode_is_file(inode));

	n_existing = 0;
	n_removing = 0;
	for (copy = inode->u.c.s.f.copies; copy != NULL;
	    copy = copy->host_next) {
		if (FILE_COPY_IS_BEING_REMOVED(copy))
			n_removing++;
		else
			n_existing++;
	}
	if (n_existing > 0) {
		GFARM_MALLOC_ARRAY(existing, n_existing);
		if (existing == NULL)
			return (GFARM_ERR_NO_MEMORY);
	}
	if (n_removing > 0) {
		GFARM_MALLOC_ARRAY(removing, n_removing);
		if (removing == NULL) {
			free(existing);
			return (GFARM_ERR_NO_MEMORY);
		}
	}
	n_existing = 0;
	n_removing = 0;
	for (copy = inode->u.c.s.f.copies; copy != NULL;
	    copy = copy->host_next) {
		if (FILE_COPY_IS_BEING_REMOVED(copy))
			removing[n_removing++] = copy->host;
		else
			existing[n_existing++] = copy->host;
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

void
inode_set_mode_in_cache(struct inode *inode, gfarm_mode_t mode)
{
	inode->i_mode = (inode->i_mode & GFARM_S_IFMT) |
	    (mode & GFARM_S_ALLPERM);
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
	inode_set_mode_in_cache(inode, mode);

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

void
inode_set_size_in_cache(struct inode *inode, gfarm_off_t size)
{
	inode->i_size = size;
}

void
inode_set_size(struct inode *inode, gfarm_off_t size)
{
	gfarm_error_t e;

	/* inode is file */
	quota_update_file_resize(inode, size);
	inode_set_size_in_cache(inode, size);

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

void
inode_set_user_by_name_in_cache(struct inode *inode, const char *username)
{
	inode->i_user = user_lookup_or_enter_invalid(username);
}

void
inode_set_group_by_name_in_cache(struct inode *inode, const char *groupname)
{
	inode->i_group = group_lookup_or_enter_invalid(groupname);
}

gfarm_error_t
inode_set_owner(struct inode *inode, struct user *user, struct group *group)
{
	gfarm_error_t e;

	if (user == NULL && group == NULL)
		return (GFARM_ERR_NO_ERROR);

	if (inode_is_file(inode))
		quota_update_file_remove(inode);
	if (user != NULL) {
		inode->i_user = user;

		e = db_inode_user_modify(inode->i_number, user_name(user));
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1000306,
			    "db_inode_user_modify(%lld): %s",
			    (unsigned long long)inode->i_number,
			    gfarm_error_string(e));
	}
	if (group != NULL) {
		inode->i_group = group;

		e = db_inode_group_modify(inode->i_number, group_name(group));
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1000307,
			    "db_inode_group_modify(%lld): %s",
			    (unsigned long long)inode->i_number,
			    gfarm_error_string(e));
	}
	if (inode_is_file(inode))
		quota_update_file_add(inode);

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
	struct inode_open_state *ios = inode->u.c.state;
	static const char diag[] = "inode_new_generation_is_pending";

	if (ios == NULL) {
		gflog_error(GFARM_MSG_1002247, "%s: not opened", diag);
		return (0);
	}
	return (ios->u.f.event_source != NULL);
}

gfarm_error_t
inode_new_generation_wait_start(struct inode *inode, struct peer *peer)
{
	struct inode_open_state *ios = inode->u.c.state;
	static const char diag[] = "inode_new_generation_wait_start";

	if (ios == NULL) {
		gflog_error(GFARM_MSG_1002248, "%s: not opened", diag);
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	}
	ios->u.f.event_source = peer;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
inode_new_generation_done(struct inode *inode, struct peer *peer,
	gfarm_int32_t result)
{
	struct inode_open_state *ios;
	struct event_waiter *waiter, *next;
	static const char diag[] = "inode_new_generation_done";

	if (!inode_is_file(inode)) {
		gflog_error(GFARM_MSG_1002249, "%s: not a file", diag);
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	}
	ios = inode->u.c.state;
	if (ios == NULL) {
		gflog_error(GFARM_MSG_1002250, "%s: not opened", diag);
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	}
	if (ios->u.f.event_source == NULL) {
		gflog_warning(GFARM_MSG_1002251, "%s: not pending", diag);
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	}
	if (peer == NULL) {
		peer = ios->u.f.event_source;
	} else if (peer != ios->u.f.event_source) {
		gflog_warning(GFARM_MSG_1002252, "%s: different peer", diag);
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	}

	peer_reset_pending_new_generation(peer);

	waiter = ios->u.f.event_waiters;
	for (; waiter != NULL; waiter = next) {
		next = waiter->next;
		resuming_enqueue(waiter);
	}
	ios->u.f.event_source = NULL;
	ios->u.f.event_waiters = NULL;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
inode_new_generation_wait(struct inode *inode, struct peer *peer,
	gfarm_error_t (*action)(struct peer *, void *, int *), void *arg)
{
	struct inode_open_state *ios;
	struct event_waiter *waiter;
	static const char diag[] = "inode_new_generation_wait";

	if (!inode_is_file(inode)) {
		gflog_error(GFARM_MSG_1002253, "%s: not a file", diag);
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	}
	ios = inode->u.c.state;
	if (ios == NULL) {
		gflog_error(GFARM_MSG_1002254, "%s: not opened", diag);
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	}
	if (ios->u.f.event_source == NULL) {
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

	waiter->next = ios->u.f.event_waiters;
	ios->u.f.event_waiters = waiter;

	return (GFARM_ERR_NO_ERROR);
}

#define check_mode(mode, mask) ((mode & mask) == mask ? \
			GFARM_ERR_NO_ERROR : GFARM_ERR_PERMISSION_DENIED)

gfarm_error_t
inode_access(struct inode *inode, struct user *user, int op)
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
		else if (user_is_root(inode, user))
			return (GFARM_ERR_NO_ERROR);
		return (GFARM_ERR_PERMISSION_DENIED);
	}

	if (user_is_root(inode, user))
		return (GFARM_ERR_NO_ERROR);

	e = acl_access(inode, user, op);
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
	st.st_user = user_name(inode->i_user);
	st.st_group = group_name(inode->i_group);
	st.st_size = inode->i_size;
	st.st_ncopy = 0;
	st.st_atimespec = inode->i_atimespec;
	st.st_mtimespec = inode->i_mtimespec;
	st.st_ctimespec = inode->i_ctimespec;
	if (inode->i_gen == 0)
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
	int expected_type, enum gfarm_inode_lookup_op op, struct user *user,
	gfarm_mode_t new_mode, char *symlink_src,
	struct inode **inp, int *createdp)
{
	gfarm_error_t e;
	DirEntry entry;
	int created;
	struct inode *n;
	void *acl_acc = NULL, *acl_def = NULL;
	size_t acl_acc_size, acl_def_size;
	void *root_user = NULL, *root_group = NULL;
	size_t root_user_size, root_group_size;

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
		if ((e = inode_access(parent, user, GFS_W_OK)) !=
		    GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001731,
				"inode_access() failed: %s",
				gfarm_error_string(e));
			return (e);
		}
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
		*createdp = 0;
		return (GFARM_ERR_NO_ERROR);
	}
	if ((e = inode_access(parent, user, GFS_W_OK)) != GFARM_ERR_NO_ERROR) {
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
	n = inode_alloc();
	if (n == NULL) {
		dir_remove_entry(parent->u.c.s.d.entries, name, len);
		gflog_debug(GFARM_MSG_1001735,
			"inode_alloc() failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	switch (expected_type) {
	case GFS_DT_DIR: e = inode_init_dir(n, parent); break;
	case GFS_DT_REG:
		e = quota_check_limits(user, parent->i_group, 1, 0);
		if (e == GFARM_ERR_NO_ERROR)
			e = inode_init_file(n);
		break;
	case GFS_DT_LNK: e = inode_init_symlink(n, symlink_src); break;
	default: assert(0); e = GFARM_ERR_UNKNOWN; break;
	}
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001736,
			"error occurred during process: %s",
			gfarm_error_string(e));
		dir_remove_entry(parent->u.c.s.d.entries, name, len);
		inode_free(n);
		return (e);
	}
	n->i_mode |= new_mode;
	n->i_user = user;
	n->i_group = parent->i_group;
	n->i_size = 0;
	inode_created(n);
	dir_entry_set_inode(entry, n);
	inode_modified(parent);

	if (inode_is_file(n))  /* after setting i_user and i_group */
		quota_update_file_add(n);

	e = xattr_inherit(parent, n,
			  &acl_def, &acl_def_size,
			  &acl_acc, &acl_acc_size,
			  &root_user, &root_user_size,
			  &root_group, &root_group_size);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002803, "xattr_inherit() failed: %s",
			    gfarm_error_string(e));
		if (inode_is_file(n)) {
			quota_update_file_remove(n);
		} else if (inode_is_dir(n)) {
			/* "." and ".." are freed automatically */
			dir_free(n->u.c.s.d.entries);
		} else if (inode_is_symlink(n)) {
			free(n->u.c.s.l.source_path);
		}
		dir_remove_entry(parent->u.c.s.d.entries, name, len);
		inode_free(n);
		return (e);
	}

	inode_db_init(n);

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
		e = db_direntry_add(n->i_number, dot, DOT_LEN, n->i_number);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1000315,
			    "db_direntry_add(%lld, \".\", %lld): %s",
			    (unsigned long long)parent->i_number,
			    (unsigned long long)n->i_number,
			    gfarm_error_string(e));
		e = db_direntry_add(
			n->i_number, dotdot, DOTDOT_LEN, parent->i_number);
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
	if (op == INODE_CREATE)
		*createdp = 1;
	return (GFARM_ERR_NO_ERROR);
}

/* XXX TODO: namei cache */
/*
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
gfarm_error_t
inode_lookup_relative(struct inode *n, char *name,
	int expected_type, enum gfarm_inode_lookup_op op, struct user *user,
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
	if ((e = inode_access(n, user, GFS_X_OK)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001738,
			"inode_access() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (op == INODE_LINK)
		nn = *inp;
	e = inode_lookup_basename(n, name, len,
	    expected_type, op, user, new_mode, symlink_src, &nn, createdp);
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

gfarm_error_t
inode_lookup_root(struct process *process, int op, struct inode **inp)
{
	gfarm_error_t e;
	struct inode *inode = inode_lookup(ROOT_INUMBER);

	if (inode == NULL) {
		gflog_debug(GFARM_MSG_1001740, "inode_lookup() failed");
		return (GFARM_ERR_STALE_FILE_HANDLE); /* XXX never happen */
	}
	e = inode_access(inode, process_get_user(process), op);
	if (e == GFARM_ERR_NO_ERROR)
		*inp = inode;
	return (e);
}

gfarm_error_t
inode_lookup_parent(struct inode *base, struct process *process, int op,
	struct inode **inp)
{
	struct inode *inode;
	struct user *user = process_get_user(process);
	gfarm_error_t e = inode_lookup_relative(base, dotdot, GFS_DT_DIR,
	    INODE_LOOKUP, user, 0, NULL, &inode, NULL);

	if (e == GFARM_ERR_NO_ERROR &&
	    (e = inode_access(inode, user, op)) == GFARM_ERR_NO_ERROR) {
		*inp = inode;
	}
	return (e);
}

gfarm_error_t
inode_lookup_by_name(struct inode *base, char *name,
	struct process *process, int op,
	struct inode **inp)
{
	struct inode *inode;
	struct user *user = process_get_user(process);
	gfarm_error_t e = inode_lookup_relative(base, name, GFS_DT_UNKNOWN,
	    INODE_LOOKUP, user, 0, NULL, &inode, NULL);

	if (e == GFARM_ERR_NO_ERROR) {
		if ((op & GFS_W_OK) != 0 && inode_is_dir(inode)) {
			gflog_debug(GFARM_MSG_1001741,
				"inode is directory");
			e = GFARM_ERR_IS_A_DIRECTORY;
		} else {
			e = inode_access(inode, user, op);
		}
		if (e == GFARM_ERR_NO_ERROR && inode_is_file(inode) &&
		    (op & GFS_W_OK) != 0)
			e = quota_check_limits(inode_get_user(inode),
					       inode_get_group(inode), 0, 0);
		if (e == GFARM_ERR_NO_ERROR) {
			*inp = inode;
		}
	}
	return (e);
}

struct inode *
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
	admin = user_lookup_including_invalid(ADMIN_USER_NAME);
	if (admin == NULL) {
		gflog_error(GFARM_MSG_1002481, "no admin user");
		return (NULL);
	}
	e = inode_lookup_relative(root, lost_found, GFS_DT_DIR,
	    INODE_CREATE, admin, 0700, NULL, &inode, &created);
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
	struct process *process, int op, gfarm_mode_t mode,
	struct inode **inp, int *createdp)
{
	struct inode *inode;
	int created;
	struct user *user = process_get_user(process);
	gfarm_error_t e = inode_lookup_relative(base, name, GFS_DT_REG,
	    INODE_CREATE, user, mode, NULL, &inode, &created);

	if (e == GFARM_ERR_NO_ERROR) {
		if (!created)
			e = inode_access(inode, user, op);
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
	    INODE_CREATE_EXCLUSIVE, process_get_user(process), mode, NULL,
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
	    INODE_CREATE_EXCLUSIVE, process_get_user(process),
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
inode_create_link_internal(struct inode *base, char *name,
	struct user *user, struct inode *inode)
{
	return (inode_lookup_relative(base, name, GFS_DT_UNKNOWN,
	    INODE_LINK, user, 0, NULL, &inode, NULL));
}


gfarm_error_t
inode_create_link(struct inode *base, char *name,
	struct process *process, struct inode *inode)
{
	gfarm_error_t e;

	e = inode_create_link_internal(base, name, process_get_user(process),
	    inode);
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

	if (dir_remove_entry(dir_inode->u.c.s.d.entries, dotdot, DOTDOT_LEN)) {
		e = db_direntry_remove(inode_get_number(dir_inode),
		    dotdot, DOTDOT_LEN);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1002805,
			    "reparent: db_direntry_remove(%llu, %.*s): %s",
			    (unsigned long long)inode_get_number(dir_inode),
			    (int)DOTDOT_LEN, dotdot, gfarm_error_string(e));

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

	entry = dir_enter(dir_inode->u.c.s.d.entries, dotdot, DOTDOT_LEN, NULL);
	if (entry == NULL)
		return (GFARM_ERR_NO_MEMORY);
	dir_entry_set_inode(entry, new_parent);
	e = db_direntry_add(inode_get_number(dir_inode),
	    dotdot, DOTDOT_LEN, inode_get_number(new_parent));
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

	if (dir_remove_entry(dir_inode->u.c.s.d.entries, dotdot, DOTDOT_LEN)) {
		e = db_direntry_remove(inode_get_number(dir_inode), dotdot, DOTDOT_LEN);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1002809,
			    "reparent_ini: db_direntry_remove(%llu, %.*s): %s",
			    (unsigned long long)inode_get_number(dir_inode),
			    (int)DOTDOT_LEN, dotdot, gfarm_error_string(e));

		assert(old_parent != NULL);
		inode_decrement_nlink_ini(old_parent);
	}

	entry = dir_enter(dir_inode->u.c.s.d.entries, dotdot, DOTDOT_LEN, NULL);
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
	e = db_direntry_add(inode_get_number(dir_inode), dotdot, DOTDOT_LEN,
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
	    dotdot, DOTDOT_LEN);

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
	admin = user_lookup_including_invalid(ADMIN_USER_NAME);
	if (admin == NULL)
		return (GFARM_ERR_NO_SUCH_USER);

	name_len = sizeof(name);
	snprintf(name, name_len, "%016llX%016llX",
	    (unsigned long long)inode_get_number(inode),
	    (unsigned long long)inode_get_gen(inode));
	e = inode_create_link_internal(base, name, admin, inode);
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
	admin = user_lookup_including_invalid(ADMIN_USER_NAME);
	if (admin == NULL)
		return (GFARM_ERR_NO_SUCH_USER);

	n = inode_alloc();
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
	e = inode_create_link_internal(lf, fname, admin, n); /* i_nlink++ */
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1003477,
		    "inode %lld:%lld on %s -> %lld:%lld: "
		    "cannot create /%s/%s: %s",
		    (long long)inum_old, (long long)gen_old, host_name(host),
		    (long long)inode_get_number(n),
		    (long long)inode_get_gen(n),
		    lost_found, fname, gfarm_error_string(e));
		inode_free(n);
		return (e);
	}
	e = db_inode_nlink_modify(inode_get_number(n), n->i_nlink);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1003478,
		    "db_inode_nlink_modify(%lld): %s",
		    (unsigned long long)inode_get_number(n),
		    gfarm_error_string(e));
	/* abandon `e' */

	*inodep = n;
	return (GFARM_ERR_NO_ERROR);
}

static void
inode_link_to_lost_found_and_report(struct inode *inode)
{
	gfarm_error_t e;

	gflog_warning(GFARM_MSG_1002491,
	    "inode %llu is not referenced, moving to /%s",
	    (unsigned long long)inode_get_number(inode), lost_found);
	/* move to the /lost+found directory */
	e = inode_link_to_lost_found(inode);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1002492,
		    "failed to link inode %llu in /%s: %s",
		    (unsigned long long)inode_get_number(inode),
		    lost_found, gfarm_error_string(e));
	}
}

static int
is_ok_to_move_to(struct inode *movee, struct inode *dir)
{
	DirEntry entry;
	int depth = 0;

	for (;;) {
		if (movee == dir) /* movee is an ancestor of dir */
			return (0);

		if (!inode_is_dir(dir)) /* something is going wrong */
			return (0);

		if (inode_get_number(dir) == ROOT_INUMBER)
			return (1);
			
		entry = dir_lookup(dir->u.c.s.d.entries, dotdot, DOTDOT_LEN);
		if (entry == NULL)
			return (0);
		dir = dir_entry_get_inode(entry);

		/* this check is not strictly necessary, but... */
		if (++depth >= MAX_DIR_DEPTH)
			return (0);
	}
}

gfarm_error_t
inode_rename(
	struct inode *sdir, char *sname,
	struct inode *ddir, char *dname,
	struct process *process,
	struct inode_trace_log_info *srctp,
	struct inode_trace_log_info *dsttp,
	int *dst_removed, int *hlink_removed) /* for gfarm_file_trace */
{
	gfarm_error_t e;
	struct user *user = process_get_user(process);
	struct inode *src, *dst;

	if (user == NULL) {
		gflog_debug(GFARM_MSG_1001742, "process_get_user() failed");
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	}

	/* can remove src? */
	if ((e = inode_access(sdir, user, GFS_X_OK|GFS_W_OK)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001743,
			"inode_access() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if ((e = inode_lookup_by_name(sdir, sname, process, 0, &src))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001744,
			"inode_lookup_by_name() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
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

	e = inode_lookup_by_name(ddir, dname, process, 0, &dst);
	if (e == GFARM_ERR_NO_ERROR) {
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
				return (e);
			} else {
				*dst_removed = 1;
			}
		} else if (GFARM_S_ISDIR(inode_get_mode(src))) {
			gflog_debug(GFARM_MSG_1001748,
				"inode 'inode_get_mode(src)' "
				"is not a directory");
			return (GFARM_ERR_NOT_A_DIRECTORY);
		} else {
			gflog_debug(GFARM_MSG_1001749,
				"inode 'inode_get_mode(src)' is directory");
			return (GFARM_ERR_IS_A_DIRECTORY);
		}
	} else if (e != GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY)
		return (e);

	e = inode_create_link_internal(ddir, dname, user, src);
	if (e != GFARM_ERR_NO_ERROR) { /* shouldn't happen */
		gflog_error(GFARM_MSG_1000319,
		    "rename(%s, %s): failed to link: %s",
		    sname, dname, gfarm_error_string(e));
		return (e);
	}
	e = inode_lookup_relative(sdir, sname, GFS_DT_UNKNOWN, INODE_REMOVE,
	    user, 0, NULL, &src, NULL);
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

		if (sdir != ddir && (inode_is_dir(src) || inode_is_file(src))
		    && (!inode_has_desired_number(src, &num) &&
			!inode_has_repattr(src, NULL)))
			replica_check_signal_rename();
	}
	/* db_inode_nlink_modify() is not necessary, because it's unchanged */
	return (e);
}

gfarm_error_t
inode_unlink(struct inode *base, char *name, struct process *process,
	struct inode_trace_log_info *inodetp,
	int *hlink_removed) /* for gfarm_file_trace */
{
	struct inode *inode;
	gfarm_error_t e = inode_lookup_by_name(base, name, process, 0, &inode);

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
		    process_get_user(process), 0, NULL, &inode, NULL);
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
		if (inode->i_nlink > 2 ||
		    !dir_is_empty(inode->u.c.s.d.entries)) {
			gflog_debug(GFARM_MSG_1001752,
				"directory is not empty");
			return (GFARM_ERR_DIRECTORY_NOT_EMPTY);
		} else if (strcmp(name, dot) == 0 ||
			strcmp(name, dotdot) == 0) {
			gflog_debug(GFARM_MSG_1001753,
				"argument 'name' is invalid");
			return (GFARM_ERR_INVALID_ARGUMENT);
		}

		e = inode_lookup_relative(base, name, GFS_DT_DIR, INODE_REMOVE,
		    process_get_user(process), 0, NULL, &inode, NULL);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001754,
				"inode_lookup_relative() failed: %s",
				gfarm_error_string(e));
			return (e);
		}

		e = db_direntry_remove(inode->i_number, dot, DOT_LEN);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1000322,
			    "db_direntry_remove(%lld, %s): %s",
			    (unsigned long long)inode->i_number, dot,
			    gfarm_error_string(e));

		/*
		 * this i_nlink change will be written to db at the end of
		 * this function.
		 */
		inode->i_nlink--;

		e = db_direntry_remove(inode->i_number, dotdot, DOTDOT_LEN);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1000323,
			    "db_direntry_remove(%lld, %s): %s",
			    (unsigned long long)inode->i_number, dotdot,
			    gfarm_error_string(e));

		base->i_nlink--;
		e = db_inode_nlink_modify(base->i_number, base->i_nlink);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1002817,
			    "db_inode_nlink_modify(%llu): %s",
			    (unsigned long long)base->i_number,
			    gfarm_error_string(e));

	} else if (inode_is_symlink(inode)) {
		e = inode_lookup_relative(base, name, GFS_DT_LNK, INODE_REMOVE,
		    process_get_user(process), 0, NULL, &inode, NULL);
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
	if (inode->u.c.state == NULL &&
	    (!inode_is_file(inode) || inode->u.c.s.f.rstate == NULL)) {
		/* no process is opening this file, just remove it */
		inode_remove(inode);
		return (GFARM_ERR_NO_ERROR);
	} else {
		/* there are some processes which open this file */
		/* leave this inode until closed */

		e = db_inode_nlink_modify(inode->i_number, inode->i_nlink);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1000326,
			    "db_inode_nlink_modify(%lld): %s",
			    (unsigned long long)inode->i_number,
			    gfarm_error_string(e));

		return (GFARM_ERR_NO_ERROR);
	}
}

gfarm_error_t
inode_open(struct file_opening *fo)
{
	struct inode *inode = fo->inode;
	struct inode_open_state *ios = inode->u.c.state;

	if (ios == NULL) {
		ios = inode_open_state_alloc();
		if (ios == NULL) {
			gflog_debug(GFARM_MSG_1001756,
				"inode_open_state_alloc() failed");
			return (GFARM_ERR_NO_MEMORY);
		}
		inode->u.c.state = ios;
	}
	if ((accmode_to_op(fo->flag) & GFS_W_OK) != 0)
		++ios->u.f.writers;
	if ((fo->flag & GFARM_FILE_TRUNC) != 0) {
		/* do not change the metadata for close-to-open consistency */
		fo->flag |= GFARM_FILE_TRUNC_PENDING;
	}

	fo->opening_prev = &ios->openings;
	fo->opening_next = ios->openings.opening_next;
	ios->openings.opening_next = fo;
	fo->opening_next->opening_prev = fo;
	return (GFARM_ERR_NO_ERROR);
}

void
inode_close(struct file_opening *fo, char **trace_logp)
{
	inode_close_read(fo, NULL, trace_logp);
}

void
inode_close_read(struct file_opening *fo, struct gfarm_timespec *atime,
	char **trace_logp)
{
	struct inode *inode = fo->inode;
	struct inode_open_state *ios = inode->u.c.state;

	if ((accmode_to_op(fo->flag) & GFS_W_OK) != 0)
		--ios->u.f.writers;
	if ((fo->flag & GFARM_FILE_TRUNC_PENDING) != 0 &&
	    ios->u.f.writers == 0) {
		/*
		 * In this case, there will be no file replica since reopen
		 * was not called or failed.  The reason why we exclude the
		 * case of writers > 0 is "lost all replica" happens if
		 * some client already opened this in write mode and the final
		 * file size is not zero.  XXX - this means a successful call
		 * of open(O_TRUNC) is ignored in this case.
		 */
		inode_file_update(fo, 0, atime, &inode->i_mtimespec,
		    NULL, NULL, trace_logp);
	} else if (atime != NULL)
		inode_set_relatime(inode, atime);

	fo->opening_prev->opening_next = fo->opening_next;
	fo->opening_next->opening_prev = fo->opening_prev;
	if (ios->openings.opening_next == &ios->openings) { /* all closed */
		if (ios->u.f.event_waiters != NULL)
			inode_new_generation_done(inode, NULL,
			    GFARM_ERR_PROTOCOL);
		inode_open_state_free(inode->u.c.state);
		inode->u.c.state = NULL;
	}

	if (inode->i_nlink == 0 && inode->u.c.state == NULL &&
	    (!inode_is_file(inode) || inode->u.c.s.f.rstate == NULL)) {
		inode_remove(inode); /* clears `ios->u.f.cksum_owner' too. */
	}
}

gfarm_error_t
inode_fhclose_read(struct inode *inode, struct gfarm_timespec *atime)
{
	static const char diag[] = "inode_fhclose_read";

	if (!inode_is_file(inode)) {
		gflog_error(GFARM_MSG_1003283, "%s: not a file", diag);
		return (GFARM_ERR_STALE_FILE_HANDLE);
	}
	if (atime != NULL)
		inode_set_relatime(inode, atime);

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
inode_fhclose_write(struct inode *inode, gfarm_uint64_t old_gen,
    gfarm_off_t size, struct gfarm_timespec *atime,
    struct gfarm_timespec *mtime, gfarm_int64_t *new_genp,
    int *generation_updatedp)
{
	static const char diag[] = "inode_fhclose_write";

	if (!inode_is_file(inode)) {
		gflog_error(GFARM_MSG_1003284, "%s: not a file", diag);
		return (GFARM_ERR_STALE_FILE_HANDLE);
	}
	
	inode_set_atime(inode, atime);
	inode_set_mtime(inode, mtime);
	inode_set_ctime(inode, mtime);
	inode_set_size(inode, size);

	if (inode->i_gen == old_gen) {
		/* update generation number */
		inode_increment_gen(inode);
		*generation_updatedp = 1;
	} else
		*generation_updatedp = 0;
	*new_genp = inode->i_gen;

	return (GFARM_ERR_NO_ERROR);
}

void
inode_add_ref_spool_writers(struct inode *inode)
{
	struct inode_open_state *ios = inode->u.c.state;

	assert(ios != NULL);
	++ios->u.f.spool_writers;
}

void
inode_del_ref_spool_writers(struct inode *inode)
{
	struct inode_open_state *ios = inode->u.c.state;

	assert(ios != NULL);
	--ios->u.f.spool_writers;
}

void
inode_check_pending_replication(struct file_opening *fo)
{
	struct inode *inode = fo->inode;
	struct host *spool_host = fo->u.f.spool_host;
	struct inode_open_state *ios = inode->u.c.state;

	assert(spool_host != NULL && ios != NULL);

	if (host_supports_async_protocols(spool_host) &&
	    ios->u.f.spool_writers == 0 && ios->u.f.replication_pending) {
		ios->u.f.replication_pending = 0;
		make_replicas_except(inode, spool_host,
		    fo->u.f.desired_replica_number, fo->u.f.repattr,
		    inode->u.c.s.f.copies);
	}
}

/*
 * returns TRUE, if generation number is updated.
 *
 * spool_host may be NULL, if GFARM_FILE_TRUNC_PENDING.
 */
static int
inode_file_update_common(struct inode *inode, gfarm_off_t size,
	struct gfarm_timespec *atime, struct gfarm_timespec *mtime,
	struct host *spool_host, int desired_replica_number, char *repattr,
	gfarm_int64_t *old_genp, gfarm_int64_t *new_genp,
	char **trace_logp)
{
	struct inode_open_state *ios = inode->u.c.state;
	gfarm_int64_t old_gen;
	int generation_updated = 0;
	int start_replication = 0;
	struct timeval tv;
	char tmp_str[4096];

	inode_set_size(inode, size);
	inode_set_atime(inode, atime);
	/* to avoid that mtime and ctime move backward */
	if (gfarm_timespec_cmp(mtime, &ios->u.f.last_update) >= 0) {
		ios->u.f.last_update = *mtime;
		inode_set_mtime(inode, mtime);
		inode_set_ctime(inode, mtime);
	}

	old_gen = inode->i_gen;

	if (spool_host == NULL || host_supports_async_protocols(spool_host)) {
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
			    "%lld/%010ld.%06ld////UPDATEGEN/%s/%d//%lld/%lld/%lld//////",
			    (long long int)trace_log_get_sequence_number(),
			    (long int)tv.tv_sec, (long int)tv.tv_usec,
			    gfarm_host_get_self_name(),
			    gfmd_port,
			    (long long int)inode_get_number(inode),
			    (long long int)old_gen,
			    (long long int)inode->i_gen);
			*trace_logp = strdup(tmp_str);
		}

		/* XXX provide an option not to start replication here? */

		/* if there is no other writing process */
		if (ios->u.f.spool_writers == 0) {
			start_replication = 1;
			ios->u.f.replication_pending = 0;
		} else {
			ios->u.f.replication_pending = 1;
		}
	}

	update_replicas(inode, spool_host, old_gen,
		start_replication, desired_replica_number, repattr);

	return (generation_updated);
}

int
inode_file_update(struct file_opening *fo, gfarm_off_t size,
	struct gfarm_timespec *atime, struct gfarm_timespec *mtime,
	gfarm_int64_t *old_genp, gfarm_int64_t *new_genp,
	char **trace_logp)
{
	struct inode *inode = fo->inode;
	struct inode_open_state *ios = inode->u.c.state;

	inode_cksum_invalidate(fo);
	if (ios->u.f.cksum_owner == NULL || ios->u.f.cksum_owner != fo)
		inode_cksum_remove(inode);

	return (inode_file_update_common(fo->inode, size, atime, mtime,
			fo->u.f.spool_host,
			fo->u.f.desired_replica_number,
			fo->u.f.repattr,
			old_genp, new_genp, trace_logp));
}

int
inode_is_opened_for_writing(struct inode *inode)
{
	struct inode_open_state *ios = inode->u.c.state;

	return (ios != NULL && ios->u.f.writers > 0);
}

int
inode_is_opened_on(struct inode *inode, struct host *spool_host)
{
	struct inode_open_state *ios = inode->u.c.state;
	struct file_opening *fo;

	if (!inode_is_file(inode))
		return (0);

	if (ios != NULL &&
	    (fo = ios->openings.opening_next) != &ios->openings) {
		for (; fo != &ios->openings; fo = fo->opening_next) {
			if (spool_host == fo->u.f.spool_host)
				return (1);
		}
	}
	return (0);
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

gfarm_error_t
inode_getdirpath(struct inode *inode, struct process *process, char **namep)
{
	gfarm_error_t e;
	struct inode *parent, *dei;
	int ok;
	struct user *user = process_get_user(process);
	struct inode *root = inode_lookup(ROOT_INUMBER);
	Dir dir;
	DirEntry entry;
	DirCursor cursor;
	char *s, *name, *names[GFS_MAX_DIR_DEPTH];
	int i, namelen, depth = 0;
	size_t totallen = 0;
	int overflow = 0;

	for (; inode != root; inode = parent) {
		e = inode_lookup_relative(inode, dotdot, GFS_DT_DIR,
		    INODE_LOOKUP, user, 0, NULL, &parent, NULL);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001757,
				"inode_lookup_relative() failed: %s",
				gfarm_error_string(e));
			return (e);
		}
		e = inode_access(parent, user, GFS_R_OK|GFS_X_OK);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001758,
				"inode_access() failed: %s",
				gfarm_error_string(e));
			return (e);
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
		if (depth >= GFS_MAX_DIR_DEPTH || s == NULL) {
			for (i = 0; i < depth; i++)
				free(names[i]);
			gflog_debug(GFARM_MSG_1001759,
				"allocation of string failed or directory "
				"too deep");
			return (GFARM_ERR_NO_MEMORY); /* directory too deep */
		}
		memcpy(s, name, namelen);
		s[namelen] = '\0';
		names[depth++] = s;
		totallen = gfarm_size_add(&overflow, totallen, namelen);
	}
	if (depth == 0)
		GFARM_MALLOC_ARRAY(s, 1 + 1);
	else {
#ifdef __GNUC__ /* workaround gcc warning: might be used uninitialized */
		s = NULL;
#endif
		totallen = gfarm_size_add(&overflow, totallen, depth + 1);
		if (!overflow)
			GFARM_MALLOC_ARRAY(s, totallen);

	}
	if (s == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_debug(GFARM_MSG_1001760,
			"allocation of string failed");
	} else if (depth == 0) {
		strcpy(s, "/");
		*namep = s;
		e = GFARM_ERR_NO_ERROR;
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
	for (i = 0; i < depth; i++)
		free(names[i]);
	return (e);
}

struct host *
inode_writing_spool_host(struct inode *inode)
{
	struct inode_open_state *ios = inode->u.c.state;
	struct file_opening *fo;

	if (!inode_is_file(inode)) {
		gflog_debug(GFARM_MSG_1001761,
			"not a file");
		return (NULL); /* not a file */
	}
	if (ios != NULL &&
	    (fo = ios->openings.opening_next) != &ios->openings) {
		for (; fo != &ios->openings; fo = fo->opening_next) {
			if ((accmode_to_op(fo->flag) & GFS_W_OK) != 0 &&
			    fo->u.f.spool_host != NULL)
				return (fo->u.f.spool_host);
		}
	}
	return (NULL);
}

int
inode_schedule_confirm_for_write(struct file_opening *opening,
	struct host *spool_host, int *to_createp)
{
	struct inode_open_state *ios = opening->inode->u.c.state;
	struct file_opening *fo;
	struct file_copy *copy;
	int already_opened, host_match;

	if (!inode_is_file(opening->inode))
		gflog_fatal(GFARM_MSG_1000331,
		    "inode_schedule_confirm_for_write: not a file");
	if (ios != NULL &&
	    (fo = ios->openings.opening_next) != &ios->openings) {
		already_opened = host_match = 0;
		for (; fo != &ios->openings; fo = fo->opening_next) {
			if ((accmode_to_op(fo->flag) & GFS_W_OK) != 0 &&
			    fo->u.f.spool_host != NULL)
				return (fo->u.f.spool_host == spool_host);
			if (fo->u.f.spool_host != NULL &&
			    host_is_disk_available(fo->u.f.spool_host, 0)) {
				already_opened = 1;
				if (fo->u.f.spool_host == spool_host)
					host_match = 1;
			}
		}
		if (already_opened)
			return (host_match);
	}
	/* not opened */
	if (inode_has_no_replica(opening->inode)) {
		*to_createp = 1;
		return (1);
	}
	copy = inode_get_file_copy(opening->inode, spool_host);
	if (copy != NULL) {
		/*
		 * if a replication is ongoing, don't allow to create new one,
		 * because it incurs a race.
		 */
		return (FILE_COPY_IS_VALID(copy)); /* == inode_has_replica() */
	}
	if ((opening->flag & GFARM_FILE_TRUNC) != 0 ||
	    inode_get_size(opening->inode) == 0) {
		/*
		 * http://sourceforge.net/apps/trac/gfarm/ticket/68
		 * (measures against disk full for a file overwriting case)
		 */
		*to_createp = 1;
		return (1);
	}
	return (0);
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
	struct inode_open_state *ios = opening->inode->u.c.state;
	struct file_opening *fo;
	int n, nhosts, write_mode, truncate_flag;
	struct host **hosts;
	gfarm_off_t necessary_space = 0; /* i.e. use default value */

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
		return (GFARM_ERR_NO_SUCH_OBJECT);
	}
	if ((write_mode || truncate_flag) && ios != NULL &&
	    (fo = ios->openings.opening_next) != &ios->openings) {

		/* try to choose already opened replicas */
		n = 0;
		for (; fo != &ios->openings; fo = fo->opening_next) {
			if ((accmode_to_op(fo->flag) & GFS_W_OK) != 0 &&
			    fo->u.f.spool_host != NULL) {
				/*
				 * already opened for writing. 
				 * only that replica is allowed in this case.
				 */
				GFARM_MALLOC_ARRAY(hosts, 1);
				if (hosts == NULL)
					return (GFARM_ERR_NO_MEMORY);
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
			GFARM_MALLOC_ARRAY(hosts, n);
			if (hosts == NULL)
				return (GFARM_ERR_NO_MEMORY);

			nhosts = 0;
			for (fo = ios->openings.opening_next;
			    fo != &ios->openings; fo = fo->opening_next) {
				if (fo->u.f.spool_host != NULL &&
				    host_is_disk_available(
				    fo->u.f.spool_host, 0)) {
					assert(nhosts < n);
					hosts[nhosts++] = fo->u.f.spool_host;
				}
			}
			if (nhosts > 0) {
				/*
				 * there may be duplicated entries in hosts[],
				 * thus we call host_unique_sort() here.
				 */
				*np = host_unique_sort(nhosts, hosts);
				*hostsp = hosts;
				return (GFARM_ERR_NO_ERROR);
			}
			free(hosts);
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

			/* exclude hosts which are during a replication */
			e = inode_alloc_file_copy_hosts(opening->inode,
			    file_copy_is_invalid, NULL, &nhosts, &hosts);
			if (e != GFARM_ERR_NO_ERROR)
				return (e);

			e = host_from_all_except(&nhosts, hosts,
				host_is_disk_available_filter,
				&necessary_space, np, hostsp);
			free(hosts);
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

gfarm_error_t
inode_remove_replica_in_cache(struct inode *inode, struct host *spool_host)
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
		gflog_error(GFARM_MSG_1002818,
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
file_replicating_new(struct inode *inode, struct host *dst, int retry,
	struct dead_file_copy *deferred_cleanup,
	struct file_replicating **frp)
{
	gfarm_error_t e;
	struct file_replicating *fr;
	struct inode_replicating_state *irs = inode->u.c.s.f.rstate;

	if (!host_is_disk_available(dst, inode_get_size(inode)))
		return (GFARM_ERR_NO_SPACE);
	if ((e = inode_add_replica(inode, dst, 0)) != GFARM_ERR_NO_ERROR)
		return (e);
	if (frp == NULL) /* client initiated replication case */
		return (GFARM_ERR_NO_ERROR);

	if ((e = host_replicating_new(dst, &fr)) != GFARM_ERR_NO_ERROR) {
		(void)inode_remove_replica_in_cache(inode, dst);
		/* abandon error */

		/*
		 * workaround for
		 * http://sourceforge.net/apps/trac/gfarm/ticket/464
		 *
		 * (retrying automatic replication after
		 *  GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE)
		 */
		if (retry && e == GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE)
			replica_check_signal_rep_request_failed();

		return (e);
	}
	if (irs == NULL) {
		GFARM_MALLOC(irs);
		if (irs == NULL) {
			peer_replicating_free(fr);
			(void)inode_remove_replica_in_cache(inode, dst);
			/* abandon error */
			return (GFARM_ERR_NO_MEMORY);
		}
		/* make circular list `replicating_hosts' empty */
		irs->replicating_hosts.prev_host =
		irs->replicating_hosts.next_host = &irs->replicating_hosts;

		inode->u.c.s.f.rstate = irs;
	}
	fr->prev_host = &irs->replicating_hosts;
	fr->next_host = irs->replicating_hosts.next_host;
	irs->replicating_hosts.next_host = fr;
	fr->next_host->prev_host = fr;

	fr->inode = inode;
	fr->igen = inode_get_gen(inode);
	fr->cleanup = deferred_cleanup;

	*frp = fr;
	return (GFARM_ERR_NO_ERROR);
}

void
file_replicating_free(struct file_replicating *fr)
{
	struct inode *inode = fr->inode;
	struct inode_replicating_state *irs = inode->u.c.s.f.rstate;

	assert(inode_is_file(inode));
	fr->prev_host->next_host = fr->next_host;
	fr->next_host->prev_host = fr->prev_host;
	if (irs->replicating_hosts.next_host == &irs->replicating_hosts) {
		/* all done */
		free(inode->u.c.s.f.rstate);
		inode->u.c.s.f.rstate = NULL;
	}
	peer_replicating_free(fr);

	if (inode->i_nlink == 0 && inode->u.c.state == NULL &&
	    inode->u.c.s.f.rstate == NULL) {
		inode_remove(inode); /* clears `ios->u.f.cksum_owner' too. */
	}
}

/*
 * this does not generate dead_file_copy, thus, only can be used
 * for an error at async_back_channel_replication_request().
 */
void
file_replicating_free_by_error_before_request(struct file_replicating *fr)
{
	/*
	 * cannot use inode_remove_replica_in_cache() directly,
	 * because it's possible that the caller released giant_lock at once,
	 * thus, inode generation might be updated.
	 * see gfm_server_replicate_file_from_to() for example.
	 */
	inode_remove_replica_completed(fr->inode->i_number, fr->igen, fr->dst);
	
	file_replicating_free(fr);
}

gfarm_int64_t
file_replicating_get_gen(struct file_replicating *fr)
{
	return (fr->igen);
}

static gfarm_error_t
inode_remove_replica_gen_deferred(struct inode *inode,
	struct host *spool_host, gfarm_int64_t gen,
	struct dead_file_copy **deferred_cleanupp);

gfarm_error_t
inode_replicated(struct file_replicating *fr,
	gfarm_int32_t src_errcode, gfarm_int32_t dst_errcode, gfarm_off_t size)
{
	struct inode *inode = fr->inode;
	int transaction = 0;
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	struct dead_file_copy *dfc;
	static const char diag[] = "inode_replicated";

	if (db_begin(diag) == GFARM_ERR_NO_ERROR)
		transaction = 1;

	if (src_errcode == GFARM_ERR_NO_ERROR &&
	    dst_errcode == GFARM_ERR_NO_ERROR &&
	    size == inode_get_size(inode) &&
	    fr->igen == inode_get_gen(inode)) {
		e = inode_add_replica(inode, fr->dst, 1);
	} else {
		if (src_errcode == GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY &&
		    dst_errcode == GFARM_ERR_NO_ERROR &&
		    (inode->i_mode == INODE_MODE_FREE ||
		     fr->igen != inode_get_gen(inode))) {
			gflog_debug(GFARM_MSG_1003285,
			    "expected failure of %lld:%lld replication to %s: "
			    "mode:0o%o gen:%lld",
			    (long long)inode_get_number(inode),
			    (long long)fr->igen,
			    host_name(fr->dst),
			    inode->i_mode, (long long)inode_get_gen(inode));
		} else if (
		    src_errcode != GFARM_ERR_NO_ERROR ||
		    dst_errcode != GFARM_ERR_NO_ERROR)
			gflog_notice(GFARM_MSG_1002257,
			    "error at %lld:%lld replication to %s: "
			    "src=%d dst=%d",
			    (long long)inode_get_number(inode),
			    (long long)fr->igen,
			    host_name(fr->dst), src_errcode, dst_errcode);
		if (debug_mode && (size != inode_get_size(inode) ||
		    fr->igen != inode_get_gen(inode)))
			gflog_debug(GFARM_MSG_1001763,
			    "invalid replica: "
			    "(gen:%lld, size:%lld) "
			    "should be (gen:%lld, size:%lld)",
			    (long long)fr->igen, (long long)size,
			    (long long)inode_get_gen(inode),
			    (long long)inode_get_size(inode));
		e = inode_remove_replica_gen_deferred(inode, fr->dst, fr->igen,
		    &dfc);
		if (e == GFARM_ERR_NO_ERROR) {
			removal_pendingq_enqueue(dfc);
		} else if (e == GFARM_ERR_NO_SUCH_OBJECT) {
			gflog_info(GFARM_MSG_1002433,
			    "cannot remove an incomplete replica "
			    "(%s, %lld:%lld): probably already removed",
			    host_name(fr->dst),
			    (long long)inode_get_number(inode),
			    (long long)fr->igen);
		} else {
			gflog_error(GFARM_MSG_1002434,
			    "cannot remove an incomplete replica "
			    "(%s, %lld:%lld): %s",
			    host_name(fr->dst),
			    (long long)inode_get_number(inode),
			    (long long)fr->igen,
			    gfarm_error_string(e));
		}
		e = GFARM_ERR_INVALID_FILE_REPLICA;
	}

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

	if (e != GFARM_ERR_NO_ERROR) {
		/*
		 * #647 - workaround for #646 - retry replication when
		 * a result of replication is failure
		 */
		replica_check_signal_rep_result_failed();
	}

	return (e);
}

static gfarm_error_t
inode_add_replica_internal(struct inode *inode, struct host *spool_host,
	int flags, int update_quota)
{
	struct file_copy *copy;

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
			} else { /* file_copy is invalid */
				assert(copy->flags == 0);
				copy->flags |= FILE_COPY_VALID;
				if (update_quota)
					quota_update_replica_add(inode);
				return (GFARM_ERR_NO_ERROR);
			}
		}
	}
	/* not exist in u.c.s.f.copies : add new replica */
	if (update_quota) {
		gfarm_error_t e;
		/* check limits of space and number of the replica */
		e = quota_check_limits(inode_get_user(inode),
			       inode_get_group(inode), 0, 1);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001767,
				"checking of limits of the replica failed");
			return (e);
		}
	}

	GFARM_MALLOC(copy);
	if (copy == NULL) {
		gflog_debug(GFARM_MSG_1001768,
			"allocation of 'copy' failed");
		return (GFARM_ERR_NO_MEMORY);
	}

	if (update_quota && (flags & FILE_COPY_VALID) != 0)
		quota_update_replica_add(inode);

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

	inode_add_replica_internal(inode, host, FILE_COPY_BEING_REMOVED, 0);
}

/*
 * 'valid == 0' means that the replica is not ready right now, and
 * going to be created or removed.
 */
gfarm_error_t
inode_add_replica(struct inode *inode, struct host *spool_host, int valid)
{
	gfarm_error_t e = inode_add_replica_internal(
		inode, spool_host, valid ? FILE_COPY_VALID : 0, 1);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001769,
			"inode_add_replica_internal() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (!valid)
		return (GFARM_ERR_NO_ERROR);
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
	return (inode_add_replica_internal(inode, host, 1, 0));
}

static gfarm_error_t
remove_replica_metadata(struct inode *inode, struct host *spool_host)
{
	gfarm_error_t e;

	quota_update_replica_remove(inode);

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
	struct host *spool_host, int valid,
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
		(void)remove_replica_metadata(inode, spool_host);
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

	(void)inode_remove_replica_in_cache(inode, host);
	/* abandon error */
}

static gfarm_error_t
check_removable_replicas(
	struct file_opening *fo, int n_valid,
	struct file_copy *copy, struct file_copy *copies_list)
{
	gfarm_error_t e;

	if (n_valid <= 1)
		return (GFARM_ERR_CANNOT_REMOVE_LAST_REPLICA);

	if (fo->u.f.repattr != NULL) {
		int nhosts;
		struct host **hosts;
		int ncopy, n_desired;
		size_t i, nreps = 0;
		gfarm_repattr_t *reps = NULL;
		char *fsng;

		e = gfarm_repattr_parse(fo->u.f.repattr, &reps, &nreps);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_UNFIXED,
			    "gfarm_repattr_parse(%s): %s",
			    fo->u.f.repattr, gfarm_error_string(e));
			return (e);
		}

		fsng = host_fsngroup(copy->host);
		n_desired = 0;
		for (i = 0; i < nreps; i++) {
			if (strcmp(gfarm_repattr_group(reps[i]), fsng) == 0) {
				n_desired = gfarm_repattr_amount(reps[i]);
				break;
			}
		}
		if (n_desired <= 0) { /* no desired number for the host */
			gfarm_repattr_free_all(nreps, reps);
			return (GFARM_ERR_NO_ERROR); /* removable */
		}

		e = fsngroup_get_hosts(fsng, &nhosts, &hosts);
		if (e != GFARM_ERR_NO_ERROR) {
			gfarm_repattr_free_all(nreps, reps);
			gflog_debug(GFARM_MSG_UNFIXED,
			    "fsngroup_get_hosts(%s): %s",
			    fsng, gfarm_error_string(e));
			return (e);
		}
		if (nhosts == 0) { /* unexpected */
			free(hosts);
			gfarm_repattr_free_all(nreps, reps);
			return (GFARM_ERR_NO_ERROR);
		}

		e = inode_count_ncopy_with_grace(
		    copies_list, 0, 0, nhosts, hosts, &ncopy);
		if (e != GFARM_ERR_NO_ERROR) {
			free(hosts);
			gfarm_repattr_free_all(nreps, reps);
			gflog_debug(GFARM_MSG_UNFIXED,
			    "inode_count_ncopy_with_grace: %s",
			    gfarm_error_string(e));
			return (e);
		}

		free(hosts);
		gfarm_repattr_free_all(nreps, reps);

		if (ncopy > n_desired)
			return (GFARM_ERR_NO_ERROR); /* removable */
		return (GFARM_ERR_INSUFFICIENT_NUMBER_OF_FILE_REPLICAS);
	}

	if (fo->u.f.desired_replica_number >= 2 &&
	    n_valid <= fo->u.f.desired_replica_number)
		return (GFARM_ERR_INSUFFICIENT_NUMBER_OF_FILE_REPLICAS);
	return (GFARM_ERR_NO_ERROR); /* removable */
}

static gfarm_error_t
inode_remove_replica_internal(struct inode *inode, struct host *spool_host,
	gfarm_int64_t gen, struct file_opening *protect_replicas,
	int invalid_is_removable, int metadata_only,
	struct dead_file_copy **deferred_cleanupp)
{
	struct file_copy **copyp, *copy, **foundp = NULL;
	gfarm_error_t e;
	int num_valid = 0;

	if (gen == inode->i_gen) {
		for (copyp = &inode->u.c.s.f.copies; (copy = *copyp) != NULL;
		    copyp = &copy->host_next) {
			if (copy->host == spool_host)
				foundp = copyp;
			if (FILE_COPY_IS_VALID(copy))
				++num_valid;
		}
		if (foundp == NULL) {
			gflog_debug(GFARM_MSG_1001770,
				"replica to remove not found");
			return (GFARM_ERR_NO_SUCH_OBJECT);
		}
		copy = *foundp;
		if (protect_replicas != NULL && FILE_COPY_IS_VALID(copy)) {
			e = check_removable_replicas(
			    protect_replicas, num_valid,
			    copy, inode->u.c.s.f.copies);
			if (e != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_UNFIXED,
				    "check_removable_replicas: %s",
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
			} else {
				/*
				 * XXX FIXME invalid_is_removable case is wrong
				 * see PROBLEM-4 of SourceForge #407
				 */
				e = remove_replica_entity(inode, gen,
				    copy->host, FILE_COPY_IS_VALID(copy),
				    deferred_cleanupp);
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
				e = remove_replica_metadata(inode, copy->host);
				if (e != GFARM_ERR_NO_ERROR) {
					gflog_error(GFARM_MSG_UNFIXED,
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
inode_remove_replica_metadata(struct inode *inode, struct host *spool_host,
	gfarm_int64_t gen)
{
	return (inode_remove_replica_internal(inode, spool_host, gen,
	    NULL, 0, 1, NULL));
}

static gfarm_error_t
inode_remove_replica_gen_deferred(struct inode *inode,
	struct host *spool_host, gfarm_int64_t gen,
	struct dead_file_copy **deferred_cleanupp)
{
	return (inode_remove_replica_internal(inode, spool_host, gen,
	    NULL, 1, 0, deferred_cleanupp));
}

gfarm_error_t
inode_remove_replica_gen(struct inode *inode, struct host *spool_host,
	gfarm_int64_t gen)
{
	return (inode_remove_replica_gen_deferred(inode, spool_host, gen,
	    NULL));
}

gfarm_error_t
inode_remove_replica_protected(struct inode *inode, struct host *spool_host,
	struct file_opening *fo)
{
	return (inode_remove_replica_internal(inode, spool_host,
	    inode_get_gen(inode), fo, 0, 0, NULL));
}

gfarm_error_t
inode_prepare_to_replicate(struct inode *inode, struct user *user,
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
	if ((e = inode_access(inode, user, GFS_R_OK)) != GFARM_ERR_NO_ERROR)
		return (e);
	if (inode_has_no_replica(inode)) /* no file copy */
		return (GFARM_ERR_NO_SUCH_OBJECT);
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
	else if ((e = file_replicating_new(inode, dst, 0, NULL, frp)) !=
	    GFARM_ERR_NO_ERROR)
		return (e);

	if (file_replicating_p != NULL)
		*file_replicating_p = *frp;
	return (GFARM_ERR_NO_ERROR);
}

int
inode_is_updated(struct inode *inode, struct gfarm_timespec *mtime)
{
	struct inode_open_state *ios = inode->u.c.state;

	/*
	 * ios->u.f.last_update is necessary,
	 * becasuse i_mtimespec may be modified by GFM_PROTO_FUTIMES.
	 */
	return (ios != NULL &&
	    gfarm_timespec_cmp(mtime, &ios->u.f.last_update) >= 0);
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
		gflog_debug(GFARM_MSG_1001773,
			"allocation of 'hosts' failed");
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
	if (hosts == NULL)
		return (GFARM_ERR_NO_MEMORY);
	GFARM_MALLOC_ARRAY(gens, n);
	if (gens == NULL) {
		free(hosts);
		return (GFARM_ERR_NO_MEMORY);
	}
	GFARM_MALLOC_ARRAY(oflags, n);
	if (oflags == NULL) {
		free(gens);
		free(hosts);
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
	if (entry == NULL)
		return (0);

	entry->inum = inum;

	entry->next = *listp;
	*listp = entry;
	return (1);
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
		free(entry);
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


struct inum_string_list_entry {
	struct inum_string_list_entry *next;

	gfarm_ino_t inum;
	char *string;
};

static int
inum_string_list_add(struct inum_string_list_entry **listp,
	gfarm_ino_t inum, char *string)
{
	struct inum_string_list_entry *entry;

	GFARM_MALLOC(entry);
	if (entry == NULL)
		return (0);

	entry->inum = inum;
	entry->string = string;

	entry->next = *listp;
	*listp = entry;
	return (1);
}

static void
inum_string_list_foreach(struct inum_string_list_entry *list,
	gfarm_error_t (*op)(gfarm_ino_t, const char *),
	const char *name, const char *label)
{
	gfarm_error_t e;
	struct inum_string_list_entry *entry, *next;

	for (entry = list; entry != NULL; entry = next) {
		next = entry->next;
		e = (*op)(entry->inum, entry->string);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1002824,
			    "orphan %s %llu %s:%s removal: %s",
			    name, (unsigned long long)entry->inum,
			    label, entry->string, gfarm_error_string(e));
		free(entry->string);
		free(entry);
	}
}

static struct inum_string_list_entry *file_copy_removal_list = NULL;

static void
file_copy_defer_db_removal(gfarm_ino_t inum, char *hostname)
{
	if (!inum_string_list_add(&file_copy_removal_list, inum, hostname)) {
		gflog_error(GFARM_MSG_1002825,
		    "file_copy %llu host:%s: no memory to record for removal",
		    (unsigned long long)inum, hostname);
		free(hostname);
	} else {
		gflog_error(GFARM_MSG_1002826,
		    "file_copy %llu host:%s: removing orphan data",
		    (unsigned long long)inum, hostname);
	}
}

static void
file_copy_db_remove_orphan(void)
{
	inum_string_list_foreach(file_copy_removal_list,
	    db_filecopy_remove, "file_copy", "hostname");
}

static struct dir_entry_removal_todo {
	struct dir_entry_removal_todo *next;

	gfarm_ino_t dir_inum;
	char *entry_name;
	int entry_len;
} *dir_entry_removal_list = NULL;

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
		free(entry->entry_name);
		free(entry);
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

static gfarm_error_t
db_xattr_remove_wrapper(gfarm_ino_t inum, const char *attrname)
{
	return (db_xattr_remove(0, inum, (char *)attrname)); /* XXX UNCONST */
}

static gfarm_error_t
db_xml_remove_wrapper(gfarm_ino_t inum, const char *attrname)
{
	return (db_xattr_remove(1, inum, (char *)attrname)); /* XXX UNCONST */
}

static void
xattr_db_remove_orphan(void)
{
	inum_string_list_foreach(xattr_removal_list, db_xattr_remove_wrapper,
	    "xattr", "attrname");
}

static void
xml_db_remove_orphan(void)
{
	inum_string_list_foreach(xml_removal_list, db_xml_remove_wrapper,
	    "xml", "attrname");
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

/*
 * loading metadata from persistent storage.
 */

void
inode_modify(struct inode *inode, struct gfs_stat *st)
{
	inode->i_gen = st->st_gen;
	inode->i_nlink = st->st_nlink;
	inode->i_size = st->st_size;
	inode->i_mode = st->st_mode;
	inode_set_user_by_name_in_cache(inode, st->st_user);
	inode_set_group_by_name_in_cache(inode, st->st_group);
	inode->i_atimespec = st->st_atimespec;
	inode->i_mtimespec = st->st_mtimespec;
	inode->i_ctimespec = st->st_ctimespec;
}

/* The memory owner of `*st' is changed to inode.c */
static gfarm_error_t
inode_add(struct gfs_stat *st, struct inode **inodep)
{
	gfarm_error_t e;
	struct inode *inode;

	inode = inode_alloc_num(st->st_ino);
	if (inode == NULL) {
		gflog_error(GFARM_MSG_1000334,
		    "cannot allocate inode %lld",
		    (unsigned long long)st->st_ino);
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
		gflog_debug(GFARM_MSG_1001776,
			"inode_alloc_num() failed");
		if (e != GFARM_ERR_UNKNOWN) {
			gflog_error(GFARM_MSG_1000336,
			    "inode %lld: %s",
			    (unsigned long long)st->st_ino,
			    gfarm_error_string(e));
		}
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
		if ((GFARM_S_IFMT & st->st_mode) ==
		    (GFARM_S_IFMT & n->i_mode)) {
			inode_modify(n, st);
			gfs_stat_free(st);
			*inodep = n;
			return (GFARM_ERR_NO_ERROR);
		}
		switch (GFARM_S_IFMT & n->i_mode) {
		case GFARM_S_IFDIR:
			dir_free(n->u.c.s.d.entries);
			break;
		case GFARM_S_IFREG:
			/* XXX need to free file_copy ? */
			break;
		case GFARM_S_IFLNK:
			inode_clear_symlink(n);
			break;
		}
		inode_clear(n);
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

/* The memory owner of `hostname' is changed to inode.c */
static void
file_copy_add_one(void *closure, gfarm_ino_t inum, char *hostname)
{
	gfarm_error_t e;
	struct inode *inode = inode_lookup(inum);
	struct host *host = host_lookup(hostname);

	if (inode == NULL) {
		gflog_error(GFARM_MSG_1000343,
		    "file_copy_add_one: no inode %lld",
		    (unsigned long long)inum);
		file_copy_defer_db_removal(inum, hostname);
		return;
	} else if (!inode_is_file(inode)) {
		gflog_error(GFARM_MSG_1000344,
		    "file_copy_add_one: not file %lld",
		    (unsigned long long)inum);
		file_copy_defer_db_removal(inum, hostname);
		return;
	} else if (host == NULL) {
		gflog_error(GFARM_MSG_1000345,
		    "file_copy_add_one: no host %s", hostname);
		file_copy_defer_db_removal(inum, hostname);
		return;
	} else if ((e = inode_add_replica_internal(inode, host,
	    FILE_COPY_VALID, 0)) != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1000346,
		    "file_copy_add_one: add_replica: %s",
		    gfarm_error_string(e));
	}
	free(hostname);
}

static int
name_is_dot_or_dotdot(const char *name, int len)
{
	return (
	    (len == DOT_LEN && memcmp(name, dot, DOT_LEN) == 0) ||
	    (len == DOTDOT_LEN && memcmp(name, dotdot, DOTDOT_LEN) == 0));
}


/* The memory owner of `entry_name' is changed to inode.c */
static void
dir_entry_add_one(void *closure,
	gfarm_ino_t dir_inum, char *entry_name, int entry_len,
	gfarm_ino_t entry_inum)
{
	(void)dir_entry_add(dir_inum, entry_name, entry_len, entry_inum);

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
	struct inode *root;
	struct gfs_stat st;

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
	    strdup_ck(dot, "inode_init: \".\""), DOT_LEN,
	    ROOT_INUMBER);
	dir_entry_add_one(NULL, ROOT_INUMBER,
	    strdup_ck(dotdot, "inode_init: \"..\""), DOTDOT_LEN,
	    ROOT_INUMBER);
	e = db_direntry_add(ROOT_INUMBER, dot, DOT_LEN, ROOT_INUMBER);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1000359,
		    "failed to store '.' in root directory to storage: %s",
		    gfarm_error_string(e));
	e = db_direntry_add(ROOT_INUMBER, dotdot, DOTDOT_LEN, ROOT_INUMBER);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1000360,
		    "failed to store '..' in root directory to storage: %s",
		    gfarm_error_string(e));
}

void
file_copy_init(void)
{
	gfarm_error_t e;

	e = db_filecopy_load(NULL, file_copy_add_one);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1000361,
		    "loading filecopy: %s", gfarm_error_string(e));
}

static void
inode_dir_check_and_repair_dot(struct inode *dir_inode)
{
	gfarm_error_t e;
	DirEntry entry = dir_lookup(dir_inode->u.c.s.d.entries, dot, DOT_LEN);
	int should_add_dot = 0;
	struct inode *old;

	if (entry == NULL) {
		should_add_dot = 1;
		old = NULL;
	} else if ((old = dir_entry_get_inode(entry)) != dir_inode) {
		should_add_dot = 1;
		assert(old != NULL);
		dir_remove_entry(dir_inode->u.c.s.d.entries, dot, DOT_LEN);
		e = db_direntry_remove(inode_get_number(dir_inode), dot, DOT_LEN);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1002832,
			    "repair_dot: db_direntry_remove(%llu, %.*s): %s",
			    (unsigned long long)inode_get_number(dir_inode),
			    (int)DOT_LEN, dot, gfarm_error_string(e));
		inode_decrement_nlink_ini(old);
	}
	if (should_add_dot) {
		entry = dir_enter(dir_inode->u.c.s.d.entries,
		    dot, DOT_LEN, NULL);
		if (entry == NULL) {
			gflog_error(GFARM_MSG_1002833,
			    "repair_dot: dir_enter(%llu, %*.s, %llu): "
			    "no memory",
			    (unsigned long long)inode_get_number(dir_inode),
			    (int)DOT_LEN, dot,
			    (unsigned long long)inode_get_number(dir_inode));
			return;
		}
		dir_entry_set_inode(entry, dir_inode);
		e = db_direntry_add(inode_get_number(dir_inode), dot, DOT_LEN,
		    inode_get_number(dir_inode));
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1002834,
			    "repair_dot: db_direntry_add(%llu, %*.s, %llu): %s",
			    (unsigned long long)inode_get_number(dir_inode),
			    (int)DOT_LEN, dot,
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
			gflog_warning(GFARM_MSG_1002490,
			    "inode %llu nlink %llu should be %llu: fixed",
			    (unsigned long long)inode_get_number(inode),
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
				gflog_error(GFARM_MSG_1002842,
				    "db_inode_nlink_modify(%llu): %s",
				    (unsigned long long)
				    inode_get_number(lost_found),
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
	struct inode *root;

	e = db_direntry_load(NULL, dir_entry_add_one);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1000363,
		    "loading direntry: %s", gfarm_error_string(e));

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
		gflog_debug(GFARM_MSG_1001777,
			"allocation of 'xattr_entry' failed");
		return NULL;
	}
	if ((entry->name = strdup_log(attrname, diag)) == NULL) {
		free(entry);
		return NULL;
	}
	entry->cached_attrvalue = NULL;
	entry->cached_attrsize = 0;
	return entry;
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

	if (!gfarm_xattr_caching("gfarm.ncopy"))
		gfarm_xattr_caching_pattern_add("gfarm.ncopy");
	if (!gfarm_xattr_caching(GFARM_REPATTR_NAME))
		gfarm_xattr_caching_pattern_add(GFARM_REPATTR_NAME);

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
inode_xattr_get_cache(struct inode *inode, int xmlMode,
	const char *attrname, void **cached_valuep, size_t *cached_sizep)
{
	struct xattrs *xattrs =
		xmlMode ? &inode->i_xmlattrs : &inode->i_xattrs;
	struct xattr_entry *entry;
	void *r;

	entry = xattr_find(xattrs, attrname);
	if (entry == NULL)
		return (GFARM_ERR_NO_SUCH_OBJECT);

	if (entry->cached_attrvalue == NULL) {
		*cached_valuep = NULL;
		*cached_sizep = 0;
	} else if ((r = malloc(entry->cached_attrsize)) == NULL)
		return (GFARM_ERR_NO_MEMORY);
	else {
		memcpy(r, entry->cached_attrvalue, entry->cached_attrsize);
		*cached_valuep = r;
		*cached_sizep = entry->cached_attrsize;
	}

	return (GFARM_ERR_NO_ERROR);
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
	char **patterns, int npattern,
	struct xattr_list **listp, size_t *np)
{
	struct inode *inode;
	size_t nxattrs;
	struct xattr_list *list;
	struct xattrs *xattrs;
	struct xattr_entry *entry;
	int i, j;
	static const char diag[] = "inode_xattr_list_get_cached_by_patterns";

	inode = inode_lookup(inum);
	if (inode == NULL)
		return (GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY);

	xattrs = &inode->i_xattrs;
	entry = xattrs->head;
	if (entry == NULL) {
		*np = 0;
		*listp = NULL;
		return (GFARM_ERR_NO_ERROR);
	}

	nxattrs = 0;
	for (; entry != NULL; entry = entry->next) {
		for (j = 0; j < npattern; j++) {
			if (gfarm_pattern_match(patterns[j], entry->name, 0)) {
				++nxattrs;
				break;
			}
		}
	}
	GFARM_CALLOC_ARRAY(list, nxattrs);
	if (list == NULL)
		return (GFARM_ERR_NO_MEMORY);
	for (entry = xattrs->head, i = 0;
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
		return GFARM_ERR_NO_ERROR;
	if (GFARM_MALLOC_ARRAY(names, size) == NULL) {
		gflog_debug(GFARM_MSG_1001782,
			"allocation of 'names' failed");
		return GFARM_ERR_NO_MEMORY;
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
	return GFARM_ERR_NO_ERROR;
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
	struct xattr_entry *ent = xattr_find(&inode->i_xattrs, "gfarm.ncopy");

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

	if (inode_xattr_get_cache(inode, 0, GFARM_REPATTR_NAME,
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
	if (inode_has_repattr(inode, repattrp)) {
		*desired_numberp = 0;
		return (1);
	}
	if (inode_has_desired_number(inode, desired_numberp)) {
		*repattrp = NULL;
		return (1);
	}
	return (0);
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

		if (inode_get_number(dir) == ROOT_INUMBER)
			return (0);
			
		entry = dir_lookup(dir->u.c.s.d.entries, dotdot, DOTDOT_LEN);
		if (entry == NULL)
			return (0);

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
