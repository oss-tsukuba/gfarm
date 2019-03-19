#include <assert.h>
#include <stdio.h>
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

#include "config.h"

#include "gfsd_subr.h"

const char TEST_FILE[] = "/.test";

const char READONLY_CONFIG_SPOOL_FILE[] = ".readonly"; /* created by hand */

const char READONLY_CONFIG_TMP_DIR[] = "/tmp";
const char *READONLY_CONFIG_TMP_FILE;	/* created by gfhost -m -f 1 */

/* initialize READONLY_CONFIG_TMP_FILE */
void
gfsd_readonly_config_init(int port)
{
	/* /tmp/gfsd-readonly-<IP address>_port<port number> */
	const char prefix[] = "gfsd-readonly-";
	char *tmp_path;
	int len = strlen(READONLY_CONFIG_TMP_DIR) + 1 /* / */
		+ strlen(prefix)
		+ strlen(canonical_self_name) + 1 /* _ */
		+ 6 /* port(0~65535) + \0 */;

	GFARM_MALLOC_ARRAY(tmp_path, len);
	if (tmp_path == NULL)
		gflog_fatal(GFARM_MSG_1005084,
		    "readonly_config_init: %s",
		    gfarm_error_string(GFARM_ERR_NO_MEMORY));

	snprintf(tmp_path, len, "%s/%s%s_%d",
	    READONLY_CONFIG_TMP_DIR, prefix, canonical_self_name, port);
	READONLY_CONFIG_TMP_FILE = tmp_path;
	gflog_info(GFARM_MSG_1005085, "READONLY_CONFIG_TMP_FILE=%s",
	    READONLY_CONFIG_TMP_FILE);
}

void
gfsd_readonly_config_update(int host_info_flags)
{
	int rv;
	static const char diag[] = "gfsd_readonly_config_update";

	if (host_info_flags_is_readonly(host_info_flags)) {
		if ((rv = open(READONLY_CONFIG_TMP_FILE,
			       O_RDONLY|O_CREAT|O_EXCL, 0400)) != -1) {
			close(rv);
			gflog_info(GFARM_MSG_1005086, "%s(enabled), %s",
			    diag, READONLY_CONFIG_TMP_FILE);
		} else if (errno == EEXIST)
			;
		else
			gflog_warning(GFARM_MSG_1005087,
			    "%s(enabled), %s: %s",
			    diag, READONLY_CONFIG_TMP_FILE, strerror(errno));
	} else {
		if ((rv = unlink(READONLY_CONFIG_TMP_FILE)) != -1)
			gflog_info(GFARM_MSG_1005088, "%s(disabled), %s",
			    diag, READONLY_CONFIG_TMP_FILE);
		else if (errno == ENOENT)
			;
		else
			gflog_warning(GFARM_MSG_1005089,
			    "%s(disabled), %s: %s",
			    diag, READONLY_CONFIG_TMP_FILE, strerror(errno));
	}
}

static int
readonly_config_tmp_file_exists(void)
{
	struct stat st;

	return (stat(READONLY_CONFIG_TMP_FILE, &st) == 0);
}

/* i: spool index */
static int
readonly_config_spool_file_exists(int i)
{
	struct stat st;
	int length;
	static char **p = NULL;
	static const char diag[] = "readonly_config_spool_file_exists";

	if (i < 0 || i >= gfarm_spool_root_num)
		fatal(GFARM_MSG_1004476, "%s: internal error: %d / %d", diag,
		    i, gfarm_spool_root_num);
	if (p == NULL) {
		GFARM_CALLOC_ARRAY(p, gfarm_spool_root_num);
		if (p == NULL)
			fatal(GFARM_MSG_1004477, "%s: no memory for %d bytes",
			    diag, gfarm_spool_root_num);
	}
	if (p[i] == NULL) {
		length = gfarm_spool_root_len[i] + 1 +
			sizeof(READONLY_CONFIG_SPOOL_FILE);
		GFARM_MALLOC_ARRAY(p[i], length);
		if (p[i] == NULL)
			fatal(GFARM_MSG_1000503, "%s: no memory for %d bytes",
			    diag, length);
		snprintf(p[i], length, "%s/%s", gfarm_spool_root[i],
			 READONLY_CONFIG_SPOOL_FILE);
	}
	return (stat(p[i], &st) == 0);
}

/* i: spool index */
int
gfsd_is_readonly_mode(int i)
{
	if (readonly_config_tmp_file_exists())
		return (1);
	if (readonly_config_spool_file_exists(i))
		return (1);
	return (0);
}

/* 1: EROFS or ENOSPC */
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

static int
gfsd_statfs(char *path, gfarm_int32_t *bsizep,
	gfarm_off_t *blocksp, gfarm_off_t *bfreep, gfarm_off_t *bavailp,
	gfarm_off_t *filesp, gfarm_off_t *ffreep, gfarm_off_t *favailp)
{
	struct statvfs buf;

	if (statvfs(path, &buf) == -1)
		return (errno);
	/*
	 * to check ENOSPC we do not use f_flag
	 * readonly = (buf.f_flag & ST_RDONLY) != 0;
	 */
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

static int
gfsd_statfs(char *path, gfarm_int32_t *bsizep,
	gfarm_off_t *blocksp, gfarm_off_t *bfreep, gfarm_off_t *bavailp,
	gfarm_off_t *filesp, gfarm_off_t *ffreep, gfarm_off_t *favailp)
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

static int
gfsd_statfs(char *path, gfarm_int32_t *bsizep,
	gfarm_off_t *blocksp, gfarm_off_t *bfreep, gfarm_off_t *bavailp,
	gfarm_off_t *filesp, gfarm_off_t *ffreep, gfarm_off_t *favailp)
{
	return (ENOSYS);
}

#endif

/* i: spool index */
int
gfsd_statfs_readonly(int i, gfarm_int32_t *bsizep,
	gfarm_off_t *blocksp, gfarm_off_t *bfreep, gfarm_off_t *bavailp,
	gfarm_off_t *filesp, gfarm_off_t *ffreep, gfarm_off_t *favailp,
	int *readonlyp)
{
	int err, readonly;
	char *spool = gfarm_spool_root[i];

	assert(spool != NULL);
	err = gfsd_statfs(spool, bsizep, blocksp, bfreep, bavailp,
	    filesp, ffreep, favailp);
	if (err)
		return (err);

	readonly = readonly_config_tmp_file_exists();
	if (readonly)
		goto end;

	readonly = readonly_config_spool_file_exists(i);
	if (readonly) {
		/*
		 * READONLY_CONFIG_SPOOL_FILE always behaves disk-full
		 * for backward compatibility reason.
		 */
		*bavailp = *bfreep = 0;
		goto end;
	}

	/*
	 * Do not write to TEST_FILE (Do not call is_readonly())
	 * when readonly_config_*_file_exists() is True.
	 */
	readonly = is_readonly(spool);
	if (readonly == -1)
		return (errno);
	else if (readonly) {
		*bavailp = *bfreep = 0;
		gflog_error(GFARM_MSG_1003715,
		    "%s: read only file system", spool);
	}
end:
	*readonlyp = readonly;
	return (0);
}
