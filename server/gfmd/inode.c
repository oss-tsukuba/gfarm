#include <stdlib.h>
#include <sys/time.h>

#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

#include "gfutil.h"
#include "hash.h"

#include "host.h"
#include "user.h"
#include "process.h" /* struct file_opening */
#include "inode.h"

#define ROOT_INUMBER			2
#define INODE_TABLE_SIZE_INITIAL	1000
#define INODE_TABLE_SIZE_DELTA		1000

#define DIR_HASH_SIZE 53 /* prime */

struct file_copy {
	struct file_copy *host_next;
	struct host *host;
};

struct inode {
	gfarm_ino_t i_number;
	gfarm_uint64_t i_gen;
	gfarm_mode_t i_mode;
	gfarm_uint64_t i_nlink;
	struct user *i_user;
	struct group *i_group;
	gfarm_off_t i_size;
	gfarm_uint64_t i_ncopy;
	struct gfarm_timespec i_atimespec;
	struct gfarm_timespec i_mtimespec;
	struct gfarm_timespec i_ctimespec;

	union {
		struct inode_free_link {
			struct inode *prev, *next;
		} l;
		struct inode_file {
			struct file_copy *copies;
			struct file_opening openings;
		} f;
		struct inode_dir {
			struct gfarm_hash_table *dir_entries;
		} d;
	} u;
};

struct inode *inode_table = NULL;
int inode_table_size = 0;
struct inode inode_free_list;

gfarm_error_t
inode_init(void)
{
	/* XXX FIXME */
	return (GFARM_ERR_NO_ERROR);
}

void
inode_free_list_init(void)
{
	inode_free_list.u.l.prev =
	inode_free_list.u.l.next = &inode_free_list;
}

struct inode *
inode_alloc_num(gfarm_ino_t inum)
{
	int i;
	struct inode *inode;

	if (inum < ROOT_INUMBER)
		return (NULL); /* we don't use 0 and 1 as i_number */
	if (inode_table_size < inum) {
		int new_table_size;
		struct inode *p;

		if (inum < INODE_TABLE_SIZE_INITIAL)
			new_table_size = INODE_TABLE_SIZE_INITIAL;
		else if (inum < inode_table_size + inode_table_size)
			new_table_size = inode_table_size + inode_table_size;
		else 
			new_table_size = inum + INODE_TABLE_SIZE_DELTA;
		p = realloc(inode_table, sizeof(*p) * new_table_size);
		if (p == NULL)
			return (NULL); /* no memory */
		inode_table = p;

		if (inode_table_size == 0) {
			inode_free_list_init();
			/* we don't use 0 and 1 as i_number */
			inode_table_size = ROOT_INUMBER;
		}
		for (i = inode_table_size; i < new_table_size; i++) {
			inode = &inode_table[i];
			inode->i_number = i;
			inode->i_gen = 0;
			inode->i_mode = 0; /* the inode is free */
			inode->i_nlink = 0;
			inode->u.l.prev = &inode_table[i - 1];
			inode->u.l.next = &inode_table[i + 1];
		}
		inode_table[inode_table_size].u.l.prev =
			&inode_free_list;
		inode_table[new_table_size - 1].u.l.next =
			inode_free_list.u.l.next;
		inode_free_list.u.l.next->u.l.prev =
			&inode_table[new_table_size - 1];
		inode_free_list.u.l.next =
			&inode_table[inode_table_size];

		inode_table_size = new_table_size;
	}
	inode = &inode_table[inum];
	if (inode->i_mode != 0) /* the inode is not free */
		return (NULL);
	/* remove from inode_free_list */
	inode->u.l.next->u.l.prev = inode->u.l.prev;
	inode->u.l.prev->u.l.next = inode->u.l.next;
	return (inode);
}

struct inode *
inode_alloc(void)
{
	if (inode_table_size == 0)
		return (inode_alloc_num(ROOT_INUMBER));
	if (inode_free_list.u.l.next != &inode_free_list)
		return (inode_alloc_num(inode_free_list.u.l.next->i_number));
	else
		return (inode_alloc_num(inode_table_size));
}

void
inode_free(struct inode *inode)
{
	inode->i_gen++;
	inode->i_mode = 0; /* the inode is free */
	inode->i_nlink = 0;
	/* add to inode_free_list */
	inode->u.l.prev = &inode_free_list;
	inode->u.l.next = inode_free_list.u.l.next;
	inode->u.l.next->u.l.prev = inode;
	inode_free_list.u.l.next = inode;
}

