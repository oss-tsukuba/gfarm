/*
 * $Id$
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>
#include "gfutil.h"
#include "gfm_client.h"

extern int debug_mode;
extern struct gfm_connection *gfm_server;
void fatal_metadb_proto(const char *, const char *, gfarm_error_t);

static gfarm_error_t
gfm_client_replica_add(gfarm_ino_t inum, gfarm_uint64_t gen, gfarm_off_t size)
{
	gfarm_error_t e;
	char *diag = "replica_add";

	if ((e = gfm_client_replica_add_request(gfm_server, inum, gen, size))
	    != GFARM_ERR_NO_ERROR)
		fatal_metadb_proto("replica_add request", diag, e);
	else if ((e = gfm_client_replica_add_result(gfm_server))
	    != GFARM_ERR_NO_ERROR) {
		if (debug_mode && e != GFARM_ERR_ALREADY_EXISTS)
			gflog_info("replica_add result: %s",
			    gfarm_error_string(e));
	}
	return (e);
}

/*
 * File format should be consistent with local_path() in gfsd.c
 */
static int
get_inum_gen(const char *path, gfarm_ino_t *inump, gfarm_uint64_t *genp)
{
	unsigned int inum32, inum24, inum16, inum8, inum0;
	unsigned int gen32, gen0;
	gfarm_ino_t inum;
	gfarm_uint64_t gen;

	if (sscanf(path, "data/%08X/%02X/%02X/%02X/%02X%08X%08X",
		   &inum32, &inum24, &inum16, &inum8, &inum0, &gen32, &gen0)
	    != 7)
		return (-1);

	inum = ((gfarm_ino_t)inum32 << 32) + (inum24 << 24) +
		(inum16 << 16) + (inum8 << 8) + inum0;
	gen = ((gfarm_uint64_t)gen32 << 32) + gen0;
	*inump = inum;
	*genp = gen;

	return (0);
}

static int delete_invalid_file = 0;

static gfarm_error_t
dir_foreach(
	gfarm_error_t (*op_file)(char *, struct stat *, void *),
	gfarm_error_t (*op_dir1)(char *, struct stat *, void *),
	gfarm_error_t (*op_dir2)(char *, struct stat *, void *),
	char *dir, void *arg)
{
	DIR* dirp;
	struct dirent *dp;
	struct stat st;
	gfarm_error_t e;
	char *dir1;

	if (lstat(dir, &st))
		return (gfarm_errno_to_error(errno));

	if (S_ISREG(st.st_mode)) {
		if (op_file != NULL)
			return (op_file(dir, &st, arg));
		else
			return (GFARM_ERR_NO_ERROR);
	}
	if (!S_ISDIR(st.st_mode))
		return (GFARM_ERR_INVALID_ARGUMENT); /* XXX */

	if (op_dir1 != NULL) {
		e = op_dir1(dir, &st, arg);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
	}
	dirp = opendir(dir);
	if (dirp == NULL)
		return (gfarm_errno_to_error(errno));

	/* if dir is '.', remove it */
	if (dir[0] == '.' && dir[1] == '\0')
		dir = "";
	while ((dp = readdir(dirp)) != NULL) {
		if (dp->d_name[0] == '.' && (dp->d_name[1] == '\0' ||
		    (dp->d_name[1] == '.' && dp->d_name[2] == '\0')))
			continue;

		GFARM_MALLOC_ARRAY(dir1, strlen(dir) + strlen(dp->d_name) + 2);
		if (dir1 == NULL) {
			closedir(dirp);
			return (GFARM_ERR_NO_MEMORY);
		}
		strcpy(dir1, dir);
		if (strcmp(dir, ""))
			strcat(dir1, "/");
		strcat(dir1, dp->d_name);
		(void)dir_foreach(op_file, op_dir1, op_dir2, dir1, arg);
		free(dir1);
	}
	if (closedir(dirp))
		return (gfarm_errno_to_error(errno));
	if (op_dir2 != NULL)
		return (op_dir2(dir, &st, arg));
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
unlink_file(char *file, struct stat *st, void *arg)
{
	if (unlink(file))
		return (gfarm_errno_to_error(errno));
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
unlink_chmod(char *dir, struct stat *st, void *arg)
{
	/* try to allow read and write access always */
	(void)chmod(dir, (st->st_mode | S_IRUSR | S_IWUSR) & 07777);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
unlink_rmdir(char *dir, struct stat *st, void *arg)
{
	if (rmdir(dir))
		return (gfarm_errno_to_error(errno));
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
unlink_dir(char *src)
{
	return (dir_foreach(unlink_file, unlink_chmod, unlink_rmdir,
			    src, NULL));
}

static gfarm_error_t
delete_invalid_file_or_directory(char *pathname)
{
	gfarm_error_t e;

	if (!delete_invalid_file) {
		gflog_notice("%s: invalid file", pathname);
		return (GFARM_ERR_NO_ERROR);
	}

	e = unlink_dir(pathname);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_warning("%s: cannot delete", pathname);
	else
		gflog_notice("%s: deleted", pathname);
	return (e);
}

static gfarm_error_t
fixfrag(char *path)
{
	gfarm_ino_t inum;
	gfarm_uint64_t gen;
	gfarm_off_t size;
	gfarm_error_t e;
	struct stat st;

	if (get_inum_gen(path, &inum, &gen))
		return (delete_invalid_file_or_directory(path));
	if (stat(path, &st))
		return (gfarm_errno_to_error(errno));
	size = st.st_size;
	e = gfm_client_replica_add(inum, gen, size);
	if (e == GFARM_ERR_ALREADY_EXISTS)
		/* correct entry */
		e = GFARM_ERR_NO_ERROR;
	else if (e == GFARM_ERR_NO_ERROR)
		gflog_notice("%s: fixed", path);
	else if (e == GFARM_ERR_INVALID_FILE_REPLICA)
		return (delete_invalid_file_or_directory(path));
	return (e);
}

static gfarm_error_t
fixdir_file(char *file, struct stat *st, void *arg)
{
	return (fixfrag(file));
}

static gfarm_error_t
fixdir(char *dir)
{
	return (dir_foreach(fixdir_file, NULL, NULL, dir, NULL));
}

/*
 * check_level:
 *  0, 1      ... display invalid files
 *  otherwise ... delete invalid files
 */
gfarm_error_t
gfsd_spool_check(int check_level)
{
	switch (check_level) {
	case 0:
	case 1:
		delete_invalid_file = 0;
		break;
	default:
		delete_invalid_file = 1;
		break;
	}
	return (fixdir("."));
}
