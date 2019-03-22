#include <pthread.h>	/* db_access.h currently needs this */
#include <stdarg.h>
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>

#define GFARM_INTERNAL_USE
#include <gfarm/gflog.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

#include "gfutil.h"
#include "id_table.h"

#include "gfp_xdr.h"
#include "auth.h"
#include "gfm_proto.h"
#include "gfs_proto.h"
#include "timespec.h"
#include "config.h"

#include "subr.h"
#include "rpcsubr.h"
#include "db_access.h"
#include "peer.h"
#include "user.h"
#include "inode.h"
#include "process.h"
#include "host.h"

#define FILETAB_INITIAL		16
#define FILETAB_MULTIPLY	2

#define PROCESS_ID_MIN			300
#define PROCESS_TABLE_INITIAL_SIZE	100


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

struct client_initiated_replication {
	gfarm_int64_t gen;
	struct host *dst;
	gfarm_int32_t cksum_request_flags;
};

struct file_opening *
file_opening_alloc(struct inode *inode,
	struct peer *peer, struct host *spool_host, int flag)
{
	struct file_opening *fo;

	GFARM_MALLOC(fo);
	if (fo == NULL) {
		gflog_debug(GFARM_MSG_1001593,
			"allocation of 'file_opening' failed");
		return (NULL);
	}
	fo->inode = inode;
	fo->flag = flag;
	fo->opener = peer;
	fo->gen = inode_get_gen(inode);

	if (inode_is_file(inode)) {
		if (spool_host == NULL) {
			fo->u.f.spool_opener = NULL;
			fo->u.f.spool_host = NULL;
		} else {
			fo->u.f.spool_opener = peer;
			fo->u.f.spool_host = spool_host;
		}
		fo->u.f.replica_spec.desired_number = 0;
		fo->u.f.replica_spec.repattr = NULL;
		fo->u.f.replica_source = NULL;
	} else if (inode_is_dir(inode)) {
		fo->u.d.offset = 0;
		fo->u.d.key = NULL;
	}

	fo->path_for_trace_log = NULL;

	return (fo);
}

void
replica_spec_free(struct replica_spec *spec)
{
	free(spec->repattr);
}

