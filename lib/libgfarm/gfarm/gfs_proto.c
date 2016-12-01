#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include <openssl/evp.h>

#include <gfarm/gfarm.h>

#define GFARM_USE_OPENSSL
#include "msgdigest.h"

#include "gfs_proto.h"

char GFS_SERVICE_TAG[] = "gfarm-data";

/*
 * Not really public interface,
 * but common routine called from both client and server.
 */
int
gfs_digest_calculate_local(int fd, char *buffer, size_t buffer_size,
	const EVP_MD *md_type, size_t *md_lenp, unsigned char *md_value,
	gfarm_off_t *filesizep)
{
	EVP_MD_CTX *md_ctx;
	int size, save_errno;
	gfarm_off_t off = 0;
	unsigned int len;

	if (lseek(fd, (off_t)0, 0) == -1) {
		save_errno = errno;
		gflog_debug(GFARM_MSG_1001020, "lseek() failed: %s",
			strerror(save_errno));
		return (save_errno);
	}

	md_ctx = gfarm_msgdigest_alloc(md_type);
	while ((size = read(fd, buffer, buffer_size)) > 0) {
		EVP_DigestUpdate(md_ctx, buffer, size);
		off += size;
	}
	len = gfarm_msgdigest_free(md_ctx, md_value);

	*md_lenp = len;
	*filesizep = off;

	if (size == -1) {
		save_errno = errno;
		gflog_debug(GFARM_MSG_1001021, "read() failed: %s",
			strerror(save_errno));
		return (save_errno);
	}

	return (0);
}
