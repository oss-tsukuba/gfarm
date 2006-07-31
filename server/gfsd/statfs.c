#include <stddef.h>
#include <errno.h>
#include <gfarm/gfarm.h>
#include "gfsd_subr.h"

#if defined(HAVE_STATVFS)
#include <sys/types.h>
#include <sys/statvfs.h>

int gfsd_statfs(char *path, gfarm_int32_t *bsizep,
	file_offset_t *blocksp, file_offset_t *bfreep, file_offset_t *bavailp,
	file_offset_t *filesp, file_offset_t *ffreep, file_offset_t *favailp)
{
	struct statvfs buf;

	if (statvfs(path, &buf) == -1)
		return (errno);
	*bsizep = buf.f_frsize;
	*blocksp = buf.f_blocks;
	*bfreep = buf.f_bfree;
	*bavailp = buf.f_bavail;
	*filesp = buf.f_files;
	*ffreep = buf.f_ffree;
	*favailp = buf.f_favail;
	return (0);
}

#elif defined(HAVE_STATFS)
#if defined(__linux__)
#include <sys/vfs.h>
#else
#include <sys/param.h>
#include <sys/mount.h>
#endif

int gfsd_statfs(char *path, gfarm_int32_t *bsizep,
	file_offset_t *blocksp, file_offset_t *bfreep, file_offset_t *bavailp,
	file_offset_t *filesp, file_offset_t *ffreep, file_offset_t *favailp)
{
	struct statfs buf;

	if (statfs(path, &buf) == -1)
		return (errno);
	*bsizep = buf.f_bsize;
	*blocksp = buf.f_blocks;
	*bfreep = buf.f_bfree;
	*bavailp = buf.f_bavail;
	*filesp = buf.f_files;
	*ffreep = buf.f_ffree;
	*favailp = buf.f_ffree; /* assumes there is no limit about i-node */
	return (0);
}

#else

int gfsd_statfs(char *path, gfarm_int32_t *bsizep,
	file_offset_t *blocksp, file_offset_t *bfreep, file_offset_t *bavailp,
	file_offset_t *filesp, file_offset_t *ffreep, file_offset_t *favailp)
{
	return (ENOSYS);
}

#endif