void
file_opening_free(struct file_opening *fo, gfarm_mode_t mode)
{
	if (GFARM_S_ISREG(mode)) {
		if (fo->u.f.replica_source != NULL) {
			gflog_debug(GFARM_MSG_1002236,
			    "file replication (%lld:%lld) to %s is canceled",
			    (long long)inode_get_number(fo->inode),
			    (long long)fo->u.f.replica_source->gen,
			    host_name(fo->u.f.replica_source->dst));
			inode_remove_replica_incomplete(fo->inode,
			    fo->u.f.replica_source->dst,
			    fo->u.f.replica_source->gen);
			free(fo->u.f.replica_source);
			fo->u.f.replica_source = NULL;
		}
		replica_spec_free(&fo->u.f.replica_spec);
	} else if (GFARM_S_ISDIR(mode))
		free(fo->u.d.key);
	free(fo->path_for_trace_log);
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
			gflog_fatal(GFARM_MSG_1000293,
			    "allocating pid table: no memory");
		gfarm_id_table_set_base(process_id_table, PROCESS_ID_MIN);
		gfarm_id_table_set_initial_size(process_id_table,
		    PROCESS_TABLE_INITIAL_SIZE);
	}

	if (keytype != GFM_PROTO_PROCESS_KEY_TYPE_SHAREDSECRET ||
	    keylen != GFM_PROTO_PROCESS_KEY_LEN_SHAREDSECRET) {
		gflog_debug(GFARM_MSG_1001594,
			"'keytype' or 'keylen' is invalid");
		return (GFARM_ERR_INVALID_ARGUMENT);
	}
	GFARM_MALLOC_ARRAY(filetab, FILETAB_INITIAL);
	if (filetab == NULL) {
		gflog_debug(GFARM_MSG_1001595,
			"allocation of 'filetab' failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	process = gfarm_id_alloc(process_id_table, &pid32);
	if (process == NULL) {
		free(filetab);
		gflog_debug(GFARM_MSG_1001596,
			"gfarm_id_alloc() failed");
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

static gfarm_error_t process_close_or_abort_file(struct process *,
	struct peer *, int, char **, int, const char *);

/* NOTE: caller of this function should acquire giant_lock as well */
static int
process_del_ref(struct process *process, struct peer *peer)
{
	int fd;
	gfarm_mode_t mode;
	struct file_opening *fo;
	struct process_link *pl, *pln;
	struct process *child;
	const char diag[] = "process_del_ref";

	for (fd = 0; fd < process->nfiles; fd++) {
		fo = process->filetab[fd];
		if (fo != NULL) {
			mode = inode_get_mode(fo->inode);
			if (fo->opener == peer ||
			    (inode_is_file(fo->inode) &&
			     fo->u.f.spool_opener == peer)) {
				process_close_or_abort_file(
				    process, peer, fd, NULL, 1, diag);
			}
		}
	}

	if (--process->refcount > 0)
		return (1); /* still referenced */

	/* sanity check: process terminated, make sure all files are closed */
	for (fd = 0; fd < process->nfiles; fd++) {
		fo = process->filetab[fd];
		if (fo != NULL) {
			mode = inode_get_mode(fo->inode);
			/* sanity check: this shouldn't happen */
			gflog_warning(GFARM_MSG_1004466,
			    "%s: minor internal error "
			    "pid:%lld fd:%d inode:%llu:%llu (mode:0%o) "
			    "remains opened, closed by %s@%s, spool is %s, "
			    "NOTE: GFM_PROTO_REVOKE_GFSD_ACCESS is%s called",
			    diag, (long long)process->pid, (int)fd,
			    (long long)inode_get_number(fo->inode),
			    (long long)inode_get_gen(fo->inode), (int)mode,
			    peer_get_username(peer), peer_get_hostname(peer),
			    fo->u.f.spool_opener == NULL ? "closed already" :
			    peer_get_hostname(fo->u.f.spool_opener),
			    (fo->flag & GFARM_FILE_GFSD_ACCESS_REVOKED) != 0 ?
			    "" : " not");
			inode_close_read(fo, NULL, NULL, diag);
			file_opening_free(fo, mode);
		}
	}
	free(process->filetab);

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

	gfarm_id_free(process_id_table, (gfarm_int32_t)process->pid);

	return (0); /* process freed */
}

/* NOTE: caller of this function should acquire giant_lock as well */
void
process_attach_peer(struct process *process, struct peer *peer)
{
	process_add_ref(process);
	/* We are currently not using peer here */
}

/*
 * NOTE:
 * - caller of this function should acquire giant_lock as well
 * - caller of this function SHOULD call db_begin()/db_end() around this
 */
void
process_detach_peer(struct process *process, struct peer *peer,
	const char *diag)
{
	(void)process_del_ref(process, peer);
}

gfarm_pid_t
process_get_pid(struct process *process)
{
	return (process->pid);
}

struct user *
process_get_user(struct process *process)
{
	return (process->user);
}

gfarm_error_t
process_verify_fd(struct process *process, struct peer *peer, int fd,
	const char *diag)
{
	struct file_opening *fo;

	if (0 <= fd && fd < process->nfiles) {
		fo = process->filetab[fd];
		if (fo != NULL)
			return (GFARM_ERR_NO_ERROR);
	}
	gflog_info(GFARM_MSG_1003804,
	    "%s: pid:%lld fd:%d by %s@%s: bad file descriptor",
	    diag, (long long)process->pid, (int)fd,
	    peer_get_username(peer), peer_get_hostname(peer));
	return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
}

gfarm_error_t
process_get_file_opening(struct process *process, struct peer *peer, int fd,
	struct file_opening **fop, const char *diag)
{
	struct file_opening *fo;

	if (0 <= fd && fd < process->nfiles) {
		fo = process->filetab[fd];
		if (fo != NULL) {
			*fop = fo;
			return (GFARM_ERR_NO_ERROR);
		}
	}
	gflog_info(GFARM_MSG_1003805,
	    "%s: pid:%lld fd:%d by %s@%s: bad file descriptor",
	    diag, (long long)process->pid, (int)fd,
	    peer_get_username(peer), peer_get_hostname(peer));
	return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
}

gfarm_error_t
process_record_replica_spec(struct process *process, struct peer *peer, int fd,
	int desired_number, char *repattr, const char *diag)
{
	struct file_opening *fo;
	gfarm_error_t e = process_get_file_opening(process, peer, fd, &fo,
	    diag);
	char *repattr_str = (repattr == NULL) ? "" : repattr;

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_warning(GFARM_MSG_1004011,
		    "process_record_replica_spec(%ld,%d,%d,'%s'): %s",
		    (long)process->pid, fd, desired_number, repattr_str,
		    gfarm_error_string(e));
		return (e);
	}
	if (!inode_is_file(fo->inode)) {
		gflog_warning(GFARM_MSG_1004012,
		    "process_record_replica_spec(%ld,%d,%d,'%s'): not a file",
		    (long)process->pid, fd, desired_number, repattr_str);
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	}
	fo->u.f.replica_spec.desired_number = desired_number;
	/*
	 * The repattr must be malloc'd. It will be free'd in
	 * file_opening_free().
	 */
	fo->u.f.replica_spec.repattr = repattr;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
process_get_file_inode(struct process *process,	struct peer *peer, int fd,
	struct inode **inp, const char *diag)
{
	struct file_opening *fo;
	gfarm_error_t e = process_get_file_opening(process, peer, fd,
	    &fo, diag);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001601,
			"process_get_file_opening() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	*inp = fo->inode;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
process_get_file_writable(struct process *process, struct peer *peer, int fd,
	const char *diag)
{
	struct file_opening *fo;
	gfarm_error_t e = process_get_file_opening(process, peer, fd,
	    &fo, diag);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001602,
			"process_get_file_opening() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if ((accmode_to_op(fo->flag) & GFS_W_OK) == 0) {
		gflog_debug(GFARM_MSG_1001603,
			"permission is denied");
		return (GFARM_ERR_PERMISSION_DENIED);
	}

	if (fo->opener == peer)
		return (GFARM_ERR_NO_ERROR);
	if (inode_is_file(fo->inode) && fo->u.f.spool_opener == peer)
		return (GFARM_ERR_NO_ERROR);
	gflog_debug(GFARM_MSG_1001604,
		"operation is not permitted");
	return (GFARM_ERR_OPERATION_NOT_PERMITTED);
}

gfarm_error_t
process_get_dir_offset(struct process *process, struct peer *peer,
	int fd, gfarm_off_t *offsetp, const char *diag)
{
	struct file_opening *fo;
	gfarm_error_t e = process_get_file_opening(process, peer, fd,
	    &fo, diag);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001605,
			"process_get_file_opening() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (!inode_is_dir(fo->inode)) {
		gflog_debug(GFARM_MSG_1001606,
			"inode is not a directory");
		return (GFARM_ERR_NOT_A_DIRECTORY);
	}
	if (fo->opener != peer) {
		gflog_debug(GFARM_MSG_1001607,
			"operation is not permitted");
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	}
	*offsetp = fo->u.d.offset;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
process_set_dir_offset(struct process *process, struct peer *peer,
	int fd, gfarm_off_t offset, const char *diag)
{
	struct file_opening *fo;
	gfarm_error_t e = process_get_file_opening(process, peer, fd,
	    &fo, diag);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001608,
			"process_get_file_opening() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (!inode_is_dir(fo->inode)) {
		gflog_debug(GFARM_MSG_1001609,
			"inode is not a directory");
		return (GFARM_ERR_NOT_A_DIRECTORY);
	}
	if (fo->opener != peer) {
		gflog_debug(GFARM_MSG_1001610,
			"operation is not permitted");
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	}
	fo->u.d.offset = offset;
	return (GFARM_ERR_NO_ERROR);
}

/*
 * The reason why we provide fo->u.d.key (not only fo->u.d.offset) is
 * because fo->u.d.key is more robust with directory entry insertion/deletion.
 */

gfarm_error_t
process_get_dir_key(struct process *process, struct peer *peer,
	int fd, char **keyp, int *keylenp, const char *diag)
{
	struct file_opening *fo;
	gfarm_error_t e = process_get_file_opening(process, peer, fd,
	    &fo, diag);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001611,
			"process_get_file_opening() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (!inode_is_dir(fo->inode)) {
		gflog_debug(GFARM_MSG_1001612,
			"inode is not a directory");
		return (GFARM_ERR_NOT_A_DIRECTORY);
	}
	if (fo->opener != peer) {
		gflog_debug(GFARM_MSG_1001613,
			"operation is not permitted");
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	}
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
	int fd, char *key, int keylen, const char *diag)
{
	struct file_opening *fo;
	gfarm_error_t e = process_get_file_opening(process, peer, fd,
	    &fo, diag);
	char *s;

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001614,
			"process_get_file_opening() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (!inode_is_dir(fo->inode)) {
		gflog_debug(GFARM_MSG_1001615,
			"inode is not a directory");
		return (GFARM_ERR_NOT_A_DIRECTORY);
	}
	if (fo->opener != peer) {
		gflog_debug(GFARM_MSG_1001616,
			"operation is not permitted");
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	}

	s = malloc(keylen + 1);
	if (s == NULL) {
		gflog_debug(GFARM_MSG_1001617,
			"allocation of string failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	memcpy(s, key, keylen);
	s[keylen] = '\0';

	if (fo->u.d.key != NULL)
		free(fo->u.d.key);
	fo->u.d.key = s;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
process_clear_dir_key(struct process *process, struct peer *peer, int fd,
	const char *diag)
{
	struct file_opening *fo;
	gfarm_error_t e = process_get_file_opening(process, peer, fd,
	    &fo, diag);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001618,
			"process_get_file_opening() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (!inode_is_dir(fo->inode)) {
		gflog_debug(GFARM_MSG_1001619,
			"inode is not a directory");
		return (GFARM_ERR_NOT_A_DIRECTORY);
	}
	if (fo->opener != peer) {
		gflog_debug(GFARM_MSG_1001620,
			"operation is not permitted");
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	}

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

	if (process == NULL) {
		gflog_debug(GFARM_MSG_1001621,
			"process_lookup() failed");
		return (GFARM_ERR_NO_SUCH_PROCESS);
	}
	if (keytype != GFM_PROTO_PROCESS_KEY_TYPE_SHAREDSECRET ||
	    keylen != GFM_PROTO_PROCESS_KEY_LEN_SHAREDSECRET ||
	    memcmp(sharedkey, process->sharedkey,
	    GFM_PROTO_PROCESS_KEY_LEN_SHAREDSECRET) != 0) {
		gflog_debug(GFARM_MSG_1001622,
			"authentication failed");
		return (GFARM_ERR_AUTHENTICATION);
	}
	*processp = process;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
process_new_generation_wait(struct peer *peer, int fd,
	gfarm_error_t (*action)(struct peer *, void *, int *), void *arg,
	const char *diag)
{
	struct process *process;
	struct file_opening *fo;
	gfarm_error_t e;

	if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1002237,
		    "%s: peer_get_process() failed", diag);
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = process_get_file_opening(process, peer, fd, &fo, diag))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002238,
		    "%s: process_get_file_opening(%d) failed", diag, fd);
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else {
		e = inode_new_generation_wait(fo->inode, peer, action, arg);
	}
	return (e);
}

gfarm_error_t
process_new_generation_done(struct process *process, struct peer *peer, int fd,
	gfarm_int32_t result, const char *diag)
{
	struct file_opening *fo;
	gfarm_mode_t mode;
	gfarm_error_t e = process_get_file_opening(process, peer, fd,
	    &fo, diag);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002325,
		    "%s: pid %lld descriptor %d: %s", diag,
		    (long long)process->pid, fd, gfarm_error_string(e));
		return (e);
	} else if ((e = inode_new_generation_by_fd_finish(fo->inode, peer,
	    result)) == GFARM_ERR_NO_ERROR) {

		/* resume deferred operaton: close the file */
		peer_reset_pending_new_generation_by_fd(peer);

		if (fo->opener != peer && fo->opener != NULL) {
			/*
			 * closing REOPENed file,
			 * but the client is still opening
			 */
			fo->u.f.spool_opener = NULL;
			fo->u.f.spool_host = NULL;
		} else {
			mode = inode_get_mode(fo->inode);
			inode_close(fo, NULL, diag);

			file_opening_free(fo, mode);
			process->filetab[fd] = NULL;
		}
	}
	return (e);
}

