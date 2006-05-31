/*
 * $Id$
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <stdio.h>
#include <errno.h>
#include <libgen.h>
#include <syslog.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <signal.h>

#include <gfarm/gfarm.h>
#include "metadb_access.h"
#include "gfutil.h"
#include "xxx_proto.h"
#include "io_fd.h"
#include "sockopt.h"
#include "agent_proto.h"
#include "agent_wrap.h"
#include "path_info_cache.h"
#include "agent_thr.h"
#include "agent_ptable.h"

#ifndef HAVE_MKDTEMP
#include <sys/stat.h>

char *
mkdtemp(char *template)
{
	char *s = mktemp(template);

	if (s == NULL)
		return (NULL);
	if (mkdir(s, 0700) != 0)
		return (NULL);
	return (s);
}
#endif

#ifdef SOMAXCONN
#define LISTEN_BACKLOG	SOMAXCONN
#else
#define LISTEN_BACKLOG	5
#endif

char *program_name = "gfarm_agent";

int debug_mode = 0;

/* this routine should be called before calling exit(). */
static void
cleanup(void)
{
	/* disconnect, do logging */
	if (debug_mode)
		gflog_notice("disconnected");
}

static void
log_proto(char *proto, char *status)
{
	gflog_notice(proto, status);
}

static void
error_proto(char *proto, char *status)
{
	cleanup();
	gflog_error(proto, status);
}

static char *
agent_server_get_request(struct xxx_connection *client, char *diag,
	char *format, ...)
{
	va_list ap;
	char *e;
	int eof;

	va_start(ap, format);
	e = xxx_proto_vrecv(client, 0, &eof, &format, &ap);
	va_end(ap);

	if (e == NULL) {
		if (eof)
			e = "missing RPC argument";
		else if (*format != '\0')
			e = "invalid format character to get request";
	}
	if (e != NULL)
		error_proto(diag, e);

	return (e);
}

static char *
agent_server_put_reply_common(struct xxx_connection *client, char *diag,
	int ecode, char *format, va_list *app)
{
	char *e;

	e = xxx_proto_send(client, "i", (gfarm_int32_t)ecode);
	if (e != NULL) {
		error_proto(diag, e);
		return (e);
	}
	if (ecode == 0) {
		e = xxx_proto_vsend(client, &format, app);
		if (e != NULL)
			error_proto(diag, e);
		if (*format != '\0') {
			e = "invalid format character to put reply";
			error_proto(diag, e);
		}
	}
	return (e);
}

static char *
agent_server_put_reply(struct xxx_connection *client, char *diag,
	char *error, char *format, ...)
{
	va_list ap;
	int eno;
	char *e;

	if (error == NULL)
		eno = 0;
	else
		eno = gfarm_error_to_errno(error);

	va_start(ap, format);
	e = agent_server_put_reply_common(client, diag, eno, format, &ap);
	va_end(ap);
	return (e);
}

static char *
agent_server_path_info_get(struct xxx_connection *client)
{
	char *path, *e, *e_rpc;
	struct gfarm_path_info info;
	char *diag = "path_info_get";

	e_rpc = agent_server_get_request(client, diag, "s", &path);
	if (e_rpc != NULL)
		return (e_rpc); /* protocol error */

	if (debug_mode)
		log_proto(diag, path);
	agent_lock();
	e = gfarm_i_path_info_get(path, &info);
	agent_unlock();
	free(path);

	e_rpc = agent_server_put_reply(client, diag, e, "");
	if (e == NULL && e_rpc == NULL) {
		e_rpc = xxx_proto_send_path_info(client, &info);
		if (e_rpc != NULL)
			error_proto(diag, e_rpc);
	}
	if (e == NULL)
		gfarm_path_info_free(&info);
	else
		log_proto(diag, e);
	return (e_rpc);
}

static char *
agent_server_path_info_set(struct xxx_connection *client)
{
	char *pathname, *e, *e_rpc;
	struct gfarm_path_info info;
	char *diag = "path_info_set";

	e_rpc = agent_server_get_request(client, diag, "s", &pathname);
	if (e_rpc != NULL)
		return (e_rpc);
	e_rpc = xxx_proto_recv_path_info_for_set(client, &info);
	if (e_rpc != NULL) {
		error_proto(diag, e_rpc);
		free(pathname);
		return (e_rpc);
	}
	if (debug_mode)
		log_proto(diag, pathname);
 	info.pathname = pathname;
	agent_lock();
	e = gfarm_i_path_info_set(pathname, &info);
	agent_unlock();
	/* pathname will be free'ed in gfarm_path_info_free(&info) */
	gfarm_path_info_free(&info);

	e_rpc = agent_server_put_reply(client, diag, e, "");
	if (e != NULL)
		log_proto(diag, e);
	return (e_rpc);
}

static char *
agent_server_path_info_replace(struct xxx_connection *client)
{
	char *pathname, *e, *e_rpc;
	struct gfarm_path_info info;
	char *diag = "path_info_replace";

	e_rpc = agent_server_get_request(client, diag, "s", &pathname);
	if (e_rpc != NULL)
		return (e_rpc);
	e_rpc = xxx_proto_recv_path_info_for_set(client, &info);
	if (e_rpc != NULL) {
		error_proto(diag, e_rpc);
		free(pathname);
		return (e_rpc);
	}
	if (debug_mode)
		log_proto(diag, pathname);
	info.pathname = pathname;
	agent_lock();
	e = gfarm_i_path_info_replace(pathname, &info);
	agent_unlock();
	gfarm_path_info_free(&info);

	e_rpc = agent_server_put_reply(client, diag, e, "");
	if (e != NULL)
		log_proto(diag, e);
	return (e_rpc);
}

static char *
agent_server_path_info_remove(struct xxx_connection *client)
{
	char *pathname, *e, *e_rpc;
	char *diag = "path_info_remove";

	e_rpc = agent_server_get_request(client, diag, "s", &pathname);
	if (e_rpc != NULL)
		return (e_rpc);

	if (debug_mode)
		log_proto(diag, pathname);
	agent_lock();
	e = gfarm_i_path_info_remove(pathname);
	agent_unlock();
	free(pathname);

	e_rpc = agent_server_put_reply(client, diag, e, "");
	if (e != NULL)
		log_proto(diag, e);
	return (e_rpc);
}

