#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

#include "gfutil.h"
#include "auth.h"
#include "gfm_proto.h"

#include "subr.h"
#include "peer.h"
#include "inode.h"
#include "process.h"
#include "id_table.h"

#define FILETAB_INITIAL	16
#define FILETAB_DELTA	16
#define FILETAB_MAX	256

struct process {
	char sharedkey[GFM_PROTO_PROCESS_KEY_LEN_SHAREDSECRET];
	gfarm_pid_t pid;
	struct user *user;
	int refcount;

	struct file_opening *cwd;

	int nfiles;
	struct file_opening **filetab;
};

static struct gfarm_id_table *process_id_table = NULL;
static struct gfarm_id_table_entry_ops process_id_table_ops = {
	sizeof(struct process)
};

static struct file_opening *
file_opening_alloc(struct inode *file, struct host *spool_host, int flag)
{
	struct file_opening *fo = malloc(sizeof(*fo));

	if (fo == NULL)
		return (NULL);
	fo->inode = file;
	fo->spool_host = spool_host;
	fo->flag = flag;

	/* for directory */
	fo->u.d.offset = 0;
	fo->u.d.key = NULL;

	return (fo);
}

void
file_opening_free(struct file_opening *fo)
{
	if (fo->u.d.key != NULL)
		free(fo->u.d.key);
	free(fo);
}

gfarm_error_t
process_alloc(struct user *user,
	gfarm_int32_t keytype, size_t keylen, char *sharedkey,
	struct process **processp, gfarm_pid_t *pidp)
{
	gfarm_pid_t pid;
	struct process *process;
	struct file_opening **filetab;
	int fd;
	gfarm_int32_t pid32;

	if (process_id_table == NULL) {
		process_id_table = gfarm_id_table_alloc(&process_id_table_ops);
		if (process_id_table == NULL)
			gflog_fatal("allocating pid table", "no memory");
	}

	if (keytype != GFM_PROTO_PROCESS_KEY_TYPE_SHAREDSECRET ||
	    keylen != GFM_PROTO_PROCESS_KEY_LEN_SHAREDSECRET)
		return (GFARM_ERR_INVALID_ARGUMENT);
	filetab = malloc(sizeof(*filetab) * FILETAB_INITIAL);
	if (filetab == NULL)
		return (GFARM_ERR_NO_MEMORY);
	process = gfarm_id_alloc(process_id_table, &pid32);
	if (process == NULL) {
		free(filetab);
		return (GFARM_ERR_NO_MEMORY);
	}
	memcpy(process->sharedkey, sharedkey, keylen);
	process->pid = pid32;
	process->user = user;
	process->refcount = 0;
	process->cwd = NULL;
	process->nfiles = FILETAB_INITIAL;
	process->filetab = filetab;
	for (fd = 0; fd < FILETAB_INITIAL; fd++)
		filetab[fd] = NULL;

	*processp = process;
	*pidp = pid32;
	return (GFARM_ERR_NO_ERROR);
}

void
process_add_ref(struct process *process)
{
	++process->refcount;
}

void
process_del_ref(struct process *process)
{
	int fd;
	struct file_opening *fo;

	if (--process->refcount <= 0) {
		fo = process->cwd;
		inode_close_read(fo, NULL);
		file_opening_free(fo);

		for (fd = 0; fd < process->nfiles; fd++) {
			fo = process->filetab[fd];
			if (fo != NULL) {
				inode_close_read(fo, NULL);
				file_opening_free(fo);
			}
		}
		free(process->filetab);

		gfarm_id_free(process_id_table, (gfarm_int32_t)process->pid);
	}
}

struct user *
process_get_user(struct process *process)
{
	return (process->user);
}

struct inode *
process_get_cwd(struct process *process)
{
	if (process->cwd == NULL)
		return (NULL);
	return (process->cwd->inode);
}

