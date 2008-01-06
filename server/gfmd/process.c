#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define GFARM_INTERNAL_USE
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

#include "gfutil.h"
#include "auth.h"
#include "gfm_proto.h"
#include "timespec.h"

#include "subr.h"
#include "peer.h"
#include "inode.h"
#include "process.h"
#include "id_table.h"
#include "host.h"

#define FILETAB_INITIAL		16
#define FILETAB_MULTIPLY	2
#define FILETAB_MAX		256

struct process_link {
	struct process_link *next, *prev;
};

/* XXX hack */
#define SIBLINGS_PTR_TO_PROCESS(sp) \
	((struct process *)((char *)(sp) - offsetof(struct process, siblings)))

struct process {
	struct process_link siblings;
	struct process_link children; /* dummy header of siblings list */
	struct process *parent;

	char sharedkey[GFM_PROTO_PROCESS_KEY_LEN_SHAREDSECRET];
	gfarm_pid_t pid;
	struct user *user;
	int refcount;

	int nfiles;
	struct file_opening **filetab;
};

static struct gfarm_id_table *process_id_table = NULL;
static struct gfarm_id_table_entry_ops process_id_table_ops = {
	sizeof(struct process)
};

static struct file_opening *
file_opening_alloc(struct inode *inode,
	struct peer *peer, struct host *spool_host, int flag)
{
	struct file_opening *fo;

	GFARM_MALLOC(fo);
	if (fo == NULL)
		return (NULL);
	fo->inode = inode;
	fo->flag = flag;
	fo->opener = peer;

	if (inode_is_file(inode)) {
		if (spool_host == NULL) {
			fo->u.f.spool_opener = NULL;
			fo->u.f.spool_host = NULL;
		} else {
			fo->u.f.spool_opener = peer;
			fo->u.f.spool_host = spool_host;
		}
	} else { /* for directory */
		fo->u.d.offset = 0;
		fo->u.d.key = NULL;
	}

	return (fo);
}

void
file_opening_free(struct file_opening *fo, int is_file)
{
	if (!is_file) { /* i.e. is a directory */
		if (fo->u.d.key != NULL)
			free(fo->u.d.key);
	}
	free(fo);
}

gfarm_error_t
process_alloc(struct user *user,
	gfarm_int32_t keytype, size_t keylen, char *sharedkey,
	struct process **processp, gfarm_pid_t *pidp)
{
	struct process *process;
	struct file_opening **filetab;
	int fd;
	gfarm_int32_t pid32;

	if (process_id_table == NULL) {
		process_id_table = gfarm_id_table_alloc(&process_id_table_ops);
		if (process_id_table == NULL)
			gflog_fatal("allocating pid table: no memory");
	}

	if (keytype != GFM_PROTO_PROCESS_KEY_TYPE_SHAREDSECRET ||
	    keylen != GFM_PROTO_PROCESS_KEY_LEN_SHAREDSECRET)
		return (GFARM_ERR_INVALID_ARGUMENT);
	GFARM_MALLOC_ARRAY(filetab, FILETAB_INITIAL);
	if (filetab == NULL)
		return (GFARM_ERR_NO_MEMORY);
	process = gfarm_id_alloc(process_id_table, &pid32);
	if (process == NULL) {
		free(filetab);
		return (GFARM_ERR_NO_MEMORY);
	}
	process->siblings.next = process->siblings.prev = &process->siblings;
	process->children.next = process->children.prev = &process->children;
	process->parent = NULL;
	memcpy(process->sharedkey, sharedkey, keylen);
	process->pid = pid32;
	process->user = user;
	process->refcount = 0;
	process->nfiles = FILETAB_INITIAL;
	process->filetab = filetab;
	for (fd = 0; fd < FILETAB_INITIAL; fd++)
		filetab[fd] = NULL;

	*processp = process;
	*pidp = pid32;
	return (GFARM_ERR_NO_ERROR);
}

static void
process_add_child(struct process *parent, struct process *child)
{
	child->siblings.next = &parent->children;
	child->siblings.prev = parent->children.prev;
	parent->children.prev->next = &child->siblings;
	parent->children.prev = &child->siblings;
	child->parent = parent;
}

