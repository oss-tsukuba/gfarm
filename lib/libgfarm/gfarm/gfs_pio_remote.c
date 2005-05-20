/*
 * pio operations for remote fragment
 *
 * $Id$
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <libgen.h>
#include <sys/socket.h> /* struct sockaddr */
#include <openssl/evp.h>
#include <gfarm/gfarm.h>
#include "host.h"
#include "config.h"
#include "gfs_proto.h"
#include "gfs_client.h"
#include "gfs_pio.h"

static char *
gfs_pio_remote_storage_close(GFS_File gf)
{
	struct gfs_file_section_context *vc = gf->view_context;
	struct gfs_connection *gfs_server = vc->storage_context;

	/*
	 * Do not close remote file from a child process because its
	 * open file count is not incremented.
	 * XXX - This behavior is not the same as expected, but better
	 * than closing the remote file.
	 */
	if (vc->pid != getpid())
		return (NULL);
	return (gfs_client_close(gfs_server, vc->fd));
}

static char *
gfs_pio_remote_storage_write(GFS_File gf, const char *buffer, size_t size,
			    size_t *lengthp)
{
	struct gfs_file_section_context *vc = gf->view_context;
	struct gfs_connection *gfs_server = vc->storage_context;

	/*
	 * buffer beyond GFS_PROTO_MAX_IOSIZE are just ignored by gfsd,
	 * we don't perform such GFS_PROTO_WRITE request, because it's
	 * inefficient.
	 * Note that upper gfs_pio layer should care this partial write.
	 */
	if (size > GFS_PROTO_MAX_IOSIZE)
		size = GFS_PROTO_MAX_IOSIZE;
	return (gfs_client_write(gfs_server, vc->fd, buffer, size, lengthp));
}

static char *
gfs_pio_remote_storage_read(GFS_File gf, char *buffer, size_t size,
			   size_t *lengthp)
{
	struct gfs_file_section_context *vc = gf->view_context;
	struct gfs_connection *gfs_server = vc->storage_context;

	/*
	 * Unlike gfs_pio_remote_storage_write(), we don't care
	 * buffer size here, because automatic i/o size truncation
	 * performed by gfsd isn't inefficient for read case.
	 * Note that upper gfs_pio layer should care the partial read.
	 */
	return (gfs_client_read(gfs_server, vc->fd, buffer, size, lengthp));
}

static char *
gfs_pio_remote_storage_seek(GFS_File gf, file_offset_t offset, int whence,
			   file_offset_t *resultp)
{
	struct gfs_file_section_context *vc = gf->view_context;
	struct gfs_connection *gfs_server = vc->storage_context;

	return (gfs_client_seek(gfs_server, vc->fd, offset, whence, resultp));
}

static char *
gfs_pio_remote_storage_ftruncate(GFS_File gf, file_offset_t length)
{
	struct gfs_file_section_context *vc = gf->view_context;
	struct gfs_connection *gfs_server = vc->storage_context;

	return (gfs_client_ftruncate(gfs_server, vc->fd, length));
}

static char *
gfs_pio_remote_storage_calculate_digest(GFS_File gf, char *digest_type,
				       size_t digest_size,
				       size_t *digest_lengthp,
				       unsigned char *digest,
				       file_offset_t *filesizep)
{
	struct gfs_file_section_context *vc = gf->view_context;
	struct gfs_connection *gfs_server = vc->storage_context;

	return (gfs_client_digest(gfs_server, vc->fd, digest_type, digest_size,
				  digest_lengthp, digest, filesizep));
}

static int
gfs_pio_remote_storage_fd(GFS_File gf)
{
	struct gfs_file_section_context *vc = gf->view_context;
	struct gfs_connection *gfs_server = vc->storage_context;

	return (gfs_client_connection_fd(gfs_server));
}

struct gfs_storage_ops gfs_pio_remote_storage_ops = {
	gfs_pio_remote_storage_close,
	gfs_pio_remote_storage_write,
	gfs_pio_remote_storage_read,
	gfs_pio_remote_storage_seek,
	gfs_pio_remote_storage_ftruncate,
	gfs_pio_remote_storage_calculate_digest,
	gfs_pio_remote_storage_fd,
};

