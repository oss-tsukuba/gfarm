#include <assert.h>
#include <stdarg.h> /* for "gfp_xdr.h" */
#include <stdlib.h>
#include <string.h>

#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

#include "gfutil.h"
#include "gfp_xdr.h"
#include "auth.h"

#include "gfm_proto.h"

#include "subr.h"
#include "user.h"
#include "group.h"
#include "dir.h"
#include "inode.h"
#include "process.h"
#include "peer.h"
#include "fs.h"

/* this assumes that giant_lock is already acquired */
gfarm_error_t
gfm_server_open_common(struct peer *peer, int from_client,
	struct host *spool_host,
	char *path, gfarm_int32_t flag, int to_create, gfarm_int32_t mode,
	int *fdp, gfarm_ino_t *inump)
{
	gfarm_error_t e;
	struct process *process;
	int op;
	struct inode *inode;
	int created;
	gfarm_int32_t fd = -1;

	if ((process = peer_get_process(peer)) == NULL)
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	if (process_get_user(process) == NULL)
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);

	switch (flag & GFARM_FILE_ACCMODE) {
	case GFARM_FILE_RDONLY:	op = GFS_R_OK; break;
	case GFARM_FILE_WRONLY:	op = GFS_W_OK; break;
	case GFARM_FILE_RDWR:	op = GFS_R_OK|GFS_W_OK; break;
	default:
		return (GFARM_ERR_INVALID_ARGUMENT);
	}

	if (to_create) {
		e = inode_create_file(path, process, op, mode, &inode,
		    &created);
	} else {
		e = inode_lookup_by_name(path, process, op, &inode);
		created = 0;
	}
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (!from_client) {
		if (!inode_is_file(inode))
			return (GFARM_ERR_IS_A_DIRECTORY);
		if ((flag & GFARM_FILE_ACCMODE) == GFARM_FILE_RDONLY &&
		    !created) {
			if (!inode_has_replica(inode, spool_host))
				return (GFARM_ERR_FILE_MIGRATED);
		} else {
			if (inode_schedule_host_for_write(inode, spool_host) !=
			    spool_host)
				return (GFARM_ERR_FILE_MIGRATED);
		}
	}
	e = process_open_file(process, inode, flag, spool_host, &fd);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (created) {
		assert(!from_client);
		e = inode_add_replica(inode, spool_host);
		if (e != GFARM_ERR_NO_ERROR) {
			inode_unlink(path, process);
			return (e);
		}
	}
	*fdp = fd;
	*inump = inode_get_number(inode);
	return(GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfm_server_open(struct peer *peer, int from_client)
{
	gfarm_error_t e;
	char *path;
	gfarm_int32_t flag;
	gfarm_int32_t error;
	struct host *spool_host = NULL;
	gfarm_int32_t fd = -1;
	gfarm_ino_t inum = 0;

	e = gfm_server_get_request(peer, "open", "si", &path, &flag);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	giant_lock();

	if (!from_client && (spool_host = peer_get_host(peer)) == NULL) {
		error = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else {
		error = gfm_server_open_common(peer, from_client, spool_host,
		    path, flag, 0, 0, &fd, &inum);
	}

	free(path);
	giant_unlock();
	return (gfm_server_put_reply(peer, "open", error, "il", fd, inum));
}

gfarm_error_t
gfm_server_create(struct peer *peer, int from_client)
{
	gfarm_error_t e;
	char *path;
	gfarm_int32_t flag, mode;
	gfarm_int32_t error;
	struct host *spool_host;
	gfarm_int32_t fd = -1;
	gfarm_ino_t inum = 0;

	e = gfm_server_get_request(peer, "create", "sii", &path, &flag, &mode);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	giant_lock();

	if (from_client) /* from gfsd only */
		error = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((spool_host = peer_get_host(peer)) == NULL)
		error = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else {
		error = gfm_server_open_common(peer, from_client, spool_host,
		    path, flag, 1, mode, &fd, &inum);
	}

	free(path);
	giant_unlock();
	return (gfm_server_put_reply(peer, "create", error, "il", fd, inum));
}

gfarm_error_t
gfm_server_close_read(struct peer *peer, int from_client)
{
	gfarm_error_t e;
	gfarm_int32_t fd;
	struct gfarm_timespec atime;
	gfarm_int32_t error;
	struct host *spool_host;
	struct process *process;

	e = gfm_server_get_request(peer, "close_read", "ili",
	    &fd, &atime.tv_sec, &atime.tv_nsec);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	giant_lock();

	if (!from_client && (spool_host = peer_get_host(peer)) == NULL)
		error = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((spool_host = peer_get_host(peer)) == NULL)
		error = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((process = peer_get_process(peer)) == NULL)
		error = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else
		error = process_close_file_read(process, spool_host,
		    fd, &atime);

	giant_unlock();
	return (gfm_server_put_reply(peer, "close_read", error, ""));
}

gfarm_error_t
gfm_server_close_write(struct peer *peer, int from_client)
{
	gfarm_error_t e;
	gfarm_int32_t fd;
	gfarm_off_t size;
	struct gfarm_timespec atime, mtime;
	gfarm_int32_t error;
	struct host *spool_host;
	struct process *process;

	e = gfm_server_get_request(peer, "close_write", "illili",
	    &fd, &size,
	    &atime.tv_sec, &atime.tv_nsec, &mtime.tv_sec, &mtime.tv_nsec);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	giant_lock();

	if (from_client) /* from gfsd only */
		error = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((spool_host = peer_get_host(peer)) == NULL)
		error = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((process = peer_get_process(peer)) == NULL)
		error = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else
		error = process_close_file_write(process, spool_host,
		    fd, size, &atime, &mtime);

	giant_unlock();
	return (gfm_server_put_reply(peer, "close_write", error, ""));
}

gfarm_error_t
gfm_server_fstat(struct peer *peer, int from_client)
{
	gfarm_error_t e;
	gfarm_int32_t fd;
	gfarm_int32_t error;
	struct host *spool_host;
	struct process *process;
	struct inode *inode;
	struct gfs_stat st;

	e = gfm_server_get_request(peer, "fstat", "i", &fd);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	giant_lock();

	if (!from_client && (spool_host = peer_get_host(peer)) == NULL)
		error = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((process = peer_get_process(peer)) == NULL)
		error = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((error = process_get_file_inode(process, spool_host, fd,
	    &inode)) == GFARM_ERR_NO_ERROR) {
		st.st_ino = inode_get_number(inode);
		st.st_gen = inode_get_gen(inode);
		st.st_mode = inode_get_mode(inode);
		st.st_nlink = inode_get_nlink(inode);
		st.st_user = user_name(inode_get_user(inode));
		st.st_group = group_name(inode_get_group(inode));
		st.st_size = inode_get_size(inode);
		st.st_ncopy = inode_get_ncopy(inode);
		st.st_atimespec = *inode_get_atime(inode);
		st.st_mtimespec = *inode_get_mtime(inode);
		st.st_ctimespec = *inode_get_ctime(inode);
	}

	giant_unlock();
	/*
	 * XXX assumes that user_name() and group_name() are not broken
	 * even without giatnt_lock.
	 */
	return (gfm_server_put_reply(peer, "fstat", error, "llilsslllilili",
	    st.st_ino, st.st_gen, st.st_mode, st.st_nlink,
	    st.st_user, st.st_group, st.st_size, st.st_ncopy,
	    st.st_atimespec.tv_sec, st.st_atimespec.tv_nsec, 
	    st.st_mtimespec.tv_sec, st.st_atimespec.tv_nsec, 
	    st.st_ctimespec.tv_sec, st.st_ctimespec.tv_nsec));
}

gfarm_error_t
gfm_server_futimes(struct peer *peer, int from_client)
{
	gfarm_error_t e;
	gfarm_int32_t fd;
	struct gfarm_timespec atime, mtime;
	gfarm_int32_t error;
	struct host *spool_host;
	struct process *process;
	struct user *user;
	struct inode *inode;

	e = gfm_server_get_request(peer, "futimes", "ilili",
	    &fd, &atime.tv_sec, &atime.tv_nsec, &mtime.tv_sec, &mtime.tv_nsec);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	giant_lock();

	if (!from_client && (spool_host = peer_get_host(peer)) == NULL)
		error = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((process = peer_get_process(peer)) == NULL)
		error = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((error = process_get_file_inode(process, spool_host, fd,
	    &inode)) != GFARM_ERR_NO_ERROR)
		;
	else if ((user = process_get_user(process)) == NULL)
		error = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if (user != inode_get_user(inode) && !user_is_admin(user) &&
	    (error = process_get_file_writable(process, spool_host, fd)) !=
	    GFARM_ERR_NO_ERROR)
		;
	else {
		inode_set_atime(inode, &atime);
		inode_set_mtime(inode, &mtime);
	}

	giant_unlock();
	return (gfm_server_put_reply(peer, "futimes", error, ""));
}

gfarm_error_t
gfm_server_fchmod(struct peer *peer, int from_client)
{
	gfarm_error_t e;
	gfarm_int32_t fd;
	gfarm_int32_t mode;
	gfarm_int32_t error;
	struct host *spool_host;
	struct process *process;
	struct user *user;
	struct inode *inode;

	e = gfm_server_get_request(peer, "fchmod", "ii", &fd, &mode);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	giant_lock();

	if (!from_client && (spool_host = peer_get_host(peer)) == NULL)
		error = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((process = peer_get_process(peer)) == NULL)
		error = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((user = process_get_user(process)) == NULL)
		error = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((error = process_get_file_inode(process, spool_host, fd,
	    &inode)) != GFARM_ERR_NO_ERROR)
		;
	else if (user != inode_get_user(inode) && !user_is_admin(user))
		error = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else
		error = inode_set_mode(inode, mode);

	giant_unlock();
	return (gfm_server_put_reply(peer, "fchmod", error, ""));
}

gfarm_error_t
gfm_server_fchown(struct peer *peer, int from_client)
{
	gfarm_error_t e;
	char *username, *groupname;
	gfarm_int32_t fd;
	gfarm_int32_t error;
	struct host *spool_host;
	struct process *process;
	struct user *user, *new_user = NULL;
	struct group *new_group = NULL;
	struct inode *inode;

	e = gfm_server_get_request(peer, "fchown", "iss",
	    &fd, &username, &groupname);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	giant_lock();

	if (!from_client && (spool_host = peer_get_host(peer)) == NULL)
		error = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((process = peer_get_process(peer)) == NULL)
		error = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((user = process_get_user(process)) == NULL)
		error = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if (!user_is_admin(user))
		error = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if (*username != '\0' &&
	    (new_user = user_lookup(username)) == NULL)
		error = GFARM_ERR_INVALID_ARGUMENT;
	else if (*groupname != '\0' &&
	    (new_group = group_lookup(groupname)) == NULL)
		error = GFARM_ERR_INVALID_ARGUMENT;
	else if ((error = process_get_file_inode(process, spool_host, fd,
	    &inode)) != GFARM_ERR_NO_ERROR)
		;
	else
		error = inode_set_owner(inode, new_user, new_group);

	free(username);
	free(groupname);
	giant_unlock();
	return (gfm_server_put_reply(peer, "fchown", error, ""));
}

gfarm_error_t
gfm_server_fchdir(struct peer *peer, int from_client)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("fchdir", "not implemented");

	e = gfm_server_put_reply(peer, "fchdir",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_getdirents(struct peer *peer, int from_client)
{
	struct gfp_xdr *client = peer_get_conn(peer);
	gfarm_error_t e;
	gfarm_int32_t fd, n, i;
	gfarm_int32_t error;
	struct host *spool_host;
	struct process *process;
	struct inode *inode, *entry_inode;
	Dir dir;
	DirEntry entry;
	DirCursor cursor;
	struct dir_result_rec {
		char *s;
		gfarm_ino_t inum;
		gfarm_int32_t type;
	} *p = NULL;
	gfarm_off_t dir_offset = 0;
	char *key, *name;
	int keylen, namelen, ok;

	e = gfm_server_get_request(peer, "getdirents", "ii", &fd, &n);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	giant_lock();

	if (!from_client && (spool_host = peer_get_host(peer)) == NULL)
		error = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((process = peer_get_process(peer)) == NULL)
		error = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((error = process_get_file_inode(process, spool_host, fd,
	    &inode)) != GFARM_ERR_NO_ERROR)
		;
	else if ((dir = inode_get_dir(inode)) == NULL)
		error = GFARM_ERR_NOT_A_DIRECTORY;
	else if ((error = process_get_dir_key(process, spool_host, fd,
		    &key, &keylen)) != GFARM_ERR_NO_ERROR)
		;
	else if (key == NULL &&
		 (error = process_get_dir_offset(process, spool_host, fd,
		    &dir_offset)) != GFARM_ERR_NO_ERROR)
		;
	else if (n <= 0)
		error = GFARM_ERR_INVALID_ARGUMENT;
	else if ((p = malloc(sizeof(*p) * n)) == NULL)
		error = GFARM_ERR_NO_MEMORY;
	else {
		if (key != NULL)
			ok = dir_cursor_lookup(dir, key, strlen(key), &cursor);
		else
			ok = 0;
		if (!ok)
			ok = dir_cursor_set_pos(dir, dir_offset, &cursor);
		if (!ok)
			n = 0; /* end of directory? */
		for (i = 0; i < n; ) {
			entry = dir_cursor_get_entry(dir, &cursor);
			if (entry == NULL)
				break;
			name = dir_entry_get_name(entry, &namelen);
			p[i].s = malloc(namelen + 1);
			if (p[i].s == NULL)
				break;
			memcpy(p[i].s, name, namelen);
			p[i].s[namelen] = '\0';
			entry_inode = dir_entry_get_inode(entry);
			p[i].inum = inode_get_number(entry_inode);
			p[i].type = inode_is_dir(entry_inode) ?
			    GFS_DT_DIR : GFS_DT_REG;

			i++;
			if (!dir_cursor_next(dir, &cursor))
				break;
		}
		n = i;
		if (n > 0)
			inode_accessed(inode);

		/* remember current position */
		entry = dir_cursor_get_entry(dir, &cursor);
		if (entry == NULL) {
			dir_offset = dir_get_entry_count(dir);
		} else {
			name = dir_entry_get_name(entry, &namelen);
			process_set_dir_key(process, spool_host, fd,
			    name, namelen);
			dir_offset = dir_cursor_get_pos(dir, &cursor);
		}
		process_set_dir_offset(process, spool_host, fd, dir_offset);
	}
	
	giant_unlock();
	e = gfm_server_put_reply(peer, "getdirents", error, "i", n);
	if (e == GFARM_ERR_NO_ERROR && error == GFARM_ERR_NO_ERROR) {
		for (i = 0; i < n; i++) {
			e = gfp_xdr_send(client, "sil",
			    p[i].s, p[i].type, p[i].inum);
			if (e != GFARM_ERR_NO_ERROR) {
				gflog_fatal("getdirent",gfarm_error_string(e));
				break;
			}
		}
	}
	if (p != NULL) {
		for (i = 0; i < n; i++)
			free(p[i].s);
		free(p);
	}
	return (e);
}

gfarm_error_t
gfm_server_seek(struct peer *peer, int from_client)
{
	gfarm_error_t e;
	gfarm_int32_t fd, whence;
	gfarm_off_t offset, current, max;
	gfarm_int32_t error;
	struct host *spool_host;
	struct process *process;
	struct inode *inode;
	Dir dir;

	e = gfm_server_get_request(peer, "seek", "ili", &fd, &offset, &whence);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	giant_lock();

	if (!from_client && (spool_host = peer_get_host(peer)) == NULL)
		error = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((process = peer_get_process(peer)) == NULL)
		error = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((error = process_get_file_inode(process, spool_host, fd,
	    &inode)) != GFARM_ERR_NO_ERROR)
		;
	else if ((dir = inode_get_dir(inode)) == NULL)
		error = GFARM_ERR_NOT_A_DIRECTORY;
	else if ((error = process_get_dir_offset(process, spool_host, fd,
		    &current)) != GFARM_ERR_NO_ERROR)
		;
	else if (whence < 0 || whence > 2)
		error = GFARM_ERR_INVALID_ARGUMENT;
	else {
		max = dir_get_entry_count(dir);
		switch (whence) {
		case 0: break;
		case 1: offset += current; break;
		case 2: offset += max; break;
		default: assert(0);
		}
		if (offset != current) {
			if (offset > max)
				offset = max;
			process_clear_dir_key(process, spool_host, fd);
			process_set_dir_offset(process, spool_host, fd,
			    offset);
		}
	}
	
	giant_unlock();
	return (gfm_server_put_reply(peer, "seek", error, "l", offset));
}

gfarm_error_t
gfm_server_lock(struct peer *peer, int from_client)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("lock", "not implemented");

	e = gfm_server_put_reply(peer, "lock",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_trylock(struct peer *peer, int from_client)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("trylock", "not implemented");

	e = gfm_server_put_reply(peer, "trylock",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_unlock(struct peer *peer, int from_client)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("unlock", "not implemented");

	e = gfm_server_put_reply(peer, "unlock",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_lock_info(struct peer *peer, int from_client)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("lock_info", "not implemented");

	e = gfm_server_put_reply(peer, "lock_info",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_access(struct peer *peer, int from_client)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("access", "not implemented");

	e = gfm_server_put_reply(peer, "access",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_stat(struct peer *peer, int from_client)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("stat", "not implemented");

	e = gfm_server_put_reply(peer, "stat",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_utimes(struct peer *peer, int from_client)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("utimes", "not implemented");

	e = gfm_server_put_reply(peer, "utimes",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_chmod(struct peer *peer, int from_client)
{
	gfarm_error_t e;
	char *path;
	gfarm_int32_t mode;
	gfarm_int32_t error;
	struct process *process;
	struct user *user;
	struct inode *inode;

	e = gfm_server_get_request(peer, "chmod", "si", &path, &mode);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	giant_lock();

	if ((process = peer_get_process(peer)) == NULL)
		error = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((user = process_get_user(process)) == NULL)
		error = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((error = inode_lookup_by_name(path, process, 0, &inode)) !=
	    GFARM_ERR_NO_ERROR)
		;
	else if (user != inode_get_user(inode) && !user_is_admin(user))
		error = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else
		error = inode_set_mode(inode, mode);

	free(path);
	giant_unlock();
	return (gfm_server_put_reply(peer, "chmod", error, ""));
}


gfarm_error_t
gfm_server_chown(struct peer *peer, int from_client)
{
	gfarm_error_t e;
	char *path, *username, *groupname;
	gfarm_int32_t error;
	struct process *process;
	struct user *user, *new_user = NULL;
	struct group *new_group = NULL;
	struct inode *inode;

	e = gfm_server_get_request(peer, "chown", "sss",
	    &path, &username, &groupname);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	giant_lock();

	if ((process = peer_get_process(peer)) == NULL)
		error = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((user = process_get_user(process)) == NULL)
		error = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if (!user_is_admin(user))
		error = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if (*username != '\0' &&
	    (new_user = user_lookup(username)) == NULL)
		error = GFARM_ERR_INVALID_ARGUMENT;
	else if (*groupname != '\0' &&
	    (new_group = group_lookup(groupname)) == NULL)
		error = GFARM_ERR_INVALID_ARGUMENT;
	else if ((error = inode_lookup_by_name(path, process, 0, &inode))
	    != GFARM_ERR_NO_ERROR)
		;
	else
		error = inode_set_owner(inode, new_user, new_group);

	free(path);
	free(username);
	free(groupname);
	giant_unlock();
	return (gfm_server_put_reply(peer, "chown", error, ""));
}

gfarm_error_t
gfm_server_chdir(struct peer *peer, int from_client)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("chdir", "not implemented");

	e = gfm_server_put_reply(peer, "chdir",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_lstat(struct peer *peer, int from_client)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("lstat", "not implemented");

	e = gfm_server_put_reply(peer, "lstat",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_rename(struct peer *peer, int from_client)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("rename", "not implemented");

	e = gfm_server_put_reply(peer, "rename",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_remove(struct peer *peer, int from_client)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("remove", "not implemented");

	e = gfm_server_put_reply(peer, "remove",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_mkdir(struct peer *peer, int from_client)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("mkdir", "not implemented");

	e = gfm_server_put_reply(peer, "mkdir",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_rmdir(struct peer *peer, int from_client)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("rmdir", "not implemented");

	e = gfm_server_put_reply(peer, "rmdir",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_link(struct peer *peer, int from_client)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("link", "not implemented");

	e = gfm_server_put_reply(peer, "link",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_symlink(struct peer *peer, int from_client)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("symlink", "not implemented");

	e = gfm_server_put_reply(peer, "symlink",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_readlink(struct peer *peer, int from_client)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("readlink", "not implemented");

	e = gfm_server_put_reply(peer, "readlink",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_getcwd(struct peer *peer, int from_client)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("getcwd", "not implemented");

	e = gfm_server_put_reply(peer, "getcwd",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_abspath(struct peer *peer, int from_client)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("abspath", "not implemented");

	e = gfm_server_put_reply(peer, "abspath",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_realpath(struct peer *peer, int from_client)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("realpath", "not implemented");

	e = gfm_server_put_reply(peer, "realpath",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_glob(struct peer *peer, int from_client)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("glob", "not implemented");

	e = gfm_server_put_reply(peer, "glob",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_replica_list_by_name(struct peer *peer, int from_client)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("replica_list_by_name", "not implemented");

	e = gfm_server_put_reply(peer, "replica_list_by_name",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_replica_list_by_host(struct peer *peer, int from_client)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("replica_list_by_host", "not implemented");

	e = gfm_server_put_reply(peer, "replica_list_by_host",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_replica_remove_by_host(struct peer *peer, int from_client)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("replica_remove_by_host", "not implemented");

	e = gfm_server_put_reply(peer, "replica_remove_by_host",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_replica_add(struct peer *peer, int from_client)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("replica_add", "not implemented");

	e = gfm_server_put_reply(peer, "replica_add",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_replica_remove(struct peer *peer, int from_client)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("replica_remove", "not implemented");

	e = gfm_server_put_reply(peer, "replica_remove",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_pio_open(struct peer *peer, int from_client)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("pio_open", "not implemented");

	e = gfm_server_put_reply(peer, "pio_open",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_pio_set_paths(struct peer *peer, int from_client)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("pio_set_paths", "not implemented");

	e = gfm_server_put_reply(peer, "pio_set_paths",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_pio_close(struct peer *peer, int from_client)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("pio_close", "not implemented");

	e = gfm_server_put_reply(peer, "pio_close",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_pio_visit(struct peer *peer, int from_client)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("pio_visit", "not implemented");

	e = gfm_server_put_reply(peer, "pio_visit",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_schedule(struct peer *peer, int from_client)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("schedule", "not implemented");

	e = gfm_server_put_reply(peer, "schedule",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}
