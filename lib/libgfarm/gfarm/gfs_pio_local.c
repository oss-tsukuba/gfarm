/*
 * pio operations for local section
 *
 * $Id$
 */

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <openssl/evp.h>
#include <gfarm/gfarm.h>
#include "gfs_proto.h" /* for gfs_digest_calculate_local() */
#include "gfs_pio.h"

int gfarm_node = -1;
int gfarm_nnode = -1;

char *
gfs_pio_set_local(int node, int nnode)
{
	if (node < 0 || node >= nnode || nnode < 0)
		return (GFARM_ERR_INVALID_ARGUMENT);

	gfarm_node = node;
	gfarm_nnode = nnode;
	return (NULL);
}

char *
gfs_pio_set_local_check(void)
{
	if (gfarm_node < 0 || gfarm_nnode <= 0)
		return ("gfs_pio_set_local() is not correctly called");
	return (NULL);
}

char *
gfs_pio_get_node_rank(int *node)
{
	char *e = gfs_pio_set_local_check();

	if (e != NULL)
		return (e);
	*node = gfarm_node;
	return (NULL);
}

char *
gfs_pio_get_node_size(int *nnode)
{
	char *e = gfs_pio_set_local_check();

	if (e != NULL)
		return (e);
	*nnode = gfarm_nnode;
	return (NULL);
}

char *
gfs_pio_set_view_local(GFS_File gf, int flags)
{
	char *e = gfs_pio_set_local_check();

	if (e != NULL)
		return (e);
	return (gfs_pio_set_view_index(gf, gfarm_nnode, gfarm_node,
				       NULL, flags));
}

/***********************************************************************/

static char *
gfs_pio_local_storage_close(GFS_File gf)
{
	struct gfs_file_section_context *vc = gf->view_context;

	if (close(vc->fd) == -1)
		return (gfarm_errno_to_error(errno));
	return (NULL);
}

static char *
gfs_pio_local_storage_write(GFS_File gf, const char *buffer, size_t size,
			    size_t *lengthp)
{
	struct gfs_file_section_context *vc = gf->view_context;
	int rv = write(vc->fd, buffer, size);

	if (rv == -1)
		return (gfarm_errno_to_error(errno));
	*lengthp = rv;
	return (NULL);
}

static char *
gfs_pio_local_storage_read(GFS_File gf, char *buffer, size_t size,
			   size_t *lengthp)
{
	struct gfs_file_section_context *vc = gf->view_context;
	int rv = read(vc->fd, buffer, size);

	if (rv == -1)
		return (gfarm_errno_to_error(errno));
	*lengthp = rv;
	return (NULL);
}

static char *
gfs_pio_local_storage_seek(GFS_File gf, file_offset_t offset, int whence,
			   file_offset_t *resultp)
{
	struct gfs_file_section_context *vc = gf->view_context;
	off_t rv = lseek(vc->fd, (off_t)offset, whence);

	if (rv == -1)
		return (gfarm_errno_to_error(errno));
	if (resultp != NULL)
		*resultp = rv;
	return (NULL);
}

static char *
gfs_pio_local_storage_calculate_digest(GFS_File gf, char *digest_type,
				       size_t digest_size,
				       size_t *digest_lengthp,
				       unsigned char *digest,
				       file_offset_t *filesizep)
{
	struct gfs_file_section_context *vc = gf->view_context;
	const EVP_MD *md_type;
	int rv;
	static int openssl_initialized = 0;

	if (!openssl_initialized) {
		SSLeay_add_all_algorithms(); /* for EVP_get_digestbyname() */
		openssl_initialized = 1;
	}
	if ((md_type = EVP_get_digestbyname(digest_type)) == NULL)
		return (GFARM_ERR_INVALID_ARGUMENT);

	/* note that this effectively breaks file offset. */
	rv = gfs_digest_calculate_local(
	    vc->fd, gf->buffer, GFS_FILE_BUFSIZE,
	    md_type, &vc->md_ctx, digest_lengthp, digest,
	    filesizep);
	if (rv != 0)
		return (gfarm_errno_to_error(rv));
	return (NULL);
}

static int
gfs_pio_local_storage_fd(GFS_File gf)
{
	struct gfs_file_section_context *vc = gf->view_context;

	return (vc->fd);
}

struct gfs_storage_ops gfs_pio_local_storage_ops = {
	gfs_pio_local_storage_close,
	gfs_pio_local_storage_write,
	gfs_pio_local_storage_read,
	gfs_pio_local_storage_seek,
	gfs_pio_local_storage_calculate_digest,
	gfs_pio_local_storage_fd,
};

char *
gfs_pio_open_local_section(GFS_File gf, int flags)
{
	struct gfs_file_section_context *vc = gf->view_context;
	char *e, *local_path;
	int fd, open_flags = gfs_open_flags_localize(gf->open_flags);

	if (open_flags == -1)
		return (GFARM_ERR_INVALID_ARGUMENT);

	e = gfarm_path_localize_file_section(gf->pi.pathname, vc->section,
					     &local_path);
	if (e != NULL)
		return (e);

	fd = open(local_path, open_flags,
		  gf->pi.status.st_mode & GFARM_S_ALLPERM);
	free(local_path);
	if (fd == -1)
		return (gfarm_errno_to_error(errno));

	vc->ops = &gfs_pio_local_storage_ops;
	vc->storage_context = NULL; /* not needed */
	vc->fd = fd;
	return (NULL);
}
