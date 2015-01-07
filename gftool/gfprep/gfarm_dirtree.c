/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h> /* gfprep.h: va_list */
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <dirent.h>
#include <errno.h>
#include <assert.h>

#include <gfarm/gfarm.h>

#include "nanosec.h"
#include "thrsubr.h"

#include "gfm_client.h"

#include "gfprep.h"
#include "gfarm_parallel.h"
#include "gfarm_dirtree.h"
#include "gfarm_fifo.h"
#include "gfarm_list.h"

struct gfarm_fifo_simple {
	unsigned int in, out, size;
	void **buf;
};

typedef struct gfarm_fifo_simple gfarm_fifo_simple_t;

#define GFARM_FIFO_SIMPLE_INIT_SIZE 64

static gfarm_error_t
gfarm_fifo_simple_init(gfarm_fifo_simple_t **fifo_p)
{
	gfarm_fifo_simple_t *fifo;
	void **buf;

	GFARM_MALLOC(fifo);
	GFARM_MALLOC_ARRAY(buf, GFARM_FIFO_SIMPLE_INIT_SIZE);
	if (fifo == NULL || buf == NULL)
		return (GFARM_ERR_NO_MEMORY);
	fifo->buf = buf;
	fifo->size = GFARM_FIFO_SIMPLE_INIT_SIZE;
	fifo->in = fifo->out = 0;
	*fifo_p = fifo;
	return (GFARM_ERR_NO_ERROR);
}

static void
gfarm_fifo_simple_free(gfarm_fifo_simple_t *fifo)
{
	free(fifo->buf);
	free(fifo);
}

/* not MT-safe */
static gfarm_error_t
gfarm_fifo_simple_enter(gfarm_fifo_simple_t *fifo, void *p)
{
	if (fifo->in >= fifo->size) {
		int newsize = fifo->size * 2;
		void **newbuf;
		GFARM_REALLOC_ARRAY(newbuf, fifo->buf, newsize);
		if (newbuf == NULL)
			return (GFARM_ERR_NO_MEMORY);
		fifo->buf = newbuf;
		fifo->size = newsize;
		if (fifo->out > 0) {
			unsigned int i, diff_in_out = fifo->in - fifo->out;
			for (i = 0; i < diff_in_out; i++)
				fifo->buf[i] = fifo->buf[fifo->out + i];
			fifo->out = 0;
			fifo->in = diff_in_out;
		}
	}
	fifo->buf[fifo->in++] = p;
	return (GFARM_ERR_NO_ERROR);
}

/* not MT-safe */
static gfarm_error_t
gfarm_fifo_simple_next(gfarm_fifo_simple_t *fifo, void **pp)
{
	if (fifo->out >= fifo->in)
		return (GFARM_ERR_NO_SUCH_OBJECT);
	/* ex.: out == 0, in == 1 */
	*pp = fifo->buf[fifo->out++];
	return (GFARM_ERR_NO_ERROR);
}

struct gfarm_dirtree {
	pthread_mutex_t mutex;
	pthread_cond_t free_procs;
	pthread_cond_t get_ents;
	gfpara_t *gfpara_handle;
	const char *src_dir;
	const char *dst_dir;
	int src_type; /* url type */
	int dst_type; /* url type */
	int n_parallel;
	int n_free_procs;
	unsigned int n_get_ents;
	int started;
	int is_recursive;
	gfarm_fifo_simple_t *fifo_dirs;
	gfarm_fifo_simple_t *fifo_ents;
	gfarm_fifo_t *fifo_handle;
};

enum url_type {
	URL_TYPE_LOCAL,
	URL_TYPE_GFARM,
	URL_TYPE_UNUSED
};

static const char *
gfarm_dirtree_path_skip_root_slash(const char *path)
{
	const char *p = path;
	while (*p != '\0' && *p == '/' && *(p + 1) == '/')
		p++;
	return (p);
}

static void
dirtree_fifo_set(void *entries, int index, void *entryp)
{
	gfarm_dirtree_entry_t **ents = entries;
	gfarm_dirtree_entry_t **entp = entryp;
	ents[index] = *entp; /* pointer */
}

static void
dirtree_fifo_get(void *entries, int index, void *entryp)
{
	gfarm_dirtree_entry_t **ents = entries;
	gfarm_dirtree_entry_t **entp = entryp;
	*entp = ents[index]; /* pointer */
}

enum dirtree_cmd {
	DIRTREE_CMD_GET_DENTS,
	DIRTREE_CMD_GET_FINFO,
	DIRTREE_CMD_TERMINATE
};

enum dirtree_stat {
	DIRTREE_STAT_GET_DENTS_OK,
	DIRTREE_STAT_GET_FINFO_OK,
	DIRTREE_STAT_IGNORE,
	DIRTREE_STAT_NG,
	DIRTREE_STAT_END
};

enum dirtree_entry {
	DIRTREE_ENTRY_NAME,
	DIRTREE_ENTRY_END
};

struct dirtree_dir_handle {
	const char *path;
	void *dir;
};

struct my_stat {
	gfarm_int32_t nlink;
	gfarm_int32_t mode;
	gfarm_int64_t size;
	gfarm_int64_t mtime_sec;
	gfarm_int32_t mtime_nsec;
};

static void
dirtree_convert_stat(struct stat *from_st, struct my_stat *to_st)
{
	to_st->nlink = from_st->st_nlink;
	to_st->mode = from_st->st_mode;
	to_st->size = from_st->st_size;
	to_st->mtime_sec = from_st->st_mtime;
	to_st->mtime_nsec = gfarm_stat_mtime_nsec(from_st);
}

static void
dirtree_convert_gfs_stat(struct gfs_stat *from_st, struct my_stat *to_st)
{
	to_st->nlink = from_st->st_nlink;
	to_st->mode = from_st->st_mode;
	to_st->size = from_st->st_size;
	to_st->mtime_sec = from_st->st_mtimespec.tv_sec;
	to_st->mtime_nsec = from_st->st_mtimespec.tv_nsec;
}

