/*
 * $Id$
 */

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h> /* sprintf */
#include <sys/time.h>
#include <pthread.h>

#define GFARM_INTERNAL_USE
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

#include "gfutil.h"

#include "gfm_proto.h"

#include "subr.h"
#include "db_access.h"
#include "host.h"
#include "user.h"
#include "group.h"
#include "dir.h"
#include "inode.h"
#include "process.h" /* struct file_opening */

#define ROOT_INUMBER			2
#define INODE_TABLE_SIZE_INITIAL	1000
#define INODE_TABLE_SIZE_MULTIPLY	2

#define INODE_MODE_FREE			0	/* struct inode:i_mode */

#define GFS_MAX_DIR_DEPTH		256

struct file_copy {
	struct file_copy *host_next;
	struct host *host;
	int valid;
};

struct inode {
	gfarm_ino_t i_number;
	gfarm_uint64_t i_gen;
	gfarm_uint64_t i_nlink;
	gfarm_off_t i_size;
	struct user *i_user;
	struct group *i_group;
	struct gfarm_timespec i_atimespec;
	gfarm_mode_t i_mode;
	struct gfarm_timespec i_mtimespec;
	struct gfarm_timespec i_ctimespec;

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
				} d;
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
			int writers;
			struct file_opening *cksum_owner;
		} f;
	} u;
};

struct inode **inode_table = NULL;
gfarm_ino_t inode_table_size = 0;
gfarm_ino_t inode_free_index = ROOT_INUMBER;

struct inode inode_free_list; /* dummy header of doubly linked circular list */
int inode_free_list_initialized = 0;

static pthread_mutex_t total_num_inodes_mutex = PTHREAD_MUTEX_INITIALIZER;
static gfarm_uint64_t total_num_inodes;

static char dot[] = ".";
static char dotdot[] = "..";

#define DOT_LEN		(sizeof(dot) - 1)
#define DOTDOT_LEN	(sizeof(dotdot) - 1)

gfarm_uint64_t
inode_total_num(void)
{
	gfarm_uint64_t num_inodes;

	pthread_mutex_lock(&total_num_inodes_mutex);
	num_inodes = total_num_inodes;
	pthread_mutex_unlock(&total_num_inodes_mutex);

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
			gflog_error(
			    "db_inode_cksum_remove(%" GFARM_PRId64 "): %s",
			    inode->i_number, gfarm_error_string(e));
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
	if (overflow || cs == NULL)
		return (GFARM_ERR_NO_MEMORY);

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

	if (!inode_is_file(fo->inode))
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);

	if ((fo->flag & GFARM_FILE_CKSUM_INVALIDATED) != 0)
		return (GFARM_ERR_EXPIRED);

	/* writable descriptor has precedence over read-only one */
	if (ios->u.f.cksum_owner != NULL &&
	    (accmode_to_op(ios->u.f.cksum_owner->flag) & GFS_W_OK) != 0 &&
	    (accmode_to_op(fo->flag) & GFS_W_OK) == 0)
		return (GFARM_ERR_EXPIRED);

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
		gflog_error("db_inode_cksum_%s(%" GFARM_PRId64 "): %s",
		    cs == NULL ? "add" : "modify",
		    inode->i_number, gfarm_error_string(e));

	/* reduce memory reallocation */
	if (cs != NULL &&
	    strcmp(cksum_type, cs->type) == 0 && cksum_len == cs->len) {
		memcpy(cs->sum, cksum, cksum_len);
		return (GFARM_ERR_NO_ERROR);
	}
	inode_cksum_clear(inode);

	e = inode_cksum_set_internal(inode, cksum_type, cksum_len, cksum);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

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

	if (!inode_is_file(fo->inode))
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);

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

struct inode_open_state *
inode_open_state_alloc(void)
{
	struct inode_open_state *ios;

	GFARM_MALLOC(ios);
	if (ios == NULL)
		return (NULL);
	/* make circular list `openings' empty */
	ios->openings.opening_prev =
	ios->openings.opening_next = &ios->openings;
	ios->u.f.writers = 0;
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
		if (p == NULL)
			return (NULL); /* no memory */
		inode_table = p;

		for (i = inode_table_size; i < new_table_size; i++)
			inode_table[i] = NULL;
		inode_table_size = new_table_size;
	}
	if ((inode = inode_table[inum]) == NULL) {
		GFARM_MALLOC(inode);
		if (inode == NULL)
			return (NULL); /* no memory */
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
	inode->u.c.state = NULL;
	pthread_mutex_lock(&total_num_inodes_mutex);
	++total_num_inodes;
	pthread_mutex_unlock(&total_num_inodes_mutex);
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
	inode->i_mode = INODE_MODE_FREE;
	inode->i_nlink = 0;
	/* add to the inode_free_list */
	inode->u.l.prev = &inode_free_list;
	inode->u.l.next = inode_free_list.u.l.next;
	inode->u.l.next->u.l.prev = inode;
	inode_free_list.u.l.next = inode;
	pthread_mutex_lock(&total_num_inodes_mutex);
	--total_num_inodes;
	pthread_mutex_unlock(&total_num_inodes_mutex);
}

void
inode_free(struct inode *inode)
{
	gfarm_error_t e;

	inode_clear(inode);

	e = db_inode_nlink_modify(inode->i_number, inode->i_nlink);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error("db_inode_nlink_modify(%" GFARM_PRId64 "): %s",
		    inode->i_number, gfarm_error_string(e));
	e = db_inode_mode_modify(inode->i_number, inode->i_mode);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error("db_inode_mode_modify(%" GFARM_PRId64 "): %s",
		    inode->i_number, gfarm_error_string(e));
}

static gfarm_error_t
remove_replica_internal(struct inode *, struct file_copy *);

void
inode_remove(struct inode *inode)
{
	if (inode->u.c.state != NULL)
		gflog_fatal("inode_remove: still opened");
	if (inode_is_file(inode)) {
		struct file_copy *copy, *cn;
		gfarm_error_t e;

		for (copy = inode->u.c.s.f.copies; copy != NULL; copy = cn) {
			e = remove_replica_internal(inode, copy);
			cn = copy->host_next;
			free(copy);
		}
		inode_cksum_remove(inode);
	} else if (inode_is_dir(inode)) {
		dir_free(inode->u.c.s.d.entries);
	} else {
		gflog_fatal("inode_unlink: unknown inode type");
		/*NOTREACHED*/
	}
	inode_free(inode);
}

