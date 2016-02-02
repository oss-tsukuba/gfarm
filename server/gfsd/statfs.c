#include <stddef.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <gfarm/gfarm.h>
#include "gfsd_subr.h"

const char TEST_FILE[] = "/.test";

static int
is_readonly(char *path)
{
	char *testfile;
	int fd, ret = 0, save_errno;

	GFARM_MALLOC_ARRAY(testfile, strlen(path) + sizeof(TEST_FILE));
	if (testfile == NULL) {
		gflog_error(GFARM_MSG_1003717, "is_readonly: no memory");
		errno = ENOMEM;
		return (-1);
	}
	strcpy(testfile, path);
	strcat(testfile, TEST_FILE);
	if ((fd = creat(testfile, 0600)) != -1) {
		close(fd);
		unlink(testfile);
	} else if (errno == EROFS || errno == ENOSPC)
		ret = 1;
	else {
		save_errno = errno;
		gflog_warning(GFARM_MSG_1003718, "is_readonly: %s",
		    strerror(errno));
		ret = -1;
	}
	free(testfile);
	if (ret == -1)
		errno = save_errno;
	return (ret);
}

#if defined(HAVE_STATVFS)
#include <sys/statvfs.h>

int gfsd_statfs(char *path, gfarm_int32_t *bsizep,
	gfarm_off_t *blocksp, gfarm_off_t *bfreep, gfarm_off_t *bavailp,
	gfarm_off_t *filesp, gfarm_off_t *ffreep, gfarm_off_t *favailp,
	int *readonlyp)
{
	struct statvfs buf;
	int readonly;

	if (statvfs(path, &buf) == -1)
		return (errno);
	/*
	 * to check ENOSPC we do not use f_flag
	 * readonly = (buf.f_flag & ST_RDONLY) != 0;
	 */
	if ((readonly = is_readonly(path)) == -1)
		return (errno);
	*bsizep = buf.f_frsize;
	*blocksp = buf.f_blocks;
	*bfreep = buf.f_bfree;
	*bavailp = buf.f_bavail;
	*filesp = buf.f_files;
	*ffreep = buf.f_ffree;
	*favailp = buf.f_favail;
	*readonlyp = readonly;
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
	gfarm_off_t *blocksp, gfarm_off_t *bfreep, gfarm_off_t *bavailp,
	gfarm_off_t *filesp, gfarm_off_t *ffreep, gfarm_off_t *favailp,
	int *readonlyp)
{
	struct statfs buf;
	int readonly;

	if (statfs(path, &buf) == -1)
		return (errno);
	if ((readonly = is_readonly(path)) == -1)
		return (errno);
	*bsizep = buf.f_bsize;
	*blocksp = buf.f_blocks;
	*bfreep = buf.f_bfree;
	*bavailp = buf.f_bavail;
	*filesp = buf.f_files;
	*ffreep = buf.f_ffree;
	*favailp = buf.f_ffree; /* assumes there is no limit about i-node */
	*readonlyp = readonly;
	return (0);
}

#else

int gfsd_statfs(char *path, gfarm_int32_t *bsizep,
	gfarm_off_t *blocksp, gfarm_off_t *bfreep, gfarm_off_t *bavailp,
	gfarm_off_t *filesp, gfarm_off_t *ffreep, gfarm_off_t *favailp)
{
	return (ENOSYS);
}

#endif