gfarm_error_t
process_set_cwd(struct process *process, struct inode *cwd)
{
	struct file_opening *fo;

	if (!GFARM_S_ISDIR(inode_get_mode(cwd)))
		return (GFARM_ERR_NOT_A_DIRECTORY);
	fo = file_opening_alloc(cwd, NULL, GFARM_FILE_RDONLY);
	if (fo == NULL)
		return (GFARM_ERR_NO_MEMORY);
	inode_open(fo);
	if (process->cwd != NULL) {
		inode_close_read(process->cwd, NULL);
		file_opening_free(process->cwd);
	}
	process->cwd = fo;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
process_get_file_opening(struct process *process, struct host *spool_host,
	int fd, struct file_opening **fop)
{
	struct file_opening *fo;

	if (fd < 0 || fd >= process->nfiles)
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	fo = process->filetab[fd];
	if (fo == NULL || spool_host != fo->spool_host)
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	*fop = fo;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
process_get_file_inode(struct process *process, struct host *spool_host,
	int fd, struct inode **inp)
{
	struct file_opening *fo;
	gfarm_error_t e = process_get_file_opening(process, spool_host, fd,
	    &fo);

	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	*inp = fo->inode;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
process_get_file_writable(struct process *process, struct host *spool_host,
	int fd)
{
	struct file_opening *fo;
	gfarm_error_t e = process_get_file_opening(process, spool_host, fd,
	    &fo);

	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	switch (fo->flag & GFARM_FILE_ACCMODE) {
	case GFARM_FILE_RDONLY:	return (GFARM_ERR_PERMISSION_DENIED);
	case GFARM_FILE_WRONLY:	
	case GFARM_FILE_RDWR:	return (GFARM_ERR_NO_ERROR);
	default:		assert(0); return (GFARM_ERR_UNKNOWN);
	}
	
}

gfarm_error_t
process_get_dir_offset(struct process *process, struct host *spool_host,
	int fd, gfarm_off_t *offsetp)
{
	struct file_opening *fo;
	gfarm_error_t e = process_get_file_opening(process, spool_host, fd,
	    &fo);

	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (!inode_is_dir(fo->inode))
		return (GFARM_ERR_NOT_A_DIRECTORY);
	*offsetp = fo->u.d.offset;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
process_set_dir_offset(struct process *process, struct host *spool_host,
	int fd, gfarm_off_t offset)
{
	struct file_opening *fo;
	gfarm_error_t e = process_get_file_opening(process, spool_host, fd,
	    &fo);

	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (!inode_is_dir(fo->inode))
		return (GFARM_ERR_NOT_A_DIRECTORY);
	fo->u.d.offset = offset;
	return (GFARM_ERR_NO_ERROR);
}

/*
 * The reason why we provide fo->u.d.key (not only fo->u.d.offset) is
 * because fo->u.d.key is more robust with directory entry insertion/deletion.
 */

gfarm_error_t
process_get_dir_key(struct process *process, struct host *spool_host,
	int fd, char **keyp, int *keylenp)
{
	struct file_opening *fo;
	gfarm_error_t e = process_get_file_opening(process, spool_host, fd,
	    &fo);

	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (!inode_is_dir(fo->inode))
		return (GFARM_ERR_NOT_A_DIRECTORY);
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
process_set_dir_key(struct process *process, struct host *spool_host,
	int fd, char *key, int keylen)
{
	struct file_opening *fo;
	gfarm_error_t e = process_get_file_opening(process, spool_host, fd,
	    &fo);
	char *s;

	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (!inode_is_dir(fo->inode))
		return (GFARM_ERR_NOT_A_DIRECTORY);

	s = malloc(keylen + 1);
	if (s == NULL)
		return (GFARM_ERR_NO_MEMORY);
	memcpy(s, key, keylen + 1);
	s[keylen] = '\0';

	if (fo->u.d.key != NULL)
		free(fo->u.d.key);
	fo->u.d.key = s;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
process_clear_dir_key(struct process *process, struct host *spool_host, int fd)
{
	struct file_opening *fo;
	gfarm_error_t e = process_get_file_opening(process, spool_host, fd,
	    &fo);

	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (!inode_is_dir(fo->inode))
		return (GFARM_ERR_NOT_A_DIRECTORY);

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
	gfarm_int32_t flag, struct host *spool_host, gfarm_int32_t *fdp)
{
	int fd, fd2;
	struct file_opening **p, *fo;

	for (fd = 0; fd < process->nfiles; fd++) {
		if (process->filetab[fd] == NULL)
			break;
	}
	if (fd >= process->nfiles) {
		if (fd >= FILETAB_MAX)
			return (GFARM_ERR_TOO_MANY_OPEN_FILES);
		p = realloc(process->filetab,
		    sizeof(*p) * (process->nfiles + FILETAB_DELTA));
		if (p == NULL)
			return (GFARM_ERR_NO_MEMORY);
		process->filetab = p;
		process->nfiles += FILETAB_DELTA;
		for (fd2 = fd + 1; fd2 < process->nfiles; fd2++)
			process->filetab[fd2] = NULL;
	}
	fo = file_opening_alloc(file, spool_host, flag);
	if (fo == NULL)
		return (GFARM_ERR_NO_MEMORY);
	process->filetab[fd] = fo;
	inode_open(fo);
	*fdp = fd;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
process_close_file_read(struct process *process, struct host *spool_host,
	int fd, struct gfarm_timespec *atime)
{
	struct file_opening *fo;
	gfarm_error_t e = process_get_file_opening(process, spool_host, fd,
	    &fo);

	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	inode_close_read(fo, atime);
	file_opening_free(fo);
	process->filetab[fd] = NULL;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
process_close_file_write(struct process *process, struct host *spool_host,
	int fd, gfarm_off_t size,
	struct gfarm_timespec *atime, struct gfarm_timespec *mtime)
{
	struct file_opening *fo;
	gfarm_error_t e = process_get_file_opening(process, spool_host, fd,
	    &fo);

	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	inode_close_write(fo, size, atime, mtime);
	file_opening_free(fo);
	process->filetab[fd] = NULL;
	return (GFARM_ERR_NO_ERROR);
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

	/* XXX - NOT IMPLEMENTED */
	gflog_error("process_free", "not implemented");

	e = gfm_server_put_reply(peer, "process_free",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}