static gfarm_error_t
inode_init_dir_internal(struct inode *inode)
{
	inode->u.c.s.d.entries = dir_alloc();
	if (inode->u.c.s.d.entries == NULL)
		return (GFARM_ERR_NO_MEMORY);

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
inode_init_dir(struct inode *inode, struct inode *parent)
{
	gfarm_error_t e;
	DirEntry entry;

	e = inode_init_dir_internal(inode);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	/*
	 * We won't do db_direntry_add() here to make LDAP happy.
	 * See the comment in inode_lookup_basename().
	 */

	entry = dir_enter(inode->u.c.s.d.entries, dot, DOT_LEN, NULL);
	if (entry == NULL) {
		dir_free(inode->u.c.s.d.entries);
		return (GFARM_ERR_NO_MEMORY);
	}
	dir_entry_set_inode(entry, inode);

	entry = dir_enter(inode->u.c.s.d.entries, dotdot, DOTDOT_LEN,
	    NULL);
	if (entry == NULL) {
		dir_free(inode->u.c.s.d.entries);
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
	return (GFARM_ERR_NO_ERROR);
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

gfarm_int64_t
inode_get_nlink(struct inode *inode)
{
	return (inode->i_nlink);
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
inode_is_creating_file(struct inode *inode)
{
	return (inode->u.c.s.f.copies == NULL);
}

gfarm_int64_t
inode_get_ncopy(struct inode *inode)
{
	struct file_copy *copy;
	int n = 0;

	for (copy = inode->u.c.s.f.copies; copy != NULL;
	    copy = copy->host_next) {
		if (copy->valid && host_is_up(copy->host))
			n++;
	}
	return (n);
}

gfarm_mode_t
inode_get_mode(struct inode *inode)
{
	return (inode->i_mode);
}

gfarm_error_t
inode_set_mode(struct inode *inode, gfarm_mode_t mode)
{
	gfarm_error_t e;

	if ((mode & GFARM_S_IFMT) != 0)
		return (GFARM_ERR_INVALID_ARGUMENT);
	inode->i_mode = (inode->i_mode & GFARM_S_IFMT) |
	    (mode & GFARM_S_ALLPERM);

	e = db_inode_mode_modify(inode->i_number, inode->i_mode);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error("db_inode_mode_modify(%" GFARM_PRId64 "): %s",
		    inode->i_number, gfarm_error_string(e));

	return (GFARM_ERR_NO_ERROR);
}

gfarm_off_t
inode_get_size(struct inode *inode)
{
	return (inode->i_size);
}

void
inode_set_size(struct inode *inode, gfarm_off_t size)
{
	gfarm_error_t e;

	inode->i_size = size;

	e = db_inode_size_modify(inode->i_number, inode->i_size);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error("db_inode_size_modify(%" GFARM_PRId64 "): %s",
		    inode->i_number, gfarm_error_string(e));
}

gfarm_error_t
inode_set_owner(struct inode *inode, struct user *user, struct group *group)
{
	gfarm_error_t e;

	if (user != NULL) {
		inode->i_user = user;

		e = db_inode_user_modify(inode->i_number, user_name(user));
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(
			    "db_inode_user_modify(%" GFARM_PRId64 "): %s",
			    inode->i_number, gfarm_error_string(e));
	}
	if (group != NULL) {
		inode->i_group = group;

		e = db_inode_group_modify(inode->i_number, group_name(group));
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(
			    "db_inode_group_modify(%" GFARM_PRId64 "): %s",
			    inode->i_number, gfarm_error_string(e));
	}
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
inode_set_atime(struct inode *inode, struct gfarm_timespec *atime)
{
	gfarm_error_t e;

	if (atime == NULL)
		return;

	inode->i_atimespec = *atime;

	e = db_inode_atime_modify(inode->i_number, &inode->i_atimespec);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error("db_inode_atime_modify(%" GFARM_PRId64 "): %s",
		    inode->i_number, gfarm_error_string(e));
}

void
inode_set_mtime(struct inode *inode, struct gfarm_timespec *mtime)
{
	gfarm_error_t e;

	inode->i_mtimespec = *mtime;

	e = db_inode_mtime_modify(inode->i_number, inode_get_mtime(inode));
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error("db_inode_mtime_modify(%" GFARM_PRId64 "): %s",
		    inode->i_number, gfarm_error_string(e));
}

static void
inode_set_ctime(struct inode *inode, struct gfarm_timespec *ctime)
{
	gfarm_error_t e;

	inode->i_ctimespec = *ctime;

	e = db_inode_ctime_modify(inode->i_number, &inode->i_ctimespec);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error("db_inode_ctime_modify(%" GFARM_PRId64 "): %s",
		    inode->i_number, gfarm_error_string(e));
}

static void
touch(struct gfarm_timespec *tsp)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	tsp->tv_sec = tv.tv_sec;
	tsp->tv_nsec = tv.tv_usec * 1000;
}

void
inode_accessed(struct inode *inode)
{
	struct gfarm_timespec ts;

	touch(&ts);
	inode_set_atime(inode, &ts);
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
	inode_status_changed(inode);
	inode->i_atimespec.tv_sec =
	inode->i_mtimespec.tv_sec =
	inode->i_ctimespec.tv_sec;
	inode->i_atimespec.tv_nsec =
	inode->i_mtimespec.tv_nsec =
	inode->i_ctimespec.tv_nsec;
}

Dir
inode_get_dir(struct inode *inode)
{
	if (!inode_is_dir(inode))
		return (NULL);
	return (inode->u.c.s.d.entries);
}

gfarm_error_t
inode_access(struct inode *inode, struct user *user, int op)
{
	gfarm_mode_t mask = 0;

	if (inode->i_user == user) {
		if (op & GFS_X_OK)
			mask |= 0100;
		if (op & GFS_W_OK)
			mask |= 0200;
		if (op & GFS_R_OK)
			mask |= 0400;
	} else if (user_in_group(user, inode->i_group)) {
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
	return ((inode->i_mode & mask) == mask ? GFARM_ERR_NO_ERROR :
	    GFARM_ERR_PERMISSION_DENIED);
}

enum gfarm_inode_lookup_op {
	INODE_LOOKUP,
	INODE_CREATE,
	INODE_REMOVE,
	INODE_LINK,
};

/* if (op != INODE_CREATE), (is_dir) may be -1, and that means "don't care". */
static gfarm_error_t
inode_lookup_basename(struct inode *parent, const char *name, int len,
	int is_dir, enum gfarm_inode_lookup_op op, struct user *user,
	struct inode **inp, int *createdp)
{
	gfarm_error_t e;
	DirEntry entry;
	int created;
	struct inode *n;

	if (len == 0) {
		if (op == INODE_REMOVE)
			return (GFARM_ERR_INVALID_ARGUMENT);
		*inp = parent;
		*createdp = 0;
		return (GFARM_ERR_NO_ERROR);
	} else if (len == 1 && name[0] == '.') {
		if (op == INODE_REMOVE)
			return (GFARM_ERR_INVALID_ARGUMENT);
		*inp = parent;
		*createdp = 0;
		return (GFARM_ERR_NO_ERROR);
	}
	if (len > GFS_MAXNAMLEN)
		len = GFS_MAXNAMLEN;
	if (op != INODE_CREATE && op != INODE_LINK) {
		entry = dir_lookup(parent->u.c.s.d.entries, name, len);
		if (entry == NULL)
			return (GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY);
		if (op == INODE_REMOVE) {
			if ((e = inode_access(parent, user, GFS_W_OK)) !=
			    GFARM_ERR_NO_ERROR) {
				return (e);
			}
			*inp = dir_entry_get_inode(entry);
			*createdp = 0;
			(*inp)->i_nlink--;
			dir_remove_entry(parent->u.c.s.d.entries, name, len);
			inode_modified(parent);

			e = db_direntry_remove(parent->i_number, name, len);
			if (e != GFARM_ERR_NO_ERROR)
				gflog_error("db_direntry_remove(%" GFARM_PRId64
				    ", %.*s): %s",
				    parent->i_number, len, name,
				    gfarm_error_string(e));

			return (GFARM_ERR_NO_ERROR);
		}
		*inp = dir_entry_get_inode(entry);
		*createdp = 0;
		return (GFARM_ERR_NO_ERROR);
	}

	entry = dir_enter(parent->u.c.s.d.entries, name, len, &created);
	if (entry == NULL)
		return (GFARM_ERR_NO_MEMORY);
	if (!created) {
		*inp = dir_entry_get_inode(entry);
		*createdp = 0;
		return (GFARM_ERR_NO_ERROR);
	}
	if ((e = inode_access(parent, user, GFS_W_OK)) != GFARM_ERR_NO_ERROR) {
		dir_remove_entry(parent->u.c.s.d.entries, name, len);
		return (e);
	}
	if (op == INODE_LINK) {
		n = *inp;
		n->i_nlink++;
		dir_entry_set_inode(entry, n);
		inode_status_changed(n);
		inode_modified(parent);

		e = db_inode_nlink_modify(n->i_number, n->i_nlink);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(
			    "db_inode_nlink_modify(%" GFARM_PRId64 "): %s",
			    n->i_number, gfarm_error_string(e));
		e = db_direntry_add(parent->i_number, name, len, n->i_number);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error("db_direntry_add(%" GFARM_PRId64
			    ", %" GFARM_PRId64 "): %s",
			    parent->i_number, n->i_number,
			    gfarm_error_string(e));

		*createdp = 1;
		return (GFARM_ERR_NO_ERROR);
	}
	n = inode_alloc();
	if (n == NULL) {
		dir_remove_entry(parent->u.c.s.d.entries, name, len);
		return (GFARM_ERR_NO_MEMORY);
	}
	e = is_dir ? inode_init_dir(n, parent) : inode_init_file(n);
	if (e != GFARM_ERR_NO_ERROR) {
		dir_remove_entry(parent->u.c.s.d.entries, name, len);
		inode_free(n);
		return (e);
	}
	n->i_user = user;
	n->i_group = parent->i_group;
	n->i_size = 0;
	inode_created(n);
	dir_entry_set_inode(entry, n);
	inode_modified(parent);

	{
		struct gfs_stat st;

		st.st_ino = n->i_number;
		st.st_gen = n->i_gen;
		st.st_mode = n->i_mode;
		st.st_nlink = n->i_nlink;
		st.st_user = user_name(n->i_user);
		st.st_group = group_name(n->i_group);
		st.st_size = n->i_size;
		st.st_ncopy = 0;
		st.st_atimespec = n->i_atimespec;
		st.st_mtimespec = n->i_mtimespec;
		st.st_ctimespec = n->i_ctimespec;
		if (n->i_gen == 0)
			e = db_inode_add(&st);
		else
			e = db_inode_modify(&st);
	}
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error("db_inode_%s(%" GFARM_PRId64 "): %s",
		    n->i_gen == 0 ? "add" : "modify",
		    n->i_number, gfarm_error_string(e));

	/*
	 * We do db_direntry_add() here to make LDAP happy.
	 * Because inode must be created before DirEntry
	 * due to LDAP DN hierarchy.
	 * See the comment in inode_init_dir() too.
	 */
	if (is_dir) {
		e = db_direntry_add(n->i_number, dot, DOT_LEN, n->i_number);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error("db_direntry_add(%" GFARM_PRId64
			    ", \".\", %" GFARM_PRId64 "): %s",
			    parent->i_number, n->i_number,
			    gfarm_error_string(e));
		e = db_direntry_add(
			n->i_number, dotdot, DOTDOT_LEN, parent->i_number);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error("db_direntry_add(%" GFARM_PRId64
			    ", \"..\", %" GFARM_PRId64 "): %s",
			    parent->i_number, n->i_number,
			    gfarm_error_string(e));
	}
	e = db_direntry_add(parent->i_number, name, len, n->i_number);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error("db_direntry_add(%" GFARM_PRId64
		    ", %" GFARM_PRId64 "): %s",
		    parent->i_number, n->i_number,
		    gfarm_error_string(e));

	*inp = n;
	*createdp = 1;
	return (GFARM_ERR_NO_ERROR);
}

/* XXX TODO: namei cache */
/* if (op == INODE_LINK) *inp is an input parameter instead of output */
/* if (op != INODE_CREATE), (is_dir) may be -1, and that means "don't care". */
gfarm_error_t
inode_lookup_relative(struct inode *n, char *name,
	int is_dir, enum gfarm_inode_lookup_op op,
	struct user *user, struct inode **inp, int *createdp)
{
	gfarm_error_t e;
	int len = strlen(name);
	struct inode *nn;

#ifdef __GNUC__ /* workaround gcc warning: may be used uninitialized */
	nn = NULL;
#endif
	if (!inode_is_dir(n))
		return (GFARM_ERR_NOT_A_DIRECTORY);
	if ((e = inode_access(n, user, GFS_X_OK)) != GFARM_ERR_NO_ERROR)
		return (e);
	if (op == INODE_LINK)
		nn = *inp;
	e = inode_lookup_basename(n, name, len,
	    is_dir, op, user, &nn, createdp);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (is_dir != -1 && inode_is_dir(nn) != is_dir)
		return (is_dir ?
		    GFARM_ERR_NOT_A_DIRECTORY :
		    GFARM_ERR_IS_A_DIRECTORY);
	*inp = nn;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
inode_lookup_root(struct process *process, int op, struct inode **inp)
{
	gfarm_error_t e;
	struct inode *inode = inode_lookup(ROOT_INUMBER);

	if (inode == NULL)
		return (GFARM_ERR_STALE_FILE_HANDLE); /* XXX never happen */
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
	int created;
	struct user *user = process_get_user(process);
	gfarm_error_t e = inode_lookup_relative(base, dotdot, 1,
	    INODE_LOOKUP, user, &inode, &created);

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
	int created;
	struct user *user = process_get_user(process);
	gfarm_error_t e = inode_lookup_relative(base, name, -1,
	    INODE_LOOKUP, user, &inode, &created);

	if (e == GFARM_ERR_NO_ERROR) {
		if ((op & GFS_W_OK) != 0 && inode_is_dir(inode)) {
			e = GFARM_ERR_IS_A_DIRECTORY;
		} else {
			e = inode_access(inode, user, op);
		}
		if (e == GFARM_ERR_NO_ERROR) {
			*inp = inode;
		}
	}
	return (e);
}

gfarm_error_t
inode_create_file(struct inode *base, char *name,
	struct process *process, int op, gfarm_mode_t mode,
	struct inode **inp, int *createdp)
{
	struct inode *inode;
	int created;
	struct user *user = process_get_user(process);
	gfarm_error_t e = inode_lookup_relative(base, name, 0,
	    INODE_CREATE, user, &inode, &created);

	if (e == GFARM_ERR_NO_ERROR) {
		if (created) {
			inode->i_mode |= mode;
			/*
			 * XXX FIXME
			 * This is just after db_inode_add/modify(), and
			 * it's wastful to write the mode to the DB again.
			 */
			e = db_inode_mode_modify(inode->i_number,
			    inode->i_mode);
			if (e != GFARM_ERR_NO_ERROR)
				gflog_error("db_inode_mode_modify(%"
				    GFARM_PRId64 "): %s",
				    inode->i_number,
				    gfarm_error_string(e));
		} else if ((op & GFS_W_OK) != 0 && inode_is_dir(inode)) {
			e = GFARM_ERR_IS_A_DIRECTORY;
		} else {
			e = inode_access(inode, user, op);
		}
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
	int created;
	struct user *user = process_get_user(process);
	gfarm_error_t e = inode_lookup_relative(base, name, 1,
	    INODE_CREATE, user, &inode, &created);

	if (e == GFARM_ERR_NO_ERROR) {
		if (created) {
			inode->i_mode |= mode;
			/*
			 * XXX FIXME
			 * This is just after db_inode_add/modify(), and
			 * it's wastful to write the mode to the DB again.
			 */
			e = db_inode_mode_modify(inode->i_number,
			    inode->i_mode);
			if (e != GFARM_ERR_NO_ERROR)
				gflog_error("db_inode_mode_modify(%"
				    GFARM_PRId64 "): %s",
				    inode->i_number, gfarm_error_string(e));
		} else {
			e = GFARM_ERR_ALREADY_EXISTS;
		}
	}
	return (e);
}

gfarm_error_t
inode_create_link(struct inode *base, char *name,
	struct process *process, struct inode *inode)
{
	int created;
	struct user *user = process_get_user(process);
	gfarm_error_t e = inode_lookup_relative(base, name, -1,
	    INODE_LINK, user, &inode, &created);

	if (e == GFARM_ERR_NO_ERROR) {
		if (!created)
			e = GFARM_ERR_ALREADY_EXISTS;
	}
	return (e);
}

gfarm_error_t
inode_rename(
	struct inode *sdir, char *sname,
	struct inode *ddir, char *dname,
	struct process *process)
{
	gfarm_error_t e;
	struct user *user = process_get_user(process);
	struct inode *src, *dst;
	int tmp;

	if (user == NULL)
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);

	/* can remove src? */
	if ((e = inode_access(sdir, user, GFS_X_OK|GFS_W_OK)) !=
	    GFARM_ERR_NO_ERROR)
		return (e);

	if ((e = inode_lookup_by_name(sdir, sname, process, 0, &src))
	    != GFARM_ERR_NO_ERROR)
		return (e);
	if (strchr(sname, '/') != NULL) /* sname should't have '/' */
		return (GFARM_ERR_INVALID_ARGUMENT);
	if (strchr(dname, '/') != NULL) /* dname should't have '/' */
		return (GFARM_ERR_INVALID_ARGUMENT);

	e = inode_lookup_by_name(ddir, dname, process, 0, &dst);
	if (e == GFARM_ERR_NO_ERROR) {
		if (GFARM_S_ISDIR(inode_get_mode(src)) ==
		    GFARM_S_ISDIR(inode_get_mode(dst))) {
			e = inode_unlink(ddir, dname, process);
			if (e != GFARM_ERR_NO_ERROR)
				return (e);
		} else if (GFARM_S_ISDIR(inode_get_mode(src)))
			return (GFARM_ERR_NOT_A_DIRECTORY);
		else
			return (GFARM_ERR_IS_A_DIRECTORY);
	} else if (e != GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY)
		return (e);

	e = inode_create_link(ddir, dname, process, src);
	if (e != GFARM_ERR_NO_ERROR) { /* shouldn't happen */
		gflog_error("rename(%s, %s): failed to link: %s",
		    sname, dname, gfarm_error_string(e));
		return (e);
	}
	e = inode_lookup_relative(sdir, sname, -1, INODE_REMOVE, user,
	    &src, &tmp);
	if (e != GFARM_ERR_NO_ERROR) /* shouldn't happen */
		gflog_error("rename(%s, %s): failed to unlink: %s",
		    sname, dname, gfarm_error_string(e));
	return (e);
}

gfarm_error_t
inode_unlink(struct inode *base, char *name, struct process *process)
{
	int tmp;
	struct inode *inode;
	gfarm_error_t e = inode_lookup_by_name(base, name, process, 0, &inode);

	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (inode_is_file(inode)) {
		e = inode_lookup_relative(base, name, 0,
		    INODE_REMOVE, process_get_user(process), &inode, &tmp);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
		if (inode->i_nlink > 0) {
			e = db_inode_nlink_modify(inode->i_number,
			    inode->i_nlink);
			if (e != GFARM_ERR_NO_ERROR)
				gflog_error("db_inode_nlink_modify(%"
				    GFARM_PRId64 "): %s",
				    inode->i_number, gfarm_error_string(e));
			return (GFARM_ERR_NO_ERROR);
		}
	} else if (inode_is_dir(inode)) {
		if (inode->i_nlink > 2 ||
		    !dir_is_empty(inode->u.c.s.d.entries))
			return (GFARM_ERR_DIRECTORY_NOT_EMPTY);
		else if (strcmp(name, dot) == 0 || strcmp(name, dotdot) == 0)
			return (GFARM_ERR_INVALID_ARGUMENT);
		e = inode_lookup_relative(base, name, 1,
		    INODE_REMOVE, process_get_user(process), &inode, &tmp);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
		e = db_direntry_remove(inode->i_number, dot, DOT_LEN);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error("db_direntry_remove(%" GFARM_PRId64
			    ", %s): %s",
			    inode->i_number, dot, gfarm_error_string(e));
		e = db_direntry_remove(inode->i_number, dotdot,DOTDOT_LEN);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error("db_direntry_remove(%" GFARM_PRId64
			    ", %s): %s",
			    inode->i_number, dotdot, gfarm_error_string(e));
	} else {
		gflog_fatal("inode_unlink: unknown inode type");
		/*NOTREACHED*/
		return (GFARM_ERR_UNKNOWN);
	}
	if (inode->u.c.state == NULL) {
		/* no process is opening this file, just remove it */
		inode_remove(inode);
		return (GFARM_ERR_NO_ERROR);
	} else {
		/* there are some processes which open this file */
		/* leave this inode until closed */

		e = db_inode_nlink_modify(inode->i_number, inode->i_nlink);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(
			    "db_inode_nlink_modify(%" GFARM_PRId64 "): %s",
			    inode->i_number, gfarm_error_string(e));

		return (GFARM_ERR_NO_ERROR);
	}
}

gfarm_error_t
inode_add_replica_internal(struct inode *inode, struct host *spool_host,
	int valid)
{
	struct file_copy *copy;

	for (copy = inode->u.c.s.f.copies; copy != NULL;
	    copy = copy->host_next) {
		if (copy->host == spool_host) {
			if (copy->valid)
				return (GFARM_ERR_ALREADY_EXISTS);
			else if (valid == 0)
				return (GFARM_ERR_OPERATION_NOW_IN_PROGRESS);
			else {
				copy->valid = valid;
				return (GFARM_ERR_NO_ERROR);
			}
		}
	}
	GFARM_MALLOC(copy);
	if (copy == NULL)
		return (GFARM_ERR_NO_MEMORY);
	copy->host = spool_host;
	copy->valid = valid;
	copy->host_next = inode->u.c.s.f.copies;
	inode->u.c.s.f.copies = copy;
	return (GFARM_ERR_NO_ERROR);
}

/*
 * 'valid == 0' means that the replica is not ready right now, and
 * going to be created.
 */
gfarm_error_t
inode_add_replica(struct inode *inode, struct host *spool_host, int valid)
{
	gfarm_error_t e = inode_add_replica_internal(inode, spool_host, valid);

	if (e != GFARM_ERR_NO_ERROR || !valid)
		return (e);

	e = db_filecopy_add(inode->i_number, host_name(spool_host));
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error("db_filecopy_add(%" GFARM_PRId64 ", %s): %s",
		    inode->i_number, host_name(spool_host),
		    gfarm_error_string(e));
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
remove_replica_internal(struct inode *inode, struct file_copy *copy)
{
	gfarm_error_t e, e2 = GFARM_ERR_NO_ERROR;

	e = host_remove_replica_enq(copy->host, inode->i_number, inode->i_gen);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error("host_remove_replica_enq(%" GFARM_PRId64
		    ", %s): %s", inode->i_number, host_name(copy->host),
		    gfarm_error_string(e));

	if (copy->valid && (e2 = db_filecopy_remove(inode->i_number,
	    host_name(copy->host))) != GFARM_ERR_NO_ERROR)
		gflog_error("db_filecopy_remove(%" GFARM_PRId64 ", %s): %s",
		    inode->i_number, host_name(copy->host),
		    gfarm_error_string(e2));
	return (e != GFARM_ERR_NO_ERROR ? e : e2);
}

gfarm_error_t
inode_remove_replica(struct inode *inode, struct host *spool_host,
	int do_not_delete_last)
{
	struct file_copy **copyp, *copy;
	gfarm_error_t e;
	int num_replica = 0;

	for (copyp = &inode->u.c.s.f.copies; (copy = *copyp) != NULL;
	    copyp = &copy->host_next) {
		if (copy->host == spool_host) {
			if (do_not_delete_last && num_replica == 0 &&
			    copy->host_next == NULL)
				return (GFARM_ERR_CANNOT_REMOVE_LAST_REPLICA);
			*copyp = copy->host_next;
			break;
		}
		if (copy->valid)
			++num_replica;
	}
	if (copy == NULL)
		return (GFARM_ERR_NO_SUCH_OBJECT);

	e = remove_replica_internal(inode, copy);
	free(copy);
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
inode_remove_every_other_replicas(struct inode *inode, struct host *spool_host)
{
	struct file_copy **copyp, *copy;
	gfarm_error_t e;

	for (copyp = &inode->u.c.s.f.copies; (copy = *copyp) != NULL;) {
		if (copy->host != spool_host) {
			*copyp = copy->host_next;
			e = remove_replica_internal(inode, copy);
			free(copy);
			continue;
		}
		copyp = &copy->host_next;
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
inode_open(struct file_opening *fo)
{
	struct inode *inode = fo->inode;
	struct inode_open_state *ios = inode->u.c.state;

	if (ios == NULL) {
		ios = inode_open_state_alloc();
		if (ios == NULL)
			return (GFARM_ERR_NO_MEMORY);
		inode->u.c.state = ios;
	}
	if ((accmode_to_op(fo->flag) & GFS_W_OK) != 0)
		++ios->u.f.writers;

	fo->opening_prev = &ios->openings;
	fo->opening_next = ios->openings.opening_next;
	ios->openings.opening_next = fo;
	fo->opening_next->opening_prev = fo;
	return (GFARM_ERR_NO_ERROR);
}

void
inode_close(struct file_opening *fo)
{
	inode_close_read(fo, NULL);
}

void
inode_close_read(struct file_opening *fo, struct gfarm_timespec *atime)
{
	struct inode *inode = fo->inode;
	struct inode_open_state *ios = inode->u.c.state;

	fo->opening_prev->opening_next = fo->opening_next;
	fo->opening_next->opening_prev = fo->opening_prev;
	if (ios->openings.opening_next == &ios->openings) { /* all closed */
		inode_open_state_free(inode->u.c.state);
		inode->u.c.state = NULL;
	}
	if ((accmode_to_op(fo->flag) & GFS_W_OK) != 0)
		--ios->u.f.writers;

	if (atime != NULL)
		inode->i_atimespec = *atime;
	if (inode->i_nlink == 0 && inode->u.c.state == NULL)
		inode_remove(inode); /* clears `ios->u.f.cksum_owner' too. */
}

void
inode_close_write(struct file_opening *fo, gfarm_off_t size,
	struct gfarm_timespec *atime, struct gfarm_timespec *mtime)
{
	gfarm_error_t e;
	struct inode *inode = fo->inode;
	struct inode_open_state *ios = inode->u.c.state;

	inode_cksum_invalidate(fo);
	if (ios->u.f.cksum_owner == NULL || ios->u.f.cksum_owner != fo)
		inode_cksum_remove(inode);

	inode->i_size = size;

	e = db_inode_size_modify(inode->i_number, inode->i_size);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error("db_inode_size_modify(%" GFARM_PRId64 "): %s",
		    inode->i_number, gfarm_error_string(e));

	if (mtime != NULL)
		inode->i_mtimespec = *mtime;
	inode_close_read(fo, atime);
}

int
inode_has_replica(struct inode *inode, struct host *spool_host)
{
	struct file_copy *copy;

	if (!inode_is_file(inode))
		gflog_fatal("inode_has_replica: not a file");
	for (copy = inode->u.c.s.f.copies; copy != NULL;
	    copy = copy->host_next) {
		if (copy->host == spool_host)
			return (copy->valid);
	}
	return (0);
}

gfarm_error_t
inode_getdirpath(struct inode *inode, struct process *process, char **namep)
{
	gfarm_error_t e;
	struct inode *parent, *dei;
	int created, ok;
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
		e = inode_lookup_relative(inode, dotdot, 1, INODE_LOOKUP,
		    user, &parent, &created);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
		e = inode_access(parent, user, GFS_R_OK|GFS_X_OK);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);

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
			return (GFARM_ERR_NO_MEMORY); /* directory too deep */
		}
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

	if (!inode_is_file(inode))
		return (NULL); /* not a file */
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
inode_schedule_confirm_for_write(struct inode *inode, struct host *spool_host,
	int to_create)
{
	struct inode_open_state *ios = inode->u.c.state;
	struct file_opening *fo;
	int already_opened, host_match;

	if (!inode_is_file(inode))
		gflog_fatal("inode_schedule_confirm_for_write: not a file");
	if (ios != NULL &&
	    (fo = ios->openings.opening_next) != &ios->openings) {
		already_opened = host_match = 0;
		for (; fo != &ios->openings; fo = fo->opening_next) {
			if ((accmode_to_op(fo->flag) & GFS_W_OK) != 0 &&
			    fo->u.f.spool_host != NULL)
				return (fo->u.f.spool_host == spool_host);
			if (fo->u.f.spool_host != NULL) {
				already_opened = 1;
				if (fo->u.f.spool_host == spool_host)
					host_match = 1;
			}
		}
		if (already_opened)
			return (host_match);
	}
	/* not opened */
	if (!inode_is_creating_file(inode))
		return (inode_has_replica(inode, spool_host));
	return (to_create);
}

gfarm_error_t
inode_schedule_file_reply(struct inode *inode, struct peer *peer,
	int writable, int creating, const char *diag)
{
	gfarm_error_t e, e_save;
	struct inode_open_state *ios = inode->u.c.state;
	struct file_opening *fo;
	struct file_copy *copy;
	int n;

	/* XXX FIXME too long giant lock */

	if (!inode_is_file(inode))
		gflog_fatal("inode_schedule_file_reply: not a file");

	if (creating)
		return (host_schedule_reply_one_or_all(peer, diag));
	if (writable && ios != NULL &&
	    (fo = ios->openings.opening_next) != &ios->openings) {
		n = 0;
		for (; fo != &ios->openings; fo = fo->opening_next) {
			if ((accmode_to_op(fo->flag) & GFS_W_OK) != 0 &&
			    fo->u.f.spool_host != NULL) {
				e_save = host_schedule_reply_n(peer, 1, diag);
				e = host_schedule_reply(fo->u.f.spool_host,
				    peer, diag);
				return (e_save!=GFARM_ERR_NO_ERROR ? e_save:e);
			}
			if (fo->u.f.spool_host != NULL)
				n++;
		}
		if (n > 0) {
			e_save = host_schedule_reply_n(peer, n, diag);
			for (; fo != &ios->openings; fo = fo->opening_next) {
				e = host_schedule_reply(fo->u.f.spool_host,
				    peer, diag);
				if (e_save == GFARM_ERR_NO_ERROR)
					e_save = e;
			}
			return (e_save);
		}
	}
	/* read access, or write access && no process is opening the file */

	if (inode_is_creating_file(inode))
		gflog_fatal("inode_schedule_file_reply: should be creating");
	n = 0;
	for (copy = inode->u.c.s.f.copies; copy != NULL;
	    copy = copy->host_next) {
		if (copy->valid && host_is_up(copy->host))
		    n++;
	}
	e_save = host_schedule_reply_n(peer, n, diag);
	for (copy = inode->u.c.s.f.copies; copy != NULL;
	    copy = copy->host_next) {
		if (copy->valid && host_is_up(copy->host)) {
			e = host_schedule_reply(copy->host, peer, diag);
			if (e_save == GFARM_ERR_NO_ERROR)
				e_save = e;
		}
	}
	return (e_save);
}

gfarm_error_t
inode_replica_list_by_name(struct inode *inode,
	gfarm_int32_t *np, char ***hostsp)
{
	struct file_copy *copy;
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	int n, i;
	char **hosts;

	if (inode_is_dir(inode))
		return (GFARM_ERR_IS_A_DIRECTORY);
	else if (!inode_is_file(inode))
		return (GFARM_ERR_INVALID_ARGUMENT);

	n = inode_get_ncopy(inode);
	GFARM_MALLOC_ARRAY(hosts, n);
	if (hosts == NULL)
		return (GFARM_ERR_NO_MEMORY);

	i = 0;
	for (copy = inode->u.c.s.f.copies; copy != NULL && i < n;
	    copy = copy->host_next) {
		if (copy->valid && host_is_up(copy->host)) {
			hosts[i] = strdup(host_name(copy->host));
			if (hosts[i] == NULL) {
				e = GFARM_ERR_NO_MEMORY;
				break;
			}
			++i;
		}
	}
	if (e != GFARM_ERR_NO_ERROR) {
		while (--i >= 0)
			free(hosts[i]);
		free(hosts);
	}
	else {
		*np = i;
		*hostsp = hosts;
	}
	return (e);
}

/*
 * loading metadata from persistent storage.
 */

/* The memory owner of `*st' is changed to inode.c */
void
inode_add_one(void *closure, struct gfs_stat *st)
{
	gfarm_error_t e;
	struct inode *inode;

	inode = inode_alloc_num(st->st_ino);
	if (inode == NULL) {
		gflog_error("cannot allocate inode %" GFARM_PRId64,st->st_ino);
		e = GFARM_ERR_UNKNOWN;
	} else if (GFARM_S_ISDIR(st->st_mode)) {
		e = inode_init_dir_internal(inode);
	} else if (GFARM_S_ISREG(st->st_mode)) {
		e = inode_init_file(inode);
	} else if (st->st_mode == INODE_MODE_FREE) {
		inode_clear(inode);
		e = GFARM_ERR_NO_ERROR;
	} else {
		gflog_error("unknown inode type %" GFARM_PRId64 ", mode 0%o",
		    st->st_ino, st->st_mode);
		e = GFARM_ERR_UNKNOWN;
		assert(0);
	}
	if (e != GFARM_ERR_NO_ERROR) {
		if (e != GFARM_ERR_UNKNOWN) {
			gflog_error("inode %" GFARM_PRId64 ": %s", st->st_ino,
			    gfarm_error_string(e));
		}
		gfs_stat_free(st);
		return;
	}

	inode->i_gen = st->st_gen;
	inode->i_nlink = st->st_nlink;
	inode->i_size = st->st_size;
	inode->i_mode = st->st_mode;
	inode->i_user = user_lookup(st->st_user);
	inode->i_group = group_lookup(st->st_group);
	inode->i_atimespec = st->st_atimespec;
	inode->i_mtimespec = st->st_mtimespec;
	inode->i_ctimespec = st->st_ctimespec;
	gfs_stat_free(st);
}

/* The memory owner of `type' and `sum' is changed to inode.c */
void
inode_cksum_add_one(void *closure,
	gfarm_ino_t inum, char *type, size_t len, char *sum)
{
	struct inode *inode = inode_lookup(inum);

	if (inode == NULL) {
		gflog_error("inode_cksum_add_one: no inode %" GFARM_PRId64,
		    inum);
	} else if (!inode_is_file(inode)) {
		gflog_error("inode_cksum_add_one: not file %" GFARM_PRId64,
		    inum);
	} else if (inode->u.c.s.f.cksum != NULL) {
		gflog_error("inode_cksum_add_one: dup cksum %" GFARM_PRId64,
		    inum);
	} else {
		inode_cksum_set_internal(inode, type, len, sum);
	}
	free(type);
	free(sum);
}

/* The memory owner of `hostname' is changed to inode.c */
void
file_copy_add_one(void *closure, gfarm_ino_t inum, char *hostname)
{
	gfarm_error_t e;
	struct inode *inode = inode_lookup(inum);
	struct host *host = host_lookup(hostname);

	if (inode == NULL) {
		gflog_error("file_copy_add_one: no inode %" GFARM_PRId64,inum);
	} else if (!inode_is_file(inode)) {
		gflog_error("file_copy_add_one: not file %" GFARM_PRId64,inum);
	} else if (host == NULL) {
		gflog_error("file_copy_add_one: no host %s", hostname);
	} else if ((e = inode_add_replica_internal(inode, host, 1)) !=
	    GFARM_ERR_NO_ERROR){
		gflog_error("file_copy_add_one: add_replica: %s",
		    gfarm_error_string(e));
	}
	free(hostname);
}

/* The memory owner of `hostname' is changed to inode.c */
void
dead_file_copy_add_one(void *closure,
	gfarm_ino_t inum, gfarm_uint64_t igen, char *hostname)
{
	gfarm_error_t e;
	struct host *host = host_lookup(hostname);
	char *funcname = "dead_file_copy_add_one";

	if (host == NULL) {
		gflog_error("%s: no host %s", funcname, hostname);
	} else if ((e = host_remove_replica_enq(host,
	    inum, igen)) != GFARM_ERR_NO_ERROR) {
		gflog_error("%s: record dead replica: %s", funcname,
		    gfarm_error_string(e));
	} else {
		e = db_deadfilecopy_remove(inum, igen, hostname);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error("%s: db_deadfilecopy_remove: %s", funcname,
			    gfarm_error_string(e));
	}
	free(hostname);
}

/* The memory owner of `entry_name' is changed to inode.c */
void
dir_entry_add_one(void *closure,
	gfarm_ino_t dir_inum, char *entry_name, int entry_len,
	gfarm_ino_t entry_inum)
{
	struct inode *dir_inode = inode_lookup(dir_inum);
	struct inode *entry_inode = inode_lookup(entry_inum);
	DirEntry entry;
	int created;

	if (dir_inode == NULL) {
		gflog_error("dir_entry_add_one: no dir %" GFARM_PRId64,
		    dir_inum);
	} else if (!inode_is_dir(dir_inode)) {
		gflog_error("dir_entry_add_one: not dir %" GFARM_PRId64,
		    dir_inum);
	} else if (entry_inode == NULL) {
		gflog_error("dir_entry_add_one: no %" GFARM_PRId64,
		    entry_inum);
	} else if ((entry = dir_enter(dir_inode->u.c.s.d.entries,
	    entry_name, entry_len, &created)) == NULL) {
		gflog_error("dir_entry_add_one: no memory");
	} else if (!created) {
		gflog_error("dir_entry_add_one: already exists ");
	} else {
		dir_entry_set_inode(entry, entry_inode);
	}
	free(entry_name);
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
		gflog_error("loading inode: %s", gfarm_error_string(e));
	e = db_inode_cksum_load(NULL, inode_cksum_add_one);
	if (e != GFARM_ERR_NO_ERROR && e != GFARM_ERR_NO_SUCH_OBJECT /* XXX */)
		gflog_error("loading inode cksum: %s", gfarm_error_string(e));

	root = inode_lookup(ROOT_INUMBER);
	if (root != NULL)
		return;

	gflog_info("root inode not found, creating filesystem");

	/* root inode */
	st.st_ino = ROOT_INUMBER;
	st.st_gen = 0;
	st.st_nlink = 2;
	st.st_size = 4;
	st.st_mode = GFARM_S_IFDIR | 0775;
	st.st_user = strdup(ADMIN_USER_NAME);
	st.st_group = strdup(ADMIN_GROUP_NAME);
	touch(&st.st_atimespec);
 	st.st_ctimespec = st.st_mtimespec = st.st_atimespec;
	/* inode_add_one will free(st). need to call db_inode_add before it */
	e = db_inode_add(&st);
	inode_add_one(NULL, &st);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error("failed to store root inode to storage: %s",
		    gfarm_error_string(e));

	/* root directory */
	dir_entry_add_one(NULL, ROOT_INUMBER, strdup(dot), DOT_LEN,
	    ROOT_INUMBER);
	dir_entry_add_one(NULL, ROOT_INUMBER, strdup(dotdot), DOTDOT_LEN,
	    ROOT_INUMBER);
	e = db_direntry_add(ROOT_INUMBER, dot, DOT_LEN, ROOT_INUMBER);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(
		    "failed to store '.' in root directory to storage: %s",
		    gfarm_error_string(e));
	e = db_direntry_add(ROOT_INUMBER, dotdot, DOTDOT_LEN, ROOT_INUMBER);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(
		    "failed to store '..' in root directory to storage: %s",
		    gfarm_error_string(e));
}

void
file_copy_init(void)
{
	gfarm_error_t e;

	e = db_filecopy_load(NULL, file_copy_add_one);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error("loading filecopy: %s", gfarm_error_string(e));
}

void
dead_file_copy_init(void)
{
	gfarm_error_t e;

	e = db_deadfilecopy_load(NULL, dead_file_copy_add_one);
	if (e != GFARM_ERR_NO_ERROR && e != GFARM_ERR_NO_SUCH_OBJECT)
		gflog_error("loading deadfilecopy: %s", gfarm_error_string(e));
}

void
dir_entry_init(void)
{
	gfarm_error_t e;

	e = db_direntry_load(NULL, dir_entry_add_one);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error("loading direntry: %s", gfarm_error_string(e));
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
		gflog_error("dir_emptry: cannot get cursor");
		abort();
	}
	for (;;) {
		entry = dir_cursor_get_entry(dir, &cursor);
		if (entry == NULL)
			return (1);
		name = dir_entry_get_name(entry, &namelen);
		if ((namelen != DOT_LEN || memcmp(name, dot, DOT_LEN) != 0) &&
		    (namelen!=DOTDOT_LEN || memcmp(name,dotdot,DOTDOT_LEN)!=0))
			return (0);
		if (!dir_cursor_next(dir, &cursor))
			return (1);
	}
}

#if 1 /* DEBUG */
void
dir_dump(gfarm_ino_t i_number)
{
	struct inode *inode = inode_lookup(i_number), *entry_inode;
	Dir dir;
	DirEntry entry;
	DirCursor cursor;
	int ok;
	char *s, *name;
	int namelen;

	if (inode == NULL) {
		gflog_info("inode_lookup %" GFARM_PRId64 " failed", i_number);
		return;
	}
	dir = inode_get_dir(inode);
	if (dir == NULL) {
		gflog_info("inode %" GFARM_PRId64 " is not a directory",
		    i_number);
		return;
	}
	ok = dir_cursor_set_pos(dir, 0, &cursor);
	if (!ok) {
		gflog_info("dir inode %" GFARM_PRId64 " cannot seek to 0",
		    i_number);
		return;
	}
	gflog_info("dir inode %" GFARM_PRId64 " dump start:", i_number);
	for (;;) {
		entry = dir_cursor_get_entry(dir, &cursor);
		if (entry == NULL)
			break;
		name = dir_entry_get_name(entry, &namelen);
		GFARM_MALLOC_ARRAY(s, namelen + 1);
		memcpy(s, name, namelen);
		s[namelen] = '\0';
		entry_inode = dir_entry_get_inode(entry);
		gflog_info("entry %s (len=%d) inum %" GFARM_PRId64,
		    name, namelen, inode_get_number(entry_inode));
		free(s);
		if (!dir_cursor_next(dir, &cursor))
			break;
	}
	gflog_info("dir inode %" GFARM_PRId64 " dump end", i_number);
}

void
rootdir_dump(void)
{
	dir_dump(ROOT_INUMBER);
}

#endif /* DEBUG */