gfarm_error_t
process_open_file(struct process *process, struct inode *file,
	gfarm_int32_t flag, int created,
	struct peer *peer, struct host *spool_host, struct dirset *tdirset,
	gfarm_int32_t *fdp)
{
	gfarm_error_t e;
	int fd, fd2, new_nfiles;
	struct file_opening **p, *fo;

	/* XXX FIXME cache minimum unused fd, and avoid liner search */
	for (fd = 0; fd < process->nfiles; fd++) {
		if (process->filetab[fd] == NULL)
			break;
	}
	if (fd >= process->nfiles) {
		if (fd >= gfarm_max_open_files) {
			gflog_debug(GFARM_MSG_1001623,
				"too many open files");
			return (GFARM_ERR_TOO_MANY_OPEN_FILES);
		}
		new_nfiles = process->nfiles * FILETAB_MULTIPLY;
		if (new_nfiles > gfarm_max_open_files)
			new_nfiles = gfarm_max_open_files;
		p = realloc(process->filetab, sizeof(*p) * new_nfiles);
		if (p == NULL) {
			gflog_debug(GFARM_MSG_1001624,
				"re-allocation of 'process' failed");
			return (GFARM_ERR_NO_MEMORY);
		}
		process->filetab = p;
		process->nfiles = new_nfiles;
		for (fd2 = fd + 1; fd2 < process->nfiles; fd2++)
			process->filetab[fd2] = NULL;
	}
	fo = file_opening_alloc(file, peer, spool_host,
	    flag | (created ? GFARM_FILE_CREATE : 0));
	if (fo == NULL) {
		gflog_debug(GFARM_MSG_1001625,
			"file_opening_alloc() failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	e = inode_open(fo, tdirset);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001626,
			"inode_open() failed: %s",
			gfarm_error_string(e));
		file_opening_free(fo, inode_get_mode(file));
		return (e);
	}
	process->filetab[fd] = fo;

	*fdp = fd;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
process_schedule_file(struct process *process, struct peer *peer, int fd,
	gfarm_int32_t *np, struct host ***hostsp, const char *diag)
{
	struct file_opening *fo;
	gfarm_error_t e = process_get_file_opening(process, peer, fd,
	    &fo, diag);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001861,
		    "process_get_file_opening() failed: %s",
		    gfarm_error_string(e));
	} else if (!inode_is_file(fo->inode)) {
		gflog_debug(GFARM_MSG_1001862,
			"inode is not file");
		e = GFARM_ERR_OPERATION_NOT_SUPPORTED;
	} else {
		return (inode_schedule_file(fo, peer, np, hostsp));
	}

	assert(e != GFARM_ERR_NO_ERROR);
	return (e);
}

gfarm_error_t
process_reopen_file(struct process *process,
	struct peer *peer, struct host *spool_host, int fd,
	gfarm_ino_t *inump, gfarm_uint64_t *genp, gfarm_int32_t *modep,
	gfarm_int32_t *flagsp, gfarm_int32_t *to_createp, const char *diag)
{
	struct file_opening *fo;
	gfarm_error_t e = process_get_file_opening(process, peer, fd,
	    &fo, diag);
	int no_replica, to_create, is_creating_file_replica;

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001627,
			"process_get_file_opening() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (!inode_is_file(fo->inode)) { /* i.e. is a directory */
		gflog_debug(GFARM_MSG_1001628,
			"inode is not file");
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	}
	if (fo->u.f.spool_opener != NULL || fo->u.f.spool_host != NULL) {
		/* already REOPENed */
		gflog_debug(GFARM_MSG_1001629,
			"file already reopened");
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	}
	if (inode_new_generation_is_pending(fo->inode)) {
		/* wait until the generation is updated */
		gflog_debug(GFARM_MSG_1002240,
		    "process_reopen_file: new_generation pending %lld:%lld",
		    (long long)inode_get_number(fo->inode),
		    (long long)inode_get_gen(fo->inode));
		return (GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE);
	}
	no_replica = inode_has_no_replica(fo->inode);
	if (no_replica &&
	    (fo->flag & GFARM_FILE_TRUNC) == 0 &&
	    inode_get_size(fo->inode) > 0) {
		gflog_error(GFARM_MSG_1003474,
		    "(%llu:%llu, %llu): lost all replicas",
		    (unsigned long long)inode_get_number(fo->inode),
		    (unsigned long long)inode_get_gen(fo->inode),
		    (unsigned long long)inode_get_size(fo->inode));
		return (GFARM_ERR_STALE_FILE_HANDLE);
	}

	to_create = 0;
	is_creating_file_replica = (fo->flag & GFARM_FILE_CREATE_REPLICA) != 0;

	if ((accmode_to_op(fo->flag) & GFS_W_OK) != 0 || no_replica) {
		if (is_creating_file_replica) {
			e = inode_add_replica(fo->inode, spool_host, 0);
			if (e != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1001630,
					"inode_add_replica() failed: %s",
					gfarm_error_string(e));
				return (e);
			}
		} else if ((e = inode_schedule_confirm_for_write(
		    fo, spool_host, &to_create)) != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001631, "%s",
			    gfarm_error_string(e));
			return (e);
		}
		if (to_create) {
			e = inode_add_replica(fo->inode, spool_host, 1);
			if (e != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1001632,
					"inode_add_replica() failed: %s",
					gfarm_error_string(e));
				return (e);
			}
		}
	} else {
		if (!inode_has_replica(fo->inode, spool_host)) {
			gflog_debug(GFARM_MSG_1001633,
				"file migrated");
			return (GFARM_ERR_FILE_MIGRATED);
		}
	}

	fo->u.f.spool_opener = peer;
	fo->u.f.spool_host = spool_host;
	fo->flag &= ~GFARM_FILE_TRUNC_PENDING; /*spool_host will truncate it*/
	if ((accmode_to_op(fo->flag) & GFS_W_OK) != 0)
		inode_add_ref_spool_writers(fo->inode);
	*inump = inode_get_number(fo->inode);
	*genp = inode_get_gen(fo->inode);
	*modep = inode_get_mode(fo->inode);
	*flagsp = fo->flag & GFARM_FILE_USER_MODE;
	*to_createp = to_create || is_creating_file_replica;
	return (GFARM_ERR_NO_ERROR);
}

static int
process_peer_is_the_spool_opener(struct process *process,
	struct peer *peer, int fd, struct file_opening *fo, gfarm_mode_t mode,
	const char *diag)
{
	/* if this request is valid, peer must be gfsd */

	if (!GFARM_S_ISREG(mode)) { /* non-opener only can open a file */
		/*
		 * use "info" level, altough this is a bad file descriptor,
		 * this may be caused by GFM_PROTO_REVOKE_GFSD_ACCESS.
		 * do not check GFARM_FILE_GFSD_ACCESS_REVOKED, because
		 * the revoked descriptor must be already closed in this case.
		 */
		gflog_info(GFARM_MSG_1003752,
		    "%s: pid:%lld fd:%d inode:%llu:%llu "
		    "not a file (0%o) by %s@%s",
		    diag, (long long)process->pid, (int)fd,
		    (long long)inode_get_number(fo->inode),
		    (long long)inode_get_gen(fo->inode), (int)mode,
		    peer_get_username(peer), peer_get_hostname(peer));
		return (0);
	}
	if (peer != fo->u.f.spool_opener) { /* peer is not the spool opener */
		gflog_info(GFARM_MSG_1003806,
		    "%s: pid:%lld fd:%d inode:%llu:%llu "
		    "invalid request by %s@%s, should be %s, "
		    "NOTE: GFM_PROTO_REVOKE_GFSD_ACCESS is%s called",
		    diag, (long long)process->pid, (int)fd,
		    (long long)inode_get_number(fo->inode),
		    (long long)inode_get_gen(fo->inode),
		    peer_get_username(peer), peer_get_hostname(peer),
		    fo->u.f.spool_opener == NULL ? "closed already" :
		    peer_get_hostname(fo->u.f.spool_opener),
		    (fo->flag & GFARM_FILE_GFSD_ACCESS_REVOKED) != 0 ?
		    "" : " not");
		return (0);
	}
	return (1);
}