static char *
agent_server_realpath_canonical(struct xxx_connection *client)
{
	char *path, *abspath, *e, *e_rpc;
	char *diag = "realpath_canonical";

	e_rpc = agent_server_get_request(client, diag, "s", &path);
	if (e_rpc != NULL)
		return (e_rpc);

	if (debug_mode)
		log_proto(diag, path);
	agent_lock();
	e = gfs_i_realpath_canonical(path, &abspath);
	agent_unlock();
	free(path);

	e_rpc = agent_server_put_reply(client, diag, e, "s", abspath);
	if (e == NULL)
		free(abspath);
	else
		log_proto(diag, e);
	return (e_rpc);
}

static char *
agent_server_get_ino(struct xxx_connection *client)
{
	char *path, *e, *e_rpc;
	long ino;
	gfarm_int32_t ino_32;
	char *diag = "get_ino";

	e_rpc = agent_server_get_request(client, diag, "s", &path);
	if (e_rpc != NULL)
		return (e_rpc);

	if (debug_mode)
		log_proto(diag, path);
	agent_lock();
	e = gfs_i_get_ino(path, &ino);
	agent_unlock();
	free(path);

	ino_32 = ino; /* XXX - ino is truncated to 32bit int in lp64 */
	e_rpc = agent_server_put_reply(client, diag, e, "i", ino_32);
	if (e != NULL)
		log_proto(diag, e);
	return (e_rpc);
}

static char *
agent_server_opendir(struct xxx_connection *client)
{
	char *path, *e, *e_rpc;
	GFS_Dir dir;
	int dir_index = -1;

	e_rpc = agent_server_get_request(client, "opendir", "s", &path);
	if (e_rpc != NULL)
		return (e_rpc);

	if (debug_mode)
		log_proto("opendir", path);
	agent_lock();
	e = gfs_i_opendir(path, &dir);
	agent_unlock();
	free(path);
	if (e == NULL) {
		dir_index = agent_ptable_entry_add(dir);
		if (dir_index < 0) {
			e = GFARM_ERR_NO_SPACE;
			agent_lock();
			(void)gfs_i_closedir(dir);
			agent_unlock();
		}
	}
	e_rpc = agent_server_put_reply(client, "opendir", e, "i", dir_index);
	if (e != NULL)
		log_proto("opendir", e);
	return (e_rpc);
}

static char *
agent_server_readdir(struct xxx_connection *client)
{
	char *e, *e_rpc;
	int dir_index;
	GFS_Dir dir;
	struct gfs_dirent *entry;

	e_rpc = agent_server_get_request(client, "readdir", "i", &dir_index);
	if (e_rpc != NULL)
		return (e_rpc);

	if (debug_mode)
		log_proto("readdir", "begin");
	dir = agent_ptable_entry_get(dir_index);
	if (dir) {
		agent_lock();
		e = gfs_i_readdir(dir, &entry);
		agent_unlock();
	}
	else
		e = GFARM_ERR_NO_SUCH_OBJECT; /* XXX - EBADF */

	if (entry)
		e_rpc = agent_server_put_reply(
			client, "readdir", e, "ihccs",
			entry->d_fileno, entry->d_reclen,
			entry->d_type, entry->d_namlen, entry->d_name);
	else
		e_rpc = agent_server_put_reply(
			client, "readdir", e, "ihccs",
			0, 0, 0, 1, "");
	if (e != NULL)
		log_proto("readdir", e);
	return (e_rpc);
}

static char *
agent_server_closedir(struct xxx_connection *client)
{
	char *e, *e_rpc;
	int dir_index;
	GFS_Dir dir;

	e_rpc = agent_server_get_request(client, "closedir", "i", &dir_index);
	if (e_rpc != NULL)
		return (e_rpc);

	if (debug_mode)
		log_proto("closedir", "begin");
	dir = agent_ptable_entry_get(dir_index);
	if (dir) {
		agent_lock();
		e = gfs_i_closedir(dir);
		agent_unlock();
	}
	else
		e = GFARM_ERR_NO_SUCH_OBJECT; /* XXX - EBADF */
	if (e == NULL)
		agent_ptable_entry_delete(dir_index);

	e_rpc = agent_server_put_reply(client, "closedir", e, "");
	if (e != NULL)
		log_proto("closedir", e);
	return (e_rpc);
}

static char *
agent_server_dirname(struct xxx_connection *client)
{
	char *e_rpc, *name = NULL;
	int dir_index;
	GFS_Dir dir;

	e_rpc = agent_server_get_request(client, "dirname", "i", &dir_index);
	if (e_rpc != NULL)
		return (e_rpc);

	if (debug_mode)
		log_proto("dirname", "begin");
	dir = agent_ptable_entry_get(dir_index);
	if (dir)
		name = gfs_i_dirname(dir);

	e_rpc = agent_server_put_reply(client, "dirname", NULL, "s",
			name != NULL ? name : GFARM_ERR_NO_SUCH_OBJECT);
	if (name == NULL)
		log_proto("dirname", GFARM_ERR_NO_SUCH_OBJECT);
	return (e_rpc);
}

static char *
agent_server_seekdir(struct xxx_connection *client)
{
	char *e, *e_rpc;
	int dir_index;
	file_offset_t off;
	GFS_Dir dir;

	e_rpc = agent_server_get_request(client, "seekdir", "io",
	    &dir_index, &off);
	if (e_rpc != NULL)
		return (e_rpc);

	if (debug_mode)
		log_proto("seekdir", "begin");
	dir = agent_ptable_entry_get(dir_index);
	if (dir) {
		agent_lock();
		e = gfs_i_seekdir(dir, off);
		agent_unlock();
	} else
		e = GFARM_ERR_NO_SUCH_OBJECT; /* XXX - EBADF */

	e_rpc = agent_server_put_reply(client, "seekdir", e, "");
	if (e != NULL)
		log_proto("seekdir", e);
	return (e_rpc);
}

