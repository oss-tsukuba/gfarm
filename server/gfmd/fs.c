#include <stdlib.h>

#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

#include "gfutil.h"
#include "auth.h"

#include "subr.h"
#include "user.h"
#include "group.h"
#include "inode.h"
#include "process.h"
#include "peer.h"
#include "fs.h"

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
	else if ((error = inode_lookup_by_name(path, process, &inode)) !=
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
	else if ((error = inode_lookup_by_name(path, process, &inode))
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
gfm_server_getdirents(struct peer *peer, int from_client)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("getdirents", "not implemented");

	e = gfm_server_put_reply(peer, "getdirents",
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
gfm_server_mount(struct peer *peer, int from_client)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("mount", "not implemented");

	e = gfm_server_put_reply(peer, "mount",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_mount_list(struct peer *peer, int from_client)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("mount_list", "not implemented");

	e = gfm_server_put_reply(peer, "mount_list",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_open(struct peer *peer, int from_client)
{
	gfarm_error_t e;
	char *path;
	gfarm_int32_t flag, mode;
	gfarm_int32_t error;
	struct process *process;
	struct host *spool_host;
	struct user *user;
	struct inode *inode;
	int created = 0;
	gfarm_int32_t fd = -1;
	internal_ino_t inum = 0;

	e = gfm_server_get_request(peer, "open", "sii", &flag, &mode);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	giant_lock();

	if (from_client) /* from gfsd only */
		error = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((spool_host = peer_get_host(peer)) == NULL)
		error = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((process = peer_get_process(peer)) == NULL)
		error = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((user = process_get_user(process)) == NULL)
		error = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else {
		if ((flag & GFARM_FILE_CREATE) != 0) {
			error = inode_create_file(path, process, mode,
			    &inode, &created);
		} else {
			error = inode_lookup_by_name(path, process, &inode);
			if (error == GFARM_ERR_NO_ERROR) {
				if (!inode_is_file(inode))
					error = GFARM_ERR_IS_A_DIRECTORY;
			}
		}
		if (error == GFARM_ERR_NO_ERROR) {
			inum = inode_get_number(inode);
			if ((flag & GFARM_FILE_ACCMODE) == GFARM_FILE_RDONLY
			    && !created) {
				if (!inode_has_replica(inode, spool_host))
					error = GFARM_ERR_FILE_MIGRATED;
				else
					error = process_open_file(process,
					    inum, flag, spool_host, &fd);
			} else {
				if (inode_schedule_host_for_write(inode,
				    spool_host) != spool_host)
					error = GFARM_ERR_FILE_MIGRATED;
				else
					error = process_open_file(process,
					    inum, flag, spool_host, &fd);
			}
			if (error == GFARM_ERR_NO_ERROR) {
				if (created) {
					error = inode_add_replica(inode,
					    spool_host);
					if (error != GFARM_ERR_NO_ERROR) {
						inode_unlink(path, process);
					}
				}
			}
		}
	}

	free(path);
	giant_unlock();
	return (gfm_server_put_reply(peer, "open", error, "il",
	    fd, (gfarm_ino_t)inum));
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

	if (from_client) /* from gfsd only */
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
	struct gfarm_timespec atime, mtime;
	gfarm_int32_t error;
	struct host *spool_host;
	struct process *process;

	e = gfm_server_get_request(peer, "close_write", "ilili",
	    &fd, &atime.tv_sec, &atime.tv_nsec, &mtime.tv_sec, &mtime.tv_nsec);
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
		    fd, &atime, &mtime);

	giant_unlock();
	return (gfm_server_put_reply(peer, "close_write", error, ""));
}

gfarm_error_t
gfm_server_fstat(struct peer *peer, int from_client)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("fstat", "not implemented");

	e = gfm_server_put_reply(peer, "fstat",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_futimes(struct peer *peer, int from_client)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("futimes", "not implemented");

	e = gfm_server_put_reply(peer, "futimes",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
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
