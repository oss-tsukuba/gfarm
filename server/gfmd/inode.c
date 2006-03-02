#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h> /* sprintf */
#include <sys/time.h>

#define GFARM_INTERNAL_USE
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

#include "gfutil.h"

#include "gfm_proto.h"

#include "host.h"
#include "user.h"
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

static char dot[] = ".";
static char dotdot[] = "..";

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

gfarm_error_t
inode_cksum_set(struct file_opening *fo,
	const char *cksum_type, size_t cksum_len, const char *cksum,
	gfarm_int32_t flags, struct gfarm_timespec *mtime)
{
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
	    (ios->u.f.cksum_owner->flag & GFARM_FILE_ACCMODE)
		!= GFARM_FILE_RDONLY &&
	    (fo->flag & GFARM_FILE_ACCMODE) == GFARM_FILE_RDONLY)
		return (GFARM_ERR_EXPIRED);

	cs = inode->u.c.s.f.cksum;

	/* reduce memory reallocation */
	if (cs != NULL &&
	    strcmp(cksum_type, cs->type) == 0 && cksum_len == cs->len) {
		memcpy(cs->sum, cksum, cksum_len);
		return (GFARM_ERR_NO_ERROR);
	}
	inode_cksum_clear(inode);

	cs = malloc(sizeof(*cs) - sizeof(cs->sum) + cksum_len +
	    strlen(cksum_type) + 1);
	if (cs == NULL)
		return (GFARM_ERR_NO_MEMORY);
	cs->type = cs->sum + cksum_len;
	cs->len = cksum_len;
	memcpy(cs->sum, cksum, cksum_len);
	strcpy(cs->type, cksum_type);
	inode->u.c.s.f.cksum = cs;

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
	     (fo->flag & GFARM_FILE_ACCMODE) == GFARM_FILE_RDONLY))
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
	struct inode_open_state *ios = malloc(sizeof(*ios));

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

gfarm_error_t
inode_init(void)
{
	/* XXX FIXME */
	return (GFARM_ERR_NO_ERROR);
}