static char *
agent_server_telldir(struct xxx_connection *client)
{
	char *e, *e_rpc;
	int dir_index;
	file_offset_t off;
	GFS_Dir dir;

	e_rpc = agent_server_get_request(client, "telldir", "i", &dir_index);
	if (e_rpc != NULL)
		return (e_rpc);

	if (debug_mode)
		log_proto("telldir", "begin");
	dir = agent_ptable_entry_get(dir_index);
	if (dir) {
		agent_lock();
		e = gfs_i_telldir(dir, &off);
		agent_unlock();
	} else
		e = GFARM_ERR_NO_SUCH_OBJECT; /* XXX - EBADF */

	e_rpc = agent_server_put_reply(client, "telldir", e, "o", off);
	if (e != NULL)
		log_proto("telldir", e);
	return (e_rpc);
}

static char *
agent_server_uncachedir(struct xxx_connection *client)
{
	if (debug_mode)
		log_proto("uncachedir", "begin");
	gfs_i_uncachedir();

	return (agent_server_put_reply(client, "uncachedir", NULL, ""));
}

/* host info */

static char *
agent_server_host_info_get(struct xxx_connection *client)
{
	char *hostname, *e, *e_rpc;
	struct gfarm_host_info info;
	char *diag = "host_info_get";

	e_rpc = agent_server_get_request(client, diag, "s", &hostname);
	if (e_rpc != NULL)
		return (e_rpc);

	if (debug_mode)
		log_proto(diag, hostname);
	agent_lock();
	e = gfarm_cache_host_info_get(hostname, &info);
	agent_unlock();
	free(hostname);

	e_rpc = agent_server_put_reply(client, diag, e, "");
	if (e == NULL && e_rpc == NULL) {
		e_rpc = xxx_proto_send_host_info(client, &info);
		if (e_rpc != NULL)
			error_proto(diag, e_rpc);
	}
	if (e == NULL)
		gfarm_host_info_free(&info);
	else
		log_proto(diag, e);
	return (e_rpc);
}

static char *
agent_server_host_info_remove_hostaliases(struct xxx_connection *client)
{
	char *hostname, *e, *e_rpc;
	char *diag = "host_info_remove_hostaliases";

	e_rpc = agent_server_get_request(client, diag, "s", &hostname);
	if (e_rpc != NULL)
		return (e_rpc);

	if (debug_mode)
		log_proto(diag, hostname);
	agent_lock();
	e = gfarm_cache_host_info_remove_hostaliases(hostname);
	agent_unlock();
	free(hostname);

	e_rpc = agent_server_put_reply(client, diag, e, "");
	if (e != NULL)
		log_proto(diag, e);
	return (e_rpc);
}

static char *
agent_server_host_info_set(struct xxx_connection *client)
{
	char *hostname, *e, *e_rpc;
	struct gfarm_host_info info;
	char *diag = "host_info_set";

	e_rpc = agent_server_get_request(client, diag, "s", &hostname);
	if (e_rpc != NULL)
		return (e_rpc);
	e_rpc = xxx_proto_recv_host_info_for_set(client, &info);
	if (e_rpc != NULL) {
		error_proto(diag, e_rpc);
		goto free_hostname;
	}
	if (debug_mode)
		log_proto(diag, hostname);
	agent_lock();
	e = gfarm_cache_host_info_set(hostname, &info);
	agent_unlock();
	gfarm_host_info_free(&info);

	e_rpc = agent_server_put_reply(client, diag, e, "");
	if (e != NULL)
		log_proto(diag, e);
free_hostname:
	free(hostname);
	return (e_rpc);
}

static char *
agent_server_host_info_replace(struct xxx_connection *client)
{
	char *hostname, *e, *e_rpc;
	struct gfarm_host_info info;
	char *diag = "host_info_replace";

	e_rpc = agent_server_get_request(client, diag, "s", &hostname);
	if (e_rpc != NULL)
		return (e_rpc);
	e_rpc = xxx_proto_recv_host_info_for_set(client, &info);
	if (e_rpc != NULL) {
		error_proto(diag, e_rpc);
		goto free_hostname;
	}
	if (debug_mode)
		log_proto(diag, hostname);
	agent_lock();
	e = gfarm_cache_host_info_replace(hostname, &info);
	agent_unlock();
	gfarm_host_info_free(&info);

	e_rpc = agent_server_put_reply(client, diag, e, "");
	if (e != NULL)
		log_proto(diag, e);
free_hostname:
	free(hostname);
	return (e_rpc);
}

static char *
agent_server_host_info_remove(struct xxx_connection *client)
{
	char *hostname, *e, *e_rpc;
	char *diag = "host_info_remove";

	e_rpc = agent_server_get_request(client, diag, "s", &hostname);
	if (e_rpc != NULL)
		return (e_rpc);

	if (debug_mode)
		log_proto(diag, hostname);
	agent_lock();
	e = gfarm_cache_host_info_remove(hostname);
	agent_unlock();
	free(hostname);

	e_rpc = agent_server_put_reply(client, diag, e, "");
	if (e != NULL)
		log_proto(diag, e);
	return (e_rpc);
}

static char *
agent_server_host_info_get_all(struct xxx_connection *client)
{
	int np, i;
	struct gfarm_host_info *hosts;
	char *e, *e_rpc;
	char *diag = "host_info_get_all";

	if (debug_mode)
		log_proto(diag, "begin");
	agent_lock();
	e = gfarm_cache_host_info_get_all(&np, &hosts);
	agent_unlock();

	e_rpc = agent_server_put_reply(client, diag, e, "i", np);
	if (e == NULL && e_rpc == NULL) {
		for (i = 0; i < np; ++i) {
			e_rpc = xxx_proto_send_host_info(client, &hosts[i]);
			if (e_rpc != NULL) {
				error_proto(diag, e_rpc);
				break;
			}
		}
	}
	if (e == NULL)
		gfarm_host_info_free_all(np, hosts);
	else
		log_proto(diag, e);
	return (e_rpc);
}

