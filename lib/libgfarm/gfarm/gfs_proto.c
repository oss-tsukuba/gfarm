#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <openssl/evp.h>
#include <gfarm/gfarm.h>
#include "gfs_proto.h"

char GFS_SERVICE_TAG[] = "gfarm-data";

/*
 * Not really public interface,
 * but common routine called from both client and server.
 */
int
gfs_digest_calculate_local(int fd, char *buffer, size_t buffer_size,
	const EVP_MD *md_type, EVP_MD_CTX *md_ctx,
	size_t *md_lenp, unsigned char *md_value,
	gfarm_off_t *filesizep)
{
	int size;
	gfarm_off_t off = 0;
	unsigned int len;

	if (lseek(fd, (off_t)0, 0) == -1) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"lseek() failed: %s",
			gfarm_error_string(gfarm_errno_to_error(errno)));
		return (errno);
	}

	EVP_DigestInit(md_ctx, md_type);
	while ((size = read(fd, buffer, buffer_size)) > 0) {
		EVP_DigestUpdate(md_ctx, buffer, size);
		off += size;
	}
	EVP_DigestFinal(md_ctx, md_value, &len);

	*md_lenp = len;
	*filesizep = off;

	if (size == -1)
		gflog_debug(GFARM_MSG_UNFIXED,
			"read() failed: %s",
			gfarm_error_string(gfarm_errno_to_error(errno)));

	return (size == -1 ? errno : 0);
}
