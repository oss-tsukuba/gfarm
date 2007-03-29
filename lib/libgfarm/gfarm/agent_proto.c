/*
 * $Id$
 */

#include <stdlib.h>
#include <stdarg.h>
#include <gfarm/gfarm.h>
#include "xxx_proto.h"

static char *
xxx_proto_send_host_info_hostaliases(struct xxx_connection *client,
	const struct gfarm_host_info *info)
{
	int i;
	char *e = NULL;

	for (i = 0; i < info->nhostaliases; ++i) {
		e = xxx_proto_send(client, "s", info->hostaliases[i]);
		if (e != NULL)
			break;
	}
	return (e);
}

static char *
xxx_proto_recv_host_info_hostaliases(struct xxx_connection *client,
	struct gfarm_host_info *info)
{
	char *e, **aliases;
	int i, eof;

	if (info->nhostaliases == 0) {
		info->hostaliases = NULL;
		return (NULL);
	}

	GFARM_MALLOC_ARRAY(aliases, info->nhostaliases + 1);
	if (aliases == NULL)
		return (GFARM_ERR_NO_MEMORY);

	for (i = 0; i < info->nhostaliases; ++i) {
		e = xxx_proto_recv(client, 0, &eof, "s", &aliases[i]);
		if (e != NULL || eof) {
			if (e == NULL)
				e = "missing RPC argument";
			while (--i >= 0)
				free(aliases[i]);
			free(aliases);
			return (e);
		}
	}
	/* hostaliases is a NULL terminated gfarm_strarray */
	aliases[i] = NULL;
	info->hostaliases = aliases;
	return (NULL);
}

char *
xxx_proto_send_host_info(struct xxx_connection *client,
	const struct gfarm_host_info *info)
{
	char *e;

	e = xxx_proto_send(client, "sisi", info->hostname, info->nhostaliases,
		info->architecture, info->ncpu);
	if (e != NULL)
		return (e);
	return (xxx_proto_send_host_info_hostaliases(client, info));
}

char *
xxx_proto_recv_host_info(struct xxx_connection *client,
	struct gfarm_host_info *info)
{
	char *e;
	int eof;

	e = xxx_proto_recv(client, 0, &eof, "sisi", &info->hostname,
		&info->nhostaliases, &info->architecture, &info->ncpu);
	if (e != NULL || eof) {
		if (e == NULL)
			e = "missing RPC argument";
		return (e);
	}
	e = xxx_proto_recv_host_info_hostaliases(client, info);
	if (e != NULL) {
		free(info->hostname);
		free(info->architecture);
	}
	return (e);
}

char *
xxx_proto_send_host_info_for_set(struct xxx_connection *client,
	const struct gfarm_host_info *info)
{
	char *e;

	/* do not send info->hostname */
	e = xxx_proto_send(client, "isi", info->nhostaliases,
		info->architecture, info->ncpu);
	if (e != NULL)
		return (e);
	return (xxx_proto_send_host_info_hostaliases(client, info));
}

char *
xxx_proto_recv_host_info_for_set(struct xxx_connection *client,
	struct gfarm_host_info *info)
{
	char *e;
	int eof;

	e = xxx_proto_recv(client, 0, &eof, "isi",
		&info->nhostaliases, &info->architecture, &info->ncpu);
	if (e != NULL || eof) {
		if (e == NULL)
			e = "missing RPC argument";
		return (e);
	}
	e = xxx_proto_recv_host_info_hostaliases(client, info);
	if (e != NULL)
		free(info->architecture);
	else
		info->hostname = NULL;
	return (e);
}

char *
xxx_proto_send_path_info(struct xxx_connection *client,
	const struct gfarm_path_info *info)
{
	return (xxx_proto_send(client, "siissoiiiiiii",
			info->pathname,
			(gfarm_uint32_t)info->status.st_ino,
			info->status.st_mode,
			info->status.st_user, info->status.st_group,
			info->status.st_size, info->status.st_nsections,
			info->status.st_atimespec.tv_sec,
			info->status.st_atimespec.tv_nsec,
			info->status.st_mtimespec.tv_sec,
			info->status.st_mtimespec.tv_nsec,
			info->status.st_ctimespec.tv_sec,
			info->status.st_ctimespec.tv_nsec));
}

char *
xxx_proto_recv_path_info(struct xxx_connection *client,
	struct gfarm_path_info *info)
{
	char *e;
	int eof;
	gfarm_uint32_t ino;