static char *
agent_server_host_info_get_by_name_alias(struct xxx_connection *client)
{
	char *alias, *e, *e_rpc;
	struct gfarm_host_info info;
	char *diag = "host_info_get_by_name_alias";

	e_rpc = agent_server_get_request(client, diag, "s", &alias);
	if (e_rpc != NULL)
		return (e_rpc);

	if (debug_mode)
		log_proto(diag, alias);
	agent_lock();
	e = gfarm_cache_host_info_get_by_name_alias(alias, &info);
	agent_unlock();
	free(alias);

	e_rpc = agent_server_put_reply(client, diag, e, "");
	if (e == NULL && e_rpc == NULL) {
		e_rpc = xxx_proto_send_host_info(client, &info);
		if (e_rpc != NULL)
			error_proto(diag, e_rpc);
	}
	if (e == NULL)
		gfarm_host_info_free(&info);
	else
		log_proto(diag, e);
	return (e_rpc);
}

static char *
agent_server_host_info_get_allhost_by_architecture(
	struct xxx_connection *client)
{
	char *arch, *e, *e_rpc;
	int np, i;
	struct gfarm_host_info *hosts;
	char *diag = "host_info_get_allhost_by_architecture";

	e_rpc = agent_server_get_request(client, diag, "s", &arch);
	if (e_rpc != NULL)
		return (e_rpc);

	if (debug_mode)
		log_proto(diag, arch);
	agent_lock();
	e = gfarm_cache_host_info_get_allhost_by_architecture(
		arch, &np, &hosts);
	agent_unlock();
	free(arch);

	e_rpc = agent_server_put_reply(client, diag, e, "i", np);
	if (e == NULL && e_rpc == NULL) {
		for (i = 0; i < np; ++i) {
			e_rpc = xxx_proto_send_host_info(client, &hosts[i]);
			if (e_rpc != NULL) {
				error_proto(diag, e_rpc);
				break;
			}
		}
	}
	if (e == NULL)
		gfarm_host_info_free_all(np, hosts);
	else
		log_proto(diag, e);
	return (e_rpc);
}

/* file section info */

static char *
agent_server_file_section_info_get(struct xxx_connection *client)
{
	char *path, *section, *e, *e_rpc;
	struct gfarm_file_section_info info;
	char *diag = "file_section_info_get";

	e_rpc = agent_server_get_request(client, diag, "ss", &path, &section);
	if (e_rpc != NULL)
		return (e_rpc);

	if (debug_mode)
		log_proto(diag, path);
	agent_lock();
	e = gfarm_metadb_file_section_info_get(path, section, &info);
	agent_unlock();
	free(path);
	free(section);

	e_rpc = agent_server_put_reply(client, diag, e, "");
	if (e == NULL && e_rpc == NULL) {
		e_rpc = xxx_proto_send_file_section_info(client, &info);
		if (e_rpc != NULL)
			error_proto(diag, e_rpc);
	}
	if (e == NULL)
		gfarm_file_section_info_free(&info);
	else
		log_proto(diag, e);
	return (e_rpc);
}

static char *
agent_server_file_section_info_set(struct xxx_connection *client)
{
	char *path, *section, *e, *e_rpc;
	struct gfarm_file_section_info info;
	char *diag = "file_section_info_set";

	e_rpc = agent_server_get_request(client, diag, "ss", &path, &section);
	if (e_rpc != NULL)
		return (e_rpc);
	e_rpc = xxx_proto_recv_file_section_info_for_set(client, &info);
	if (e_rpc != NULL) {
		error_proto(diag, e_rpc);
		goto free_path;
	}
	if (debug_mode)
		log_proto(diag, path);
	agent_lock();
	e = gfarm_metadb_file_section_info_set(path, section, &info);
	agent_unlock();
	gfarm_file_section_info_free(&info);

	e_rpc = agent_server_put_reply(client, diag, e, "");
	if (e != NULL)
		log_proto(diag, e);
free_path:
	free(path);
	free(section);
	return (e_rpc);
}

static char *
agent_server_file_section_info_replace(struct xxx_connection *client)
{
	char *path, *section, *e, *e_rpc;
	struct gfarm_file_section_info info;
	char *diag = "file_section_info_replace";

	e_rpc = agent_server_get_request(
		client, diag, "ss", &path, &section);
	if (e_rpc != NULL)
		return (e_rpc);
	e_rpc = xxx_proto_recv_file_section_info_for_set(client, &info);
	if (e_rpc != NULL) {
		error_proto(diag, e_rpc);
		goto free_path;
	}
	if (debug_mode)
		log_proto(diag, path);
	agent_lock();
	e = gfarm_metadb_file_section_info_replace(path, section, &info);
	agent_unlock();
	gfarm_file_section_info_free(&info);

	e_rpc = agent_server_put_reply(client, diag, e, "");
	if (e != NULL)
		log_proto(diag, e);
free_path:
	free(path);
	free(section);
	return (e_rpc);
}

static char *
agent_server_file_section_info_remove(struct xxx_connection *client)
{
	char *path, *section, *e, *e_rpc;
	char *diag = "file_section_info_remove";

	e_rpc = agent_server_get_request(client, diag, "ss", &path, &section);
	if (e_rpc != NULL)
		return (e_rpc);

	if (debug_mode)
		log_proto(diag, path);
	agent_lock();
	e = gfarm_metadb_file_section_info_remove(path, section);
	agent_unlock();
	free(path);
	free(section);

	e_rpc = agent_server_put_reply(client, diag, e, "");
	if (e != NULL)
		log_proto(diag, e);
	return (e_rpc);
}

static char *
agent_server_file_section_info_get_all_by_file(struct xxx_connection *client)
{
	char *path, *e, *e_rpc;
	int np, i;
	struct gfarm_file_section_info *infos;
	char *diag = "file_section_info_get_all_by_file";

	e_rpc = agent_server_get_request(client, diag, "s", &path);
	if (e_rpc != NULL)
		return (e_rpc);

	if (debug_mode)
		log_proto(diag, path);
	agent_lock();
	e = gfarm_metadb_file_section_info_get_all_by_file(path, &np, &infos);
	agent_unlock();
	free(path);

	e_rpc = agent_server_put_reply(client, diag, e, "i", np);
	if (e == NULL && e_rpc == NULL) {
		for (i = 0; i < np; ++i) {
			e_rpc = xxx_proto_send_file_section_info(
				client, &infos[i]);
			if (e_rpc != NULL) {
				error_proto(diag, e_rpc);
				break;
			}
		}
	}
	if (e == NULL)
		gfarm_file_section_info_free_all(np, infos);
	else
		log_proto(diag, e);
	return (e_rpc);
}