char *
gfs_pio_open_remote_section(GFS_File gf, char *hostname, int flags)
{
	struct gfs_file_section_context *vc = gf->view_context;
	char *e, *path_section;
	struct gfs_connection *gfs_server;
	/*
	 * We won't use GFARM_FILE_EXCLUSIVE flag for the actual storage
	 * level access (at least for now) to avoid the effect of
	 * remaining junk files.
	 * It's already handled anyway at the metadata level.
	 *
	 * NOTE: Same thing must be done in gfs_pio_local.c.
	 */
	int oflags = (gf->open_flags & ~GFARM_FILE_EXCLUSIVE) |
	    (flags & GFARM_FILE_CREATE);
	int fd;
	struct sockaddr peer_addr;

	e = gfarm_path_section(gf->pi.pathname, vc->section, &path_section);
	if (e != NULL)
		return (e);

	e = gfarm_host_address_get(hostname, gfarm_spool_server_port,
	    &peer_addr, NULL);
	if (e != NULL) {
		free(path_section);
		return (e);
	}

	e = gfs_client_connection(vc->canonical_hostname, &peer_addr,
	    &gfs_server);
	if (e != NULL) {
		free(path_section);
		return (e);
	}
	vc->storage_context = gfs_server;

	e = gfs_client_open(gfs_server, path_section, oflags,
			    gf->pi.status.st_mode & GFARM_S_ALLPERM, &fd);
	/* FT - the parent directory may be missing */
	if (e == GFARM_ERR_NO_SUCH_OBJECT)
		if (gfs_pio_remote_mkdir_parent_canonical_path(
			    gfs_server, gf->pi.pathname) == NULL)
			e = gfs_client_open(gfs_server, path_section, oflags,
				gf->pi.status.st_mode & GFARM_S_ALLPERM, &fd);
	/* FT - physical file should be missing */
	if ((oflags & GFARM_FILE_CREATE) == 0 && e == GFARM_ERR_NO_SUCH_OBJECT)
		/* Delete the section copy info */
		if (gfarm_file_section_copy_info_remove(gf->pi.pathname,
			vc->section, vc->canonical_hostname) == NULL)
			e = GFARM_ERR_INCONSISTENT_RECOVERABLE;

	free(path_section);
	if (e != NULL)
		return (e);

	vc->ops = &gfs_pio_remote_storage_ops;
	vc->fd = fd;
	vc->pid = getpid();
	return (NULL);
}

static char *
gfs_pio_remote_mkdir_p(
	struct gfs_connection *gfs_server, char *canonic_dir)
{
	struct gfs_stat stata;
	gfarm_mode_t mode;
	char *e, *user;

	/* dirname(3) may return '.'.  This means the spool root directory. */
	if (strcmp(canonic_dir, "/") == 0 || strcmp(canonic_dir, ".") == 0)
		return (NULL); /* should exist */

	e = gfs_stat_canonical_path(canonic_dir, &stata);
	if (e != NULL)
		return (e);
	mode = stata.st_mode;
	/*
	 * XXX - if the owner of a directory is not the same, create a
	 * directory with permission 0777 - This should be fixed in
	 * the next major release.
	 */
	user = gfarm_get_global_username();
	if (strcmp(stata.st_user, user) != 0)
		mode |= 0777;
	gfs_stat_free(&stata);
	if (!GFARM_S_ISDIR(mode))
		return (GFARM_ERR_NOT_A_DIRECTORY);

	if (gfs_client_exist(gfs_server, canonic_dir)
	    == GFARM_ERR_NO_SUCH_OBJECT) {
		char *par_dir, *saved_par_dir;

		par_dir = saved_par_dir = strdup(canonic_dir);
		if (par_dir == NULL)
			return (GFARM_ERR_NO_MEMORY);
		par_dir = dirname(par_dir);
		e = gfs_pio_remote_mkdir_p(gfs_server, par_dir);
		free(saved_par_dir);
		if (e != NULL)
			return (e);
		
		e = gfs_client_mkdir(gfs_server, canonic_dir, mode);
	}
	return (e);
}

char *
gfs_pio_remote_mkdir_parent_canonical_path(
	struct gfs_connection *gfs_server, char *canonic_dir)
{
	char *par_dir, *saved_par_dir, *e;

	par_dir = saved_par_dir = strdup(canonic_dir);
	if (par_dir == NULL)
		return (GFARM_ERR_NO_MEMORY);

	par_dir = dirname(par_dir);
	if (gfs_client_exist(gfs_server, par_dir) == GFARM_ERR_NO_SUCH_OBJECT)
		e = gfs_pio_remote_mkdir_p(gfs_server, par_dir);
	else
		e = GFARM_ERR_ALREADY_EXISTS;

	free(saved_par_dir);

	return (e);
}