gfarm_error_t
process_getgen(struct process *process, struct peer *peer, int fd,
	gfarm_uint64_t *genp, const char *diag)
{
	struct file_opening *fo;
	gfarm_error_t e = process_get_file_opening(process, peer, fd,
	    &fo, diag);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1004701,
			"process_get_file_opening() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	*genp = fo->gen;
	return (GFARM_ERR_NO_ERROR);
}
static gfarm_error_t
process_close_or_abort_file(struct process *process, struct peer *peer, int fd,
	char **trace_logp, int aborted, const char *diag)
{
	struct file_opening *fo;
	gfarm_mode_t mode;
	gfarm_error_t e = process_get_file_opening(process, peer, fd,
	    &fo, diag);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001634,
			"process_get_file_opening() failed: %s",
			gfarm_error_string(e));
		return (e);
	}

	mode = inode_get_mode(fo->inode);

	if (fo->opener != peer) {
		if (!process_peer_is_the_spool_opener(process, peer, fd, fo,
		    mode, diag))
			return (GFARM_ERR_OPERATION_NOT_PERMITTED);

		/* i.e. REOPENed file, and I am a gfsd. */
		if ((accmode_to_op(fo->flag) & GFS_W_OK) != 0) {
			if (aborted) {
				gflog_warning(GFARM_MSG_1004354,
				    "gfsd on %s@%s exited"
				    " without closing write-opened file"
				    " (pid:%lld fd:%d). inode %llu:%llu"
				    " might be modified, run gfspooldigest",
				    peer_get_username(peer),
				    peer_get_hostname(peer),
				    (long long)process->pid, (int)fd,
				    (long long)inode_get_number(fo->inode),
				    (long long)inode_get_gen(fo->inode));
			}
			inode_del_ref_spool_writers(fo->inode);
			inode_check_pending_replication(fo);
		}
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
		if ((accmode_to_op(fo->flag) & GFS_W_OK) != 0 && aborted) {
			gflog_info(GFARM_MSG_1005068,
			    "(%s@%s) aborted without closing a write-opened "
			    "file %llu:%llu",
			    peer_get_username(peer), peer_get_hostname(peer),
			    (unsigned long long)inode_get_number(fo->inode),
			    (unsigned long long)inode_get_gen(fo->inode));
		}
		if (GFARM_S_ISREG(mode) &&
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

	inode_close(fo, trace_logp, diag);
	file_opening_free(fo, mode);
	process->filetab[fd] = NULL;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
process_close_file(struct process *process, struct peer *peer, int fd,
	char **trace_logp, const char *diag)
{
	return (process_close_or_abort_file(process, peer, fd, trace_logp, 0,
	    diag));
}

gfarm_error_t
process_close_file_read(struct process *process, struct peer *peer, int fd,
	struct gfarm_timespec *atime, const char *diag)
{
	struct file_opening *fo;
	gfarm_mode_t mode;
	gfarm_error_t e = process_get_file_opening(process, peer, fd,
	    &fo, diag);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001637,
			"process_get_file_opening() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	mode = inode_get_mode(fo->inode);
	if (!process_peer_is_the_spool_opener(process, peer, fd, fo,
	    mode, diag))
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);

	if ((accmode_to_op(fo->flag) & GFS_W_OK) != 0) {
		inode_del_ref_spool_writers(fo->inode);
		inode_check_pending_replication(fo);
	}
	if (fo->opener != peer && fo->opener != NULL) {
		/* closing REOPENed file, but the client is still opening */
		fo->u.f.spool_opener = NULL;
		fo->u.f.spool_host = NULL;
		inode_set_relatime(fo->inode, atime);
		return (GFARM_ERR_NO_ERROR);
	}

	inode_close_read(fo, atime, NULL, diag);
	file_opening_free(fo, mode);
	process->filetab[fd] = NULL;
	return (GFARM_ERR_NO_ERROR);
}

/*
 * if called from GFM_PROTO_CLOSE_WRITE_4:
 *	flagsp, inump, old_genp, new_genp, trace_logp are all NULL.
 * if called from GFM_PROTO_CLOSE_WRITE_V2_4
 *	flagsp, inump, old_genp, new_genp, trace_logp are all not NULL.
 */
gfarm_error_t
process_close_file_write(struct process *process, struct peer *peer, int fd,
	gfarm_off_t size,
	struct gfarm_timespec *atime, struct gfarm_timespec *mtime,
	gfarm_int32_t *flagsp, gfarm_ino_t *inump,
	gfarm_int64_t *old_genp, gfarm_int64_t *new_genp, char **trace_logp,
	const char *diag)
{
	struct file_opening *fo;
	gfarm_mode_t mode;
	gfarm_error_t e = process_get_file_opening(process, peer, fd,
	    &fo, diag);
	gfarm_int32_t flags = 0;
	int is_v2_4 = (flagsp != NULL);

	/*
	 * NOTE: gfsd uses CLOSE_FILE_WRITE protocol only if the file is
	 * really updated.
	 */

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001640,
			"process_get_file_opening() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	mode = inode_get_mode(fo->inode);
	if (!process_peer_is_the_spool_opener(process, peer, fd, fo,
	    mode, diag))
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);

	if ((accmode_to_op(fo->flag) & GFS_W_OK) == 0) {
		gflog_debug(GFARM_MSG_1001643,
			"bad file descriptor");
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	}
	if (is_v2_4 && inode_new_generation_is_pending(fo->inode)) {
		gflog_debug(GFARM_MSG_1002241,
		    "%s: new_generation pending %lld:%lld", diag,
		    (long long)inode_get_number(fo->inode),
		    (long long)inode_get_gen(fo->inode));
		return (GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE);
	}

	inode_del_ref_spool_writers(fo->inode);
	if ((is_v2_4 || inode_is_updated(fo->inode, mtime)) &&

	    /*
	     * GFARM_FILE_CREATE_REPLICA flag means to create and add a
	     * file replica by gfs_pio_write if this file already has file
	     * replicas.  GFARM_ERR_ALREADY_EXISTS error means this is the
	     * first one and this file has only one replica.  If it is
	     * not, do not change the status.
	     */
	    ((fo->flag & GFARM_FILE_CREATE_REPLICA) == 0 ||
	    inode_add_replica(fo->inode, fo->u.f.spool_host, 1)
	    == GFARM_ERR_ALREADY_EXISTS) &&
	    inode_file_update(fo, size, atime, mtime, is_v2_4,
	    old_genp, new_genp, trace_logp, diag)) {
		flags = GFM_PROTO_CLOSE_WRITE_GENERATION_UPDATE_NEEDED;
	}

	if (inump != NULL)
		*inump = inode_get_number(fo->inode);

	if ((flags & GFM_PROTO_CLOSE_WRITE_GENERATION_UPDATE_NEEDED) != 0) {
		/* defer file close for GFM_PROTO_GENERATION_UPDATED */
		inode_new_generation_by_fd_start(fo->inode, peer);
		peer_set_pending_new_generation_by_fd(peer, fo->inode);
	} else if (fo->opener != peer && fo->opener != NULL) {
		/* closing REOPENed file, but the client is still opening */
		fo->u.f.spool_opener = NULL;
		fo->u.f.spool_host = NULL;
	} else {
		inode_close(fo, NULL, diag);

		file_opening_free(fo, mode);
		process->filetab[fd] = NULL;
	}
	if (flagsp != NULL)
		*flagsp = flags;

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
process_cksum_set(struct process *process, struct peer *peer, int fd,
	const char *cksum_type, size_t cksum_len, const char *cksum,
	gfarm_int32_t flags, struct gfarm_timespec *mtime, const char *diag)
{
	struct file_opening *fo;
	gfarm_error_t e = process_get_file_opening(process, peer, fd,
	    &fo, diag);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001644,
			"process_get_file_opening() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (!inode_is_file(fo->inode)) {
		e = GFARM_ERR_NOT_A_REGULAR_FILE;
		gflog_info(GFARM_MSG_1003754, "%s: inode %lld: %s",
		    diag, (long long)inode_get_number(fo->inode),
		    gfarm_error_string(e));
		return (e);
	}
	e = file_opening_cksum_set(fo, cksum_type, cksum_len, cksum,
	    flags, mtime);
	if (e != GFARM_ERR_NO_ERROR && e != GFARM_ERR_EXPIRED) {
		gflog_notice(GFARM_MSG_1004322,
		    "(%s@%s) %s: inode %lld:%lld for %s-open%s: %s",
		    peer_get_username(peer), peer_get_hostname(peer), diag,
		    (long long)inode_get_number(fo->inode),
		    (long long)inode_get_gen(fo->inode),
		    accmode_to_string(fo->flag),
		    (flags & GFM_PROTO_CKSUM_SET_REPORT_ONLY) != 0 ?
		    " (report only)" : "",
		    gfarm_error_string(e));
	}
	return (e);
}