/* file section copy info */

static char *
agent_server_file_section_copy_info_get(struct xxx_connection *client)
{
	char *path, *section, *host, *e, *e_rpc;
	struct gfarm_file_section_copy_info info;
	char *diag = "file_section_copy_info_get";

	e_rpc = agent_server_get_request(
		client, diag, "sss", &path, &section, &host);
	if (e_rpc != NULL)
		return (e_rpc);

	if (debug_mode)
		log_proto(diag, path);
	agent_lock();
	e = gfarm_metadb_file_section_copy_info_get(path, section, host, &info);
	agent_unlock();
	free(path);
	free(section);
	free(host);

	e_rpc = agent_server_put_reply(client, diag, e, "");
	if (e == NULL && e_rpc == NULL) {
		e_rpc = xxx_proto_send_file_section_copy_info(client, &info);
		if (e_rpc != NULL)
			error_proto(diag, e_rpc);
	}
	if (e == NULL)
		gfarm_file_section_copy_info_free(&info);
	else
		log_proto(diag, e);
	return (e_rpc);
}

static char *
agent_server_file_section_copy_info_set(struct xxx_connection *client)
{
	char *path, *section, *host, *e, *e_rpc;
	struct gfarm_file_section_copy_info info;
	char *diag = "file_section_copy_info_set";

	e_rpc = agent_server_get_request(
		client, diag, "sss", &path, &section, &host);
	if (e_rpc != NULL)
		return (e_rpc);

	if (debug_mode)
		log_proto(diag, path);
	agent_lock();
	e = gfarm_metadb_file_section_copy_info_set(path, section, host, &info);
	agent_unlock();
	/* no need to free info... */
	free(path);
	free(section);
	free(host);

	e_rpc = agent_server_put_reply(client, diag, e, "");
	if (e != NULL)
		log_proto(diag, e);
	return (e_rpc);
}

static char *
agent_server_file_section_copy_info_remove(struct xxx_connection *client)
{
	char *path, *section, *host, *e, *e_rpc;
	char *diag = "file_section_copy_info_remove";

	e_rpc = agent_server_get_request(
		client, diag, "sss", &path, &section, &host);
	if (e_rpc != NULL)
		return (e_rpc);

	if (debug_mode)
		log_proto(diag, path);
	agent_lock();
	e = gfarm_metadb_file_section_copy_info_remove(path, section, host);
	agent_unlock();
	free(path);
	free(section);
	free(host);

	e_rpc = agent_server_put_reply(client, diag, e, "");
	if (e != NULL)
		log_proto(diag, e);
	return (e_rpc);
}

static char *
agent_server_file_section_copy_info_get_all_by_file(
	struct xxx_connection *client)
{
	char *path, *e, *e_rpc;
	char *diag = "file_section_copy_info_get_all_by_file";
	int np, i;
	struct gfarm_file_section_copy_info *infos;

	e_rpc = agent_server_get_request(
		client, diag, "s", &path);
	if (e_rpc != NULL)
		return (e_rpc);

	if (debug_mode)
		log_proto(diag, path);
	agent_lock();
	e = gfarm_metadb_file_section_copy_info_get_all_by_file(
		path, &np, &infos);
	agent_unlock();
	free(path);

	e_rpc = agent_server_put_reply(
		client, diag, e, "i", np);
	if (e == NULL && e_rpc == NULL) {
		for (i = 0; i < np; ++i) {
			e_rpc = xxx_proto_send_file_section_copy_info(
				client, &infos[i]);
			if (e_rpc != NULL) {
				error_proto(diag, e_rpc);
				break;
			}
		}
	}
	if (e == NULL)
		gfarm_file_section_copy_info_free_all(np, infos);
	else
		log_proto(diag, e);
	return (e_rpc);
}

static char *
agent_server_file_section_copy_info_get_all_by_section(
	struct xxx_connection *client)
{
	char *path, *section, *e, *e_rpc;
	int np, i;
	struct gfarm_file_section_copy_info *infos;
	char *diag = "file_section_copy_info_get_all_by_section";

	e_rpc = agent_server_get_request(client, diag, "ss", &path, &section);
	if (e_rpc != NULL)
		return (e_rpc);

	if (debug_mode)
		log_proto(diag, path);
	agent_lock();
	e = gfarm_metadb_file_section_copy_info_get_all_by_section(
		path, section, &np, &infos);
	agent_unlock();
	free(path);
	free(section);

	e_rpc = agent_server_put_reply(client, diag, e, "i", np);
	if (e == NULL && e_rpc == NULL) {
		for (i = 0; i < np; ++i) {
			e_rpc = xxx_proto_send_file_section_copy_info(
				client, &infos[i]);
			if (e_rpc != NULL) {
				error_proto(diag, e_rpc);
				break;
			}
		}
	}
	if (e == NULL)
		gfarm_file_section_copy_info_free_all(np, infos);
	else
		log_proto(diag, e);
	return (e_rpc);
}

static char *
agent_server_file_section_copy_info_get_all_by_host(
	struct xxx_connection *client)
{
	char *host, *e, *e_rpc;
	int np, i;
	struct gfarm_file_section_copy_info *infos;
	char *diag = "file_section_copy_info_get_all_by_host";

	e_rpc = agent_server_get_request(client, diag, "s", &host);
	if (e_rpc != NULL)
		return (e_rpc);

	if (debug_mode)
		log_proto(diag, host);
	agent_lock();
	e = gfarm_metadb_file_section_copy_info_get_all_by_host(
		host, &np, &infos);
	agent_unlock();
	free(host);

	e_rpc = agent_server_put_reply(client, diag, e, "i", np);
	if (e == NULL && e_rpc == NULL) {
		for (i = 0; i < np; ++i) {
			e_rpc = xxx_proto_send_file_section_copy_info(
				client, &infos[i]);
			if (e_rpc != NULL) {
				error_proto(diag, e_rpc);
				break;
			}
		}
	}
	if (e == NULL)
		gfarm_file_section_copy_info_free_all(np, infos);
	else
		log_proto(diag, e);
	return (e_rpc);
}

