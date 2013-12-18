#include <stddef.h>
#include <errno.h>
#include <gfarm/gfarm.h>
#include "gfsd_subr.h"

#if defined(HAVE_STATVFS)
#include <sys/types.h>
#include <sys/statvfs.h>

int gfsd_statfs(char *path, gfarm_int32_t *bsizep,
	gfarm_off_t *blocksp, gfarm_off_t *bfreep, gfarm_off_t *bavailp,
	gfarm_off_t *filesp, gfarm_off_t *ffreep, gfarm_off_t *favailp,
	int *readonlyp)
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
	*readonlyp = (buf.f_flag & ST_RDONLY) != 0;
	return (0);
}

#elif defined(HAVE_STATFS)
#if defined(__linux__)
#include <sys/vfs.h>
#else
#include <sys/param.h>
#include <sys/mount.h>
#endif

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

static int
is_readonly(char *path)
{
	char *testfile;
	int fd, ret = 0;

	GFARM_MALLOC_ARRAY(testfile, strlen(path) + 3);
	if (testfile == NULL) {
		gflog_error(GFARM_MSG_UNFIXED, "is_readonly: no memory");
		return (ret);
	}
	strcpy(testfile, path);
	strcat(testfile, "/a");
	if ((fd = creat(testfile, 0400)) != -1) {
		close(fd);
		unlink(testfile);
	} else if (errno == EROFS)
		ret = 1;
	else
		gflog_warning(GFARM_MSG_UNFIXED, "is_readonly: %s",
		    strerror(errno));
	free(testfile);
	return (ret);
}

int gfsd_statfs(char *path, gfarm_int32_t *bsizep,
	gfarm_off_t *blocksp, gfarm_off_t *bfreep, gfarm_off_t *bavailp,
	gfarm_off_t *filesp, gfarm_off_t *ffreep, gfarm_off_t *favailp,
	int *readonlyp)
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
	*readonlyp = is_readonly(path);
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