void
inode_remove(struct inode *inode)
{
	if (inode_is_file(inode)) {
		struct file_copy *copy;
		gfarm_error_t e;

		if (inode->u.f.openings.opening_next != &inode->u.f.openings)
			gflog_fatal("inode_remove", "still opened");
		for (copy = inode->u.f.copies; copy != NULL;
		    copy = copy->host_next) {
			e = host_remove_replica(copy->host, inode->i_number);
			if (e != GFARM_ERR_NO_ERROR)
				gflog_error("host_remove_replica",
				    host_name(copy->host));
		}
	} else if (inode_is_dir(inode)) {
		gfarm_hash_table_free(inode->u.d.dir_entries);
	} else {
		gflog_fatal("inode_unlink", "unknown inode type");
		/*NOTREACHED*/
	}
	inode_free(inode);
}

int
inode_init_dir(struct inode *inode, struct inode *parent)
{
	static char dot[] = ".";
	static char dotdot[] = "..";
	struct gfarm_hash_entry *entry;

	inode->u.d.dir_entries = gfarm_hash_table_alloc(DIR_HASH_SIZE,
	    gfarm_hash_default, gfarm_hash_key_equal_default);
	if (inode->u.d.dir_entries == NULL)
		return (0);

	entry = gfarm_hash_enter(inode->u.d.dir_entries, dot, sizeof(dot)-1,
	    sizeof(struct inode *), NULL);
	if (entry == NULL) {
		gfarm_hash_table_free(inode->u.d.dir_entries);
		return (0);
	}
	*(struct inode **)gfarm_hash_entry_data(entry) = inode;

	entry = gfarm_hash_enter(inode->u.d.dir_entries, dot, sizeof(dotdot)-1,
	    sizeof(struct inode *), NULL);
	if (entry == NULL) {
		gfarm_hash_table_free(inode->u.d.dir_entries);
		return (0);
	}
	*(struct inode **)gfarm_hash_entry_data(entry) = parent;

	inode->i_nlink = 2;
	inode->i_mode = GFARM_S_IFDIR;
	return (1);
}

int
inode_init_file(struct inode *inode)
{
	inode->i_nlink = 1;
	inode->i_mode = GFARM_S_IFREG;
	inode->u.f.copies = NULL;
	inode->u.f.openings.opening_prev =
	inode->u.f.openings.opening_next = &inode->u.f.openings;
	return (1);
}

