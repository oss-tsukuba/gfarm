#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <openssl/evp.h>
#include <gfarm/gfarm.h>
#include "gfs_proto.h"

char GFS_SERVICE_TAG[] = "gfarm-data";

int gfs_proto_error_to_errno_map[] = {
	0,
	EPERM,
	ENOENT,
	ESRCH,
	EINTR,
	EIO,
	ENXIO,
	E2BIG,
	ENOEXEC,
	EBADF,
	EDEADLK,
	ENOMEM,
	EACCES,
	EFAULT,
	EBUSY,
	EEXIST,
	ENOTDIR,
	EISDIR,
	EINVAL, /* real EINVAL, should be first EINVAL entry for first match */
	ENFILE,
	EMFILE,
	ETXTBSY,
	EFBIG,
	ENOSPC,
	ESPIPE,
	EROFS,
	EPIPE,
	EAGAIN,
	EOPNOTSUPP,
	ECONNREFUSED,
	ELOOP,
	ENAMETOOLONG,
	EDQUOT,
	ESTALE,
	ENOLCK,
	ENOSYS,
#ifdef EAUTH /* BSD */
	EAUTH,
#else
	EPERM,
#endif
#ifdef ETIME /* Linux */
	ETIME,
#else
	EINVAL, /* as GFS_ERROR_UNKNOWN */
#endif
#ifdef EPROTO /* SVR4 and Linux */
	EPROTO,
#else
	EPROTONOSUPPORT,
#endif
	EPROTONOSUPPORT,
	EINVAL /* as GFS_ERROR_UNKNOWN */
};

/*
 * convert machine independent error number to error string.
 * (errno may be different between platforms)
 */
char *
gfs_proto_error_string(enum gfs_proto_error err)
{
	/*
	 * The reason we treat somes errors as special cases
	 * is that those errors may not have corresponding errno on some
	 * platforms.
	 * (So gfs_proto_error_to_errno_map[] cannot convert such errors).
	 */

	return (err == GFS_ERROR_AUTH ? GFARM_ERR_AUTHENTICATION :
		err == GFS_ERROR_EXPIRED ? GFARM_ERR_EXPIRED :
		err == GFS_ERROR_PROTO ? GFARM_ERR_PROTOCOL :
		err == GFS_ERROR_PROTONOSUPPORT ?
					GFARM_ERR_PROTOCOL_NOT_SUPPORTED:
		err >= GFS_ERROR_UNKNOWN ? GFARM_ERR_UNKNOWN :
		gfarm_errno_to_error(gfs_proto_error_to_errno_map[err]));
}

/*
 * convert error string to machine independent error number.
 * (errno may be different between platforms)
 *
 * See also gfarm_errno_error_map[] in error.c.
 */
enum gfs_proto_error
gfs_string_to_proto_error(char *e)
{
	/*
	 * The reason we treat somes errors as special cases
	 * is that those errors may not have corresponding errno on some
	 * platforms.
	 * (So gfs_errno_to_proto_error() cannot convert such errors).
	 */

	return (e == GFARM_ERR_AUTHENTICATION ? GFS_ERROR_AUTH :
		e == GFARM_ERR_EXPIRED ? GFS_ERROR_EXPIRED :
		e == GFARM_ERR_PROTOCOL ? GFS_ERROR_PROTO :
		e == GFARM_ERR_PROTOCOL_NOT_SUPPORTED ?
					GFS_ERROR_PROTONOSUPPORT :
		e == GFARM_ERR_UNKNOWN ? GFS_ERROR_UNKNOWN :
		gfs_errno_to_proto_error(gfarm_error_to_errno(e)));
}

#ifndef ELAST /* sys_nerr isn't constant */
#define ELAST 127
#endif
enum gfs_proto_error gfs_errno_to_proto_error_map[ELAST + 1];
int gfs_errno_to_proto_error_initialized;
int gfs_errno_to_proto_error_table_overflow;