gfarm_error_t
process_cksum_get(struct process *process, struct peer *peer, int fd,
	char **cksum_typep, size_t *cksum_lenp, char **cksump,
	gfarm_int32_t *flagsp, const char *diag)
{
	struct file_opening *fo;
	gfarm_error_t e = process_get_file_opening(process, peer, fd,
	    &fo, diag);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001648,
			"process_get_file_opening() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (!inode_is_file(fo->inode)) {
		gflog_debug(GFARM_MSG_1001649,
			"inode is not file");
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	}

	return (file_opening_cksum_get(fo, cksum_typep, cksum_lenp, cksump,
	    flagsp));
}

gfarm_error_t
process_bequeath_fd(struct process *process, struct peer *peer,
	gfarm_int32_t fd, const char *diag)
{
	struct file_opening *fo;
	gfarm_error_t e = process_get_file_opening(process, peer, fd,
	    &fo, diag);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001650,
			"process_get_file_opening() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	fo->flag |= GFARM_FILE_BEQUEATHED;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
process_inherit_fd(struct process *process, gfarm_int32_t parent_fd,
	struct peer *peer, struct host *spool_host, gfarm_int32_t *fdp,
	const char *diag)
{
	struct file_opening *fo;
	gfarm_error_t e;

	if (process->parent == NULL) {
		gflog_debug(GFARM_MSG_1001651,
			"process->parent does not exist");
		return (GFARM_ERR_NO_SUCH_PROCESS);
	}
	e = process_get_file_opening(process, peer, parent_fd, &fo, diag);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001652,
			"process_get_file_opening() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if ((fo->flag & GFARM_FILE_BEQUEATHED) == 0) {
		gflog_debug(GFARM_MSG_1001653,
			"bad file descriptor");
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	}
	return (process_open_file(process, fo->inode, fo->flag,
	    (fo->flag & GFARM_FILE_CREATE) != 0, peer, spool_host,
	    inode_get_tdirset(fo->inode), fdp));
}

static gfarm_error_t
process_prepare_to_replicate(struct process *process, struct peer *peer,
	struct host *src, struct host *dst, int fd, gfarm_int32_t flags,
	struct file_replicating **frp, struct inode **inodep, const char *diag)
{
	gfarm_error_t e;
	struct file_opening *fo;
	struct user *user;

	if ((e = process_get_file_opening(process, peer, fd, &fo, diag))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001654,
			"process_get_file_opening() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (fo->u.f.spool_opener != NULL) { /* already REOPENed */
		gflog_debug(GFARM_MSG_1001655,
			"operation is not permitted, already reopened");
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	}
	if ((user = process_get_user(process)) == NULL) {
		gflog_debug(GFARM_MSG_1001656,
			"process_get_user() failed");
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	}

	e = inode_prepare_to_replicate(fo->inode, user, src, dst, flags, frp);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001657,
			"inode_prepare_to_replicate() failed: %s",
			gfarm_error_string(e));
		return (e);
	}

	*inodep = fo->inode;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
process_replication_request(struct process *process, struct peer *peer,
	struct host *src, struct host *dst, int fd, gfarm_int32_t flags,
	const char *diag)
{
	gfarm_error_t e;
	struct inode *inode;
	struct file_replicating *fr;

	if ((e = process_prepare_to_replicate(process, peer, src, dst,
	    fd, flags, &fr, &inode, diag)) == GFARM_ERR_NO_ERROR)
		e = inode_replication_request(inode, fr, diag);
	return (e);
}

gfarm_error_t
process_replica_adding(struct process *process, struct peer *peer,
	int cksum_protocol, struct host *src, struct host *dst, int fd,
	struct inode **inodep,
	char **cksum_typep, size_t *cksum_lenp, char *cksum,
	gfarm_int32_t *cksum_request_flagsp, const char *diag)
{
	struct file_opening *fo;
	gfarm_error_t e = process_get_file_opening(process, peer, fd,
	    &fo, diag);

	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (fo->u.f.replica_source != NULL)
		return (GFARM_ERR_FILE_BUSY);
	if (inode_new_generation_is_pending(fo->inode)) {
		gflog_debug(GFARM_MSG_1002242,
		    "process_replica_adding: new_generation pending %lld:%lld",
		    (long long)inode_get_number(fo->inode),
		    (long long)inode_get_gen(fo->inode));
		return (GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE);
	}

	e = process_prepare_to_replicate(process, peer, src, dst, fd,
	    0, NULL, inodep, diag);
	if (e == GFARM_ERR_NO_ERROR) {
		size_t cksum_len = 0;
		char *cksum_type = NULL, *cksump = NULL;
		gfarm_int32_t cksum_request_flags = 0;

		if (cksum_protocol) {
			inode_replication_get_cksum_mode(fo->inode, src,
			    &cksum_type, &cksum_len, &cksump,
			    &cksum_request_flags);
			cksum_type = strdup_log(cksum_type, diag);
			if (cksum_type == NULL)
				return (GFARM_ERR_NO_MEMORY);
		}

		/*
		 * do not set spool_host
		 * since replica is now creating to this host
		 */
		GFARM_MALLOC(fo->u.f.replica_source);
		if (fo->u.f.replica_source == NULL) {
			free(cksum_type);
			return (GFARM_ERR_NO_MEMORY);
		}
		fo->u.f.replica_source->gen = inode_get_gen(fo->inode);
		fo->u.f.replica_source->dst = dst;
		fo->u.f.replica_source->cksum_request_flags =
		    cksum_request_flags;
		fo->u.f.spool_opener = peer;

		*cksum_typep = cksum_type;
		*cksum_lenp = cksum_len;
		if (cksum_len > 0)
			memcpy(cksum, cksump, cksum_len);
		*cksum_request_flagsp = cksum_request_flags;
	}
	return (e);
}