struct inode *
inode_alloc_num(gfarm_ino_t inum)
{
	gfarm_ino_t i;
	struct inode *inode;

	if (inum < ROOT_INUMBER)
		return (NULL); /* we don't use 0 and 1 as i_number */
	if (inode_table_size < inum) {
		gfarm_ino_t new_table_size;
		struct inode **p;

		if (inum < INODE_TABLE_SIZE_INITIAL)
			new_table_size = INODE_TABLE_SIZE_INITIAL;
		else if (inum < inode_table_size * INODE_TABLE_SIZE_MULTIPLY)
			new_table_size =
			    inode_table_size * INODE_TABLE_SIZE_MULTIPLY;
		else 
			new_table_size = inum * INODE_TABLE_SIZE_MULTIPLY;
		p = realloc(inode_table, sizeof(*p) * new_table_size);
		if (p == NULL)
			return (NULL); /* no memory */
		inode_table = p;

		for (i = inode_table_size; i < new_table_size; i++)
			inode_table[i] = NULL;
		inode_table_size = new_table_size;
	}
	if ((inode = inode_table[inum]) == NULL) {
		inode = malloc(sizeof(*inode));
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
	return (inode);
}

struct inode *
inode_alloc(void)
{
	if (inode_free_list_initialized)
		inode_free_list_init();
		
	if (inode_free_list.u.l.next != &inode_free_list)
		return (inode_alloc_num(inode_free_list.u.l.next->i_number));
	else
		return (inode_alloc_num(inode_free_index));
}

void
inode_free(struct inode *inode)
{
	inode->i_mode = INODE_MODE_FREE;
	inode->i_nlink = 0;
	/* add to the inode_free_list */
	inode->u.l.prev = &inode_free_list;
	inode->u.l.next = inode_free_list.u.l.next;
	inode->u.l.next->u.l.prev = inode;
	inode_free_list.u.l.next = inode;
}

void
inode_remove(struct inode *inode)
{
	if (inode->u.c.state != NULL)
		gflog_fatal("inode_remove: still opened");
	if (inode_is_file(inode)) {
		struct file_copy *copy, *cn;
		gfarm_error_t e;

		for (copy = inode->u.c.s.f.copies; copy != NULL;
		    copy = cn) {
			e = host_remove_replica(copy->host, inode->i_number);
			if (e != GFARM_ERR_NO_ERROR)
				gflog_error("host_remove_replica: %s",
				    host_name(copy->host));
			cn = copy->host_next;
			free(copy);
		}
		inode_cksum_clear(inode);
	} else if (inode_is_dir(inode)) {
		dir_free(inode->u.c.s.d.entries);
	} else {
		gflog_fatal("inode_unlink: unknown inode type");
		/*NOTREACHED*/
	}
	inode_free(inode);
}

int
inode_init_dir(struct inode *inode, struct inode *parent)
{
	DirEntry entry;

	inode->u.c.s.d.entries = dir_alloc();
	if (inode->u.c.s.d.entries == NULL)
		return (0);

	entry = dir_enter(inode->u.c.s.d.entries, dot, sizeof(dot) - 1, NULL);
	if (entry == NULL) {
		dir_free(inode->u.c.s.d.entries);
		return (0);
	}
	dir_entry_set_inode(entry, inode);

	entry = dir_enter(inode->u.c.s.d.entries, dotdot, sizeof(dotdot) - 1,
	    NULL);
	if (entry == NULL) {
		dir_free(inode->u.c.s.d.entries);
		return (0);
	}
	dir_entry_set_inode(entry, parent);

	inode->i_nlink = 2;
	inode->i_mode = GFARM_S_IFDIR;
	return (1);
}

int
inode_init_file(struct inode *inode)
{
	inode->i_nlink = 1;
	inode->i_mode = GFARM_S_IFREG;
	inode->u.c.s.f.copies = NULL;
	inode->u.c.s.f.cksum = NULL;
	return (1);
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

gfarm_off_t
inode_get_size(struct inode *inode)
{
	return (inode->i_size);
}

gfarm_int64_t
inode_get_ncopy(struct inode *inode)
{
	struct file_copy *copy;
	int n = 0;

	for (copy = inode->u.c.s.f.copies; copy != NULL;
	    copy = copy->host_next) {
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
	if ((mode & GFARM_S_IFMT) != 0)
		return (GFARM_ERR_INVALID_ARGUMENT);
	inode->i_mode = (inode->i_mode & GFARM_S_IFMT) |
	    (mode & GFARM_S_ALLPERM);
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
inode_set_owner(struct inode *inode, struct user *user, struct group *group)
{
	if (user != NULL)
		inode->i_user = user;
	if (group != NULL)
		inode->i_group = group;
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
	inode->i_atimespec = *atime;
}


void
inode_set_mtime(struct inode *inode, struct gfarm_timespec *mtime)
{
	inode->i_atimespec = *mtime;
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
	touch(&inode->i_atimespec);
}

void
inode_modified(struct inode *inode)
{
	touch(&inode->i_mtimespec);
}

void
inode_status_changed(struct inode *inode)
{
	touch(&inode->i_ctimespec);
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
			dir_remove_entry(parent->u.c.s.d.entries, name, len);
			inode_modified(parent);
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
		*createdp = 1;
		return (GFARM_ERR_NO_ERROR);
	}
	n = inode_alloc();
	if (n == NULL ||
	    !(is_dir ? inode_init_dir(n, parent) : inode_init_file(n))) {
		dir_remove_entry(parent->u.c.s.d.entries, name, len);
		if (n != NULL)
			inode_free(n);
		return (GFARM_ERR_NO_MEMORY);
	}
	n->i_user = user;
	n->i_group = parent->i_group;
	n->i_size = 0;
	inode_created(n);
	dir_entry_set_inode(entry, n);
	inode_modified(parent);
	*inp = n;
	*createdp = 1;
	return (GFARM_ERR_NO_ERROR);
}

/* XXX TODO: namei cache */
/* if (op != INODE_CREATE), (is_dir) may be -1, and that means "don't care". */
gfarm_error_t
inode_lookup_relative(struct inode *n, char *name,
	int is_dir, enum gfarm_inode_lookup_op op,
	struct user *user, struct inode **inp, int *createdp)
{
	gfarm_error_t e;
	int len = strlen(name);
	struct inode *nn;

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
inode_lookup_root(struct process *process, struct inode **inp)
{
	gfarm_error_t e;
	struct inode *inode = inode_lookup(ROOT_INUMBER);

	if (inode == NULL)
		return (GFARM_ERR_STALE_FILE_HANDLE); /* XXX never happen */
	e = inode_access(inode, process_get_user(process), GFS_X_OK);
	if (e == GFARM_ERR_NO_ERROR)
		*inp = inode;
	return (e);
}

gfarm_error_t
inode_lookup_parent(struct inode *base, struct process *process,
	struct inode **inp)
{
	struct inode *inode;
	int created;
	struct user *user = process_get_user(process);
	gfarm_error_t e = inode_lookup_relative(base, dotdot, 1,
	    INODE_LOOKUP, user, &inode, &created);

	if (e == GFARM_ERR_NO_ERROR &&
	    (e = inode_access(inode, user, GFS_X_OK)) == GFARM_ERR_NO_ERROR) {
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
			e = inode_access(inode, user, op |
			    (inode_is_dir(inode) ? GFS_X_OK : 0));
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
		if (--inode->i_nlink > 0)
			return (GFARM_ERR_UNKNOWN);
	} else if (inode_is_dir(inode)) {
		if (inode->i_nlink > 2 ||
		    !dir_is_empty(inode->u.c.s.d.entries))
			return (GFARM_ERR_DIRECTORY_NOT_EMPTY);
		e = inode_lookup_relative(base, name, 0,
		    INODE_REMOVE, process_get_user(process), &inode, &tmp);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
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
		return (GFARM_ERR_NO_ERROR);
	}
}

gfarm_error_t
inode_add_replica(struct inode *inode, struct host *spool_host)
{
	struct file_copy *copy;

	for (copy = inode->u.c.s.f.copies; copy != NULL;
	    copy = copy->host_next) {
		if (copy->host == spool_host)
			return (GFARM_ERR_ALREADY_EXISTS);
	}
	copy = malloc(sizeof(*copy));
	if (copy == NULL)
		return (GFARM_ERR_NO_MEMORY);
	copy->host_next = inode->u.c.s.f.copies;
	inode->u.c.s.f.copies = copy;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
inode_remove_replica(struct inode *inode, struct host *spool_host)
{
	struct file_copy **copyp, *copy;

	for (copyp = &inode->u.c.s.f.copies; (copy = *copyp) != NULL;
	    copyp = &copy->host_next) {
		if (copy->host == spool_host) {
			*copyp = copy->host_next;
			free(copy);
			return (GFARM_ERR_NO_ERROR);
		}
	}
	return (GFARM_ERR_NO_SUCH_OBJECT);
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
	if ((fo->flag & GFARM_FILE_ACCMODE) != GFARM_FILE_RDONLY)
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
	if ((fo->flag & GFARM_FILE_ACCMODE) != GFARM_FILE_RDONLY)
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
	struct inode *inode = fo->inode;
	struct inode_open_state *ios = inode->u.c.state;

	inode_cksum_invalidate(fo);
	if (ios->u.f.cksum_owner == NULL || ios->u.f.cksum_owner != fo)
		inode_cksum_clear(inode);

	inode->i_size = size;
	if (mtime != NULL)
		inode->i_mtimespec = *mtime;
	inode_close_read(fo, atime);
}

void
inode_update_atime(struct inode *inode, struct gfarm_timespec *atime)
{
	inode->i_atimespec = *atime;
}

void
inode_update_mtime(struct inode *inode, struct gfarm_timespec *mtime)
{
	inode->i_mtimespec = *mtime;
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
			return (1);
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
	int i, namelen, depth = 0, totallen = 0;

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
		s = malloc(namelen + 1);
		if (depth >= GFS_MAX_DIR_DEPTH || s == NULL) {
			for (i = 0; i < depth; i++)
				free(names[i]);
			return (GFARM_ERR_NO_MEMORY); /* directory too deep */
		}
		names[depth++] = s;
		totallen += namelen;
	}
	if (depth == 0)
		s = malloc(1 + 1);
	else
		s = malloc(totallen + depth + 1);
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
inode_schedule_host_for_read(struct inode *inode, struct host *spool_host)
{
	struct file_copy *copy;
	struct host *h = NULL;
	double loadav, best_loadav;

#ifdef __GNUC__ /* workaround gcc warning: might be used uninitialized */
	best_loadav = 0;
#endif
	if (!inode_is_file(inode))
		gflog_fatal("inode_schedule_host_for_read: not a file");
	for (copy = inode->u.c.s.f.copies; copy != NULL;
	    copy = copy->host_next) {
		if (copy->host == spool_host)
			return (spool_host);
		if (host_get_loadav(copy->host, &loadav) !=
		    GFARM_ERR_NO_ERROR) {
			if (h == NULL) {
				h = copy->host;
				best_loadav = loadav;
			} else if (loadav < best_loadav) {
				h = copy->host;
				best_loadav = loadav;
			}
		}
	}
	return (h);
}

struct host *
inode_schedule_host_for_write(struct inode *inode, struct host *spool_host)
{
	struct inode_open_state *ios = inode->u.c.state;
	struct file_opening *fo;
	struct host *h = NULL;
	double loadav, best_loadav;
	int host_match = 0;

#ifdef __GNUC__ /* workaround gcc warning: might be used uninitialized */
	best_loadav = 0;
#endif
	if (!inode_is_file(inode))
		gflog_fatal("inode_schedule_host_for_write: not a file");
	if (ios == NULL) /* not opened */
		return (inode_schedule_host_for_read(inode, spool_host));
	fo = ios->openings.opening_next;
	if (fo == &ios->openings) /* not opened */
		return (inode_schedule_host_for_read(inode, spool_host));
	for (; fo != &ios->openings; fo = fo->opening_next) {
		if ((fo->flag & GFARM_FILE_ACCMODE) != GFARM_FILE_RDONLY)
			return (fo->u.f.spool_host);
		/* XXX better scheduling is needed */
		if (host_match)
			continue;
		else if (spool_host == fo->u.f.spool_host)
			host_match = 1;
		else if (host_get_loadav(fo->u.f.spool_host, &loadav) !=
		    GFARM_ERR_NO_ERROR) {
			if (h == NULL) {
				h = fo->u.f.spool_host;
				best_loadav = loadav;
			} else if (loadav < best_loadav) {
				h = fo->u.f.spool_host;
				best_loadav = loadav;
			}
		}
	}
	if (host_match)
		return (spool_host);
	if (h != NULL)
		return (h);
	return (inode_schedule_host_for_read(inode, spool_host));
}

gfarm_error_t dir_entry_init(void) { return (GFARM_ERR_NO_ERROR); }
gfarm_error_t file_copy_init(void) { return (GFARM_ERR_NO_ERROR); }
gfarm_error_t dead_file_copy_init(void) { return (GFARM_ERR_NO_ERROR); }
