/*
 * $Id$
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <assert.h>

#include <gfarm/gflog.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

#include "gfutil.h"
#include "hash.h"

#include "config.h"
#include "gfp_xdr.h"
#include "gfm_proto.h"
#include "gfm_client.h"

#include "gfsd_subr.h"

#define DIR8_ENTRIES	256			/* level 4 dir */
#define DIR16_ENTRIES	(256*DIR8_ENTRIES)	/* level 3 dir */
#define DIR24_ENTRIES	(256*DIR16_ENTRIES)	/* level 2 dir */
#define DIR32_ENTRIES	(256*DIR24_ENTRIES)	/* level 1 dir */

#define ALLOT_LEVEL	3
#define ALLOT_ENTRIES	DIR16_ENTRIES		/* level 3 dir */

static enum gfarm_spool_check_level spool_check_level;

static int gfs_spool_check_parallel_index = -1;
static pid_t *gfs_spool_check_parallel_pids;

static gfarm_ino_t inum_step_per_process, inum_step;

static gfarm_error_t
gfm_client_replica_add(gfarm_ino_t inum, gfarm_uint64_t gen, gfarm_off_t size)
{
	gfarm_error_t e;
	static const char diag[] = "replica_add";

	if ((e = gfm_client_replica_add_request(gfm_server, inum, gen, size))
	    != GFARM_ERR_NO_ERROR)
		fatal_metadb_proto(GFARM_MSG_1000601, "replica_add request",
		    diag, e);
	else if ((e = gfm_client_replica_add_result(gfm_server))
	    != GFARM_ERR_NO_ERROR) {
		if (debug_mode && e != GFARM_ERR_ALREADY_EXISTS)
			gflog_info(GFARM_MSG_1000602, "replica_add result: %s",
			    gfarm_error_string(e));
	}
	return (e);
}

static gfarm_error_t
gfm_client_replica_get_my_entries_range(
	gfarm_ino_t start_inum, gfarm_ino_t n_inum,
	int *np, gfarm_uint32_t *flagsp,
	gfarm_ino_t **inumsp, gfarm_uint64_t **gensp, gfarm_off_t **sizesp)
{
	gfarm_error_t e;
	static const char diag[] = "replica_get_my_entries2";

	if ((e = gfm_client_replica_get_my_entries_range_request(
	    gfm_server, start_inum, n_inum, *np)) != GFARM_ERR_NO_ERROR)
		fatal_metadb_proto(GFARM_MSG_1003516, "request", diag, e);
	else if ((e = gfm_client_replica_get_my_entries_range_result(
	    gfm_server, np, flagsp, inumsp, gensp, sizesp))
	    != GFARM_ERR_NO_ERROR) {
		if (debug_mode)
			gflog_info(GFARM_MSG_1003517, "%s result: %s", diag,
			    gfarm_error_string(e));
	}
	return (e);
}

static gfarm_error_t
gfm_client_replica_create_file_in_lost_found(
	gfarm_ino_t inum_old, gfarm_uint64_t gen_old, gfarm_off_t size,
	const struct gfarm_timespec *mtime,
	gfarm_ino_t *inum_newp, gfarm_uint64_t *gen_newp)
{
	gfarm_error_t e;
	static const char diag[] = "replica_create_file_in_lost_found";

	if ((e = gfm_client_replica_create_file_in_lost_found_request(
	    gfm_server, inum_old, gen_old, size, mtime))
	    != GFARM_ERR_NO_ERROR)
		fatal_metadb_proto(GFARM_MSG_1003518, "request", diag, e);
	else if ((e = gfm_client_replica_create_file_in_lost_found_result(
	    gfm_server, inum_newp, gen_newp)) != GFARM_ERR_NO_ERROR) {
		if (debug_mode)
			gflog_info(GFARM_MSG_1003519, "%s result: %s", diag,
			    gfarm_error_string(e));
	}
	return (e);
}

/*
 * switch (op)
 * case 0: rename
 * case 1: copy
 */