gfarm_error_t
process_replica_added(struct process *process,
	struct peer *peer, int cksum_protocol, struct host *spool_host, int fd,
	gfarm_int32_t src_err, gfarm_int32_t dst_err,
	int flags, gfarm_int64_t mtime_sec, gfarm_int32_t mtime_nsec,
	gfarm_off_t size,
	const char *cksum_type, size_t cksum_len, const char *cksum,
	gfarm_int32_t cksum_result_flags, const char *diag)
{
	struct file_opening *fo;
	struct gfarm_timespec *mtime;
	gfarm_error_t e = process_get_file_opening(process, peer, fd,
	    &fo, diag);
	gfarm_error_t e2;

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003539,
		    "%s: invalid file descriptor %d: %s", diag, fd,
		    gfarm_error_string(e));
		return (e);
	}
	if (!inode_is_file(fo->inode)) { /* i.e. is a directory */
		e = GFARM_ERR_NOT_A_REGULAR_FILE;
		gflog_info(GFARM_MSG_1003540, "%s: inode %lld: %s",
		    diag, (long long)inode_get_number(fo->inode),
		    gfarm_error_string(e));
		return (e);
	}
	if (fo->u.f.spool_opener != peer) {
		gflog_debug(GFARM_MSG_1003541,
		    "%s: inode %lld: inconsistent replication "
		    "(adding by %s, added by %s)", diag,
		    (long long)inode_get_number(fo->inode),
		    peer_get_hostname(fo->u.f.spool_opener),
		    peer_get_hostname(peer));
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	}
	if (fo->u.f.replica_source == NULL) {
		gflog_debug(GFARM_MSG_1003542,
		    "%s: inode %lld: replica_added was called by %s "
		    "without adding", diag,
		    (long long)inode_get_number(fo->inode),
		    peer_get_hostname(peer));
		return (GFARM_ERR_INVALID_ARGUMENT);
	}

	mtime = inode_get_mtime(fo->inode);
	if (cksum_protocol) {
		/*
		 * mtime_sec and mtime_nsec parameters are removed
		 * in cksum_protocol.
		 * see the comment about mtime_sec/mtime_nsec below.
		 */
		mtime_sec = mtime->tv_sec;
		mtime_nsec = mtime->tv_nsec;
	}

	if (inode_has_no_replica(fo->inode)) {
		gflog_debug(GFARM_MSG_1003543,
		    "%s: inode %lld: no replica", diag,
		    (long long)inode_get_number(fo->inode));
		e = GFARM_ERR_NO_SUCH_OBJECT;
	} else if (inode_has_replica(fo->inode, spool_host)) {
		e = GFARM_ERR_ALREADY_EXISTS;
		gflog_debug(GFARM_MSG_1003544, "%s: inode %lld, host %s: %s",
		    diag, (long long)inode_get_number(fo->inode),
		    host_name(spool_host), gfarm_error_string(e));
	} else if (inode_is_opened_for_writing(fo->inode) ||
	    mtime_sec != mtime->tv_sec ||
	    mtime_nsec != mtime->tv_nsec ||
	    (size != -1 && size != inode_get_size(fo->inode)) ||
	    fo->u.f.replica_source->gen != inode_get_gen(fo->inode) ||
	    src_err != GFARM_ERR_NO_ERROR || dst_err != GFARM_ERR_NO_ERROR) {
		/*
		 * the mtime_sec/mtime_nsec comparison above is for
		 * GFM_PROTO_REPLICA_ADDED and GFM_PROTO_REPLICA_ADDED2
		 * used by gfsd before gfarm-2.6.0.
		 * GFM_PROTO_REPLICA_ADDED_CKSUM doesn't need the comparison.
		 */
		if (src_err != GFARM_ERR_NO_ERROR ||
		    dst_err != GFARM_ERR_NO_ERROR) {
			gflog_notice(GFARM_MSG_1004013,
			    "inode(%lld:%lld) current gen=%lld: error happened"
			    " during client-initiated replication: %s/%s",
			    (long long)inode_get_number(fo->inode),
			    (long long)fo->u.f.replica_source->gen,
			    (long long)inode_get_gen(fo->inode),
			    gfarm_error_string(src_err),
			    gfarm_error_string(dst_err));
			/*
			 * in this case, gfsd already knows the src_err
			 * and dst_err, and will return the appropriate error
			 * to the client.
			 * thus, what gfmd should do here is to acknowledge
			 * the report of the error.
			 */
			e = GFARM_ERR_NO_ERROR;
		} else {
			gflog_notice(GFARM_MSG_1002244,
			    "inode(%lld) updated during replication: "
			    "mtime %lld.%09lld/%lld.%09lld, "
			    "size: %lld/%lld, gen:%lld/%lld",
			    (long long)inode_get_number(fo->inode),
			    (long long)mtime_sec, (long long)mtime_nsec,
			    (long long)inode_get_mtime(fo->inode)->tv_sec,
			    (long long)inode_get_mtime(fo->inode)->tv_nsec,
			    (long long)size,
			    (long long)inode_get_size(fo->inode),
			    (long long)fo->u.f.replica_source->gen,
			    (long long)inode_get_gen(fo->inode));
			e = GFARM_ERR_INVALID_FILE_REPLICA;
		}
		inode_remove_replica_incomplete(fo->inode, spool_host,
		    fo->u.f.replica_source->gen);
	} else {
		if (cksum_protocol &&
		    (fo->u.f.replica_source->cksum_request_flags &
		    GFS_PROTO_REPLICATION_CKSUM_REQFLAG_INTERNAL_SUM_AVAIL
		    ) == 0 && cksum_type != NULL && *cksum_type != '\0' &&
		    cksum_len > 0) {
			int cksum_is_set;

			/*
			 * calling inode_cksum_set() without checking
			 * `cs == NULL' is OK, since r8972.
			 */
			e = inode_cksum_set(fo->inode, cksum_type, cksum_len,
			    cksum, cksum_result_flags, 0, &cksum_is_set);
			if (e != GFARM_ERR_NO_ERROR)
				gflog_notice(GFARM_MSG_1004221,
				    "checksum error during replication of "
				    "inode %lld:%lld to %s: %s",
				    (long long)inode_get_number(fo->inode),
				    (long long)fo->u.f.replica_source->gen,
				    host_name(spool_host),
				    gfarm_error_string(e));
			else if (cksum_is_set)
				gflog_notice(GFARM_MSG_1004355,
				    "inode %lld:%lld: checksum set to "
				    "<%s>:<%.*s> by replication to %s",
				    (long long)inode_get_number(fo->inode),
				    (long long)fo->u.f.replica_source->gen,
				    cksum_type, (int)cksum_len, cksum,
				    host_name(spool_host));
		}
		if (e == GFARM_ERR_NO_ERROR) {
			e = inode_add_replica(fo->inode, spool_host, 1);
			if (e != GFARM_ERR_NO_ERROR) {
				/* possibly quota check failure? */
				gflog_notice(GFARM_MSG_1004222,
				    "replication of inode %lld:%lld to %s "
				    "completed, but: %s",
				    (long long)inode_get_number(fo->inode),
				    (long long)fo->u.f.replica_source->gen,
				    host_name(spool_host),
				    gfarm_error_string(e));
			}
		}
		if (e != GFARM_ERR_NO_ERROR)
			inode_remove_replica_incomplete(fo->inode, spool_host,
			    fo->u.f.replica_source->gen);
	}
	free(fo->u.f.replica_source);
	fo->u.f.replica_source = NULL;
	e2 = process_close_file_read(process, peer, fd, NULL, diag);
	return (e != GFARM_ERR_NO_ERROR ? e : e2);
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
	gfarm_pid_t pid = 0;
	static const char diag[] = "GFM_PROTO_PROCESS_ALLOC";

	e = gfm_server_get_request(peer, diag,
	    "ib", &keytype, sizeof(sharedkey), &keylen, sharedkey);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001663,
			"process_alloc request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	giant_lock();
	if (peer_get_process(peer) != NULL) {
		gflog_debug(GFARM_MSG_1001664,
			"peer_get_process() failed");
		e = GFARM_ERR_ALREADY_EXISTS;
	} else if (!from_client || (user = peer_get_user(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1001665,
			"operation is not permitted");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = process_alloc(user, keytype, keylen, sharedkey,
	    &process, &pid)) == GFARM_ERR_NO_ERROR) {
		peer_set_process(peer, process);
	}
	giant_unlock();
	return (gfm_server_put_reply(peer, diag, e, "l", pid));
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
	gfarm_pid_t parent_pid, pid = 0;
	static const char diag[] = "GFM_PROTO_PROCESS_ALLOC_CHILD";

	e = gfm_server_get_request(peer, diag, "iblib",
	    &parent_keytype,
	    sizeof(parent_sharedkey), &parent_keylen, parent_sharedkey,
	    &parent_pid,
	    &keytype, sizeof(sharedkey), &keylen, sharedkey);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001666,
			"process_alloc_child request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	giant_lock();
	if (peer_get_process(peer) != NULL) {
		gflog_debug(GFARM_MSG_1001667,
			"peer_get_process() failed");
		e = GFARM_ERR_ALREADY_EXISTS;
	} else if (!from_client || (user = peer_get_user(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1001668,
			"operation is not permitted");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if (parent_keytype != GFM_PROTO_PROCESS_KEY_TYPE_SHAREDSECRET ||
	    parent_keylen != GFM_PROTO_PROCESS_KEY_LEN_SHAREDSECRET) {
		gflog_debug(GFARM_MSG_1001669,
			"'parent_keytype' or 'parent_keylen' is invalid");
		e = GFARM_ERR_INVALID_ARGUMENT;
	} else if ((e = process_does_match(parent_pid,
	    parent_keytype, parent_keylen, parent_sharedkey,
	    &parent_process)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001670,
			"process_does_match() failed: %s",
			gfarm_error_string(e));
		/* error */
	} else if ((e = process_alloc(user, keytype, keylen, sharedkey,
	    &process, &pid)) == GFARM_ERR_NO_ERROR) {
		peer_set_process(peer, process);
		process_add_child(parent_process, process);
	}
	giant_unlock();
	return (gfm_server_put_reply(peer, diag, e, "l", pid));
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
	static const char diag[] = "GFM_PROTO_PROCESS_SET";

	e = gfm_server_get_request(peer, diag,
	    "ibl", &keytype, sizeof(sharedkey), &keylen, sharedkey, &pid);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001671,
			"process_set request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	giant_lock();
	if (peer_get_process(peer) != NULL) {
		gflog_debug(GFARM_MSG_1001672,
			"peer_get_process() failed");
		e = GFARM_ERR_ALREADY_EXISTS;
	} else if (keytype != GFM_PROTO_PROCESS_KEY_TYPE_SHAREDSECRET ||
	    keylen != GFM_PROTO_PROCESS_KEY_LEN_SHAREDSECRET) {
		gflog_debug(GFARM_MSG_1001673,
			"'parent_keytype' or 'parent_keylen' is invalid");
		e = GFARM_ERR_INVALID_ARGUMENT;
	} else if ((e = process_does_match(pid, keytype, keylen, sharedkey,
	    &process)) == GFARM_ERR_NO_ERROR) {
		peer_set_process(peer, process);
		if (!from_client)
			peer_set_user(peer, process_get_user(process));
	}
	giant_unlock();
	return (gfm_server_put_reply(peer, diag, e, ""));
}

gfarm_error_t
gfm_server_process_free(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	int transaction = 0;
	static const char diag[] = "GFM_PROTO_PROCESS_FREE";

	if (skip)
		return (GFARM_ERR_NO_ERROR);

	giant_lock();
	if (peer_get_process(peer) == NULL) {
		gflog_debug(GFARM_MSG_1001674,
			"peer_get_process() failed");
		e = GFARM_ERR_NO_SUCH_PROCESS;
	} else {
		if (db_begin(diag) == GFARM_ERR_NO_ERROR)
			transaction = 1;
		/*
		 * the following internally calls inode_close*() and
		 * closing must be done regardless of the result of db_begin().
		 * because not closing may cause descriptor leak.
		 */
		peer_unset_process(peer, diag);
		e = GFARM_ERR_NO_ERROR;
		if (transaction)
			db_end(diag);
	}

	giant_unlock();
	return (gfm_server_put_reply(peer, diag, e, ""));
}

struct process_fd_info {
	struct user *user;
	gfarm_pid_t pid;
	gfarm_uint32_t fd;
	gfarm_mode_t mode;
	gfarm_ino_t inum;
	gfarm_uint64_t igen;
	int open_flag;
	unsigned short client_port, gfsd_peer_port; /* optional */
	gfarm_off_t off; /* optional, DIR and REG only */
	char *client_host; /* optional */
	struct host *gfsd_host; /* optional, REG only */
};

struct process_fd_info_closure {
	struct peer *peer;
	char *gfsd_domain;
	char *user_host_domain;
	struct user *proc_user;
	gfarm_uint64_t flags;

	gfarm_int32_t nfds;
	gfarm_int32_t idx;
	gfarm_error_t e;
	struct process_fd_info *fd_info;
};

static int
process_fd_info_match(struct file_opening *fo, gfarm_mode_t mode,
	struct process_fd_info_closure *c)
{
	int is_file = GFARM_S_ISREG(mode);
	int op = accmode_to_op(fo->flag);

	if ((c->flags &
	     GFM_PROTO_PROCESS_FD_FLAG_EXCLUDE_WRITE_NO_OPEN) != 0 &&
	    (op & GFS_W_OK) == 0)
		return (0);
	if ((c->flags &
	     GFM_PROTO_PROCESS_FD_FLAG_EXCLUDE_WRITE_OPEN) != 0 &&
	    (op & GFS_W_OK) != 0)
		return (0);
	if ((c->flags &
	     GFM_PROTO_PROCESS_FD_FLAG_EXCLUDE_READ_NO_OPEN) != 0 &&
	    (op & GFS_R_OK) == 0)
		return (0);
	if ((c->flags &
	     GFM_PROTO_PROCESS_FD_FLAG_EXCLUDE_READ_OPEN) != 0 &&
	    (op & GFS_R_OK) != 0)
		return (0);

	if ((c->flags &
	    GFM_PROTO_PROCESS_FD_FLAG_EXCLUDE_CLIENT_DETACH) != 0 &&
	    fo->opener == NULL)
		return (0);
	if ((c->flags &
	    GFM_PROTO_PROCESS_FD_FLAG_EXCLUDE_CLIENT_ATTACH) != 0 &&
	    fo->opener != NULL)
		return (0);
	if (is_file && (c->flags &
	    GFM_PROTO_PROCESS_FD_FLAG_EXCLUDE_GFSD_DETACH) != 0 &&
	    fo->u.f.spool_opener == NULL)
		return (0);
	if (is_file && (c->flags &
	    GFM_PROTO_PROCESS_FD_FLAG_EXCLUDE_GFSD_ATTACH) != 0 &&
	    fo->u.f.spool_opener != NULL)
		return (0);

	if ((c->flags &
	    GFM_PROTO_PROCESS_FD_FLAG_EXCLUDE_INODE_DIR) != 0 &&
	    GFARM_S_ISDIR(mode))
		return (0);
	if ((c->flags &
	    GFM_PROTO_PROCESS_FD_FLAG_EXCLUDE_INODE_REG) != 0 &&
	    GFARM_S_ISREG(mode))
		return (0);
	if ((c->flags &
	    GFM_PROTO_PROCESS_FD_FLAG_EXCLUDE_INODE_LNK) != 0 &&
	    GFARM_S_ISLNK(mode)) /* lookup-mode-open is possible */
		return (0);

	if (is_file && c->gfsd_domain[0] != '\0' &&
	    fo->u.f.spool_host != NULL &&
	    !gfarm_host_is_in_domain(host_name(fo->u.f.spool_host),
	    c->gfsd_domain))
		return (0);

	if (c->user_host_domain[0] != '\0' &&
	    fo->opener != NULL &&
	    !gfarm_host_is_in_domain(peer_get_hostname(fo->opener),
	    c->user_host_domain))
		return (0);

	return (1);
}

static void
process_fd_info_count(void *closure, struct gfarm_id_table *idtab,
	gfarm_int32_t pid, void *proc)
{
	struct process_fd_info_closure *c = closure;
	struct process *process = proc;
	gfarm_int32_t fd, nfds;
	struct file_opening *fo;

	if (c->proc_user != NULL && c->proc_user != process->user)
		return;

	nfds = 0;
	for (fd = 0; fd < process->nfiles; fd++) {
		fo = process->filetab[fd];
		if (fo == NULL)
			continue;

		if (!process_fd_info_match(fo, inode_get_mode(fo->inode), c))
			continue;

		++nfds;
	}
	c->nfds += nfds;
}

static  void
process_fd_info_record(void *closure, struct gfarm_id_table *idtab,
	gfarm_int32_t pid, void *proc)
{
	struct process_fd_info_closure *c = closure;
	struct process *process = proc;
	int fd;
	struct file_opening *fo;
	gfarm_mode_t mode;
	int is_file, port;
	struct process_fd_info *fdi;
	static const char diag[] = "GFM_PROTO_PROCESS_FD_INFO:record";

	if (c->e != GFARM_ERR_NO_ERROR)
		return;

	if (c->proc_user != NULL && c->proc_user != process->user)
		return;

	for (fd = 0; fd < process->nfiles; fd++) {
		fo = process->filetab[fd];
		if (fo == NULL)
			continue;

		mode = inode_get_mode(fo->inode);
		is_file = GFARM_S_ISREG(mode);

		if (!process_fd_info_match(fo, mode, c))
			continue;

		fdi = &c->fd_info[c->idx++];
		fdi->user = process->user;
		fdi->pid = pid;
		fdi->fd = fd;
		fdi->mode = mode;
		fdi->inum = inode_get_number(fo->inode);
		fdi->igen = inode_get_gen(fo->inode);
		fdi->open_flag = (fo->flag & GFARM_FILE_USER_MODE);
		fdi->off = (GFARM_S_ISDIR(mode) ? fo->u.d.offset :
		     is_file ? inode_get_size(fo->inode) : 0);
		if (fo->opener == NULL) {
			fdi->client_host = NULL;
			fdi->client_port = 0;
		} else {
			fdi->client_host =
			    strdup_log(peer_get_hostname(fo->opener), diag);
			if (fdi->client_host == NULL)
				c->e = GFARM_ERR_NO_MEMORY;
			if (peer_get_port(fo->opener, &port)
			    == GFARM_ERR_NO_ERROR)
				fdi->client_port = port;
			else
				fdi->client_port = 0;
		}
		if (!is_file || fo->u.f.spool_opener == NULL) {
			fdi->gfsd_host = NULL;
			fdi->gfsd_peer_port = 0;
		} else {
			fdi->gfsd_host = fo->u.f.spool_host;
			if (peer_get_port(fo->u.f.spool_opener, &port)
			    == GFARM_ERR_NO_ERROR)
				fdi->gfsd_peer_port = port;
			else
				fdi->gfsd_peer_port = 0;
		}
	}
}

gfarm_error_t
gfm_server_process_fd_info(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	struct user *user = peer_get_user(peer), *proc_user;
	char *gfsd_domain, *user_host_domain, *proc_username;
	gfarm_uint64_t flags;
	gfarm_int32_t i;
	struct process_fd_info *fdi;
	static const char diag[] = "GFM_PROTO_PROCESS_FD_INFO";

	e = gfm_server_get_request(peer, diag, "sssl",
	    &gfsd_domain, &user_host_domain, &proc_username, &flags);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1004509, "%s: %s",
		    diag, gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		free(gfsd_domain);
		free(user_host_domain);
		free(proc_username);
		return (GFARM_ERR_NO_ERROR);
	}
	giant_lock();

	if (proc_username[0] == '\0')
		proc_user = NULL;
	else
		proc_user = user_lookup(proc_username);

	if (!from_client || user == NULL) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_1004510, "%s: %s",
		    diag, gfarm_error_string(e));
	} else if (!user_is_admin(user) &&
	    (proc_user == NULL || proc_user != peer_get_user(peer))) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_1004511, "%s: specified user '%s': %s",
		    diag, proc_username, gfarm_error_string(e));
	} else if (proc_username[0] != '\0' && proc_user == NULL) {
		e = GFARM_ERR_NO_SUCH_USER;
		gflog_debug(GFARM_MSG_1004512, "%s: specified user '%s': %s",
		    diag, proc_username, gfarm_error_string(e));
	} else if ((flags & ~GFM_PROTO_PROCESS_FD_FLAG_MASK) != 0) {
		e = GFARM_ERR_INVALID_ARGUMENT;
		gflog_debug(GFARM_MSG_1004513, "%s: flags: 0x%llx: %s",
		    diag, (long long)flags, gfarm_error_string(e));
	} else {
		struct process_fd_info_closure closure;

		closure.peer = peer;
		closure.gfsd_domain = gfsd_domain;
		closure.user_host_domain = user_host_domain;
		closure.proc_user = proc_user;
		closure.flags = flags;
		closure.nfds = 0;
		closure.idx = 0;
		closure.e = GFARM_ERR_NO_ERROR;
		gfarm_id_table_foreach(process_id_table, &closure,
		    process_fd_info_count);

		/* record reply to avoid too long giant lock */
		GFARM_MALLOC_ARRAY(closure.fd_info, closure.nfds);
		if (closure.nfds > 0 && closure.fd_info == NULL)
			closure.e = GFARM_ERR_NO_MEMORY;
		else if (closure.nfds > 0)
			gfarm_id_table_foreach(process_id_table, &closure,
			    process_fd_info_record);

		giant_unlock();

		if (closure.e == GFARM_ERR_NO_ERROR &&
		    closure.nfds != closure.idx) {
			gflog_error(GFARM_MSG_1004514,
			    "%s: nfds:%d but %d - inconsistent",
			    diag, (int)closure.nfds, (int)closure.idx);
			closure.e = GFARM_ERR_INTERNAL_ERROR;
		}
		e = gfm_server_put_reply(peer, diag, closure.e, "i",
		    closure.nfds);

		if (closure.e == GFARM_ERR_NO_ERROR &&
		    e == GFARM_ERR_NO_ERROR) {
			for (i = 0; i < closure.nfds; i++) {
				fdi = &closure.fd_info[i];
				e = gfp_xdr_send(peer_get_conn(peer),
				    "sliillilsisiil", user_name(fdi->user),
				    (gfarm_uint64_t)fdi->pid, fdi->fd,
				    (gfarm_uint32_t)fdi->mode,
				    (gfarm_uint64_t)fdi->inum, fdi->igen,
				    (gfarm_uint32_t)fdi->open_flag,
				    (gfarm_uint64_t)fdi->off,
				    fdi->client_host == NULL ? "" :
				    fdi->client_host,
				    (gfarm_uint32_t)fdi->client_port,
				    fdi->gfsd_host == NULL ? "" :
				    host_name(fdi->gfsd_host),
				    (gfarm_uint32_t)
				    (fdi->gfsd_host == NULL ?
				     0 : host_port(fdi->gfsd_host)),
				    (gfarm_uint32_t)fdi->gfsd_peer_port,
				    (gfarm_uint64_t)0 /* for future use */);
				if (e != GFARM_ERR_NO_ERROR)
					break;
			}
		}
		if (closure.fd_info != NULL) {
			for (i = 0; i < closure.idx; i++)
				free(closure.fd_info[i].client_host);
			free(closure.fd_info);
		}
		free(gfsd_domain);
		free(user_host_domain);
		free(proc_username);
		return (e);
	}
	giant_unlock();

	free(gfsd_domain);
	free(user_host_domain);
	free(proc_username);
	return (gfm_server_put_reply(peer, diag, e, ""));
}