struct inode *
inode_lookup(gfarm_ino_t inum)
{
	struct inode *inode;

	if (inum >= inode_table_size)
		return (NULL);
	inode = &inode_table[inum];
	if (inode->i_mode == 0) /* the inode is free */
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

struct user *
inode_get_user(struct inode *inode)
{
	return (inode->i_user);
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
	INODE_REMOVE
};

/* if (op != INODE_CREATE), (is_dir) may be -1, and that means "don't care". */
static gfarm_error_t
inode_lookup_basename(struct inode *parent, const char *name, int len,
	int is_dir, enum gfarm_inode_lookup_op op, struct user *user,
	struct inode **inp, int *createdp)
{
	gfarm_error_t e;
	struct gfarm_hash_entry *entry;
	int created;
	struct inode **np, *n;

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
	if (op != INODE_CREATE) {
		entry = gfarm_hash_lookup(parent->u.d.dir_entries, name, len);
		if (entry == NULL)
			return (GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY);
		if (op == INODE_REMOVE) {
			if ((e = inode_access(parent, user, GFS_W_OK)) !=
			    GFARM_ERR_NO_ERROR) {
				return (e);
			}
			*inp = *(struct inode **)gfarm_hash_entry_data(entry);
			*createdp = 0;
			gfarm_hash_purge(parent->u.d.dir_entries, name, len);
			return (GFARM_ERR_NO_ERROR);
		}
		*inp = *(struct inode **)gfarm_hash_entry_data(entry);
		*createdp = 0;
		return (GFARM_ERR_NO_ERROR);
	}

	entry = gfarm_hash_enter(parent->u.d.dir_entries, name, len,
	    sizeof(struct inode *), &created);
	if (entry == NULL)
		return (GFARM_ERR_NO_MEMORY);
	np = gfarm_hash_entry_data(entry);
	if (!created) {
		*inp = *np;
		*createdp = 0;
		return (GFARM_ERR_NO_ERROR);
	}
	if ((e = inode_access(parent, user, GFS_W_OK)) != GFARM_ERR_NO_ERROR) {
		gfarm_hash_purge(parent->u.d.dir_entries, name, len);
		return (e);
	}
	n = inode_alloc();
	if (n == NULL ||
	    !(is_dir ? inode_init_dir(n, parent) : inode_init_file(n))) {
		gfarm_hash_purge(parent->u.d.dir_entries, name, len);
		if (n != NULL)
			inode_free(n); /* XXX i_gen is incremented */
		return (GFARM_ERR_NO_MEMORY);
	}
	n->i_user = user;
	n->i_group = parent->i_group;
	n->i_size = 0;
	n->i_ncopy = 0;
	{
		struct timeval now;

		gettimeofday(&now, NULL);
		n->i_atimespec.tv_sec = n->i_mtimespec.tv_sec =
		    n->i_ctimespec.tv_sec = now.tv_sec;
		n->i_atimespec.tv_nsec = n->i_mtimespec.tv_nsec =
		    n->i_ctimespec.tv_nsec = now.tv_usec * 1000;
	}
	*inp = *np = n;
	*createdp = 1;
	return (GFARM_ERR_NO_ERROR);
}

/* if (op != INODE_CREATE), (is_dir) may be -1, and that means "don't care". */
gfarm_error_t
inode_lookup_relative(struct inode *n, char *path,
	int is_dir, enum gfarm_inode_lookup_op op,
	struct user *user, struct inode **inp, int *createdp)
{
	gfarm_error_t e;
	int len, tmp;

	for (;;) {
		if ((e = inode_access(n, user, GFS_X_OK))
		    != GFARM_ERR_NO_ERROR)
			return (e);
		while (*path == '/')
			path++;
		for (len = 0; path[len] != '/'; len++) {
			if (path[len] == '\0') {
				e = inode_lookup_basename(n, path, len,
				    is_dir, op, user, &n, createdp);
				if (e != GFARM_ERR_NO_ERROR)
					return (e);
				if (is_dir != -1 && inode_is_dir(n) != is_dir)
					return (is_dir ?
					    GFARM_ERR_NOT_A_DIRECTORY :
					    GFARM_ERR_IS_A_DIRECTORY);
				*inp = n;
				return (GFARM_ERR_NO_ERROR);
			}
		}
		e = inode_lookup_basename(n, path, len, 1, INODE_LOOKUP, user,
		    &n, &tmp);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
		if (!inode_is_dir(n))
			return (GFARM_ERR_NOT_A_DIRECTORY);
		path += len;
	}
}

/* XXX TODO: namei cache */
/* if (op != INODE_CREATE), (is_dir) may be -1, and that means "don't care". */
gfarm_error_t
inode_lookup_by_name_internal(char *path, struct process *process,
	int is_dir, enum gfarm_inode_lookup_op op,
	struct inode **inp, int *createdp)
{
	struct user *user = process_get_user(process);
	struct inode *cwd;

	/* assert(user != NULL); */
	if (path[0] == '/')
		cwd = inode_lookup(ROOT_INUMBER);
	else
		cwd = process_get_cwd(process);
	if (cwd == NULL)
		return (GFARM_ERR_STALE_FILE_HANDLE);
	return (inode_lookup_relative(cwd, path, is_dir, op, user,
	    inp, createdp));
}

gfarm_error_t
inode_lookup_by_name(char *path, struct process *process, struct inode **inp)
{
	int tmp;

	return (inode_lookup_by_name_internal(path, process, -1, INODE_LOOKUP,
	    inp, &tmp));
}

gfarm_error_t
inode_create_file(char *path, struct process *process, gfarm_mode_t mode,
	struct inode **inp, int *createdp)
{
	return (inode_lookup_by_name_internal(path, process, 0, INODE_CREATE,
	    inp, createdp));
}

gfarm_error_t
inode_unlink(char *path, struct process *process)
{
	int tmp;
	struct inode *inode;
	gfarm_error_t e = inode_lookup_by_name(path, process, &inode);

	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (inode_is_file(inode)) {
		e = inode_lookup_by_name_internal(path, process,
		    0, INODE_REMOVE, &inode, &tmp);
		if (--inode->i_nlink > 0)
			return (GFARM_ERR_UNKNOWN);
		if (inode->u.f.openings.opening_next == &inode->u.f.openings) {
			/* no process is opening this file, just remove it */
			inode_remove(inode);
			return (GFARM_ERR_NO_ERROR);
		} else {
			/* there are some processes which open this file */
			/* leave this inode until closed */
			return (GFARM_ERR_NO_ERROR);
		}
	} else if (inode_is_dir(inode)) {
		if (inode->i_nlink > 2)
			return (GFARM_ERR_DIRECTORY_NOT_EMPTY);
		e = inode_lookup_by_name_internal(path, process,
		    0, INODE_REMOVE, &inode, &tmp);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
		inode_remove(inode);
		return (GFARM_ERR_NO_ERROR);
	} else {
		gflog_fatal("inode_unlink", "unknown inode type");
		/*NOTREACHED*/
		return (GFARM_ERR_UNKNOWN);
	}
}

gfarm_error_t
inode_add_replica(struct inode *inode, struct host *spool_host)
{
	struct file_copy *copy;

	for (copy = inode->u.f.copies; copy != NULL; copy = copy->host_next) {
		if (copy->host == spool_host)
			return (GFARM_ERR_ALREADY_EXISTS);
	}
	copy = malloc(sizeof(*copy));
	if (copy == NULL)
		return (GFARM_ERR_NO_MEMORY);
	copy->host_next = inode->u.f.copies;
	inode->u.f.copies = copy;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
inode_remove_replica(struct inode *inode, struct host *spool_host)
{
	struct file_copy **copyp, *copy;

	for (copyp = &inode->u.f.copies; (copy = *copyp) != NULL;
	    copyp = &copy->host_next) {
		if (copy->host == spool_host) {
			*copyp = copy->host_next;
			free(copy);
			return (GFARM_ERR_NO_ERROR);
		}
	}
	return (GFARM_ERR_NO_SUCH_OBJECT);
}

void
inode_open(struct file_opening *fo)
{
	struct inode *inode = fo->inode;

	fo->opening_prev = &inode->u.f.openings;
	fo->opening_next = inode->u.f.openings.opening_next;
	inode->u.f.openings.opening_next = fo;
	fo->opening_next->opening_prev = fo;
}

void
inode_close_read(struct file_opening *fo, struct gfarm_timespec *atime)
{
	struct inode *inode = fo->inode;

	fo->opening_prev->opening_next = fo->opening_next;
	fo->opening_next->opening_prev = fo->opening_prev;
	inode->i_atimespec = *atime;
	if (inode->i_nlink == 0 &&
	    inode->u.f.openings.opening_next == &inode->u.f.openings)
		inode_remove(inode);
}

void
inode_close_write(struct file_opening *fo,
	struct gfarm_timespec *atime, struct gfarm_timespec *mtime)
{
	struct inode *inode = fo->inode;

	inode->i_mtimespec = *mtime;
	inode_close_read(fo, atime);
}

int
inode_has_replica(struct inode *inode, struct host *spool_host)
{
	struct file_copy *copy;

	if (!inode_is_file(inode))
		gflog_fatal("inode_has_replica", "not a file");
	for (copy = inode->u.f.copies; copy != NULL; copy = copy->host_next) {
		if (copy->host == spool_host)
			return (1);
	}
	return (0);
}

struct host *
inode_schedule_spool_host(struct inode *inode, struct host *spool_host)
{
	struct file_copy *copy;
	struct host *h = NULL;
	double loadav, best_loadav;

	for (copy = inode->u.f.copies; copy != NULL; copy = copy->host_next) {
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
	struct file_opening *fo;
	struct host *h = NULL;
	double loadav, best_loadav;
	int host_match = 0;

	if (!inode_is_file(inode))
		gflog_fatal("inode_schedule_host_for_write", "not a file");
	fo = inode->u.f.openings.opening_next;
	if (fo == &inode->u.f.openings)
		return (inode_schedule_spool_host(inode, spool_host));
	for (; fo != &inode->u.f.openings; fo = fo->opening_next) {
		if ((fo->flag & GFARM_FILE_ACCMODE) != GFARM_FILE_RDONLY)
			return (fo->spool_host);
		/* XXX better scheduling is needed */
		if (spool_host == fo->spool_host)
			host_match = 1;
		if (host_get_loadav(fo->spool_host, &loadav) !=
		    GFARM_ERR_NO_ERROR) {
			if (h == NULL) {
				h = fo->spool_host;
				best_loadav = loadav;
			} else if (loadav < best_loadav) {
				h = fo->spool_host;
				best_loadav = loadav;
			}
		}
	}
	if (host_match)
		return (spool_host);
	if (h != NULL)
		return (h);
	return (inode_schedule_spool_host(inode, spool_host));
}

gfarm_error_t dir_entry_init(void) { return (GFARM_ERR_NO_ERROR); }
gfarm_error_t file_copy_init(void) { return (GFARM_ERR_NO_ERROR); }
gfarm_error_t dead_file_copy_init(void) { return (GFARM_ERR_NO_ERROR); }
