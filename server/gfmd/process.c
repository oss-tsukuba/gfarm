#include <stdlib.h>

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
	internal_ino_t cwd;

	int nfiles;
	struct file_opening **filetab;
};

static struct gfarm_id_table *process_id_table = NULL;
static struct gfarm_id_table_entry_ops process_id_table_ops = {
	sizeof(struct process)
};

gfarm_error_t
process_alloc(struct user *user,
	gfarm_int32_t keytype, size_t keylen, char *sharedkey,
	struct process **processp, gfarm_pid_t *pidp)
{
	gfarm_pid_t pid;
	struct process *process;
	struct file_opening **filetab;
	int fd;

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
	process = gfarm_id_alloc(process_id_table, &pid);
	if (process == NULL) {
		free(filetab);
		return (GFARM_ERR_NO_MEMORY);
	}
	memcpy(process->sharedkey, sharedkey, keylen);
	process->pid = pid;
	process->user = user;
	process->refcount = 0;
	process->cwd = 0;
	process->nfiles = FILETAB_INITIAL;
	process->filetab = filetab;
	for (fd = 0; fd < FILETAB_INITIAL; fd++)
		filetab[fd] = NULL;

	*processp = process;
	*pidp = pid;
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
		for (fd = 0; fd < process->nfiles; fd++) {
			fo = process->filetab[fd];
			if (fo != NULL) {
				inode_close_read(fo, NULL);
				free(fo);
				process->filetab[fd] = NULL;
			}
		}
		free(process->filetab);
		gfarm_id_free(process_id_table, process->pid);
	}
}

struct user *
process_get_user(struct process *process)
{
	return (process->user);
}

internal_ino_t
process_get_cwd(struct process *process)
{
	return (process->cwd);
}

gfarm_error_t
process_set_cwd(struct process *process, internal_ino_t cwd)
{
	struct inode *inode = inode_lookup(cwd);

	if (!GFARM_S_ISDIR(inode_get_mode(inode)))
		return (GFARM_ERR_NOT_A_DIRECTORY);
	process->cwd = cwd;
	return (GFARM_ERR_NO_ERROR);
}

struct process *
process_lookup(gfarm_pid_t pid)
{
	if (process_id_table == NULL)
		return (NULL);
	return (gfarm_id_lookup(process_id_table, pid));
}

gfarm_error_t
process_does_match(gfarm_pid_t pid,
	gfarm_int32_t keytype, size_t keylen, char *sharedkey,
	struct process **processp)
{
	struct process *process = process_lookup(pid);

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
process_open_file(struct process *process, internal_ino_t inum,
	gfarm_int32_t flag, struct host *spool_host, gfarm_int32_t *fdp)
{
	int fd, fd2;
	struct file_opening **p;

	for (fd = 0; fd < process->nfiles; fd++) {
		if (process->filetab == NULL)
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
	process->filetab[fd] = malloc(sizeof(*process->filetab[fd]));
	if (process->filetab[fd] == NULL)
		return (GFARM_ERR_NO_MEMORY);
	process->filetab[fd]->inum = 0;
	process->filetab[fd]->spool_host = spool_host;
	process->filetab[fd]->flag = flag;
	inode_open(process->filetab[fd]);
	*fdp = fd;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
process_close_file_read(struct process *process, struct host *spool_host,
	int fd, struct gfarm_timespec *atime)
{
	struct file_opening *fo;

	if (fd < 0 || fd >= process->nfiles)
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	fo = process->filetab[fd];
	if (fo == NULL || spool_host != fo->spool_host)
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	inode_close_read(fo, atime);
	free(fo);
	process->filetab[fd] = NULL;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
process_close_file_write(struct process *process, struct host *spool_host,
	int fd, struct gfarm_timespec *atime, struct gfarm_timespec *mtime)
{
	struct file_opening *fo;

	if (fd < 0 || fd >= process->nfiles)
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	fo = process->filetab[fd];
	if (fo == NULL || spool_host != fo->spool_host)
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	inode_close_write(fo, atime, mtime);
	free(fo);
	process->filetab[fd] = NULL;
	return (GFARM_ERR_NO_ERROR);
}

/*
 * protocol handler
 */

gfarm_error_t
gfm_server_process_alloc(struct peer *peer, int from_client)
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
	return (gfm_server_put_reply(peer, "process_alloc", e, "i", pid));
}

gfarm_error_t
gfm_server_process_set(struct peer *peer, int from_client)
{
	gfarm_int32_t e;
	gfarm_pid_t pid;
	gfarm_int32_t keytype;
	size_t keylen;
	char sharedkey[GFM_PROTO_PROCESS_KEY_LEN_SHAREDSECRET];
	struct process *process;

	e = gfm_server_get_request(peer, "process_set",
	    "iib", &pid, &keytype, sizeof(sharedkey), &keylen, sharedkey);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

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
gfm_server_process_free(struct peer *peer, int from_client)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("process_free", "not implemented");

	e = gfm_server_put_reply(peer, "process_free",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}
