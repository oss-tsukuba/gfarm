/*
 * pio operations for remote fragment
 *
 * $Id$
 */

#include <stdlib.h>
#include <sys/socket.h> /* struct sockaddr */
#include <openssl/evp.h>
#include <gfarm/gfarm.h>
#include "host.h"
#include "gfs_proto.h"
#include "gfs_client.h"
#include "gfs_pio.h"

static char *
gfs_pio_remote_storage_close(GFS_File gf)
{
	struct gfs_file_section_context *vc = gf->view_context;
	struct gfs_connection *gfs_server = vc->storage_context;

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
	gfs_pio_remote_storage_calculate_digest,
	gfs_pio_remote_storage_fd,
};

char *
gfs_pio_open_remote_section(GFS_File gf, char *hostname, int flags)
{
	struct gfs_file_section_context *vc = gf->view_context;
	char *e, *path_section;
	struct gfs_connection *gfs_server;
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

	e = gfs_client_open(gfs_server, path_section, gf->open_flags,
			    gf->pi.status.st_mode & GFARM_S_ALLPERM, &fd);
	free(path_section);
	if (e != NULL)
		return (e);

	vc->ops = &gfs_pio_remote_storage_ops;
	vc->storage_context = gfs_server;
	vc->fd = fd;
	return (NULL);
}
