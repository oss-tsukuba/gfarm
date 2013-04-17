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

#include <gfarm/gflog.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

#include "gfutil.h"
#include "hash.h"

#include "config.h"
#include "gfm_client.h"

#include "gfsd_subr.h"

static enum gfarm_spool_check_level spool_check_level;

static gfarm_error_t
move_file_to_lost_found_main(const char *file, struct stat *stp,
	gfarm_ino_t inum_old, gfarm_uint64_t gen_old)
{
	gfarm_error_t e;
	int save_errno;
	struct gfarm_timespec mtime;
	char *newpath;
	gfarm_ino_t inum_new;
	gfarm_uint64_t gen_new;

	mtime.tv_sec = stp->st_mtime;
	mtime.tv_nsec = gfarm_stat_mtime_nsec(stp);
	e = gfm_client_replica_create_file_in_lost_found(gfm_server,
	    inum_old, gen_old, (gfarm_off_t)stp->st_size, &mtime,
	    &inum_new, &gen_new);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1003520,
		    "%s: replica_create_file_in_lost_found: %s",
		    file, gfarm_error_string(e));
		/*
		 * GFARM_ERR_ALREADY_EXISTS will occur, if below
		 * gfsd_create_ancestor_dir() or rename() are failed
		 * and gfsd -ccc is retried.  But
		 * GFARM_ERR_ALREADY_EXISTS should not be ignored
		 * here.  Because if the metadata is initialized (all
		 * metadata is lost) or reverted to the backup-data
		 * (rollback), and if gfsd -ccc is called several
		 * times, GFARM_ERR_ALREADY_EXISTS also may occur in
		 * low probability.
		 */
		return (e);
	}
	gfsd_local_path(inum_new, gen_new, "move_file_to_lost_found",
	    &newpath);
	if (gfsd_create_ancestor_dir(newpath)) {
		save_errno = errno;
		gflog_error(GFARM_MSG_1003521,
		    "%s: cannot create ancestor directory: %s",
		    newpath, strerror(save_errno));
		free(newpath);
		return (gfarm_errno_to_error(save_errno));
	}
	if (rename(file, newpath)) {
		save_errno = errno;
		gflog_error(GFARM_MSG_1003522,
		    "%s: cannot rename to %s: %s", file, newpath,
		    strerror(save_errno));
		free(newpath);
		return (gfarm_errno_to_error(save_errno));
	}
	e = gfm_client_replica_add(gfm_server, inum_new, gen_new,
	    (gfarm_off_t)stp->st_size);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1003523,
		    "%s: replica_add failed: %s", newpath,
		    gfarm_error_string(e));
	free(newpath);
	return (e);
}

static void
replica_lost(gfarm_ino_t inum, gfarm_uint64_t gen)
{
	gfarm_error_t e = gfm_client_replica_lost(gfm_server, inum, gen);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1003527,
		    "replica_lost(%llu, %llu): %s",
		    (unsigned long long)inum,
		    (unsigned long long)gen,
		    gfarm_error_string(e));
	}
}

static gfarm_error_t
move_file_to_lost_found(const char *file, struct stat *stp,
	gfarm_ino_t inum_old, gfarm_uint64_t gen_old, int size_mismatch)
{
	gfarm_error_t e;

	if (stp->st_size == 0) { /* unreferred empty file is unnecessary */
		if (size_mismatch)
			replica_lost(inum_old, gen_old);
		if (unlink(file)) {
			e = gfarm_errno_to_error(errno);
			gflog_warning(GFARM_MSG_1003524,
			    "%s: unlink empty file: %s",
			    file, gfarm_error_string(e));
			return (e);
		}
		gflog_notice(GFARM_MSG_1003525,
		    "%s: unlinked (empty file)", file);
		return (GFARM_ERR_NO_ERROR);
	}

	e = move_file_to_lost_found_main(file, stp, inum_old, gen_old);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1003526,
		    "%s cannot be moved to /lost+found/%016llX%016llX-%s: %s",
		    file, (unsigned long long)inum_old,
		    (unsigned long long)gen_old, canonical_self_name,
		    gfarm_error_string(e));
		return (e);
	}
	if (size_mismatch) {
		replica_lost(inum_old, gen_old);
		gflog_notice(GFARM_MSG_1003528,
		     "(%llu:%llu): size mismatch, "
		     "moved to /lost+found/%016llX%016llX-%s",
		     (unsigned long long)inum_old,
		     (unsigned long long)gen_old,
		     (unsigned long long)inum_old,
		     (unsigned long long)gen_old, canonical_self_name);
	} else
		gflog_notice(GFARM_MSG_1003529, "unknown file is found: "
		     "registered to /lost+found/%016llX%016llX-%s",
		     (unsigned long long)inum_old,
		     (unsigned long long)gen_old, canonical_self_name);
	return (e);
}