/* */

static int gfarm_initialized = 0;

static int
client_arg_alloc(int fd, void **argp)
{
	int *arg;

	arg = malloc(sizeof(int));
	if (arg == NULL)
		return (-1);
	arg[0] = fd;
	*argp = arg;
	return (0);
}

static void
client_arg_free(void *arg)
{
	free(arg);
}

void *
server(void *arg)
{
	int client_fd = *(int *)arg;
	char *e;
	struct xxx_connection *client;
	int eof;
	gfarm_int32_t request;
	char buffer[GFARM_INT32STRLEN];

	client_arg_free(arg);
	agent_ptable_alloc();

	agent_lock();
	if (!gfarm_initialized) {
		e = gfarm_initialize(NULL, NULL);
		if (e == NULL)
			gfarm_initialized = 1;
		else {
			(void)gfarm_terminate();
			agent_unlock();
			close(client_fd);
			error_proto("gfarm_initialize", e);
			return (NULL);
		}
	}
	agent_unlock();

	e = xxx_fd_connection_new(client_fd, &client);
	if (e != NULL) {
		close(client_fd);
		error_proto("xxx_connection_new", e);
		return (NULL);
	}

	for (;;) {
		char *cmd;
		e = xxx_proto_recv(client, 0, &eof, "i", &request);
		if (e != NULL) {
			error_proto("request number", e);
			goto exit_free_conn;
		}
		if (eof) {
			cleanup();
			goto exit_free_conn;
		}
		switch (request) {
		case AGENT_PROTO_PATH_INFO_GET:
			cmd = "path_info_get";
			e = agent_server_path_info_get(client);
			break;
		case AGENT_PROTO_PATH_INFO_SET:
			cmd = "path_info_set";
			e = agent_server_path_info_set(client);
			break;
		case AGENT_PROTO_PATH_INFO_REPLACE:
			cmd = "path_info_replace";
			e = agent_server_path_info_replace(client);
			break;
		case AGENT_PROTO_PATH_INFO_REMOVE:
			cmd = "path_info_remove";
			e = agent_server_path_info_remove(client);
			break;
		case AGENT_PROTO_REALPATH_CANONICAL:
			cmd = "realpath_canonical";
			e = agent_server_realpath_canonical(client);
			break;
		case AGENT_PROTO_GET_INO:
			cmd = "get_ino";
			e = agent_server_get_ino(client);
			break;
		case AGENT_PROTO_OPENDIR:
			cmd = "opendir";
			e = agent_server_opendir(client);
			break;
		case AGENT_PROTO_READDIR:
			cmd = "readdir";
			e = agent_server_readdir(client);
			break;
		case AGENT_PROTO_CLOSEDIR:
			cmd = "closedir";
			e = agent_server_closedir(client);
			break;
		case AGENT_PROTO_DIRNAME:
			cmd = "dirname";
			e = agent_server_dirname(client);
			break;
		case AGENT_PROTO_UNCACHEDIR:
			cmd = "uncachedir";
			e = agent_server_uncachedir(client);
			break;
		case AGENT_PROTO_HOST_INFO_GET:
			cmd = "host_info_get";
			e = agent_server_host_info_get(client);
			break;
		case AGENT_PROTO_HOST_INFO_REMOVE_HOSTALIASES:
			cmd = "host_info_remove_hostaliases";
			e = agent_server_host_info_remove_hostaliases(client);
			break;
		case AGENT_PROTO_HOST_INFO_SET:
			cmd = "host_info_set";
			e = agent_server_host_info_set(client);
			break;
		case AGENT_PROTO_HOST_INFO_REPLACE:
			cmd = "host_info_replace";
			e = agent_server_host_info_replace(client);
			break;
		case AGENT_PROTO_HOST_INFO_REMOVE:
			cmd = "host_info_remove";
			e = agent_server_host_info_remove(client);
			break;
		case AGENT_PROTO_HOST_INFO_GET_ALL:
			cmd = "host_info_get_all";
			e = agent_server_host_info_get_all(client);
			break;
		case AGENT_PROTO_HOST_INFO_GET_BY_NAME_ALIAS:
			cmd = "host_info_get_by_name_alias";
			e = agent_server_host_info_get_by_name_alias(client);
			break;
		case AGENT_PROTO_HOST_INFO_GET_ALLHOST_BY_ARCHITECTURE:
			cmd = "host_info_get_allhost_by_architecture";
			e = agent_server_host_info_get_allhost_by_architecture(
				client);
			break;
		case AGENT_PROTO_FILE_SECTION_INFO_GET:
			cmd = "file_section_info_get";
			e = agent_server_file_section_info_get(client);
			break;
		case AGENT_PROTO_FILE_SECTION_INFO_SET:
			cmd = "file_section_info_set";
			e = agent_server_file_section_info_set(client);
			break;
		case AGENT_PROTO_FILE_SECTION_INFO_REPLACE:
			cmd = "file_section_info_replace";
			e = agent_server_file_section_info_replace(client);
			break;
		case AGENT_PROTO_FILE_SECTION_INFO_REMOVE:
			cmd = "file_section_info_remove";
			e = agent_server_file_section_info_remove(client);
			break;
		case AGENT_PROTO_FILE_SECTION_INFO_GET_ALL_BY_FILE:
			cmd = "file_section_info_get_all_by_file";
			e = agent_server_file_section_info_get_all_by_file(
				client);
			break;
		case AGENT_PROTO_FILE_SECTION_COPY_INFO_GET:
			cmd = "file_section_copy_info_get";
			e = agent_server_file_section_copy_info_get(client);
			break;
		case AGENT_PROTO_FILE_SECTION_COPY_INFO_SET:
			cmd = "file_section_copy_info_set";
			e = agent_server_file_section_copy_info_set(client);
			break;
		case AGENT_PROTO_FILE_SECTION_COPY_INFO_REMOVE:
			cmd = "file_section_copy_info_remove";
			e = agent_server_file_section_copy_info_remove(client);
			break;
		case AGENT_PROTO_FILE_SECTION_COPY_INFO_GET_ALL_BY_FILE:
			cmd = "file_section_copy_info_get_all_by_file";
			e = agent_server_file_section_copy_info_get_all_by_file(
				client);
			break;
		case AGENT_PROTO_FILE_SECTION_COPY_INFO_GET_ALL_BY_SECTION:
			cmd = "file_section_copy_info_get_all_by_section";
		     e = agent_server_file_section_copy_info_get_all_by_section(
				client);
			break;
		case AGENT_PROTO_FILE_SECTION_COPY_INFO_GET_ALL_BY_HOST:
			cmd = "file_section_copy_info_get_all_by_host";
			e = agent_server_file_section_copy_info_get_all_by_host(
				client);
			break;
		case AGENT_PROTO_SEEKDIR:
			cmd = "seekdir";
			e = agent_server_seekdir(client);
			break;
		case AGENT_PROTO_TELLDIR:
			cmd = "telldir";
			e = agent_server_telldir(client);
			break;
		default:
			sprintf(buffer, "%d", (int)request);
			gflog_warning("unknown request: %s", buffer);
			cleanup();
			goto exit_free_conn;
		}
		if (e != NULL) {
			/* protocol error */
			error_proto(cmd, e);
			goto exit_free_conn;
		}
	}
 exit_free_conn:
	/* 'client_fd' will be free'ed in xxx_connection_free() */
	e = xxx_connection_free(client);
	if (e != NULL)
		error_proto("xxx_connection_free", e);
	return (NULL);
}