void
gfs_errno_to_proto_error_initialize(void)
{
	enum gfs_proto_error err;
	int unix_errno;

	if (gfs_errno_to_proto_error_initialized)
		return;

	/* sanity */
	if (GFARM_ARRAY_LENGTH(gfs_proto_error_to_errno_map) !=
	    GFS_ERROR_NUMBER)
		abort();

	gfs_errno_to_proto_error_initialized = 1;

	for (err = GFS_ERROR_NOERROR;
	     err < GFARM_ARRAY_LENGTH(gfs_proto_error_to_errno_map); err++) {
		unix_errno = gfs_proto_error_to_errno_map[err];
		if (unix_errno > ELAST) {
			gfs_errno_to_proto_error_table_overflow = 1;
		} else if (gfs_errno_to_proto_error_map[unix_errno] == 0) {
			/* make sure to be first match */
			gfs_errno_to_proto_error_map[unix_errno] = err;
		}
	}
}

/*
 * convert errno to machine independent error number,
 * because errno may be different between platforms.
 */
enum gfs_proto_error
gfs_errno_to_proto_error(int unix_errno)
{
	enum gfs_proto_error err;

	if (!gfs_errno_to_proto_error_initialized)
		gfs_errno_to_proto_error_initialize();

	if (unix_errno <= ELAST) {
		err = gfs_errno_to_proto_error_map[unix_errno];
		if (err != GFS_ERROR_NOERROR) /* i.e. != 0 */
			return (err);
		else if (unix_errno == 0)
			return (GFS_ERROR_NOERROR);
		else
			return (GFS_ERROR_UNKNOWN); /* errno not mapped */
	}

	if (gfs_errno_to_proto_error_table_overflow) {
		for (err = GFS_ERROR_NOERROR; err < GFS_ERROR_NUMBER; err++)
			if (unix_errno == gfs_proto_error_to_errno_map[err])
				return (err);
	}

	return (GFS_ERROR_UNKNOWN); /* errno not mapped */
}

/*
 * Not really public interface,
 * but common routine called from both client and server.
 */
int
gfs_open_flags_localize(int open_flags)
{
	int local_flags;

	switch (open_flags & GFARM_FILE_ACCMODE) {
	case GFARM_FILE_RDONLY:	local_flags = O_RDONLY; break;
	case GFARM_FILE_WRONLY:	local_flags = O_WRONLY; break;
	case GFARM_FILE_RDWR:	local_flags = O_RDWR; break;
	default: return (-1);
	}

	if ((open_flags & GFARM_FILE_CREATE) != 0)
		local_flags |= O_CREAT;
	if ((open_flags & GFARM_FILE_TRUNC) != 0)
		local_flags |= O_TRUNC;
	if ((open_flags & GFARM_FILE_APPEND) != 0)
		local_flags |= O_APPEND;
	if ((open_flags & GFARM_FILE_EXCLUSIVE) != 0)
		local_flags |= O_EXCL;
	return (local_flags);
}

/*
 * Not really public interface,
 * but common routine called from both client and server.
 */
int
gfs_digest_calculate_local(int fd, char *buffer, size_t buffer_size,
	const EVP_MD *md_type, EVP_MD_CTX *md_ctx,
	size_t *md_lenp, unsigned char *md_value,
	file_offset_t *filesizep)
{
	int size;
	file_offset_t off = 0;
	unsigned int len;

	if (lseek(fd, (off_t)0, 0) == -1)
		return (errno);

	EVP_DigestInit(md_ctx, md_type);
	while ((size = read(fd, buffer, buffer_size)) > 0) {
		EVP_DigestUpdate(md_ctx, buffer, size);
		off += size;
	}
	EVP_DigestFinal(md_ctx, md_value, &len);

	*md_lenp = len;
	*filesizep = off;
	return (size == -1 ? errno : 0);
}