	e = xxx_proto_recv(client, 0, &eof, "siissoiiiiiii",
		&info->pathname,
		&ino, &info->status.st_mode,
		&info->status.st_user, &info->status.st_group,
		&info->status.st_size, &info->status.st_nsections,
		&info->status.st_atimespec.tv_sec,
		&info->status.st_atimespec.tv_nsec,
		&info->status.st_mtimespec.tv_sec,
		&info->status.st_mtimespec.tv_nsec,
		&info->status.st_ctimespec.tv_sec,
		&info->status.st_ctimespec.tv_nsec);
	if (eof)
		return (GFARM_ERR_PROTOCOL);
	if (e == NULL)
		info->status.st_ino = ino;
	return (e);
}

char *
xxx_proto_send_path_info_for_set(struct xxx_connection *client,
	const struct gfarm_path_info *info)
{
	/* do not send info->pathname */
	return (xxx_proto_send(client, "iissoiiiiiii",
			(gfarm_uint32_t)info->status.st_ino,
			info->status.st_mode,
			info->status.st_user, info->status.st_group,
			info->status.st_size, info->status.st_nsections,
			info->status.st_atimespec.tv_sec,
			info->status.st_atimespec.tv_nsec,
			info->status.st_mtimespec.tv_sec,
			info->status.st_mtimespec.tv_nsec,
			info->status.st_ctimespec.tv_sec,
			info->status.st_ctimespec.tv_nsec));
}

char *
xxx_proto_recv_path_info_for_set(struct xxx_connection *client,
	struct gfarm_path_info *info)
{
	char *e;
	int eof;
	gfarm_uint32_t ino;

	e = xxx_proto_recv(client, 0, &eof, "iissoiiiiiii",
		&ino, &info->status.st_mode,
		&info->status.st_user, &info->status.st_group,
		&info->status.st_size, &info->status.st_nsections,
		&info->status.st_atimespec.tv_sec,
		&info->status.st_atimespec.tv_nsec,
		&info->status.st_mtimespec.tv_sec,
		&info->status.st_mtimespec.tv_nsec,
		&info->status.st_ctimespec.tv_sec,
		&info->status.st_ctimespec.tv_nsec);
	if (eof)
		return (GFARM_ERR_PROTOCOL);
	if (e == NULL) {
		info->pathname = NULL;
		info->status.st_ino = ino;
	}
	return (e);
}

char *
xxx_proto_send_file_section_info(struct xxx_connection *client,
	const struct gfarm_file_section_info *info)
{
	return (xxx_proto_send(client, "ssoss", info->pathname, info->section,
			info->filesize, info->checksum_type, info->checksum));
}

char *
xxx_proto_recv_file_section_info(struct xxx_connection *client,
	struct gfarm_file_section_info *info)
{
	char *e;
	int eof;

	e = xxx_proto_recv(client, 0, &eof, "ssoss",
		&info->pathname, &info->section, &info->filesize,
		&info->checksum_type, &info->checksum);
	if (eof)
		return (GFARM_ERR_PROTOCOL);
	return (e);
}

char *
xxx_proto_send_file_section_info_for_set(struct xxx_connection *client,
	const struct gfarm_file_section_info *info)
{
	/* do not send info->pathname and info->section */
	return (xxx_proto_send(client, "oss",
			info->filesize, info->checksum_type, info->checksum));
}

char *
xxx_proto_recv_file_section_info_for_set(struct xxx_connection *client,
	struct gfarm_file_section_info *info)
{
	char *e;
	int eof;

	e = xxx_proto_recv(client, 0, &eof, "oss",
		&info->filesize, &info->checksum_type, &info->checksum);
	if (eof)
		return (GFARM_ERR_PROTOCOL);
	if (e == NULL)
		info->pathname = info->section = NULL;
	return (e);
}

char *
xxx_proto_send_file_section_copy_info(struct xxx_connection *client,
	const struct gfarm_file_section_copy_info *info)
{
	return (xxx_proto_send(client, "sss", info->pathname, info->section,
			info->hostname));
}

char *
xxx_proto_recv_file_section_copy_info(struct xxx_connection *client,
	struct gfarm_file_section_copy_info *info)
{
	char *e;
	int eof;

	e = xxx_proto_recv(client, 0, &eof, "sss",
		&info->pathname, &info->section, &info->hostname);
	if (eof)
		return (GFARM_ERR_PROTOCOL);
	return (e);
}