static char *sock_dir;
static char *sock_path;
static enum {
	UNDECIDED,
	B_SHELL_LIKE,
	C_SHELL_LIKE
} shell_type;

static void
guess_shell_type(void)
{
	char *shell;

	if (shell_type == UNDECIDED && (shell = getenv("SHELL")) != NULL) {
		int shell_len = strlen(shell);

		if (shell_len < 3 ||
		    memcmp(shell + shell_len - 3, "csh", 3) != 0)
			shell_type = B_SHELL_LIKE;
		else
			shell_type = C_SHELL_LIKE;
	}
}

static void
display_env(int fd, int port)
{
	FILE *f = fdopen(fd, "w");
	pid_t pid = getpid();
	char *hostname;
	enum agent_type server_type = gfarm_agent_type_get();

	if (f == NULL)
		return;

	guess_shell_type();

	if (server_type != UNIX_DOMAIN) {
		if (gfarm_host_get_canonical_self_name(&hostname) != NULL)
			hostname = gfarm_host_get_self_name();
	}
	switch (shell_type) {
	case B_SHELL_LIKE:
		switch (server_type) {
		case UNIX_DOMAIN:
			fprintf(f, "GFARM_AGENT_SOCK=%s; "
				"export GFARM_AGENT_SOCK;\n", sock_path);
			break;
		case INET:
			fprintf(f, "GFARM_AGENT_HOST=%s; "
				"export GFARM_AGENT_HOST;\n", hostname);
			fprintf(f, "GFARM_AGENT_PORT=%d; "
				"export GFARM_AGENT_PORT;\n", port);
			break;
		default:
			break;
		}			
		fprintf(f, "GFARM_AGENT_PID=%d; export GFARM_AGENT_PID;\n",
			pid);
		fprintf(f, "echo Agent pid %d;\n", pid);
		break;
	case C_SHELL_LIKE:
		switch (server_type) {
		case UNIX_DOMAIN:
			fprintf(f, "setenv GFARM_AGENT_SOCK %s;\n", sock_path);
			break;
		case INET:
			fprintf(f, "setenv GFARM_AGENT_HOST %s;\n", hostname);
			fprintf(f, "setenv GFARM_AGENT_PORT %d;\n", port);
			break;
		default:
			break;
		}
		fprintf(f, "setenv GFARM_AGENT_PID %d;\n", pid);
		fprintf(f, "echo Agent pid %d;\n", pid);
		break;
	default:
		break;
	}
	fclose(f);
}

void
sigterm_handler(int sig)
{
	if (sock_path)
		unlink(sock_path);
	if (sock_dir)
		rmdir(sock_dir);
	exit(1);
}

#define AGENT_SOCK_TEMPLATE	"/tmp/.gfarm-XXXXXX"

static int
open_accepting_unix_domain(void)
{
	struct sockaddr_un self_addr;
	socklen_t self_addr_size;
	int sock, sockopt;
	char *sdir, sockdir[] = AGENT_SOCK_TEMPLATE;
	char tmpsoc[7 + GFARM_INT32STRLEN];

	sdir = mkdtemp(sockdir);
	if (sdir == NULL)
		gflog_fatal_errno("mkdtemp");
	sock_dir = strdup(sdir);
	sprintf(tmpsoc, "/agent.%ld", (long)getpid());

	memset(&self_addr, 0, sizeof(self_addr));
	self_addr.sun_family = AF_UNIX;
	strcpy(self_addr.sun_path, sdir);
	strcat(self_addr.sun_path, tmpsoc);
	self_addr_size = sizeof(self_addr);
	sock = socket(PF_UNIX, SOCK_STREAM, 0);
	if (sock < 0) {
		(void)rmdir(sdir);
		gflog_fatal_errno("accepting socket");
	}
	sockopt = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
	    &sockopt, sizeof(sockopt)) == -1)
		gflog_warning_errno("SO_REUSEADDR");
	if (bind(sock, (struct sockaddr *)&self_addr, self_addr_size) < 0) {
		(void)unlink(self_addr.sun_path);
		(void)rmdir(sdir);
		gflog_fatal_errno("bind accepting socket");
	}
	if (listen(sock, LISTEN_BACKLOG) < 0) {
		(void)unlink(self_addr.sun_path);
		(void)rmdir(sdir);
		gflog_fatal_errno("listen");
	}
	sock_path = strdup(self_addr.sun_path);
	return (sock);
}