/*
 * File format should be consistent with gfsd_local_path() in gfsd.c
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

static gfarm_error_t
dir_foreach(
	gfarm_error_t (*op_file)(char *, struct stat *, void *),
	gfarm_error_t (*op_dir1)(char *, struct stat *, void *),
	gfarm_error_t (*op_dir2)(char *, struct stat *, void *),
	char *dir, void *arg)
{
	DIR *dirp;
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
	if (!S_ISDIR(st.st_mode)) {
		gflog_debug(GFARM_MSG_1002204,
			"Invalid argument st.st_mode");
		return (GFARM_ERR_INVALID_ARGUMENT); /* XXX */
	}

	if (op_dir1 != NULL) {
		e = op_dir1(dir, &st, arg);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1002205,
				"op_dir1() failed: %s",
				gfarm_error_string(e));
			return (e);
		}
	}
	dirp = opendir(dir);
	if (dirp == NULL) {
		int err = errno;
		gflog_debug(GFARM_MSG_1002206, "opendir() failed: %s",
		    strerror(err));
		return (gfarm_errno_to_error(err));
	}

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
			gflog_debug(GFARM_MSG_1002207,
				"allocation of array 'dir1' failed");
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
unlink_file(const char *file)
{
	if (unlink(file))
		return (gfarm_errno_to_error(errno));
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
deal_with_invalid_file(const char *file, struct stat *stp, int valid_inum_gen,
	gfarm_ino_t inum, gfarm_uint64_t gen, int size_mismatch)
{
	gfarm_error_t e;

	switch (spool_check_level) {
	case GFARM_SPOOL_CHECK_LEVEL_DISPLAY:
		gflog_notice(GFARM_MSG_1000603, "%s: invalid file", file);
		e = GFARM_ERR_NO_ERROR;
		break;
	case GFARM_SPOOL_CHECK_LEVEL_DELETE:
		e = unlink_file(file);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_warning(GFARM_MSG_1000604,
			    "%s: cannot delete", file);
		else
			gflog_notice(GFARM_MSG_1000605,
			    "%s: deleted", file);
		break;
	case GFARM_SPOOL_CHECK_LEVEL_LOST_FOUND:
	default:
		if (valid_inum_gen)
			e = move_file_to_lost_found(file, stp, inum, gen,
			    size_mismatch);
		else {
			gflog_notice(GFARM_MSG_1003530,
			    "%s: unsupported file (ignored)", file);
			e = GFARM_ERR_INVALID_ARGUMENT;
		}
		break;
	}
	return (e);
}

static gfarm_error_t
check_file(char *file, struct stat *stp, void *arg)
{
	gfarm_ino_t inum;
	gfarm_uint64_t gen, *genp;
	gfarm_off_t size;
	gfarm_error_t e;
	struct gfarm_hash_table *hash_ok = arg;
	struct gfarm_hash_entry *hash_ent;

	/* READONLY_CONFIG_FILE should be skipped */
	if (strcmp(file, READONLY_CONFIG_FILE) == 0)
		return (GFARM_ERR_NO_ERROR);

	if (get_inum_gen(file, &inum, &gen))
		return (deal_with_invalid_file(file, stp, 0, 0, 0, 0));
	if (hash_ok) {
		hash_ent = gfarm_hash_lookup(hash_ok, &inum, sizeof(inum));
		if (hash_ent) {
			genp = gfarm_hash_entry_data(hash_ent);
			if (*genp == gen)  /* already checked, valid file */
				return (GFARM_ERR_NO_ERROR);
		}
	}

	size = stp->st_size;
	e = gfm_client_replica_add(gfm_server, inum, gen, size);
	switch (e) {
	case GFARM_ERR_ALREADY_EXISTS:
		/* correct entry */
		e = GFARM_ERR_NO_ERROR;
		break;
	case GFARM_ERR_NO_ERROR:
		gflog_notice(GFARM_MSG_1000606, "%s: fixed", file);
		break;
	case GFARM_ERR_NO_SUCH_OBJECT:
	case GFARM_ERR_NOT_A_REGULAR_FILE:
		e = deal_with_invalid_file(file, stp, 1, inum, gen, 0);
		break;
	case GFARM_ERR_INVALID_FILE_REPLICA: /* size mismatch */
		e = deal_with_invalid_file(file, stp, 1, inum, gen, 1);
		break;
	case GFARM_ERR_FILE_BUSY:
		gflog_notice(GFARM_MSG_1003531,
		    "%s: ignored (writing or removing now)", file);
		break;
	default:
		gflog_error(GFARM_MSG_1003532, "replica_add(%llu, %llu): %s",
		    (unsigned long long)inum, (unsigned long long)gen,
		    gfarm_error_string(e));
	}
	return (e);
}

static gfarm_error_t
check_spool(char *dir, struct gfarm_hash_table *hash_ok)
{
	return (dir_foreach(check_file, NULL, NULL, dir, hash_ok));
}

static void
check_existing(
	struct gfarm_hash_table *hash_ok,
	gfarm_ino_t inum, gfarm_uint64_t gen, gfarm_off_t size)
{
	gfarm_error_t e;
	char *path, *file;
	struct stat st;
	int save_errno, lost = 0;
	gfarm_ino_t inum2;
	gfarm_uint64_t gen2, *genp;
	struct gfarm_hash_entry *hash_ent;

	/*
	 * If gfsd_local_path() or get_inum_gen() are broken,
	 * all replica-references are deleted....
	 */
	gfsd_local_path(inum, gen, "compare_file", &path);
	file = path + gfarm_spool_root_len + 1;
	get_inum_gen(file, &inum2, &gen2);
	if (inum != inum2 || gen != gen2)
		fatal(GFARM_MSG_1003533,
		    "%s: gfsd_local_path or get_inum_gen are broken", path);
	else if (lstat(path, &st)) {
		save_errno = errno;
		if (save_errno == ENOENT) {
			gflog_notice(GFARM_MSG_1003534,
			    "physical file does not exist, "
			    "delete the metadata entry for %llu:%llu",
			    (unsigned long long)inum, (unsigned long long)gen);
			lost = 1;
		} else
			/*
			 * This error is usually EACCES.  If the
			 * permissions are fixed, this problem may be
			 * fixed.  Therefore the replica-reference is
			 * not deleted from metadata in this case.
			 */
			gflog_error(GFARM_MSG_1003535, "stat(%s): %s",
			    path, strerror(save_errno));
	} else if (!S_ISREG(st.st_mode)) {
		gflog_notice(GFARM_MSG_1003536, "%s: not a file, "
		    "delete the metadata entry for %llu:%llu", path,
		    (unsigned long long)inum, (unsigned long long)gen);
		lost = 1;
	} else if (st.st_size == size && hash_ok) { /* valid file */
		/* save inum:gen pair to skip this file in check_spool() */
		hash_ent = gfarm_hash_enter(
		    hash_ok, &inum, sizeof(inum), sizeof(gen), NULL);
		if (hash_ent) {
			genp = gfarm_hash_entry_data(hash_ent);
			*genp = gen;
		}
		/*
		 * If hash_ent == NULL, this file will be checked by
		 * gfm_client_replica_add().
		 */
	}
	/* else: This file will be checked by gfm_client_replica_add(). */

	if (lost) { /* delete the replica-reference from metadata */
		e = gfm_client_replica_lost(gfm_server, inum, gen);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1003537,
			    "replica_lost(%llu, %llu): %s",
			    (unsigned long long)inum, (unsigned long long)gen,
			    gfarm_error_string(e));
	}
	free(path);
}