static gfarm_error_t
dirtree_local_opendir(const char *path, struct dirtree_dir_handle *dh)
{
	int save_errno;
	DIR *dir;

	dir = opendir(path);
	if (dir == NULL) {
		save_errno = errno;
		fprintf(stderr, "ERROR: opendir(%s): %s\n",
			path, strerror(save_errno));
		return (gfarm_errno_to_error(save_errno));
	}
	dh->path = path;
	dh->dir = dir;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
dirtree_local_readdirplus(struct dirtree_dir_handle *dh,
			  struct gfs_dirent *dentp, struct my_stat *stp)
{
	struct dirent *dent;
	int retv, save_errno;
	char *child;
	DIR *dir = dh->dir;
	struct stat st;

	errno = 0;
	dent = readdir(dir);
	if (dent == NULL) {
		save_errno = errno;
		if (save_errno == 0)
			return (GFARM_ERR_NO_SUCH_OBJECT); /* end */
		fprintf(stderr, "ERROR: readdir(%s): %s\n",
			dh->path, strerror(save_errno));
		return (gfarm_errno_to_error(save_errno));
	}
	retv = gfprep_asprintf(&child, "%s/%s", dh->path, dent->d_name);
	if (retv == -1) {
		fprintf(stderr, "FATAL: no memory(%s)\n", dh->path);
		return (GFARM_ERR_NO_MEMORY);
	}
	retv = lstat(child, &st);
	if (retv == -1) {
		save_errno = errno;
		fprintf(stderr, "ERROR: lstat(%s) for readdirplus(%s): %s\n",
			child, dh->path, strerror(save_errno));
		free(child);
		return (gfarm_errno_to_error(save_errno));
	}
	free(child);
	dirtree_convert_stat(&st, stp);
	/* not use dent->d_type here */
	if (S_ISREG(st.st_mode))
		dentp->d_type = GFS_DT_REG;
	else if (S_ISDIR(st.st_mode))
		dentp->d_type = GFS_DT_DIR;
	else if (S_ISLNK(st.st_mode))
		dentp->d_type = GFS_DT_LNK;
	else
		dentp->d_type = GFS_DT_UNKNOWN;
	strncpy(dentp->d_name, dent->d_name, strlen(dent->d_name) + 1);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
dirtree_local_closedir(struct dirtree_dir_handle *dh)
{
	int retv, save_errno;
	DIR *dir = dh->dir;

	retv = closedir(dir);
	if (retv == -1) {
		save_errno = errno;
		fprintf(stderr, "ERROR: closedir(%s): %s\n",
			dh->path, strerror(save_errno));
		return (gfarm_errno_to_error(save_errno));
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
dirtree_local_lstat(const char *path, struct my_stat *stp)
{
	int retv, save_errno;
	struct stat st;

	retv = lstat(path, &st);
	if (retv == -1) {
		save_errno = errno;
		if (save_errno != ENOENT)
			fprintf(stderr, "ERROR: lstat(%s): %s\n",
				path, strerror(save_errno));
		return (gfarm_errno_to_error(save_errno));
	}
	dirtree_convert_stat(&st, stp);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
dirtree_gfarm_opendir(const char *path, struct dirtree_dir_handle *dh)
{
	GFS_DirPlus dir;
	gfarm_error_t e;

	e = gfs_opendirplus(path, &dir);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "ERROR: gfs_opendirplus(%s): %s\n",
			path, gfarm_error_string(e));
		return (e);
	}
	dh->dir = dir;
	dh->path = path;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
dirtree_gfarm_readdirplus(struct dirtree_dir_handle *dh,
			  struct gfs_dirent *dentp, struct my_stat *stp)
{
	GFS_DirPlus dir = dh->dir;
	struct gfs_dirent *dent;
	struct gfs_stat *st;
	gfarm_error_t e;

	e = gfs_readdirplus(dir, &dent, &st);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "ERROR: gfs_readdirplus(%s): %s\n",
			dh->path, gfarm_error_string(e));
		return (e);
	}
	if (dent == NULL)
		return (GFARM_ERR_NO_SUCH_OBJECT); /* end */
	*dentp = *dent; /* copy */
	dirtree_convert_gfs_stat(st, stp);

	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
dirtree_gfarm_closedir(struct dirtree_dir_handle *dh)
{
	GFS_DirPlus dir = dh->dir;
	gfarm_error_t e;

	e = gfs_closedirplus(dir);
	if (e != GFARM_ERR_NO_ERROR)
		fprintf(stderr, "ERROR: gfs_closedirplus(%s): %s\n",
			dh->path, gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
dirtree_gfarm_lstat(const char *path, struct my_stat *stp)
{
	struct gfs_stat st;
	gfarm_error_t e;

	e = gfs_lstat(path, &st);
	if (e != GFARM_ERR_NO_ERROR) {
		if (e != GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY)
			fprintf(stderr, "ERROR: gfs_lstat(%s): %s\n",
				path, gfarm_error_string(e));
		return (e);
	}
	dirtree_convert_gfs_stat(&st, stp);
	gfs_stat_free(&st);
	return (GFARM_ERR_NO_ERROR);
}

static int
dirtree_child(void *param, FILE *from_parent, FILE *to_parent)
{
	int command, is_file;
	gfarm_error_t e;
	char *subpath, *src_dir, *src_path, *dst_path;
	char *name;
	struct dirtree_dir_handle dh;
	struct gfs_dirent dent;
	struct my_stat src_st, dst_st;
	int ncopy, i, retv, is_retry;
	char **copy;
	FILE *tmpfp;
	char buf[64];
	gfarm_dirtree_t *handle = param;
	gfarm_error_t (*func_opendir)(const char *path,
				      struct dirtree_dir_handle *dh);
	gfarm_error_t (*func_readdirplus)(struct dirtree_dir_handle *dh,
					  struct gfs_dirent *dentp,
					  struct my_stat *stp);
	gfarm_error_t (*func_closedir)(struct dirtree_dir_handle *dh);
	gfarm_error_t (*func_lstat)(const char *path, struct my_stat *stp);

	if (handle->src_type == URL_TYPE_GFARM) {
		func_opendir = dirtree_gfarm_opendir;
		func_readdirplus = dirtree_gfarm_readdirplus;
		func_closedir = dirtree_gfarm_closedir;
	} else {
		assert(handle->src_type == URL_TYPE_LOCAL);
		func_opendir = dirtree_local_opendir;
		func_readdirplus = dirtree_local_readdirplus;
		func_closedir = dirtree_local_closedir;
	}
	if (handle->dst_type == URL_TYPE_GFARM)
		func_lstat = dirtree_gfarm_lstat;
	else
		func_lstat = dirtree_local_lstat;

	e = gfarm_initialize(NULL, NULL);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "ERROR: gfarm_initialize: %s\n",
			gfarm_error_string(e));
		gfpara_recv_purge(from_parent);
		gfpara_send_int(to_parent, DIRTREE_STAT_NG);
		return (0);
	}
	/* -------------------------------------------------------- */
next_command: /* instead of "for (;;)" */
	gfpara_recv_int(from_parent, &command);
	switch (command) {
	case DIRTREE_CMD_GET_DENTS:
		subpath = NULL;
		src_dir = NULL;
		gfpara_recv_string(from_parent, &subpath);
		if (subpath[0] == '\0')
			retv = gfprep_asprintf(&src_dir, "%s",
					       handle->src_dir);
		else if (strcmp(handle->src_dir, "/") == 0)
			retv = gfprep_asprintf(&src_dir, "/%s", subpath);
		else
			retv = gfprep_asprintf(&src_dir, "%s/%s",
					       handle->src_dir, subpath);
		if (retv == -1) {
			fprintf(stderr, "FATAL: no memory\n");
			gfpara_send_int(to_parent, DIRTREE_STAT_NG);
			free(subpath);
			goto term;
		}
		is_retry = 0;
dents_retry:
		tmpfp = tmpfile();
		if (tmpfp == NULL) {
			fprintf(stderr, "FATAL: tmpfile(3) failed\n");
			gfpara_send_int(to_parent, DIRTREE_STAT_NG);
			free(subpath);
			goto term;
		}
		e = func_opendir(src_dir, &dh); /* with printing error */
		if (e != GFARM_ERR_NO_ERROR) {
			if (!is_retry && gfm_client_is_connection_error(e)) {
				fclose(tmpfp);
				fflush(stderr);
				is_retry = 1;
				goto dents_retry;
			}
			gfpara_send_int(to_parent, DIRTREE_STAT_IGNORE);
			free(subpath);
			free(src_dir);
			goto next_command;
		}
		gfpara_send_int(tmpfp, DIRTREE_STAT_GET_DENTS_OK);
dents_loop:
		e = func_readdirplus(&dh, &dent, &src_st);
		if (e != GFARM_ERR_NO_ERROR) {
			if (!is_retry && gfm_client_is_connection_error(e)) {
				fclose(tmpfp);
				(void)func_closedir(&dh);
				fflush(stderr);
				is_retry = 1;
				goto dents_retry;
			}
			if (e != GFARM_ERR_NO_SUCH_OBJECT) {
				fclose(tmpfp);
				(void)func_closedir(&dh);
				fflush(stderr);
				free(src_dir);
				free(subpath);
				gfpara_send_int(
					to_parent, DIRTREE_STAT_IGNORE);
				goto next_command;
			}
			/* GFARM_ERR_NO_SUCH_OBJECT: end of directory */
			e = func_closedir(&dh);
			if (!is_retry && gfm_client_is_connection_error(e)) {
				fclose(tmpfp);
				fflush(stderr);
				is_retry = 1;
				goto dents_retry;
			}
			/* abandon error */

			rewind(tmpfp);
			for (;;) {
				retv = fread(buf, 1, sizeof(buf), tmpfp);
				if (ferror(tmpfp)) {
					fprintf(stderr,
						"FATAL: fread() failed\n");
					goto dents_error;
				}
				if (retv == 0)
					break; /* EOF */
				fwrite(buf, 1, retv, to_parent);
				if (ferror(to_parent)) {
					fprintf(stderr,
						"FATAL: fwrite() failed\n");
					goto dents_error;
				}
				if (retv < (int)sizeof(buf))
					break; /* EOF */
			}
			fclose(tmpfp);
			fflush(to_parent);
			gfpara_send_int(to_parent, DIRTREE_ENTRY_END);
			if (is_retry)
				fprintf(stderr, "INFO: retry opendir(%s) OK\n",
					src_dir);
			free(src_dir);
			free(subpath);
			goto next_command; /* success */
dents_error:
			fprintf(stderr,
				"FATAL: cannot read directory: %s\n", src_dir);
			gfpara_send_int(to_parent, DIRTREE_STAT_NG);
			fclose(tmpfp);
			free(subpath);
			free(src_dir);
			goto term; /* unrecoverable */
		}
		name = dent.d_name;
		if (name[0] == '.' &&
		    (name[1] == '\0' || (name[1] == '.' && name[2] == '\0')))
			goto dents_loop; /* . and .. : skip */
		/* send entry information */
		gfpara_send_int(tmpfp, DIRTREE_ENTRY_NAME);
		/* 1: subpath/name */
		if (subpath[0] == '\0')
			gfpara_send_string(tmpfp, "%s", name);
		else
			gfpara_send_string(tmpfp, "%s/%s", subpath, name);
		/* 2: src_m_sec */
		gfpara_send_int64(tmpfp, src_st.mtime_sec);
		/* 3: src_tv_nsec */
		gfpara_send_int(tmpfp, src_st.mtime_nsec);
		/* 4: src_mode */
		gfpara_send_int(tmpfp, src_st.mode);
		/* 5: src_nlink */
		gfpara_send_int(tmpfp, src_st.nlink);
		/* 6: src_d_type */
		gfpara_send_int(tmpfp, (int) dent.d_type);
		if (dent.d_type == GFS_DT_REG) /* src is file */
			/* 7: src_size */
			gfpara_send_int64(tmpfp, src_st.size);
		goto dents_loop; /* loop */
		/* ---------------------------------- */
	case DIRTREE_CMD_GET_FINFO:
		subpath = NULL;
		src_path = NULL;
		dst_path = NULL;
		gfpara_recv_string(from_parent, &subpath);
		gfpara_recv_int(from_parent, &is_file);
		if (subpath[0] == '\0') {
			fprintf(stderr, "FATAL: unexpected message\n");
			gfpara_send_int(to_parent, DIRTREE_STAT_NG);
			goto term;
		}
		retv = gfprep_asprintf(&src_path, "%s/%s",
				       handle->src_dir, subpath);
		if (retv == -1) {
			fprintf(stderr, "FATAL: no memory\n");
			gfpara_send_int(to_parent, DIRTREE_STAT_NG);
			src_dir = NULL;
			free(subpath);
			goto term;
		}
		gfpara_send_int(to_parent, DIRTREE_STAT_GET_FINFO_OK);
		/* ----- src ----- */
		if (handle->src_type == URL_TYPE_LOCAL || is_file == 0) {
			/* 1: src_ncopy */
			gfpara_send_int(to_parent, 0);
			goto finfo_dst;
		}
		assert(handle->src_type == URL_TYPE_GFARM);
		is_retry = 0;
finfo_retry1:
		e = gfs_replica_list_by_name(src_path, &ncopy, &copy);
		if (e != GFARM_ERR_NO_ERROR) {
			if (!is_retry && gfm_client_is_connection_error(e)) {
				is_retry = 1;
				goto finfo_retry1;
			}
			/* 1: src_ncopy */
			gfpara_send_int(to_parent, 0);
			fprintf(stderr,
				"ERROR: gfs_replica_list_by_name(%s): %s\n",
				src_path, gfarm_error_string(e));
			goto finfo_dst;
		}
		if (is_retry)
			fprintf(stderr,
				"INFO: retry gfs_replica_list_by_name(%s)"
				" OK\n", src_dir);
		/* 1: src_ncopy */
		gfpara_send_int(to_parent, ncopy);
		for (i = 0; i < ncopy; i++) /* 2: src_copy */
			gfpara_send_string(to_parent, "%s", copy[i]);
		gfarm_strings_free_deeply(ncopy, copy);
finfo_dst:	/* ----- dst ----- */
		if (handle->dst_type == URL_TYPE_UNUSED) {
			/* dst is unused */
			gfpara_send_int(to_parent, 0); /* 3: dst_exist */
			free(subpath);
			free(src_path);
			goto next_command;
		}
		assert(handle->dst_dir);
		retv = gfprep_asprintf(&dst_path, "%s/%s",
				       handle->dst_dir, subpath);
		if (retv == -1) {
			fprintf(stderr, "FATAL: no memory\n");
			gfpara_send_int(to_parent, 0); /* 3: dst_exist */
			free(subpath);
			free(src_path);
			goto next_command;
		}
		is_retry = 0;
finfo_retry2:
		e = func_lstat(dst_path, &dst_st);
		if (e != GFARM_ERR_NO_ERROR) {
			if (!is_retry && gfm_client_is_connection_error(e)) {
				is_retry = 1;
				goto finfo_retry2;
			}
			/* dst does not exist */
			gfpara_send_int(to_parent, 0); /* 3: dst_exist */
			free(subpath);
			free(src_path);
			free(dst_path);
			goto next_command;
		}
		if (is_retry)
			fprintf(stderr, "INFO: retry lstat(%s) OK\n", src_dir);

		/* 3: dst_exist */
		gfpara_send_int(to_parent, 1);
		/* 4: dst_m_sec */
		gfpara_send_int64(to_parent, dst_st.mtime_sec);
		/* 5: dst_m_nsec */
		gfpara_send_int(to_parent, dst_st.mtime_nsec);
		if (S_ISREG(dst_st.mode)) {
			/* 6: dst_d_type */
			gfpara_send_int(to_parent, GFS_DT_REG);
			/* 7: dst_size */
			gfpara_send_int64(to_parent, dst_st.size);
			if (handle->dst_type == URL_TYPE_GFARM) {
				is_retry = 0;
finfo_retry3:
				e = gfs_replica_list_by_name(
					dst_path, &ncopy, &copy);
				if (e == GFARM_ERR_NO_ERROR) {
					if (is_retry)
						fprintf(stderr,
						"INFO: retry "
						"gfs_replica_list_by_name(%s) "
						"OK\n", src_dir);
					/* 8: dst_ncopy */
					gfpara_send_int(to_parent, ncopy);
					for (i = 0; i < ncopy; i++)
						/* 9: dst_copy */
						gfpara_send_string(
							to_parent,
							"%s", copy[i]);
					gfarm_strings_free_deeply(ncopy, copy);
				} else { /* no replica: ncopy == 0 */
					if (!is_retry &&
					    gfm_client_is_connection_error(e)
						) {
						is_retry = 1;
						goto finfo_retry3;
					}
					fprintf(stderr,
					"INFO: gfs_replica_list_by_name(%s):"
					" %s\n", dst_path,
					gfarm_error_string(e));
					/* 8: dst_ncopy */
					gfpara_send_int(to_parent, 0);
				}
			} else /* URL_TYPE_LOCAL: ncopy == 0 */
				/* 8: dst_ncopy */
				gfpara_send_int(to_parent, 0);
		} else if (S_ISDIR(dst_st.mode)) /* 6: dst_d_type */
			gfpara_send_int(to_parent, GFS_DT_DIR);
		else if (S_ISLNK(dst_st.mode)) /* 6: dst_d_type */
			gfpara_send_int(to_parent, GFS_DT_LNK);
		else /* 6: dst_d_type */
			gfpara_send_int(to_parent, GFS_DT_UNKNOWN);
		free(subpath);
		free(src_path);
		free(dst_path);
		goto next_command;
		 /* ---------------------------------- */
	case DIRTREE_CMD_TERMINATE:
		e = gfarm_terminate();
		if (e != GFARM_ERR_NO_ERROR)
			fprintf(stderr, "ERROR: gfarm_terminate: %s\n",
			    gfarm_error_string(e));
		gfpara_send_int(to_parent, DIRTREE_STAT_END);
		return (0);
		/* ---------------------------------- */
	default:
		fprintf(stderr, "ERROR: unexpected DIRTREE_CMD: %d\n",
			command);
		gfpara_send_int(to_parent, DIRTREE_STAT_NG);
	}
term: /* -------------------------------------------------------- */
	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR)
		fprintf(stderr, "ERROR: gfarm_terminate: %s\n",
			gfarm_error_string(e));
	return (0);
}

static int
dirtree_send(FILE *child_in, gfpara_proc_t *proc, void *param, int stop)
{
	static const char diag[] = "dirtree_send";
	gfarm_dirtree_t *handle = param;
	char *subdir;
	gfarm_error_t e;
	gfarm_dirtree_entry_t *ent;
	void *p;

	if (stop) {
		gfpara_send_int(child_in, DIRTREE_CMD_TERMINATE);
		gfarm_mutex_lock(&handle->mutex, diag, "mutex");
		handle->n_free_procs = handle->n_parallel; /* terminate */
		gfarm_cond_broadcast(&handle->free_procs, diag, "free_procs");
		gfarm_mutex_unlock(&handle->mutex, diag, "mutex");
		return (GFPARA_NEXT);
	}
	gfarm_mutex_lock(&handle->mutex, diag, "mutex");
retry:
	/* Don't read fifo_dirs before fifo_ents */
	e = gfarm_fifo_simple_next(handle->fifo_ents, &p); /* nonblock */
	if (e == GFARM_ERR_NO_ERROR) {
		handle->n_get_ents++;
		gfarm_mutex_unlock(&handle->mutex, diag, "mutex");
		ent = p;
		gfpara_send_int(child_in, DIRTREE_CMD_GET_FINFO);
		gfpara_send_string(child_in, "%s", ent->subpath);
		gfpara_send_int(child_in,
				ent->src_d_type == GFS_DT_REG ? 1 : 0);
		gfpara_data_set(proc, ent);
		return (GFPARA_NEXT);
	}
	if (handle->n_get_ents > 0) { /* wait all dirtree_recv_finfo() */
		gfarm_cond_wait(&handle->get_ents, &handle->mutex, diag,
				"get_ents");
		goto retry;
	}
	e = gfarm_fifo_simple_next(handle->fifo_dirs, &p); /* nonblock */
	if (e == GFARM_ERR_NO_ERROR) {
		gfarm_cond_broadcast(&handle->free_procs, diag, "free_prpcs");
		gfarm_mutex_unlock(&handle->mutex, diag, "mutex");
		subdir = p;
		gfpara_send_int(child_in, DIRTREE_CMD_GET_DENTS);
		gfpara_send_string(child_in, "%s", subdir);
		free(subdir);
		return (GFPARA_NEXT);
	} else if (e == GFARM_ERR_NO_SUCH_OBJECT) {
		handle->n_free_procs++;
		if (handle->n_free_procs >= handle->n_parallel) { /* done */
			gfarm_cond_broadcast(&handle->free_procs, diag,
					     "free_procs");
			gfarm_mutex_unlock(&handle->mutex, diag, "mutex");
			gfpara_send_int(child_in, DIRTREE_CMD_TERMINATE);
			return (GFPARA_NEXT);
		}
		gfarm_cond_wait(&handle->free_procs, &handle->mutex, diag,
				"free_procs");
		if (handle->n_free_procs >= handle->n_parallel) { /* done */
			gfarm_cond_broadcast(&handle->free_procs, diag,
					     "free_procs");
			gfarm_mutex_unlock(&handle->mutex, diag, "mutex");
			gfpara_send_int(child_in, DIRTREE_CMD_TERMINATE);
			return (GFPARA_NEXT);
		}
		handle->n_free_procs--;
		goto retry;
	} else { /* never reach */
		handle->n_free_procs++;
		fprintf(stderr, "FATAL: gfarm_fifo_simple_next: %s\n",
			gfarm_error_string(e));
		gfarm_cond_broadcast(&handle->free_procs, diag, "free_procs");
		gfarm_mutex_unlock(&handle->mutex, diag, "mutex");
		gfpara_send_int(child_in, DIRTREE_CMD_TERMINATE);
		return (GFPARA_NEXT);
	}
}

static int
dirtree_recv_dents(FILE *child_out, gfpara_proc_t *proc, void *param)
{
	static const char diag[] = "dirtree_recv_dents";
	gfarm_error_t e;
	gfarm_dirtree_t *handle = param;
	int type, d_type_int;
	gfarm_dirtree_entry_t *ent;
	char *subpath;

	for (;;) {
		gfpara_recv_int(child_out, &type);
		if (type == DIRTREE_ENTRY_NAME) {
			GFARM_MALLOC(ent);
			gfpara_recv_string(child_out, &subpath); /* 1 */
			if (ent == NULL || subpath == NULL) {
				fprintf(stderr, "FATAL: no memory\n");
				free(subpath);
				free(ent);
				gfpara_recv_purge(child_out);
				return (GFPARA_FATAL);
			}
			/* initialize */
			ent->src_ncopy = 0;
			ent->dst_ncopy = 0;

			ent->subpath = subpath;
			gfpara_recv_int64(child_out, &ent->src_m_sec); /* 2 */
			gfpara_recv_int(child_out, &ent->src_m_nsec); /* 3 */
			gfpara_recv_int(child_out, &ent->src_mode); /* 4 */
			gfpara_recv_int(child_out, &ent->src_nlink); /* 5 */
			gfpara_recv_int(child_out, &d_type_int); /* 6 */
			ent->src_d_type = (unsigned char) d_type_int;
			gfarm_mutex_lock(&handle->mutex, diag, "mutex");
			if (handle->is_recursive &&
			    ent->src_d_type == GFS_DT_DIR) {
				char *subpath_copy = strdup(subpath);
				if (subpath_copy == NULL) {
					gfarm_mutex_unlock(&handle->mutex,
							   diag, "mutex");
					fprintf(stderr, "FATAL: no memory\n");
					gfpara_recv_purge(child_out);
					return (GFPARA_FATAL);
				}
				e = gfarm_fifo_simple_enter(handle->fifo_dirs,
							    subpath_copy);
				if (e != GFARM_ERR_NO_ERROR) {
					gfarm_mutex_unlock(&handle->mutex,
							   diag, "mutex");
					fprintf(stderr,
					"FATAL: gfarm_fifo_simple_enter: "
					"%s\n", gfarm_error_string(e));
					gfpara_recv_purge(child_out);
					return (GFPARA_FATAL);
				}
			}
			if (ent->src_d_type == GFS_DT_REG) /* 7 */
				gfpara_recv_int64(child_out, &ent->src_size);
			else
				ent->src_size = 0;
			e = gfarm_fifo_simple_enter(handle->fifo_ents, ent);
			gfarm_mutex_unlock(&handle->mutex, diag, "mutex");
			if (e != GFARM_ERR_NO_ERROR) {
				fprintf(stderr,
					"FATAL: gfarm_fifo_simple_enter: "
					"%s\n", gfarm_error_string(e));
				gfpara_recv_purge(child_out);
				return (GFPARA_FATAL);
			}
		} else if (type == DIRTREE_ENTRY_END)
			return (GFPARA_NEXT);
		else {
			fprintf(stderr,
				"FATAL: unexpected message from child\n");
			gfpara_recv_purge(child_out);
			return (GFPARA_FATAL);
		}
	}
}

static int
dirtree_recv_finfo(FILE *child_out, gfpara_proc_t *proc, void *param)
{
	gfarm_dirtree_t *handle = param;
	int d_type_int, i;
	gfarm_dirtree_entry_t *ent;

	ent = gfpara_data_get(proc);
	gfpara_recv_int(child_out, &ent->src_ncopy); /* 1 */
	if (ent->src_ncopy > 0) {
		GFARM_MALLOC_ARRAY(ent->src_copy, ent->src_ncopy);
		if (ent->src_copy == NULL) {
			fprintf(stderr, "FATAL: no memory\n");
			gfpara_recv_purge(child_out);
			return (GFPARA_FATAL);
		}
		for (i = 0; i < ent->src_ncopy; i++) { /* 2 */
			gfpara_recv_string(child_out, &ent->src_copy[i]);
			if (ent->src_copy[i] == NULL) {
				fprintf(stderr, "FATAL: no memory\n");
				gfpara_recv_purge(child_out);
				return (GFPARA_FATAL);
			}
		}
	} else
		ent->src_copy = NULL;
	gfpara_recv_int(child_out, &i); /* 3 */
	ent->dst_exist = i;
	if (ent->dst_exist) {
		gfpara_recv_int64(child_out, &ent->dst_m_sec); /* 4 */
		gfpara_recv_int(child_out, &ent->dst_m_nsec); /* 5 */
		gfpara_recv_int(child_out, &d_type_int); /* 6 */
		ent->dst_d_type = (unsigned char) d_type_int;
		if (ent->dst_d_type == GFS_DT_REG) {
			gfpara_recv_int64(child_out, &ent->dst_size); /* 7 */
			gfpara_recv_int(child_out, &ent->dst_ncopy); /* 8 */
			if (ent->dst_ncopy > 0) {
				GFARM_MALLOC_ARRAY(ent->dst_copy,
						   ent->dst_ncopy);
				if (ent->dst_copy == NULL) {
					fprintf(stderr, "FATAL: no memory\n");
					gfpara_recv_purge(child_out);
					return (GFPARA_FATAL);
				}
			} else
				ent->dst_copy = NULL;
			for (i = 0; i < ent->dst_ncopy; i++) { /* 9 */
				gfpara_recv_string(child_out,
						   &ent->dst_copy[i]);
				if (ent->dst_copy[i] == NULL) {
					fprintf(stderr, "FATAL: no memory\n");
					gfpara_recv_purge(child_out);
					return (GFPARA_FATAL);
				}
			}
		} else { /* dst is not a file */
			ent->dst_size = 0; /* 7 */
			ent->dst_ncopy = 0; /* 8 */
			ent->dst_copy = NULL; /* 9 */
		}
	} else { /* dst does not exist, or dst is unused */
		ent->dst_m_sec = 0;  /* 4 */
		ent->dst_m_nsec = 0;  /* 5 */
		ent->dst_d_type = 0; /* 6 */
		ent->dst_size = 0;   /* 7 */
		ent->dst_ncopy = 0;  /* 8 */
		ent->dst_copy = NULL; /* 9 */
	}
	ent->n_pending = 0;
	gfarm_fifo_enter(handle->fifo_handle, &ent);
	return (GFPARA_NEXT);
}

static int
dirtree_recv(FILE *child_out, gfpara_proc_t *proc, void *param)
{
	static const char diag[] = "dirtree_recv";
	gfarm_dirtree_t *handle = param;
	int status, retv;

	gfpara_recv_int(child_out, &status);
	switch (status) {
	case DIRTREE_STAT_GET_DENTS_OK:
		return (dirtree_recv_dents(child_out, proc, param));
	case DIRTREE_STAT_GET_FINFO_OK:
		retv = dirtree_recv_finfo(child_out, proc, param);
		gfarm_mutex_lock(&handle->mutex, diag, "mutex");
		handle->n_get_ents--;
		gfarm_cond_broadcast(&handle->get_ents, diag, "get_ents");
		gfarm_mutex_unlock(&handle->mutex, diag, "mutex");
		return (retv);
	case DIRTREE_STAT_IGNORE:
		return (GFPARA_NEXT);
	case DIRTREE_STAT_END:
		return (GFPARA_END);
	case DIRTREE_STAT_NG:
	default:
		gfpara_recv_purge(child_out);
		fprintf(stderr,
			"FATAL: unexpected dirtree status: %d\n", status);
		return (GFPARA_FATAL);
	}
}

static void *
dirtree_end(void *param)
{
	gfarm_dirtree_t *handle = param;
	gfarm_fifo_wait_to_finish(handle->fifo_handle);
	return (NULL);
}

static const char dirtree_startdir[] = "";

static const char *
convert_gfarm_url(const char *gfarm_url)
{
	/* gfarm:/... to gfarm: */
	if (strncmp(gfarm_url, GFARM_URL_PREFIX, GFARM_URL_PREFIX_LENGTH) == 0
	    &&
	    *(gfarm_url + GFARM_URL_PREFIX_LENGTH) == '/'
	    &&
	    *(gfarm_url + GFARM_URL_PREFIX_LENGTH + 1) == '\0')
		return (GFARM_URL_PREFIX);
	else
		return (gfarm_url);
}

/* Do not call this function after gfarm_initialize() */
gfarm_error_t
gfarm_dirtree_init_fork(
	gfarm_dirtree_t **handlep, const char *src_url, const char *dst_url,
	int n_parallel, int fifo_size, int is_recursive)
{
	static const char diag[] = "gfarm_dirtree_init_fork";
	gfarm_dirtree_t *handle;
	gfarm_error_t e;
	int src_type, dst_type;
	const char *src_dir, *dst_dir;
	char *startdir;

	if (n_parallel <= 0 || handlep == NULL || src_url == NULL)
		return (GFARM_ERR_INVALID_ARGUMENT);
	if (gfprep_url_is_gfarm(src_url)) {
		src_type = URL_TYPE_GFARM;
		/* not remove gfarm: prefix */
		src_dir = convert_gfarm_url(src_url);
	} else if (gfprep_url_is_local(src_url)) {
		src_type = URL_TYPE_LOCAL;
		src_dir = src_url + GFPREP_FILE_URL_PREFIX_LENGTH;
		src_dir = gfarm_dirtree_path_skip_root_slash(src_dir);
	} else
		return (GFARM_ERR_INVALID_ARGUMENT);

	if (dst_url == NULL) {
		dst_type = URL_TYPE_UNUSED;
		dst_dir = NULL;
	} else if (gfprep_url_is_gfarm(dst_url)) {
		dst_type = URL_TYPE_GFARM;
		/* not remove gfarm: prefix */
		dst_dir = convert_gfarm_url(dst_url);
	} else if (gfprep_url_is_local(dst_url)) {
		dst_type = URL_TYPE_LOCAL;
		dst_dir = dst_url + GFPREP_FILE_URL_PREFIX_LENGTH;
		dst_dir = gfarm_dirtree_path_skip_root_slash(dst_dir);
	} else
		return (GFARM_ERR_INVALID_ARGUMENT);

	GFARM_MALLOC(handle);
	if (handle == NULL)
		return (GFARM_ERR_NO_MEMORY);

	e = gfarm_fifo_init(&handle->fifo_handle,
			    fifo_size, sizeof(gfarm_dirtree_entry_t *),
			    dirtree_fifo_set, dirtree_fifo_get);
	if (e != GFARM_ERR_NO_ERROR) {
		free(handle);
		return (e);
	}

	e = gfarm_fifo_simple_init(&handle->fifo_dirs);
	if (e != GFARM_ERR_NO_ERROR) {
		gfarm_fifo_free(handle->fifo_handle);
		free(handle);
		return (e);
	}
	e = gfarm_fifo_simple_init(&handle->fifo_ents);
	if (e != GFARM_ERR_NO_ERROR) {
		gfarm_fifo_free(handle->fifo_handle);
		gfarm_fifo_simple_free(handle->fifo_dirs);
		free(handle);
		return (e);
	}

	/* set src_dir and dst_dir before gfpara_init() */
	handle->src_dir = src_dir;
	handle->dst_dir = dst_dir;
	handle->src_type = src_type;
	handle->dst_type = dst_type;
	e = gfpara_init(&handle->gfpara_handle, n_parallel,
			dirtree_child, handle,
			dirtree_send, handle,
			dirtree_recv, handle,
			dirtree_end, handle);
	if (e != GFARM_ERR_NO_ERROR) {
		gfarm_fifo_free(handle->fifo_handle);
		gfarm_fifo_simple_free(handle->fifo_dirs);
		gfarm_fifo_simple_free(handle->fifo_ents);
		free(handle);
		return (e);
	}
	handle->n_parallel = n_parallel;
	startdir = strdup(dirtree_startdir);
	if (startdir == NULL) {
		gfpara_join(handle->gfpara_handle);
		gfarm_fifo_free(handle->fifo_handle);
		gfarm_fifo_simple_free(handle->fifo_dirs);
		gfarm_fifo_simple_free(handle->fifo_ents);
		free(handle);
		return (GFARM_ERR_NO_MEMORY);
	}
	e = gfarm_fifo_simple_enter(handle->fifo_dirs, startdir);
	if (e != GFARM_ERR_NO_ERROR) {
		free(startdir);
		gfpara_join(handle->gfpara_handle);
		gfarm_fifo_free(handle->fifo_handle);
		gfarm_fifo_simple_free(handle->fifo_dirs);
		gfarm_fifo_simple_free(handle->fifo_ents);
		free(handle);
		fprintf(stderr, "ERROR: gfarm_fifo_simple_enter: %s\n",
			gfarm_error_string(e));
		return (e);
	}
	gfarm_mutex_init(&handle->mutex, diag, "mutex");
	gfarm_cond_init(&handle->free_procs, diag, "free_procs");
	gfarm_cond_init(&handle->get_ents, diag, "get_ents");
	handle->n_free_procs = 0;
	handle->n_get_ents = 0;
	handle->is_recursive = is_recursive;
	handle->started = 0;

	*handlep = handle;
	return (e);
}

gfarm_error_t
gfarm_dirtree_open(gfarm_dirtree_t *handle)
{
	gfarm_error_t e;

	e = gfpara_start(handle->gfpara_handle);
	if (e == GFARM_ERR_NO_ERROR)
		handle->started = 1;
	return (e);
}

/* MT-safe */
gfarm_error_t
gfarm_dirtree_pending(gfarm_dirtree_t *handle)
{
	return (gfarm_fifo_pending(handle->fifo_handle));
}

/* MT-safe */
/* do not gfarm_dirtree_entry_free(entp) */
gfarm_error_t
gfarm_dirtree_checknext(gfarm_dirtree_t *handle, gfarm_dirtree_entry_t **entp)
{
	return (gfarm_fifo_checknext(handle->fifo_handle, entp));
}

void
gfarm_dirtree_entry_free(gfarm_dirtree_entry_t *entp)
{
	free(entp->subpath);
	if (entp->src_ncopy > 0 && entp->src_copy)
		gfarm_strings_free_deeply(entp->src_ncopy, entp->src_copy);
	if (entp->dst_ncopy > 0 && entp->dst_copy)
		gfarm_strings_free_deeply(entp->dst_ncopy, entp->dst_copy);
	free(entp);
}

/* MT-safe */
/* must do gfarm_dirtree_entry_free(entp) */
gfarm_error_t
gfarm_dirtree_next(gfarm_dirtree_t *handle, gfarm_dirtree_entry_t **entp)
{
	return (gfarm_fifo_delete(handle->fifo_handle, entp));
}

/* MT-safe */
gfarm_error_t
gfarm_dirtree_delete(gfarm_dirtree_t *handle)
{
	gfarm_error_t e;
	gfarm_dirtree_entry_t *ent;

	e = gfarm_fifo_delete(handle->fifo_handle, &ent);
	if (e == GFARM_ERR_NO_ERROR)
		gfarm_dirtree_entry_free(ent);

	return (e);
}

/* not MT-safe */
gfarm_error_t
gfarm_dirtree_close(gfarm_dirtree_t *handle)
{
	gfarm_error_t e;
	static const char diag[] = "gfarm_dirtree_close";
	void *p;
	gfarm_dirtree_entry_t *ent;
	char *subdir;

	if (handle->started) {
		gfpara_terminate(handle->gfpara_handle, 10000);
		while (gfarm_dirtree_delete(handle) == GFARM_ERR_NO_ERROR)
			;
	}
	while (gfarm_fifo_simple_next(handle->fifo_ents, &p)
	    == GFARM_ERR_NO_ERROR) {
		ent = p;
		gfarm_dirtree_entry_free(ent);
	}
	while (gfarm_fifo_simple_next(handle->fifo_dirs, &p)
	    == GFARM_ERR_NO_ERROR) {
		subdir = p;
		free(subdir);
	}

	/* fifo: quitted */
	e = gfpara_join(handle->gfpara_handle);
	gfarm_fifo_free(handle->fifo_handle);
	gfarm_fifo_simple_free(handle->fifo_dirs);
	gfarm_fifo_simple_free(handle->fifo_ents);
	gfarm_mutex_destroy(&handle->mutex, diag, "mutex");
	gfarm_cond_destroy(&handle->free_procs, diag, "free_procs");
	gfarm_cond_destroy(&handle->get_ents, diag, "get_ents");
	free(handle);

	return (e);
}

/* close automatically */
gfarm_error_t
gfarm_dirtree_array(gfarm_dirtree_t *handle, int *n_entsp,
		    gfarm_dirtree_entry_t ***entsp)
{
	gfarm_error_t e, e2;
	gfarm_list list;
	gfarm_dirtree_entry_t *entry;

	e = gfarm_list_init(&list);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	while ((e = gfarm_dirtree_next(handle, &entry))
	       == GFARM_ERR_NO_ERROR) {
		e = gfarm_list_add(&list, entry);
		if (e != GFARM_ERR_NO_ERROR)
			goto end; /* fatal */
	}
	if (e != GFARM_ERR_NO_ERROR && e != GFARM_ERR_NO_SUCH_OBJECT)
		goto end; /* fatal */
	e = GFARM_ERR_NO_ERROR;
	*entsp = gfarm_array_alloc_from_list(&list);
	if (*entsp == NULL) {
		e = GFARM_ERR_NO_MEMORY; /* fatal */
		goto end;
	}
	*n_entsp = gfarm_list_length(&list);
end:
	gfarm_list_free(&list);
	e2 = gfarm_dirtree_close(handle);
	if (e == GFARM_ERR_NO_ERROR)
		e = e2;
	return (e);
}

static void
dirtree_list_free(void *void_entp)
{
	gfarm_dirtree_entry_t *entp = void_entp;
	gfarm_dirtree_entry_free(entp);
}

void
gfarm_dirtree_array_free(int n_ents, gfarm_dirtree_entry_t **ents)
{
	gfarm_array_free_deeply(n_ents, ents, dirtree_list_free);
}