static int
open_accepting_socket(int port)
{
	struct sockaddr_in self_addr;
	socklen_t self_addr_size;
	int sock, sockopt;
	enum agent_type server_type = gfarm_agent_type_get();
	char *e;

	if (server_type == UNIX_DOMAIN)
		return (open_accepting_unix_domain());

	memset(&self_addr, 0, sizeof(self_addr));
	self_addr.sin_family = AF_INET;
	self_addr.sin_addr.s_addr = INADDR_ANY;
	self_addr.sin_port = htons(port);
	self_addr_size = sizeof(self_addr);
	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock < 0)
		gflog_fatal_errno("accepting socket");
	sockopt = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
	    &sockopt, sizeof(sockopt)) == -1)
		gflog_warning_errno("SO_REUSEADDR");
	if (bind(sock, (struct sockaddr *)&self_addr, self_addr_size) < 0)
		gflog_fatal_errno("bind accepting socket");
	e = gfarm_sockopt_apply_listener(sock);
	if (e != NULL)
		gflog_warning("setsockopt: %s", e);
	if (listen(sock, LISTEN_BACKLOG) < 0)
		gflog_fatal_errno("listen");
	return (sock);
}

void
usage(void)
{
	fprintf(stderr, "Usage: %s [option]\n", program_name);
	fprintf(stderr, "option:\n");
	fprintf(stderr, "\t-c\t\tGenerate C-shell commands\n");
	fprintf(stderr, "\t-s\t\tGenerate Bourne shell commands\n");
	fprintf(stderr, "\t-d\t\tdebug mode\n");
	fprintf(stderr, "\t-f <gfarm-configuration-file>\n");
	fprintf(stderr, "\t-P <pid-file>\n");
	fprintf(stderr, "\t-S <syslog-facility>\n");
	fprintf(stderr, "\t-v\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	extern char *optarg;
	extern int optind;
	struct sockaddr_un client_addr;
	socklen_t client_addr_size;
	char *e, *config_file = NULL, *pid_file = NULL;
	FILE *pid_fp = NULL;
	int syslog_facility = GFARM_DEFAULT_FACILITY;
	int ch, agent_port = -1, accepting_sock, client, stdout_fd;

	if (argc >= 1)
		program_name = basename(argv[0]);
	gflog_set_identifier(program_name);

	while ((ch = getopt(argc, argv, "cdf:p:P:sS:v")) != -1) {
		switch (ch) {
		case 'c':
			shell_type = C_SHELL_LIKE;
			break;
		case 'd':
			debug_mode = 1;
			break;
		case 'f':
			config_file = optarg;
			break;
		case 'p':
			agent_port = strtol(optarg, NULL, 0);
			gfarm_agent_type_set(INET);
			break;
		case 'P':
			pid_file = optarg;
			break;
		case 's':
			shell_type = B_SHELL_LIKE;
			break;
		case 'S':
			syslog_facility =
			    gflog_syslog_name_to_facility(optarg);
			if (syslog_facility == -1)
				gflog_fatal("%s: unknown syslog facility",
				    optarg);
			break;
		case 'v':
			gflog_auth_set_verbose(1);
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (config_file != NULL)
		gfarm_config_set_filename(config_file);

	/*
	 * Gfarm_initialize() may fail when the metadata server is not
	 * running at start-up time.  In this case, retry at connection time.
	 */
	gfarm_agent_disable();
	gfarm_metadb_share_connection();
	/* set timeout 500 msec for path_info cache */
	gfarm_cache_path_info_param_set(500, 0);

	e = gfarm_initialize(NULL, NULL);
	if (e == NULL)
		gfarm_initialized = 1;
	else
		(void)gfarm_terminate();

	/* default is UNIX_DOMAIN */
	gfarm_agent_type_set(UNIX_DOMAIN);
	if (gfarm_agent_type_get() == INET && agent_port == -1)
		agent_port = gfarm_agent_port_get();

#ifndef __FreeBSD__
	/*
	 * __exit() function in the userland thread library (libc_r)
	 * on FreeBSD-4.X makes all descriptors blocking mode, thus,
	 * as soon as the parent process calls __exit() via gfarm_daemon(),
	 * pthread functions in the child process become broken,
	 * because the implementation needs non-blocking mode, and the
	 * descriptor mode is shared between the parent and the child.
	 * So, we move open_accepting_socket() after gfarm_daemon()
	 * to workaround this problem.
	 * Fortunately, it's rare that open_accepting_socket() fails,
	 * thus I hope this workaround is acceptable.
	 *
	 * libc_r isn't not longer default pthread library on FreeBSD-5.X,
	 * but it's provided as an option, so we do this on FreeBSD-5.X too.
	 */
	accepting_sock = open_accepting_socket(agent_port);
#endif
	stdout_fd = dup(1);

	if (pid_file != NULL) {
		/*
		 * We do this before calling gfarm_daemon()
		 * to print the error message to stderr.
		 */
		pid_fp = fopen(pid_file, "w");
		if (pid_fp == NULL)
			gflog_fatal_errno(pid_file);
	}
	if (!debug_mode) {
		gflog_syslog_open(LOG_PID, syslog_facility);
		gfarm_daemon(0, 0);
	}
#ifdef __FreeBSD__ /* see above comment about FreeBSD */
	accepting_sock = open_accepting_socket(agent_port);
#endif
	if (pid_file != NULL) {
		/*
		 * We do this after calling gfarm_daemon(),
		 * because it changes pid.
		 */
		fprintf(pid_fp, "%ld\n", (long)getpid());
		fclose(pid_fp);
	}
	display_env(stdout_fd, agent_port);

	signal(SIGTERM, sigterm_handler);
	signal(SIGPIPE, SIG_IGN);

	for (;;) {
		int err;
		void *arg;

		client_addr_size = sizeof(client_addr);
		client = accept(accepting_sock,
			(struct sockaddr *)&client_addr, &client_addr_size);
		if (client < 0) {
			if (errno != EINTR)
				gflog_warning_errno("accept");
			continue;
		}
		err = client_arg_alloc(client, &arg);
		if (err) {
			gflog_warning("client_arg_alloc: no memory");
			close(client);
			continue;
		}
		err = agent_schedule(arg, server);
		if (err) {
			gflog_warning_errno("agent_schedule");
			close(client);
			client_arg_free(arg);
		}
	}
	/*NOTREACHED*/
#ifdef __GNUC__ /* to shut up warning */
	return (0);
#endif
}