#define REQUEST_NUM 10000

static gfarm_error_t
check_metadata(struct gfarm_hash_table *hash_ok)
{
	gfarm_error_t e;
	gfarm_ino_t inum, *inums;
	gfarm_uint64_t *gens;
	gfarm_off_t *sizes;
	int i, n;

	for (inum = 0;; inum++) {
		n = REQUEST_NUM;
		e = gfm_client_replica_get_my_entries(gfm_server,
		    inum, n, &n, &inums, &gens, &sizes);
		if (e == GFARM_ERR_NO_SUCH_OBJECT)
			return (GFARM_ERR_NO_ERROR); /* end */
		else if (e != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_1003538,
			    "replica_get_my_entries(%llu, %d): %s",
			    (unsigned long long)inum, REQUEST_NUM,
			    gfarm_error_string(e));
			return (e);
		}
		for (i = 0; i < n && i < REQUEST_NUM; i++) {
			check_existing(hash_ok, inums[i], gens[i], sizes[i]);
			inum = inums[i];
		}
		free(inums);
		free(gens);
		free(sizes);
		if (n < REQUEST_NUM)
			return (GFARM_ERR_NO_ERROR); /* end */
	}
}

#define HASH_OK_SIZE 999983

/*
 *  gfarm_spool_check_level == GFARM_SPOOL_CHECK_LEVEL_... :
 *  DISPLAY    ... display invalid files (slow)
 *  DELETE     ... delete invalid files  (slow)
 *  LOST_FOUND ... move invalid files to gfarm:///lost+found
 *                 and delete invalid replica-references from metadata
 */
void
gfsd_spool_check()
{
	struct gfarm_hash_table *hash_ok; /* valid files */

	gflog_debug(GFARM_MSG_1003680, "spool_check_level=%s",
	    gfarm_spool_check_level_get_by_name());

	spool_check_level = gfarm_spool_check_level_get();
	switch (spool_check_level) {
	case GFARM_SPOOL_CHECK_LEVEL_LOST_FOUND:
		hash_ok = gfarm_hash_table_alloc(HASH_OK_SIZE,
		    gfarm_hash_default, gfarm_hash_key_equal_default);
		if (hash_ok == NULL)
			fatal(GFARM_MSG_1003560, "no memory for spool_check");
		check_metadata(hash_ok);
		break;
	case GFARM_SPOOL_CHECK_LEVEL_DISPLAY:
	case GFARM_SPOOL_CHECK_LEVEL_DELETE:
		hash_ok = NULL;
		break;
	default:
		return;
	}
	(void)check_spool(".", hash_ok);
	if (hash_ok)
		gfarm_hash_table_free(hash_ok);
}