static gfarm_error_t
move_or_copy_to_lost_found(int op, const char *file, int fd, struct stat *stp,
	gfarm_ino_t inum, gfarm_uint64_t gen, const char *diag)
{
	gfarm_error_t e;
	int save_errno;
	struct gfarm_timespec mtime;
	char *newpath, *newfile, *path = NULL;
	gfarm_ino_t inum_new;
	gfarm_uint64_t gen_new;
	struct stat sb1;

	mtime.tv_sec = stp->st_mtime;
	mtime.tv_nsec = gfarm_stat_mtime_nsec(stp);
	for (;;) {
		e = gfm_client_replica_create_file_in_lost_found(
		    inum, gen, (gfarm_off_t)stp->st_size,
		    &mtime, &inum_new, &gen_new);
		if (!IS_CONNECTION_ERROR(e))
			break;
		free_gfm_server();
		if ((e = connect_gfm_server(diag)) != GFARM_ERR_NO_ERROR)
			fatal(GFARM_MSG_1004392, "die");
	}
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1003520,
		    "%s (%lld:%lld): %s: %s",
		    file != NULL ? file : "<unknown filename>",
		    (long long)inum, (long long)gen,
		    diag, gfarm_error_string(e));
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
	switch (op) {
	case 0: /* rename */
		if (file == NULL) {
			gfsd_local_path2(inum, gen, diag, &path,
			    inum_new, gen_new, diag, &newpath);
			file = path;
			newfile = newpath;
		} else {
			gfsd_local_path(inum_new, gen_new, diag, &newpath);
			newfile = gfsd_skip_spool_root(newpath);
		}
		if (gfsd_create_ancestor_dir(newfile)) {
			save_errno = errno;
			gflog_error(GFARM_MSG_1003521,
			    "%s: cannot create ancestor directory: %s",
			    newfile, strerror(save_errno));
			free(path);
			free(newpath);
			return (gfarm_errno_to_error(save_errno));
		}
		if (rename(file, newfile)) {
			save_errno = errno;
			gflog_error(GFARM_MSG_1003522,
			    "%s: cannot rename to %s: %s", file, newfile,
			    strerror(save_errno));
			e = gfarm_errno_to_error(save_errno);
		}
		free(path);
		break;
	case 1: /* copy */
		if ((e = gfsd_copy_file(fd, inum_new, gen_new, diag, &newpath))
		    != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1004500,
			    "inode %lld:%lld: cannot be copied, "
			    "invalid file may remain: %s",
			    (unsigned long long)inum, (unsigned long long)gen,
			    gfarm_error_string(e));
		else if (stat(newpath, &sb1) == -1) {
			save_errno = errno;
			gflog_error(GFARM_MSG_1004190,
			    "inode %lld:%lld: copied file does not exist: %s",
			    (unsigned long long)inum, (unsigned long long)gen,
			    strerror(save_errno));
			e = gfarm_errno_to_error(save_errno);
		} else if (sb1.st_size != stp->st_size) {
			e = GFARM_ERR_INPUT_OUTPUT;
			gflog_error(GFARM_MSG_1004191,
			    "inode %lld:%lld: size mismatch: copied file has "
			    "%lld byte that should be %lld byte.  invalid file"
			    " remains at %s",
			    (unsigned long long)inum, (unsigned long long)gen,
			    (unsigned long long)sb1.st_size,
			    (unsigned long long)stp->st_size, newpath);
		}
		break;
	}
	if (e == GFARM_ERR_NO_ERROR) {
		for (;;) {
			e = gfm_client_replica_add(inum_new, gen_new,
			    (gfarm_off_t)stp->st_size);
			if (!IS_CONNECTION_ERROR(e))
				break;
			free_gfm_server();
			if ((e = connect_gfm_server(diag))
			    != GFARM_ERR_NO_ERROR)
				fatal(GFARM_MSG_1004393, "die");
		}
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1003523,
			    "%s: replica_add failed: %s", newpath,
			    gfarm_error_string(e));
	}
	free(newpath);
	return (e);
}

static void
replica_lost(gfarm_ino_t inum, gfarm_uint64_t gen)
{
	gfarm_error_t e = gfm_client_replica_lost(inum, gen);

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
	static const char diag[] = "move_file_to_lost_found";

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

	e = move_or_copy_to_lost_found(0, file, -1, stp, inum_old, gen_old,
		diag);
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

gfarm_error_t
register_to_lost_found(int op, int fd, gfarm_ino_t inum, gfarm_uint64_t gen)
{
	struct stat sb;
	int save_errno;
	static char diag[] = "register_to_lost_found";

	if (fstat(fd, &sb) == -1) {
		save_errno = errno;
		gflog_warning_errno(GFARM_MSG_1004187,
		    "inode %lld:%lld: fstat()",
		    (unsigned long long)inum, (unsigned long long)gen);
		return (gfarm_errno_to_error(save_errno));
	}
	return (move_or_copy_to_lost_found(op, NULL, fd, &sb, inum, gen, diag));
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

static int
is_my_duty(const char *path)
{
	unsigned int inum32, inum24, inum16;
	gfarm_ino_t inum;

	if (sscanf(path, "data/%08X/%02X/%02X", &inum32, &inum24, &inum16)
	    != 3)
		return (0);

	inum = ((gfarm_ino_t)inum32 << 32) + (inum24 << 24) +
		(inum16 << 16);
	return ((inum / inum_step_per_process) % gfarm_spool_check_parallel
	    == gfs_spool_check_parallel_index);
}


static gfarm_error_t
dir_foreach(
	gfarm_error_t (*op_file)(char *, struct stat *, void *),
	gfarm_error_t (*op_dir1)(char *, struct stat *, void *),
	gfarm_error_t (*op_dir2)(char *, struct stat *, void *),
	char *dir, void *arg, int level)
{
	DIR *dirp;
	struct dirent *dp;
	struct stat st;
	gfarm_error_t e;
	char *dir1;

	if (level == ALLOT_LEVEL) { /* this dir has ALLOT_ENTRIES at maximum */
		if (!is_my_duty(dir))
			return (GFARM_ERR_NO_ERROR);
	}

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
		(void)dir_foreach(op_file, op_dir1, op_dir2, dir1, arg,
		    level + 1);
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
	e = gfm_client_replica_add(inum, gen, size);
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
	return (dir_foreach(check_file, NULL, NULL, dir, hash_ok, 0));
}

static void
check_existing(
	struct gfarm_hash_table *hash_ok,
	gfarm_ino_t inum, gfarm_uint64_t gen, gfarm_off_t size)
{
	gfarm_error_t e;
	char *path;
	const char *file;
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
	file = gfsd_skip_spool_root(path);
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
		e = gfm_client_replica_lost(inum, gen);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1003537,
			    "replica_lost(%llu, %llu): %s",
			    (unsigned long long)inum, (unsigned long long)gen,
			    gfarm_error_string(e));
	}
	free(path);
}