static void
process_add_ref(struct process *process)
{
	++process->refcount;
}

static int
process_del_ref(struct process *process)
{
	int fd, is_file;
	struct file_opening *fo;
	struct process_link *pl, *pln;
	struct process *child;

	if (--process->refcount > 0)
		return (1); /* still referenced */

	/* make all children orphan */
	for (pl = process->children.next; pl != &process->children; pl = pln) {
		pln = pl->next;
		child = SIBLINGS_PTR_TO_PROCESS(pl);
		child->parent = NULL;
		pl->next = pl->prev = pl;
	}
	/* detach myself from children list */
	process->siblings.next->prev = process->siblings.prev;
	process->siblings.prev->next = process->siblings.next;

	for (fd = 0; fd < process->nfiles; fd++) {
		fo = process->filetab[fd];
		if (fo != NULL) {
			is_file = inode_is_file(fo->inode);
			inode_close_read(fo, NULL);
			file_opening_free(fo, is_file);
		}
	}
	free(process->filetab);
	gfarm_id_free(process_id_table, (gfarm_int32_t)process->pid);

	return (0); /* process freed */
}

void
process_attach_peer(struct process *process, struct peer *peer)
{
	process_add_ref(process);
	/* We are currently not using peer here */
}

void
process_detach_peer(struct process *process, struct peer *peer)
{
	int fd;

	if (!process_del_ref(process)) /* process freed */
		return;

	for (fd = 0; fd < process->nfiles; fd++) {
		/*
		 * XXX This shouldn't be done,
		 * if we'll support gfmd reconnection.
		 */
		process_close_file(process, peer, fd);
	}
}

struct user *
process_get_user(struct process *process)
{
	return (process->user);
}