gfarm_error_t
gfm_server_bequeath_fd(struct peer *peer, int from_client, int skip)
{
	gfarm_int32_t e;
	struct host *spool_host;
	struct process *process;
	gfarm_int32_t fd;
	static const char diag[] = "GFM_PROTO_BEQUEATH_FD";

	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if (!from_client && (spool_host = peer_get_host(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1001675,
			"operation is not permitted ");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1001676,
			"peer_get_process() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001677,
			"peer_fdpair_get_current() failed");
	} else
		e = process_bequeath_fd(process, peer, fd, diag);

	giant_unlock();
	return (gfm_server_put_reply(peer, diag, e, ""));
}

gfarm_error_t
gfm_server_inherit_fd(struct peer *peer, int from_client, int skip)
{
	gfarm_int32_t e;
	gfarm_int32_t parent_fd, fd;
	struct host *spool_host;
	struct process *process;
	static const char diag[] = "GFM_PROTO_INHERIT_FD";

	e = gfm_server_get_request(peer, diag, "i", &parent_fd);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001678,
			"inherit_fd request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if (!from_client && (spool_host = peer_get_host(peer)) == NULL) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_1001679,
			"operation is not permitted");
	} else if ((process = peer_get_process(peer)) == NULL) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_1001680,
			"peer_get_process() failed");
	} else if ((e = process_inherit_fd(process, parent_fd, peer, NULL,
	    &fd, diag)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001681,
			"process_inherit_fd() failed: %s",
			gfarm_error_string(e));
	} else
		peer_fdpair_set_current(peer, fd, diag);

	giant_unlock();
	return (gfm_server_put_reply(peer, diag, e, ""));
}

gfarm_error_t
process_set_path_for_trace_log(struct process *process, struct peer *peer,
	int fd, char *path, const char *diag)
{
	struct file_opening *fo;
	gfarm_error_t e = process_get_file_opening(process, peer, fd,
	    &fo, diag);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003280,
			"process_get_file_opening() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (fo->path_for_trace_log != NULL)
		free(fo->path_for_trace_log);
	fo->path_for_trace_log = path;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
process_get_path_for_trace_log(struct process *process, struct peer *peer,
	int fd, char **path, const char *diag)
{
	struct file_opening *fo;
	gfarm_error_t e = process_get_file_opening(process, peer, fd,
	    &fo, diag);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003281,
			"process_get_file_opening() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (fo->path_for_trace_log == NULL)
		*path = strdup("");
	else
		*path = strdup(fo->path_for_trace_log);
	return (GFARM_ERR_NO_ERROR);
}