#define REQUEST_NUM 16384

static gfarm_error_t
check_metadata(struct gfarm_hash_table *hash_ok)
{
	gfarm_error_t e;
	gfarm_ino_t inum_base, inum_end, inum, *inums;
	gfarm_uint32_t flags;
	gfarm_uint64_t *gens;
	gfarm_off_t *sizes;
	int i, n;

	inum_base = gfs_spool_check_parallel_index * inum_step_per_process;
	inum_end = inum_base + inum_step_per_process;
	inum = inum_base;
	for (;;) {
		n = REQUEST_NUM;
		e = gfm_client_replica_get_my_entries_range(
		    inum, inum_end - inum, &n,
		    &flags, &inums, &gens, &sizes);
		if (e == GFARM_ERR_NO_SUCH_OBJECT)
			return (GFARM_ERR_NO_ERROR); /* end */
		else if (e != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_1005019,
			    "replica_get_my_entries(%llu, %llu, %d): %s",
			    (unsigned long long)inum,
			    (unsigned long long)inum_step_per_process,
			    REQUEST_NUM,
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
		if (flags & GFM_PROTO_REPLICA_GET_MY_ENTRIES_END_OF_INODE)
			return (GFARM_ERR_NO_ERROR); /* end */


		inum++;
		if (flags & GFM_PROTO_REPLICA_GET_MY_ENTRIES_END_OF_RANGE ||
		    inum >= inum_end) {
			inum_base += inum_step;
			inum_end = inum_base + inum_step_per_process;
			inum = inum_base;
		}
	}
}

static void
gfs_spool_check_parallel_fork(void)
{
	gfarm_error_t e;
	int i;
	pid_t pid;

	if (gfarm_spool_check_parallel < 0)
		fatal(GFARM_MSG_1005020,
		    "spool_check_parallel configuration %d is invalid",
		    gfarm_spool_check_parallel);
	if (gfarm_spool_check_parallel ==
	    GFARM_SPOOL_CHECK_PARALLEL_AUTOMATIC) {
		gfarm_int32_t bsize;
		gfarm_off_t blocks, bfree, bavail, files, ffree, favail;
		int readonly;

		gfsd_statfs_all(&bsize, &blocks, &bfree, &bavail,
		    &files, &ffree, &favail, &readonly);
		gfarm_spool_check_parallel = blocks * bsize /
		    gfarm_spool_check_parallel_per_capacity;
		if (gfarm_spool_check_parallel >
		    gfarm_spool_check_parallel_max) {
			gflog_info(GFARM_MSG_1005021,
			    "spool_check: automatically calculated "
			    "number of parallel processes %d exceeds max (%d)",
			    gfarm_spool_check_parallel,
			    gfarm_spool_check_parallel_max);
			gfarm_spool_check_parallel =
			    gfarm_spool_check_parallel_max;
		}
	}
	if (gfarm_spool_check_parallel <= 0)
		gfarm_spool_check_parallel = 1;
	GFARM_MALLOC_ARRAY(gfs_spool_check_parallel_pids,
	    gfarm_spool_check_parallel);
	if (gfs_spool_check_parallel_pids == NULL)
		fatal(GFARM_MSG_1005022, "could not remember %d pids",
		    gfarm_spool_check_parallel);

	/* gfarm_spool_check_parallel has to be fixed here */
	inum_step_per_process =
	    (gfarm_ino_t)gfarm_spool_check_parallel_step * ALLOT_ENTRIES;
	inum_step = inum_step_per_process * gfarm_spool_check_parallel;

	gfs_spool_check_parallel_pids[0] = getpid();
	for (i = 1; i < gfarm_spool_check_parallel; i++) {
		/*
		 * do not use do_fork(), because gfsd is not ready
		 * for failover_notify in the spool_check phase.
		 */
		pid = fork();
		if (pid == 0) { /* child */
			static const char base[] = "spool_check()";
			char diag[sizeof(base) + GFARM_INT32STRLEN + 1];

			snprintf(diag, sizeof diag, "spool_check(%d)", i);
			free_gfm_server(); /* to make sure to avoid race */
			e = connect_gfm_server(diag);
			if (e != GFARM_ERR_NO_ERROR)
				fatal(GFARM_MSG_1005023, "die");

			/* children shouldn't access this */
			free(gfs_spool_check_parallel_pids);
			gfs_spool_check_parallel_pids = NULL;

			gfs_spool_check_parallel_index = i;
			return;
		}
		/* parent */
		if (pid == -1) {
			/*
			 * this is fatal, because gfarm_spool_check_parallel
			 * has to be fixed before fork()
			 */
			fatal(GFARM_MSG_1005024,
			    "spool_check: cannot fork %dth child", i);
			break;
		}
		gfs_spool_check_parallel_pids[i] = pid;
	}
	gfs_spool_check_parallel_index = 0;
	gflog_info(GFARM_MSG_1005025,
	    "spool_check: parallel process: %d",
	    gfarm_spool_check_parallel);

}

static void
gfs_spool_check_parallel_wait(void)
{
	int i, status;
	pid_t pid;

	for (i = 1; i < gfarm_spool_check_parallel; i++) {
		pid = waitpid(gfs_spool_check_parallel_pids[i], &status, 0);
		if (pid == -1)
			gflog_error_errno(GFARM_MSG_1005026,
			    "spool_check: wait %dth child (%d)",
			    i, (int)gfs_spool_check_parallel_pids[i]);
		else if (WIFSIGNALED(status))
			gflog_warning(GFARM_MSG_1005027,
			    "spool_check: %dth child exited with signal %d%s",
			    i, (int)WTERMSIG(status),
			    WCOREDUMP(status) ? " (core dumped)" : "");
		else if (WEXITSTATUS(status) != 0)
			gflog_warning(GFARM_MSG_1005028,
			    "spool_check: %dth child exited with status %d",
			    i, (int)WEXITSTATUS(status));
	}
	free(gfs_spool_check_parallel_pids);
	gfs_spool_check_parallel_pids = NULL;
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
	int i;

	gflog_debug(GFARM_MSG_1003680, "spool_check_level=%s",
	    gfarm_spool_check_level_get_by_name());

	spool_check_level = gfarm_spool_check_level_get();
	if (spool_check_level == GFARM_SPOOL_CHECK_LEVEL_DISABLE)
		return;

	gfs_spool_check_parallel_fork();

	switch (spool_check_level) {
	case GFARM_SPOOL_CHECK_LEVEL_LOST_FOUND:
		hash_ok = gfarm_hash_table_alloc(HASH_OK_SIZE,
		    gfarm_hash_default, gfarm_hash_key_equal_default);
		if (hash_ok == NULL)
			fatal(GFARM_MSG_1003560, "no memory for spool_check");
		gflog_info(GFARM_MSG_1005029,
		    "spool_check(%d): metadata check started",
		    gfs_spool_check_parallel_index);
		check_metadata(hash_ok);
		break;
	case GFARM_SPOOL_CHECK_LEVEL_DISPLAY:
	case GFARM_SPOOL_CHECK_LEVEL_DELETE:
		hash_ok = NULL;
		break;
	default:
		assert(0);
		/*NOTREACHED*/
		return;
	}
	for (i = 0; i < gfarm_spool_root_num; ++i) {
		if (gfarm_spool_root[i] == NULL)
			break;
		if (chdir(gfarm_spool_root[i]) == -1)
			gflog_fatal_errno(GFARM_MSG_1004484, "chdir(%s)",
			    gfarm_spool_root[i]);
		gflog_info(GFARM_MSG_1005030,
		    "spool_check(%d): directory check #%d started at %s",
		    gfs_spool_check_parallel_index, i, gfarm_spool_root[i]);
		(void)check_spool("data", hash_ok);
	}
	if (hash_ok)
		gfarm_hash_table_free(hash_ok);

	gflog_info(GFARM_MSG_1005031,
	    "spool_check(%d): finished", gfs_spool_check_parallel_index);

	if (gfs_spool_check_parallel_index == 0)
		gfs_spool_check_parallel_wait();
	else
		exit(0);

	gflog_info(GFARM_MSG_1004775, "spool_check: completed");
}