gfarm_error_t
process_verify_fd(struct process *process, int fd)
{
	struct file_opening *fo;

	if (fd < 0 || fd >= process->nfiles)
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	fo = process->filetab[fd];
	if (fo == NULL)
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
process_get_file_opening(struct process *process, int fd,
	struct file_opening **fop)
{
	struct file_opening *fo;

	if (fd < 0 || fd >= process->nfiles)
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	fo = process->filetab[fd];
	if (fo == NULL)
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	*fop = fo;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
process_get_file_inode(struct process *process,	int fd, struct inode **inp)
{
	struct file_opening *fo;
	gfarm_error_t e = process_get_file_opening(process, fd, &fo);

	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	*inp = fo->inode;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
process_get_file_writable(struct process *process, struct peer *peer, int fd)
{
	struct file_opening *fo;
	gfarm_error_t e = process_get_file_opening(process, fd, &fo);

	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if ((accmode_to_op(fo->flag) & GFS_W_OK) == 0)
		return (GFARM_ERR_PERMISSION_DENIED);

	if (fo->opener == peer)
		return (GFARM_ERR_NO_ERROR);
	if (inode_is_file(fo->inode) && fo->u.f.spool_opener == peer)
		return (GFARM_ERR_NO_ERROR);
	return (GFARM_ERR_OPERATION_NOT_PERMITTED);
}

gfarm_error_t
process_get_dir_offset(struct process *process, struct peer *peer,
	int fd, gfarm_off_t *offsetp)
{
	struct file_opening *fo;
	gfarm_error_t e = process_get_file_opening(process, fd, &fo);

	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (!inode_is_dir(fo->inode))
		return (GFARM_ERR_NOT_A_DIRECTORY);
	if (fo->opener != peer)
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	*offsetp = fo->u.d.offset;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
process_set_dir_offset(struct process *process, struct peer *peer,
	int fd, gfarm_off_t offset)
{
	struct file_opening *fo;
	gfarm_error_t e = process_get_file_opening(process, fd, &fo);

	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (!inode_is_dir(fo->inode))
		return (GFARM_ERR_NOT_A_DIRECTORY);
	if (fo->opener != peer)
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	fo->u.d.offset = offset;
	return (GFARM_ERR_NO_ERROR);
}

/*
 * The reason why we provide fo->u.d.key (not only fo->u.d.offset) is
 * because fo->u.d.key is more robust with directory entry insertion/deletion.
 */

gfarm_error_t
process_get_dir_key(struct process *process, struct peer *peer,
	int fd, char **keyp, int *keylenp)
{
	struct file_opening *fo;
	gfarm_error_t e = process_get_file_opening(process, fd, &fo);

	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (!inode_is_dir(fo->inode))
		return (GFARM_ERR_NOT_A_DIRECTORY);
	if (fo->opener != peer)
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	if (fo->u.d.key == NULL) {
		*keyp = NULL;
		*keylenp = 0;
	} else {
		*keyp = fo->u.d.key;
		*keylenp = strlen(fo->u.d.key);
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
process_set_dir_key(struct process *process, struct peer *peer,
	int fd, char *key, int keylen)
{
	struct file_opening *fo;
	gfarm_error_t e = process_get_file_opening(process, fd, &fo);
	char *s;

	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (!inode_is_dir(fo->inode))
		return (GFARM_ERR_NOT_A_DIRECTORY);
	if (fo->opener != peer)
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);

	s = malloc(keylen + 1);
	if (s == NULL)
		return (GFARM_ERR_NO_MEMORY);
	memcpy(s, key, keylen);
	s[keylen] = '\0';

	if (fo->u.d.key != NULL)
		free(fo->u.d.key);
	fo->u.d.key = s;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
process_clear_dir_key(struct process *process, struct peer *peer, int fd)
{
	struct file_opening *fo;
	gfarm_error_t e = process_get_file_opening(process, fd, &fo);

	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (!inode_is_dir(fo->inode))
		return (GFARM_ERR_NOT_A_DIRECTORY);
	if (fo->opener != peer)
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);

	if (fo->u.d.key != NULL)
		free(fo->u.d.key);
	fo->u.d.key = NULL;
	return (GFARM_ERR_NO_ERROR);
}

struct process *
process_lookup(gfarm_pid_t pid)
{
	if (process_id_table == NULL)
		return (NULL);
	return (gfarm_id_lookup(process_id_table, (gfarm_int32_t)pid));
}

gfarm_error_t
process_does_match(gfarm_pid_t pid,
	gfarm_int32_t keytype, size_t keylen, char *sharedkey,
	struct process **processp)
{
	struct process *process = process_lookup((gfarm_int32_t)pid);

	if (process == NULL)
		return (GFARM_ERR_NO_SUCH_PROCESS);
	if (keytype != GFM_PROTO_PROCESS_KEY_TYPE_SHAREDSECRET ||
	    keylen != GFM_PROTO_PROCESS_KEY_LEN_SHAREDSECRET ||
	    memcmp(sharedkey, process->sharedkey,
	    GFM_PROTO_PROCESS_KEY_LEN_SHAREDSECRET) != 0)
		return (GFARM_ERR_AUTHENTICATION);
	*processp = process;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
process_open_file(struct process *process, struct inode *file,
	gfarm_int32_t flag, int created,
	struct peer *peer, struct host *spool_host,
	gfarm_int32_t *fdp)
{
	gfarm_error_t e;
	int fd, fd2;
	struct file_opening **p, *fo;

	/* XXX FIXME cache minimum unused fd, and avoid liner search */
	for (fd = 0; fd < process->nfiles; fd++) {
		if (process->filetab[fd] == NULL)
			break;
	}
	if (fd >= process->nfiles) {
		if (fd >= FILETAB_MAX)
			return (GFARM_ERR_TOO_MANY_OPEN_FILES);
		p = realloc(process->filetab,
		    sizeof(*p) * (process->nfiles * FILETAB_MULTIPLY));
		if (p == NULL)
			return (GFARM_ERR_NO_MEMORY);
		process->filetab = p;
		process->nfiles *= FILETAB_MULTIPLY;
		for (fd2 = fd + 1; fd2 < process->nfiles; fd2++)
			process->filetab[fd2] = NULL;
	}
	fo = file_opening_alloc(file, peer, spool_host,
	    flag | (created ? GFARM_FILE_CREATE : 0));
	if (fo == NULL)
		return (GFARM_ERR_NO_MEMORY);
	e = inode_open(fo);
	if (e != GFARM_ERR_NO_ERROR) {
		file_opening_free(fo, inode_is_file(file));
		return (e);
	}
	process->filetab[fd] = fo;
	
	*fdp = fd;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
process_reopen_file(struct process *process,
	struct peer *peer, struct host *spool_host, int fd,
	gfarm_ino_t *inump, gfarm_uint64_t *genp, gfarm_int32_t *modep,
	gfarm_int32_t *flagsp, gfarm_int32_t *to_createp)
{
	struct file_opening *fo;
	gfarm_error_t e = process_get_file_opening(process, fd, &fo);
	int to_create, is_creating_file_replica;

	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (!inode_is_file(fo->inode)) /* i.e. is a directory */
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	if (fo->u.f.spool_opener != NULL) /* already REOPENed */
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);

	to_create = inode_is_creating_file(fo->inode);
	is_creating_file_replica = (fo->flag & GFARM_FILE_CREATE_REPLICA) != 0;

	if ((accmode_to_op(fo->flag) & GFS_W_OK) != 0 || to_create) {
		if (is_creating_file_replica) {
			e = inode_add_replica(fo->inode, spool_host, 0);
			if (e != GFARM_ERR_NO_ERROR)
				return (e);
		}
		else if (!inode_schedule_confirm_for_write(fo->inode,
		    spool_host, to_create))
			return (GFARM_ERR_FILE_MIGRATED);
		if (to_create) {
			e = inode_add_replica(fo->inode, spool_host, 1);
			if (e != GFARM_ERR_NO_ERROR)
				return (e);
		}
	} else {
		if (!inode_has_replica(fo->inode, spool_host))
			return (GFARM_ERR_FILE_MIGRATED);
	}

	fo->u.f.spool_opener = peer;
	fo->u.f.spool_host = spool_host;
	*inump = inode_get_number(fo->inode);
	*genp = inode_get_gen(fo->inode);
	*modep = inode_get_mode(fo->inode);
	*flagsp = fo->flag & GFARM_FILE_USER_MODE;
	*to_createp = to_create || is_creating_file_replica;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
process_close_file(struct process *process, struct peer *peer, int fd)
{
	int is_file;
	struct file_opening *fo;
	gfarm_error_t e = process_get_file_opening(process, fd, &fo);

	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	is_file = inode_is_file(fo->inode);

	if (fo->opener != peer) {
		if (!is_file) /* i.e. is a directory */
			return (GFARM_ERR_OPERATION_NOT_PERMITTED);
		if (fo->u.f.spool_opener != peer)
			return (GFARM_ERR_OPERATION_NOT_PERMITTED);
		/* i.e. REOPENed file, and I am a gfsd. */
		if (fo->opener != NULL) {
			/*
			 * a gfsd is closing a REOPENed file,
			 * but the client is still opening it.
			 */
			fo->u.f.spool_opener = NULL;
			fo->u.f.spool_host = NULL;
			return (GFARM_ERR_NO_ERROR);
		}
	} else {
		if (is_file &&
		    fo->u.f.spool_opener != NULL &&
		    fo->u.f.spool_opener != peer) {
			/*
			 * a client is closing a file,
			 * but the gfsd is still opening it.
			 */
			fo->opener = NULL;
			return (GFARM_ERR_NO_ERROR);
			
		}
	}

	inode_close(fo);
	file_opening_free(fo, is_file);
	process->filetab[fd] = NULL;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
process_close_file_read(struct process *process, struct peer *peer, int fd,
	struct gfarm_timespec *atime)
{
	struct file_opening *fo;
	gfarm_error_t e = process_get_file_opening(process, fd, &fo);

	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (!inode_is_file(fo->inode))
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	if (fo->u.f.spool_opener != peer)
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);

	if (fo->opener != peer && fo->opener != NULL) {
		/* closing REOPENed file, but the client is still opening */
		fo->u.f.spool_opener = NULL;
		fo->u.f.spool_host = NULL;
		inode_set_atime(fo->inode, atime);
		return (GFARM_ERR_NO_ERROR);
	}

	inode_close_read(fo, atime);
	file_opening_free(fo, 1);
	process->filetab[fd] = NULL;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
process_close_file_write(struct process *process, struct peer *peer, int fd,
	gfarm_off_t size,
	struct gfarm_timespec *atime, struct gfarm_timespec *mtime)
{
	struct file_opening *fo;
	gfarm_error_t e = process_get_file_opening(process, fd, &fo);
	struct host *spool_host;

	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (!inode_is_file(fo->inode))
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	if (fo->u.f.spool_opener != peer)
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	if ((accmode_to_op(fo->flag) & GFS_W_OK) == 0)
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);

	if (fo->opener != peer && fo->opener != NULL) {
		spool_host = fo->u.f.spool_host;
		/* closing REOPENed file, but the client is still opening */
		fo->u.f.spool_opener = NULL;
		fo->u.f.spool_host = NULL;
		/*
		 * GFARM_FILE_CREATE_REPLICA means just to create a
		 * file replica.
		 */
		if ((fo->flag & GFARM_FILE_CREATE_REPLICA) != 0) {
			e = inode_add_replica(fo->inode, spool_host, 1);
			/* if this is not the first replica, return */
			if (e != GFARM_ERR_ALREADY_EXISTS)
				return (e);
		}
		else if (gfarm_timespec_cmp(inode_get_mtime(fo->inode), mtime))
			/* invalidate file replicas if updated */
			inode_remove_every_other_replicas(
				fo->inode, spool_host);

		inode_set_size(fo->inode, size);
		inode_set_atime(fo->inode, atime);
		inode_set_mtime(fo->inode, mtime);
		return (GFARM_ERR_NO_ERROR);
	}

	inode_close_write(fo, size, atime, mtime);
	file_opening_free(fo, 1);
	process->filetab[fd] = NULL;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
process_cksum_set(struct process *process, struct peer *peer, int fd,
	const char *cksum_type, size_t cksum_len, const char *cksum,
	gfarm_int32_t flags, struct gfarm_timespec *mtime)
{
	struct file_opening *fo;
	gfarm_error_t e = process_get_file_opening(process, fd, &fo);

	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (!inode_is_file(fo->inode))
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	if (fo->u.f.spool_opener != peer)
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	if ((accmode_to_op(fo->flag) & GFS_W_OK) == 0)
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);

	return (inode_cksum_set(fo, cksum_type, cksum_len, cksum,
	    flags, mtime));
}

gfarm_error_t
process_cksum_get(struct process *process, struct peer *peer, int fd,
	char **cksum_typep, size_t *cksum_lenp, char **cksump,
	gfarm_int32_t *flagsp)
{
	struct file_opening *fo;
	gfarm_error_t e = process_get_file_opening(process, fd, &fo);

	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (!inode_is_file(fo->inode))
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);

	return (inode_cksum_get(fo, cksum_typep, cksum_lenp, cksump,
	    flagsp));
}

gfarm_error_t
process_bequeath_fd(struct process *process, gfarm_int32_t fd)
{
	struct file_opening *fo;
	gfarm_error_t e = process_get_file_opening(process, fd, &fo);

	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	fo->flag |= GFARM_FILE_BEQUEATHED;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
process_inherit_fd(struct process *process, gfarm_int32_t parent_fd,
	struct peer *peer, struct host *spool_host, gfarm_int32_t *fdp)
{
	struct file_opening *fo;
	gfarm_error_t e;

	if (process->parent == NULL)
		return (GFARM_ERR_NO_SUCH_PROCESS);
	e = process_get_file_opening(process, parent_fd, &fo);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if ((fo->flag & GFARM_FILE_BEQUEATHED) == 0)
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	return (process_open_file(process, fo->inode, fo->flag,
	    (fo->flag & GFARM_FILE_CREATE) != 0, peer, spool_host, fdp));
}

gfarm_error_t
process_replica_adding(struct process *process,
	struct peer *peer, struct host *spool_host, char *src_host, int fd,
	gfarm_ino_t *inump, gfarm_uint64_t *genp,
	gfarm_int64_t *mtime_secp, gfarm_int32_t *mtime_nsecp)
{
	struct file_opening *fo;
	struct gfarm_timespec *mtime;
	gfarm_error_t e = process_get_file_opening(process, fd, &fo);

	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (!inode_is_file(fo->inode)) /* i.e. is a directory */
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	if (fo->u.f.spool_opener != NULL) /* already REOPENed */
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	if (inode_is_creating_file(fo->inode)) /* no file copy */
		return (GFARM_ERR_NO_SUCH_OBJECT);
	if (!inode_has_replica(fo->inode, host_lookup(src_host)))
		return (GFARM_ERR_NO_SUCH_OBJECT);
	if (inode_has_replica(fo->inode, spool_host))
		return (GFARM_ERR_ALREADY_EXISTS);

	e = inode_add_replica(fo->inode, spool_host, 0);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	fo->u.f.spool_opener = peer;
	fo->u.f.spool_host = spool_host;
	*inump = inode_get_number(fo->inode);
	*genp = inode_get_gen(fo->inode);
	mtime = inode_get_mtime(fo->inode);
	*mtime_secp = mtime->tv_sec;
	*mtime_nsecp = mtime->tv_nsec;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
process_replica_added(struct process *process,
	struct peer *peer, struct host *spool_host, int fd,
	int flags, gfarm_int64_t mtime_sec, gfarm_int32_t mtime_nsec)
{
	struct file_opening *fo;
	struct gfarm_timespec *mtime;
	gfarm_error_t e = process_get_file_opening(process, fd, &fo);

	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (!inode_is_file(fo->inode)) /* i.e. is a directory */
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	if (fo->u.f.spool_opener != peer)
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	if (inode_is_creating_file(fo->inode)) /* no file copy */
		return (GFARM_ERR_NO_SUCH_OBJECT);
	if (inode_has_replica(fo->inode, spool_host))
		return (GFARM_ERR_ALREADY_EXISTS);

	mtime = inode_get_mtime(fo->inode);
	if (mtime_sec != mtime->tv_sec || mtime_nsec != mtime->tv_nsec) {
		e = inode_remove_replica(fo->inode, spool_host);
		return (e == GFARM_ERR_NO_ERROR ?
			GFARM_ERR_INVALID_FILE_REPLICA : e);
	}
	return (inode_add_replica(fo->inode, spool_host, 1));
}

/*
 * protocol handler
 */

gfarm_error_t
gfm_server_process_alloc(struct peer *peer, int from_client, int skip)
{
	gfarm_int32_t e;
	struct user *user;
	gfarm_int32_t keytype;
	size_t keylen;
	char sharedkey[GFM_PROTO_PROCESS_KEY_LEN_SHAREDSECRET];
	struct process *process;
	gfarm_pid_t pid;

	e = gfm_server_get_request(peer, "process_alloc",
	    "ib", &keytype, sizeof(sharedkey), &keylen, sharedkey);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	giant_lock();
	if (peer_get_process(peer) != NULL) {
		e = GFARM_ERR_ALREADY_EXISTS;
	} else if (!from_client || (user = peer_get_user(peer)) == NULL) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = process_alloc(user, keytype, keylen, sharedkey,
	    &process, &pid)) == GFARM_ERR_NO_ERROR) {
		peer_set_process(peer, process);
	}
	giant_unlock();
	return (gfm_server_put_reply(peer, "process_alloc", e, "l", pid));
}

gfarm_error_t
gfm_server_process_alloc_child(struct peer *peer, int from_client, int skip)
{
	gfarm_int32_t e;
	struct user *user;
	gfarm_int32_t parent_keytype, keytype;
	size_t parent_keylen, keylen;
	char parent_sharedkey[GFM_PROTO_PROCESS_KEY_LEN_SHAREDSECRET];
	char sharedkey[GFM_PROTO_PROCESS_KEY_LEN_SHAREDSECRET];
	struct process *parent_process, *process;
	gfarm_pid_t parent_pid, pid;

	e = gfm_server_get_request(peer, "process_alloc_child", "iblib",
	    &parent_keytype,
	    sizeof(parent_sharedkey), &parent_keylen, parent_sharedkey,
	    &parent_pid,
	    &keytype, sizeof(sharedkey), &keylen, sharedkey);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	giant_lock();
	if (peer_get_process(peer) != NULL) {
		e = GFARM_ERR_ALREADY_EXISTS;
	} else if (!from_client || (user = peer_get_user(peer)) == NULL) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if (parent_keytype != GFM_PROTO_PROCESS_KEY_TYPE_SHAREDSECRET ||
	    parent_keylen != GFM_PROTO_PROCESS_KEY_LEN_SHAREDSECRET) {
		e = GFARM_ERR_INVALID_ARGUMENT;
	} else if ((e = process_does_match(parent_pid,
	    parent_keytype, parent_keylen, parent_sharedkey,
	    &parent_process)) != GFARM_ERR_NO_ERROR) {
		/* error */
	} else if ((e = process_alloc(user, keytype, keylen, sharedkey,
	    &process, &pid)) == GFARM_ERR_NO_ERROR) {
		peer_set_process(peer, process);
		process_add_child(parent_process, process);
	}
	giant_unlock();
	return (gfm_server_put_reply(peer, "process_alloc_child", e, "l",
	    pid));
}

gfarm_error_t
gfm_server_process_set(struct peer *peer, int from_client, int skip)
{
	gfarm_int32_t e;
	gfarm_pid_t pid;
	gfarm_int32_t keytype;
	size_t keylen;
	char sharedkey[GFM_PROTO_PROCESS_KEY_LEN_SHAREDSECRET];
	struct process *process;

	e = gfm_server_get_request(peer, "process_set",
	    "ibl", &keytype, sizeof(sharedkey), &keylen, sharedkey, &pid);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	giant_lock();
	if (peer_get_process(peer) != NULL) {
		e = GFARM_ERR_ALREADY_EXISTS;
	} else if (keytype != GFM_PROTO_PROCESS_KEY_TYPE_SHAREDSECRET ||
	    keylen != GFM_PROTO_PROCESS_KEY_LEN_SHAREDSECRET) {
		e = GFARM_ERR_INVALID_ARGUMENT;
	} else if ((e = process_does_match(pid, keytype, keylen, sharedkey,
	    &process)) == GFARM_ERR_NO_ERROR) {
		peer_set_process(peer, process);
		if (!from_client)
			peer_set_user(peer, process_get_user(process));
	}
	giant_unlock();
	return (gfm_server_put_reply(peer, "process_set", e, ""));
}

gfarm_error_t
gfm_server_process_free(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;

	if (skip)
		return (GFARM_ERR_NO_ERROR);
	
	giant_lock();
	if (peer_get_process(peer) == NULL)
		e = GFARM_ERR_NO_SUCH_PROCESS;
	else {
		peer_unset_process(peer);
		e = GFARM_ERR_NO_ERROR;
	}

	giant_unlock();
	return (gfm_server_put_reply(peer, "process_free", e, ""));
}

gfarm_error_t
gfm_server_bequeath_fd(struct peer *peer, int from_client, int skip)
{
	gfarm_int32_t e;
	struct host *spool_host;
	struct process *process;
	gfarm_int32_t fd;

	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if (!from_client && (spool_host = peer_get_host(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((process = peer_get_process(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR)
		;
	else
		e = process_bequeath_fd(process, fd);

	giant_unlock();
	return (gfm_server_put_reply(peer, "bequeath_fd", e, ""));
}

gfarm_error_t
gfm_server_inherit_fd(struct peer *peer, int from_client, int skip)
{
	gfarm_int32_t e;
	gfarm_int32_t parent_fd, fd;
	struct host *spool_host;
	struct process *process;

	e = gfm_server_get_request(peer, "inherit_fd", "i", &parent_fd);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if (!from_client && (spool_host = peer_get_host(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((process = peer_get_process(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((e = process_inherit_fd(process, parent_fd, peer, NULL,
	    &fd)) != GFARM_ERR_NO_ERROR)
		;
	else
		peer_fdpair_set_current(peer, fd);

	giant_unlock();
	return (gfm_server_put_reply(peer, "inherit_fd", e, ""));
}

