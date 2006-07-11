/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <dirent.h>
#include <fnmatch.h>
#include <fcntl.h>
#include <sys/time.h>

#include <gfarm/gfarm.h>

#include "config.h"
#include "metadb_access.h"
#include "metadb_sw.h"

/*
 * in datadir
 *
 * gfarm_host_info/{hostname...}
 *
 * gfarm_path_info/{dir}/{name}__gfarm_pi       # path_info
 *                      /{name}__gfarm_fsi_idx  # file_section_info index
 *                      /{name}__gfarm_fsi__{section_name} # file_section_info
 *                      /{name}/{children...}   # {name} is a directory
 */

/*
 * COMMIT_MODE
 * commit (write) pattern:
 *
 *         normal  O_SYNC  fsync
 * normal    0       1       2
 * rename    3       4       5
 *
 * O_SYNC: use open(2) O_SYNC
 * fsync : use fsync(2)
 * rename: write to temporary file and rename after close().
 */
#define COMMIT_MODE 0

#if COMMIT_MODE == 3 || COMMIT_MODE == 4 || COMMIT_MODE == 5
#define RENAME_WITH_LOCK       /* lock for multi-client (no test) */
#define LOCK_INTERVAL 100000   /* microsecond */
#define LOCK_RETRY    5
#endif

#if 0
#ifdef HAVE_FDATASYNC
#define fsync(x) fdatasync(x)
#endif
#endif

/* #define CREATE_FSCI_WITHOUT_FSI */

/**********************************************************************/

#if   COMMIT_MODE == 0
#elif COMMIT_MODE == 1
#  define USE_FDOPEN_SYNC
#elif COMMIT_MODE == 2
#  define USE_FSYNC
#elif COMMIT_MODE == 3
#  define USE_WRITE_RENAME
#elif COMMIT_MODE == 4
#  define USE_WRITE_RENAME
#  define USE_FDOPEN_SYNC
#elif COMMIT_MODE == 5
#  define USE_WRITE_RENAME
#  define USE_FSYNC
#endif

#define HOSTINFODIR "gfarm_host_info"
#define PATHINFODIR "gfarm_path_info"

#define PATHINFO_SUFFIX "__gfarm_pi"
#define SECTIONINFO_INDEX_SUFFIX "__gfarm_fsi_idx"
#define SECTIONINFO_AFFIX  "__gfarm_fsi__"

/* host_info */
/* #define NHOSTALIASES "nhostalias" */
#define HOSTALIASES "hostaliases"   /* 1 */
#define ARCHITECTURE "architecture" /* 2 */
#define NCPU "ncpu"                 /* 3 */

/* path_info */
/* #define ST_INO "st_ino" */
#define ST_MODE "st_mode"     /* 1 */
#define ST_USER "st_user"     /* 2 */
#define ST_GROUP "st_group"   /* 3 */
#define ST_ATIME "st_atime"   /* 4 */
#define ST_MTIME "st_mtime"   /* 5 */
#define ST_CTIME "st_ctime"   /* 6 */
/* #define ST_SIZE "st_size" */
#define ST_NSECTIONS "st_nsections" /* 7 */

/* file_section_info and file_section_copies */
#define FILESIZE "filesize"           /* 1 */
#define CHECKSUMTYPE "checksum_type"  /* 2 */
#define CHECKSUM "checksum"           /* 3 */
#define SECTIONCOPIES "sectioncopies" /* 4 */

/* for open tools */
#define OPEN_NOCHECK 0
#define OPEN_FAILEXIST 1
#define OPEN_NEEDEXIST 2

static char *datadir = NULL;
static char *hostinfodir = NULL;
static char *pathinfodir = NULL;

/**********************************************************************/

static char *
is_dir(const char *path)
{
	struct stat st;

	if (lstat(path, &st) != 0)
		return (GFARM_ERR_NO_SUCH_OBJECT);
	if (!S_ISDIR(st.st_mode))
		return (GFARM_ERR_NOT_A_DIRECTORY);
	return (NULL);
}

static char *
is_file(const char *path)
{
	struct stat st;

	if (lstat(path, &st) != 0)
		return (GFARM_ERR_NO_SUCH_OBJECT);
	if (!S_ISREG(st.st_mode))
		return (GFARM_ERR_IS_A_DIRECTORY);
	return (NULL);
}

static char *
join_path(const char *path1, const char *path2)
{
	char *newpath;

	GFARM_MALLOC_ARRAY(newpath, strlen(path1) + strlen(path2) + 2);
	if (newpath == NULL)
		return (NULL);
	strcpy(newpath, path1);
	strcat(newpath, "/");
	strcat(newpath, path2);
	return (newpath);
}

static char *
join_str(const char *str1, const char *str2)
{
	char *newstr;

	GFARM_MALLOC_ARRAY(newstr, strlen(str1) + strlen(str2) + 1);
	if (newstr == NULL)
		return (NULL);
	strcpy(newstr, str1);
	strcat(newstr, str2);
	return (newstr);
}

static char *
join_str3(const char *str1, const char *str2, const char *str3)
{
	char *newstr;

	GFARM_MALLOC_ARRAY(newstr,
		strlen(str1) + strlen(str2) + strlen(str3) + 1);
	if (newstr == NULL)
		return (NULL);
	strcpy(newstr, str1);
	strcat(newstr, str2);
	strcat(newstr, str3);
	return (newstr);
}

/* separated by a space */
static char *
str_array_to_str_with_space(char **array, int start, int count)
{
	int len = 0;
	char *str = NULL;
	int i;

	for (i = start; i < count && array[i] != NULL; i++)
		len += strlen(array[i]) + 1;
	str = calloc(len + 1, 1);
	if (str == NULL)
		return (NULL);
	for (i = start; i < count && array[i] != NULL; i++) {
		strcat(str, array[i]);
		strcat(str, " ");
	}
	return (str);
}

/* free: gfarm_strarray_free();  */
static char *
str_to_words(char *str, int *np, char ***wordsp)
{
	char *tmpbuf[64]; /* XXX MAX */
	char *p;
	int i;
	char **tmp;

	*np = 0;
	if (str == NULL)
		return (GFARM_ERR_INVALID_ARGUMENT);
	p = str;
	while (*p != '\0') {
		while (*p == ' ')
			p++;
		if  (*p == '\0' || *p == '\n')
			break;
		tmpbuf[*np] = p;
		(*np)++;
		while (*p != ' ' && *p != '\0' && *p != '\n')
			p++;
		if  (*p == '\0' || *p == '\n')
			break;
		*p = '\0';
		p++;
	}
	tmpbuf[*np] = NULL;

	GFARM_MALLOC_ARRAY(tmp, *np + 1);
	if (tmp == NULL)
		return (GFARM_ERR_NO_MEMORY);
	for (i = 0; i < *np; i++) {
                tmp[i] = strdup(tmpbuf[i]);
        }
	tmp[i] = NULL;
	*wordsp = tmp;
	return (NULL);
}

/* for line_value_put() */
static char *
itoa(int i)
{
	static char n[16];

	snprintf(n, 16, "%d", i);
	return (n);
}

static char *
ltoa(long i)
{
	static char n[16];

	snprintf(n, 16, "%ld", i);
	return (n);
}


static char *
timespectoa(struct gfarm_timespec *time)
{
	static char ts[32];

	snprintf(ts, 32, "%u %u", time->tv_sec, time->tv_nsec);
	return (ts);
}


/* --------------------------- */

#define BUFSIZE 256

/* need free() */
static char *
line_value_get(FILE *fp, const char *key)
{
	char buf[BUFSIZE];
	char name[64];
	int len;
	char *ret = NULL;
	char *p;

	snprintf(name, 64, "%s = ", key);
	len = strlen(name);
	while (fgets(buf, BUFSIZE, fp) != NULL) {
		p = strchr(buf, '\n');
		if (p != NULL)
			*p = '\0';
		if (strncmp(buf, name, len) == 0) {
			GFARM_MALLOC_ARRAY(ret, BUFSIZE - len);
			strncpy(ret, &buf[len], BUFSIZE - len);
			return (ret);
		}
	}
	return (NULL);
}

static int
line_value_get_int(FILE *fp, const char *key, int error_val)
{
	char *tmp;
	int ret;

	tmp = line_value_get(fp, key);
	if (tmp == NULL)
		return (error_val);
	else {
		ret = atoi(tmp);
		free(tmp);
		return (ret);
	}
}

static long
line_value_get_long(FILE *fp, const char *key, long error_val)
{
	char *tmp;
	long ret;

	tmp = line_value_get(fp, key);
	if (tmp == NULL)
		return (error_val);
	else {
		ret = atol(tmp);
		free(tmp);
		return (ret);
	}
}

static struct gfarm_timespec
line_value_get_timespec(FILE *fp, const char *key)
{
	char *e;
	char *tmp;
	char **ts;
	int n;
	struct gfarm_timespec ret = {0, 0};

	tmp = line_value_get(fp, key);
	if (tmp == NULL)
		return (ret);
	e = str_to_words(tmp, &n, &ts);
	if (e != NULL)
		goto end2;
	if (n < 2)
		goto end1;
	ret.tv_sec = atoi(ts[0]);
	ret.tv_nsec = atoi(ts[1]);
end1:
	gfarm_strarray_free(ts);
end2:
	free(tmp);
	return (ret);
}


static char*
line_value_put(FILE *fp, const char *key, const char *value)
{
	char buf[BUFSIZE];
	char *emp = "";

	if (value == NULL)
		value = emp;

	snprintf(buf, BUFSIZE, "%s = %s\n", key, value);
	if (fputs(buf, fp) == EOF)
		return (GFARM_ERR_INPUT_OUTPUT);
	else
		return (NULL);
}

/* !0: success */
static int
is_path_info_name(char *name)
{
	static char pattern[PATH_MAX];
	static int initialize = 0;

	if (initialize == 0) {
		snprintf(pattern, PATH_MAX, "*%s", PATHINFO_SUFFIX);
		initialize = 0;
	}
	return !fnmatch(pattern, name, 0);
}

/* !0: success */
static int
is_file_section_info_name(char *name)
{
	static char pattern[PATH_MAX];
	static int initialize = 0;

	if (initialize == 0) {
		snprintf(pattern, PATH_MAX, "*%s*", SECTIONINFO_AFFIX);
		initialize = 0;
	}
	return !fnmatch(pattern, name, 0);
}

#ifdef USE_FDOPEN_SYNC
static FILE *
sync_fopen(const char *filename, const char *mode)
{
	int fd;
	FILE *fp;
	int save_errno;

	if (strcmp(mode, "w") != 0)
		return fopen(filename, mode);

	fd = open(filename, O_WRONLY|O_CREAT|O_TRUNC|O_SYNC,
		  0644); /* XXX need mode_t ? */
	if (fd < 0)
		return (NULL);

	fp = fdopen(fd, "w");
	if (fp == NULL) {
		save_errno = errno;
		close(fd);
		errno = save_errno;
		return (NULL);
	} else
		return (fp);
}

#define sync_fclose(x) fclose(x)
#else
#ifdef USE_FSYNC
#define sync_fopen(x,y) fopen(x,y)

static int
sync_fclose(FILE *stream)
{
	fflush(stream);
	fsync(fileno(stream));
	return fclose(stream);
}
#else
#define sync_fopen(x,y) fopen(x,y)
#define sync_fclose(x) fclose(x)
#endif /* USE_FSYNC */
#endif /* USE_FDOPEN_SYNC */

#ifdef USE_WRITE_RENAME
/* not MT-safe */
static char *my_fopen_filename;
static char *my_fopen_filename_orig;

#define FOPEN_TMP_SUFFIX "__tmp"

static FILE *
my_fopen(const char *filename, const char *mode)
{
	static int sfxlen = 0;
	int len;
	FILE *fp;
	int save_errno;
#ifdef RENAME_WITH_LOCK
	struct stat st;
	int retry;
#endif

	if (sfxlen == 0) {  /* initialize */
		sfxlen = strlen(FOPEN_TMP_SUFFIX);
		my_fopen_filename = NULL;
		my_fopen_filename_orig = NULL;
	}
	if (strcmp(mode, "w") == 0) {
		my_fopen_filename_orig = strdup(filename);
		if (my_fopen_filename_orig == NULL) {
			errno = ENOMEM;
			return (NULL);
		}
		len = sfxlen + strlen(filename) + 1;
		GFARM_MALLOC_ARRAY(my_fopen_filename, len);
		if (my_fopen_filename == NULL) {
			free(my_fopen_filename_orig);
			errno = ENOMEM;
			return (NULL);
		}
		snprintf(my_fopen_filename, len, "%s%s",
			 filename, FOPEN_TMP_SUFFIX);
#ifdef RENAME_WITH_LOCK
		retry = 0;
		while (stat(my_fopen_filename, &st) == 0) { /* EXIST */
			if (retry >= LOCK_RETRY) {
				break; /* give up */
			}
			usleep(LOCK_INTERVAL);
			retry++;
		}
#endif
		fp = sync_fopen(my_fopen_filename, mode);
		if (fp == NULL) {
			save_errno = errno;
			free(my_fopen_filename_orig);
			free(my_fopen_filename);
			errno = save_errno;
			return (NULL);
		} else
			return (fp);
	} else
		return sync_fopen(filename, mode);
}

static int
my_fclose(FILE *stream)
{
	int ret;

	ret = sync_fclose(stream);
	if (my_fopen_filename != NULL && my_fopen_filename_orig != NULL) {
		rename(my_fopen_filename, my_fopen_filename_orig);
	}

	if (my_fopen_filename != NULL) {
		free(my_fopen_filename);
		my_fopen_filename = NULL;
	}
	if (my_fopen_filename_orig != NULL) {
		free(my_fopen_filename_orig);
		my_fopen_filename_orig = NULL;
	}
	return (ret);
}

#else
#define my_fopen(x,y) sync_fopen(x,y)
#define my_fclose(x) sync_fclose(x)
#endif /* USE_WRITE_RENAME */

/**********************************************************************/

static char *
gfarm_localfs_initialize(void)
{
	char *e = NULL;
	static int initialized = 0;

	if (initialized)
		return (NULL);

	datadir = gfarm_localfs_datadir;

	e = is_dir(datadir);
	if (e != NULL)
		return (e);
	hostinfodir = join_path(datadir, HOSTINFODIR);
	if (hostinfodir == NULL)
		return (GFARM_ERR_NO_MEMORY);
	e = is_dir(hostinfodir);
	if (e != NULL) {
		e = NULL;
		if (mkdir(hostinfodir, 0755) != 0) { /* XXX permission */
			e = "cannot create hostinfodir";
			goto error2;
		}
	}
	pathinfodir = join_path(datadir, PATHINFODIR);
	if (pathinfodir == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto error2;
	}
	e = is_dir(pathinfodir);
	if (e != NULL) {
		e = NULL;
		if (mkdir(pathinfodir, 0755) != 0) { /* XXX permission */
			e = "cannot create pathinfodir";
			goto error1;
		}
	}
	if (e == NULL)
		initialized = 1;
	return (NULL);
error1:
	free(pathinfodir);
error2:
	free(hostinfodir);
	return (e);
}

static char *
gfarm_localfs_terminate(void)
{
	return (NULL);
}

#if 0
static char *
gfarm_localfs_check(void)
{
	if (gfarm_does_own_metadb_connection())
		return (NULL);
}
#else
#define gfarm_localfs_check() gfarm_metadb_initialize()
#endif

/**********************************************************************/

static char *
localfs_host_info_data_open(
	const char *hostname,
	const char *mode, int checkexist,
	FILE **fpp)
{
	char *e = NULL;
	char *datafile;
	FILE *fp = NULL;

	datafile = join_path(hostinfodir, hostname);
	if (datafile == NULL)
		return (GFARM_ERR_NO_MEMORY);
	if (checkexist > OPEN_NOCHECK) {
		e = is_file(datafile);
		if (e == NULL && checkexist == OPEN_FAILEXIST) {
			e = GFARM_ERR_ALREADY_EXISTS;
			goto end;
		} else if (e != NULL && checkexist == OPEN_NEEDEXIST) {
			e = GFARM_ERR_NO_SUCH_OBJECT;
			goto end;
		}
	}
	e = NULL;
	fp = my_fopen(datafile, mode);
	if (fp == NULL)
		e = gfarm_errno_to_error(errno);
	else
		*fpp = fp;
end:
	free(datafile);
	return (e);
}

static char *
localfs_host_info_remove(const char *hostname)
{
	char *datafile;

	datafile = join_path(hostinfodir, hostname);
	if (datafile == NULL)
		return (GFARM_ERR_NO_MEMORY);
	if (unlink(datafile) == 0)
		return (NULL);
	else
		return (gfarm_errno_to_error(errno));
}

static char *
localfs_host_info_get(
	const char *hostname,
	struct gfarm_host_info *info)
{
	char *e;
	FILE *fp;
	char *tmp;
	int n;

	e = localfs_host_info_data_open(hostname, "r", OPEN_NEEDEXIST, &fp);
	if (e != NULL)
		return (e);
	info->hostname = strdup(hostname);
	tmp = line_value_get(fp, HOSTALIASES);
	e = str_to_words(tmp, &n, &info->hostaliases); /* 1 */
	free(tmp);

	info->nhostaliases = n;
	info->architecture = line_value_get(fp, ARCHITECTURE); /* 2 */
	info->ncpu = line_value_get_int(fp, NCPU, 0); /* 3 */
	/* XXX check error (invalid format) */

	fclose(fp);
	return (NULL);
}

static char *
localfs_host_info_data_write(
	FILE *fp,
	struct gfarm_host_info *info)
{
	char *tmp;

	tmp = str_array_to_str_with_space(info->hostaliases, 0,
					  info->nhostaliases);
	line_value_put(fp, HOSTALIASES, tmp); /* 1 */
	if (tmp != NULL)
		free(tmp);
	line_value_put(fp, ARCHITECTURE, info->architecture); /* 2 */
	line_value_put(fp, NCPU, itoa(info->ncpu)); /* 3 */
	my_fclose(fp);
	return (NULL);
}

static char *
localfs_host_info_set(
	char *hostname,
	struct gfarm_host_info *info)
{
	char *e;
	FILE *fp;

	e = localfs_host_info_data_open(hostname, "w", OPEN_FAILEXIST, &fp);
	if (e != NULL)
		return (e);
	return localfs_host_info_data_write(fp, info);
}

static char *
localfs_host_info_replace(
	char *hostname,
	struct gfarm_host_info *info)
{
	char *e;
	FILE *fp;

	e = localfs_host_info_data_open(hostname, "w", OPEN_NEEDEXIST, &fp);
	if (e != NULL)
		return (e);
	return localfs_host_info_data_write(fp, info);
}

/* -------------------------- */

static char *
gfarm_localfs_host_info_get(
	const char *hostname,
	struct gfarm_host_info *info)
{
	char *e;
	if ((e = gfarm_localfs_check()) != NULL)
		return (e);
	return localfs_host_info_get(hostname, info);
}

static char *
gfarm_localfs_host_info_remove_hostaliases(const char *hostname)
{
	char *e;
	struct gfarm_host_info info;
	char **tmp;
	int n;

	if ((e = gfarm_localfs_check()) != NULL)
		return (e);
	e = localfs_host_info_get(hostname, &info);
	if (e != NULL)
		return (e);
	tmp = info.hostaliases; /* save */
	n = info.nhostaliases;
	info.hostaliases = NULL;
	info.nhostaliases = 0;
	e = localfs_host_info_replace((char*)hostname, &info);

	/* cleanup */
	info.hostaliases = tmp;
	info.nhostaliases = n;
	gfarm_metadb_host_info_free(&info);

	return (e);
}

static char *
gfarm_localfs_host_info_set(
	char *hostname,
	struct gfarm_host_info *info)
{
	char *e;
	if ((e = gfarm_localfs_check()) != NULL)
		return (e);
	return localfs_host_info_set(hostname, info);
}

static char *
gfarm_localfs_host_info_replace(
	char *hostname,
	struct gfarm_host_info *info)
{
	char *e;
	if ((e = gfarm_localfs_check()) != NULL)
		return (e);
	return localfs_host_info_replace(hostname, info);
}

static char *
gfarm_localfs_host_info_remove(const char *hostname)
{
	char *e;
	if ((e = gfarm_localfs_check()) != NULL)
		return (e);
	return localfs_host_info_remove(hostname);
}

static char *
localfs_host_info_get_all(
	int *np,
	struct gfarm_host_info **infosp)
{
	char *e = NULL;
	DIR *dp;
	struct dirent *dir;
	int i;
	struct gfarm_host_info *infos;

	*np = 0;
	if ((dp = opendir(hostinfodir)) == NULL)
		return (gfarm_errno_to_error(errno));
	while ((dir = readdir(dp)) != NULL) {
		if (strcmp(dir->d_name, ".") == 0 ||
		    strcmp(dir->d_name, "..") == 0)
			continue; /* ignore */
		(*np)++;
	}
	if (*np == 0) {
		closedir(dp);
		return (GFARM_ERR_NO_SUCH_OBJECT);
	}
	rewinddir(dp);
	GFARM_MALLOC_ARRAY(infos, *np);
	if (infos == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto end;
	}
	i = 0;
	e = NULL;
	while (e == NULL && (dir = readdir(dp)) != NULL && i < *np) {
		if (strcmp(dir->d_name, ".") == 0 ||
		    strcmp(dir->d_name, "..") == 0)
			continue; /* ignore */
		e = localfs_host_info_get(dir->d_name, &infos[i]);
		i++;
	}
	if (e != NULL)
		free(infos);
	else
		*infosp = infos;
end:
	closedir(dp);
	return (e);
}

static char *
gfarm_localfs_host_info_get_all(
	int *np,
	struct gfarm_host_info **infosp)
{
	char *e;
	if ((e = gfarm_localfs_check()) != NULL)
		return (e);

	return localfs_host_info_get_all(np, infosp);
}

/* slow..., but there is no probrem thanks to hostcache. */
static char *
gfarm_localfs_host_info_get_by_name_alias(
	const char *name_alias,
	struct gfarm_host_info *info)
{
	char *e;
	int n, i, j;
	struct gfarm_host_info *infos;
	int found = 0;

	if ((e = gfarm_localfs_check()) != NULL)
		return (e);
	e = localfs_host_info_get_all(&n, &infos);
	if (e != NULL)
		return (e);
	for (i = 0; i < n; i++) {
		int ok = 0;
		for (j = 0; j < infos[i].nhostaliases && found == 0; j++) {
			if (strcmp(name_alias, infos[i].hostaliases[j]) == 0) {
				*info = infos[i];
				found = ok = 1;
				break;
			}
		}
		if (ok == 0)
			gfarm_metadb_host_info_free(&infos[i]);
	}
	free(infos);
	if (found == 0)
		return (GFARM_ERR_NO_SUCH_OBJECT);
	else
		return (NULL);
}

static char *
gfarm_localfs_host_info_get_allhost_by_architecture(
	const char *architecture,
	int *np, struct gfarm_host_info **infosp)
{
	char *e;
	int n, i, j;
	struct gfarm_host_info *infos, *newinfos;

	if ((e = gfarm_localfs_check()) != NULL)
		return (e);
	*np = 0;
	e = localfs_host_info_get_all(&n, &infos);
	if (e != NULL)
		return (e);
	for (i = 0; i < n; i++) {
		if (strcmp(architecture, infos[i].architecture) == 0) {
			(*np)++;
			break;
		}
	}
	GFARM_MALLOC_ARRAY(newinfos, *np);
	if (newinfos == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto end;
	}
	for (i = 0, j = 0; i < n && j < *np; i++) {
		if (strcmp(architecture, infos[i].architecture) == 0)
			newinfos[j++] = infos[i];
		else
			gfarm_metadb_host_info_free(&infos[i]);
	}
	free(infos);
	*infosp = newinfos;
end:
	return (e);
}

/**********************************************************************/

#define CREATE_DIR 1
#define NOTCREATE_DIR 0

static char *
localfs_path_info_data_pathname_get(
	const char *pathname, char **pinfo_datap, int createdir)
{
	char *e = NULL;
	char *pi_real, *pi_data;

	pi_real = join_path(pathinfodir, pathname);
	if (pi_real == NULL)
		return (GFARM_ERR_NO_MEMORY);
	if (createdir == CREATE_DIR) {
		e = is_dir(pi_real);
		if (e == GFARM_ERR_NO_SUCH_OBJECT) {
			if (mkdir(pi_real, 0755) != 0) { /* XXX permission */
				e = gfarm_errno_to_error(errno);
				goto free_pi_real;
			} else {
				e = NULL;
			}
		} else if (e != NULL)
			goto free_pi_real;
	}
	pi_data = join_str(pi_real, PATHINFO_SUFFIX);
	if (pi_data == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto free_pi_real;
	}
	*pinfo_datap = pi_data;
free_pi_real:
	free(pi_real);

	return (e);
}

static char *
localfs_path_info_data_open(
	const char *pathname,
	const char *mode, int checkexist, int createdir, FILE **fpp)
{
	char *e = NULL;
	char *pi_data;
	FILE *fp;

	e = localfs_path_info_data_pathname_get(pathname, &pi_data, createdir);
	if (e != NULL)
		return (e);

	if (checkexist > OPEN_NOCHECK) {
		e = is_file(pi_data);
		if (e == NULL && checkexist == OPEN_FAILEXIST) {
			e = GFARM_ERR_ALREADY_EXISTS;
			goto free_pi_data;
		} else if (e == GFARM_ERR_NO_SUCH_OBJECT) {
			if (checkexist == OPEN_NEEDEXIST)
				goto free_pi_data;
			else
				e = NULL; /* create new file */
		} else if (e != NULL) /* other error */
			goto free_pi_data;
	}
	fp = my_fopen(pi_data, mode);
	if (fp == NULL)
		e = gfarm_errno_to_error(errno);
	else
		*fpp = fp;
free_pi_data:
	free(pi_data);

	return (e);
}

/* defined in below */
static char *
localfs_file_section_info_remove_all_by_file(const char *);

static char *
localfs_path_info_remove(
	const char *pathname)
{
	char *e;
	char *pi_real, *pi_data;

	pi_real = join_path(pathinfodir, pathname);
	if (pi_real == NULL)
		return (GFARM_ERR_NO_MEMORY);

	e = is_dir(pi_real);
	if (e == NULL) {
		if (rmdir(pi_real) != 0) {
			e = gfarm_errno_to_error(errno);
			goto free_pi_real;
		}
	} else if (e != GFARM_ERR_NO_SUCH_OBJECT)
		goto free_pi_real;
	/* else: do nothing */
	e = NULL;

	pi_data = join_str(pi_real, PATHINFO_SUFFIX);
	if (pi_data == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto free_pi_real;
	}

	/* delete file_section_info data files */
	e = localfs_file_section_info_remove_all_by_file(pathname);
	if (e == NULL || e == GFARM_ERR_NO_SUCH_OBJECT) {
		/* delete path_info data file */
		if (unlink(pi_data) != 0) {
			e = gfarm_errno_to_error(errno);
		} else {
			e = NULL;
		}
	}
	free(pi_data);
free_pi_real:
	free(pi_real);

	return (e);
}

static char *localfs_path_info_replace(char *, struct gfarm_path_info *);

static char *
localfs_path_info_get(
	const char *pathname,
	struct gfarm_path_info *info)
{
	char *e;
	FILE *fp;

	e = localfs_path_info_data_open(pathname, "r", OPEN_NEEDEXIST,
				       NOTCREATE_DIR, &fp);
	if (e != NULL)
		return (e);

	info->pathname = strdup(pathname);
	info->status.st_mode =
		(gfarm_mode_t) line_value_get_int(fp, ST_MODE, 0600); /* 1 */
	info->status.st_user = line_value_get(fp, ST_USER);   /* 2 */
	info->status.st_group = line_value_get(fp, ST_GROUP); /* 3 */
	info->status.st_atimespec =
		line_value_get_timespec(fp, ST_ATIME);   /* 4 */
	info->status.st_mtimespec =
		line_value_get_timespec(fp, ST_MTIME);   /* 5 */
	info->status.st_ctimespec =
		line_value_get_timespec(fp, ST_CTIME);   /* 6 */
	info->status.st_nsections =
		line_value_get_int(fp, ST_NSECTIONS, 0); /* 7 */

	info->status.st_size = 0;

	fclose(fp);

	/* check error (invalid format) */
	if (gfarm_base_path_info_ops.validate(info))
		return (NULL);
	else {
		gfarm_path_info_free(info);
		/* XXX recover ? */
		/* if (localfs_path_info_is_directory(pathname)) */
		/* else {create path_info of regular file} */
		e = localfs_path_info_remove(pathname); /* XXX temporary */
		if (e == GFARM_ERR_DIRECTORY_NOT_EMPTY) {
			mode_t mask;
			struct timeval now;

			mask = umask(0);
			umask(mask);
			gettimeofday(&now, NULL);
			info->pathname = strdup(pathname);
			info->status.st_mode =
				(GFARM_S_IFDIR | (0755 & ~mask)); /* XXX */
			info->status.st_user =
				strdup(gfarm_get_global_username());
			/* XXX NULL check */
			info->status.st_group = strdup("*"); /* XXX for now */
			info->status.st_atimespec.tv_sec =
			info->status.st_mtimespec.tv_sec =
			info->status.st_ctimespec.tv_sec = now.tv_sec;
			info->status.st_atimespec.tv_nsec =
			info->status.st_mtimespec.tv_nsec =
			info->status.st_ctimespec.tv_nsec = now.tv_usec * 1000;
			info->status.st_size = 0;
			info->status.st_nsections = 0;

			(void)localfs_path_info_replace((char*)pathname, info);

			return (NULL);
		}
		return (GFARM_ERR_NO_SUCH_OBJECT);
	}
}

static char *
localfs_path_info_data_write(
	FILE *fp,
	struct gfarm_path_info *info)
{
	line_value_put(fp, ST_MODE, itoa(info->status.st_mode)); /* 1 */
	line_value_put(fp, ST_USER, info->status.st_user);       /* 2 */
	line_value_put(fp, ST_GROUP, info->status.st_group);     /* 3 */
	line_value_put(fp, ST_ATIME,
		       timespectoa(&info->status.st_atimespec)); /* 4 */
	line_value_put(fp, ST_MTIME,
		       timespectoa(&info->status.st_mtimespec)); /* 5 */
	line_value_put(fp, ST_CTIME,
		       timespectoa(&info->status.st_ctimespec)); /* 6 */
	line_value_put(fp, ST_NSECTIONS,
		       itoa(info->status.st_nsections));         /* 7 */
	my_fclose(fp);
	return (NULL);
}

static char *
localfs_path_info_set(
	char *pathname,
	struct gfarm_path_info *info)
{
	char *e;
	FILE *fp;
	int createdir;

	if (!gfarm_base_path_info_ops.validate(info))
		return (GFARM_ERR_INVALID_ARGUMENT);

	if (GFARM_S_ISDIR(info->status.st_mode))
		createdir = CREATE_DIR;
	else
		createdir = NOTCREATE_DIR;

	e = localfs_path_info_data_open(pathname, "w", OPEN_FAILEXIST,
				       createdir, &fp);
	if (e != NULL)
		return (e);
	return localfs_path_info_data_write(fp, info);
}

static char *
localfs_path_info_replace(
	char *pathname,
	struct gfarm_path_info *info)
{
	char *e;
	FILE *fp;

	if (!gfarm_base_path_info_ops.validate(info))
		return (GFARM_ERR_INVALID_ARGUMENT);

	e = localfs_path_info_data_open(pathname, "w", OPEN_NEEDEXIST,
				       NOTCREATE_DIR, &fp);
	if (e != NULL)
		return (e);
	return localfs_path_info_data_write(fp, info);
}

static char *
gfarm_localfs_path_info_get(
	const char *pathname,
	struct gfarm_path_info *info)
{
	char *e;
	if ((e = gfarm_localfs_check()) != NULL)
		return (e);
	return localfs_path_info_get(pathname, info);
}

static char *
gfarm_localfs_path_info_set(
	char *pathname,
	struct gfarm_path_info *info)
{
	char *e;
	if ((e = gfarm_localfs_check()) != NULL)
		return (e);
	return localfs_path_info_set(pathname, info);
}

static char *
gfarm_localfs_path_info_replace(
	char *pathname,
	struct gfarm_path_info *info)
{
	char *e;
	if ((e = gfarm_localfs_check()) != NULL)
		return (e);
	return localfs_path_info_replace(pathname, info);
}

static char *
gfarm_localfs_path_info_remove(const char *pathname)
{
	char *e;
	if ((e = gfarm_localfs_check()) != NULL)
		return (e);
	return localfs_path_info_remove(pathname);
}

/* filter for find */
#define TYPE_ALL 0
#define TYPE_FILE 1
#define TYPE_DIR 2

static char MY_FIND_IGNORE[] = "my_find ignore this entry";

struct my_find {
	DIR *parentdir;
	char*pathname;
	struct my_find *saved; /* save parent my_find */
	int isclose;
	char *(*filter)(char *, char *);
	char *error;
};

static char *
my_find_init(
	char *pathname, /* start */
	struct my_find *mfp,
	char *(*filter)(char *, char *))
{
	char dir[PATH_MAX];

	snprintf(dir, PATH_MAX, "%s/%s", pathinfodir, pathname);
	mfp->parentdir = opendir(dir);
	if (mfp->parentdir == NULL) {
		return (GFARM_ERR_NOT_A_DIRECTORY);
	}
	mfp->pathname = strdup(pathname);
	mfp->isclose = 0;
	mfp->filter = filter;
	mfp->saved = NULL; /* NULL is topdir */
	mfp->error = NULL;
	return (NULL);
}

static char *
my_find_get_next(
	struct my_find *mfp,
	char **pathname, int type)
{
	char *e;
	char *newpath;
	struct my_find nextmfp, *parentmfp;
	struct dirent *entry;
#ifndef HAVE_D_TYPE
	struct stat st;
#endif

	while ((entry = readdir(mfp->parentdir)) != NULL) {
		if (strcmp(entry->d_name, ".") == 0
		    || strcmp(entry->d_name, "..") == 0) {
			continue; /* ignore */
		} else {
			GFARM_MALLOC_ARRAY(newpath, 
				strlen(mfp->pathname) +
				strlen(entry->d_name) + 2);
			if (newpath == NULL) {
				mfp->error = GFARM_ERR_NO_MEMORY;
				break;
			}
			sprintf(newpath, "%s/%s",
				mfp->pathname, entry->d_name);
		}
		e = mfp->filter(newpath, entry->d_name);
		if (e == MY_FIND_IGNORE) {
			free(newpath);
			continue; /* ignore and go next */
		} else if (e != NULL) {
			free(newpath);
			mfp->error = e;
			return (NULL); /* go next */
		}
#ifdef HAVE_D_TYPE
		if (S_ISDIR(DTTOIF(entry->d_type))) {
#else
		if (stat(newpath, &st) != -1 && S_ISDIR(st.st_mode)) {
#endif
			my_find_init(newpath, &nextmfp, mfp->filter);
			nextmfp.saved = calloc(sizeof(struct my_find), 1);
			if (nextmfp.saved == NULL) {
				mfp->error = GFARM_ERR_NO_MEMORY;
				free(newpath);
				break;
			}
			*(nextmfp.saved) = *mfp; /* save parent my_find */
			*mfp = nextmfp; /* overwrite: go to child directory */
			if (type == TYPE_FILE) {
				*pathname = NULL;
				free(newpath);
			} else {
				*pathname = newpath;
			}
			return (NULL); /* go next */
		} else { /* regular file */
			if (type == TYPE_DIR) {
				*pathname = NULL;
				free(newpath);
			} else {
				*pathname = newpath;
			}
			return (NULL); /* go next */
		}
	}
	/* readdir is NULL: end of this directory */

	closedir(mfp->parentdir);
	mfp->isclose = 1;
	parentmfp = mfp->saved;
	free(mfp->pathname); /* strdup(pathname) */
	if (parentmfp != NULL) { /* if not topdir */
		parentmfp->error = mfp->error; /* Is error happened ? */
		*mfp = *parentmfp; /* parent -> current */
		free(parentmfp); /* malloc(sizeof(struct gfarm_find)) */
	} else /* topdir */
		mfp->saved = NULL;
	*pathname = NULL;
	if (mfp->saved == NULL && mfp->error != NULL)
		return (mfp->error); /* return error attop dir only */
	else
		return (NULL);  /* go next */
}

static int
my_find_is_end(struct my_find *mfp)
{
	if (mfp->saved == NULL && mfp->isclose == 1)
		return 1;
	else
		return 0;
}

static void
(*my_find_filter_path_info_get_callback)(void *, struct gfarm_path_info *);
static void *my_find_filter_path_info_get_closure;

void
my_find_filter_path_info_get_callback_set(
	void (*callback)(void *, struct gfarm_path_info *),
	void *closure)
{
	my_find_filter_path_info_get_callback = callback;
	my_find_filter_path_info_get_closure = closure;
}

/* check name to execute or ignore. */
/* XXX use scandir (?) */
static char *
my_find_filter(
	char *pathname,
	char *name)
{
	char *e;
	struct gfarm_path_info info;
	int len, sufflen;
	char *realpath;

	if (is_path_info_name(name)) {
		/* cut PATHINFO_SUFFIX */
		len = strlen(pathname);
		sufflen = strlen(PATHINFO_SUFFIX); /* XXX */
		realpath = calloc((len - sufflen + 1) * sizeof(char), 1);
		if (realpath == NULL)
			return (MY_FIND_IGNORE); /* ignore */
		strncpy(realpath, pathname, len - sufflen);

		e = localfs_path_info_get(realpath, &info);
		free(realpath);
		if (e == NULL) {
			(*my_find_filter_path_info_get_callback)
				(my_find_filter_path_info_get_closure, &info);
			gfarm_base_path_info_ops.free(&info);
			return (NULL);
		} else
			return (MY_FIND_IGNORE); /* ignore */
	} else if (is_file_section_info_name(name)) {
		return (MY_FIND_IGNORE); /* ignore */
	} else /* normal directory */
		return (NULL);
}

/* XXX - this is for a stopgap implementation of gfs_opendir() */
static char *
gfarm_localfs_path_info_get_all_foreach(
	void (*callback)(void *, struct gfarm_path_info *),
	void *closure)
{
	char *e;
	struct my_find mf;
	char *pathname;

#ifdef __GNUC__ /* workaround gcc warning: may be used uninitialized */
	pathname = NULL;
#endif
	if ((e = gfarm_localfs_check()) != NULL)
		return (e);

	my_find_filter_path_info_get_callback_set(callback, closure);
	my_find_init(".", &mf, my_find_filter);
	while ((e = my_find_get_next(&mf, &pathname, TYPE_DIR)) == NULL) {
		if (pathname != NULL)
			free(pathname);
		else if (my_find_is_end(&mf))
			break;
		/* else: continue: go next entry */
	}
	return (e);
}

/**********************************************************************/

static char *
localfs_file_section_info_index_data_pathname_get(
	const char *pathname,
	char **index_datap)
{
	char *tmp, *index_data;

	tmp = join_path(pathinfodir, pathname);
	if (tmp == NULL)
		return (GFARM_ERR_NO_MEMORY);
	index_data = join_str(tmp, SECTIONINFO_INDEX_SUFFIX);
	free(tmp);
	if (index_data == NULL)
		return (GFARM_ERR_NO_MEMORY);
	*index_datap = index_data;
	return (NULL);
}

static char *
localfs_file_section_info_index_get2(
	const char *data_file,
	int *np, char ***sectionsp)
{
	char *e;
	FILE *fp;
	char buf[BUFSIZE];

	*np = 0;
	fp = fopen(data_file, "r");
	if (fp == NULL)
		return gfarm_errno_to_error(errno);
	if (fgets(buf, BUFSIZE, fp) != NULL)
		e = str_to_words(buf, np, sectionsp);
	else
		e = GFARM_ERR_INVALID_ARGUMENT;
	fclose(fp);
	return (e);
}

static char *
localfs_file_section_info_index_replace2(
	const char *data_file,
	int n, char **sections)
{
	char *e = NULL;
	FILE *fp;
	int i;

	if (n <= 0) {
		if (unlink(data_file) != 0)
			return gfarm_errno_to_error(errno);
		else
			return (NULL);
	}
	fp = my_fopen(data_file, "w");
	if (fp == NULL)
		return gfarm_errno_to_error(errno);
	for (i = 0; i < n; i++) {
		if (fputs(sections[i], fp) == EOF || fputs(" ", fp) == EOF) {
			e = GFARM_ERR_INPUT_OUTPUT;
			break;
		}
	}
	my_fclose(fp);
	return (e);
}

static char *
localfs_file_section_info_index_get(
	const char *pathname,
	int *np, char ***sectionsp)
{
	char *e;
	char *index_data;

	e = localfs_file_section_info_index_data_pathname_get(pathname,
							     &index_data);
	if (e != NULL)
		return (e);
	e = localfs_file_section_info_index_get2(index_data, np, sectionsp);
	free(index_data);
	return (e);
}

#if 0
static char *
localfs_file_section_info_index_replace(
	const char *pathname,
	int n, char **sections)
{
	char *e;
	char *index_data;

	e = localfs_file_section_info_index_data_pathname_get(pathname,
							     &index_data);
	if (e != NULL)
		return (e);
	e = localfs_file_section_info_index_replace2(index_data, n, sections);
	free(index_data);
	return (e);
}
#endif

static char *
localfs_file_section_info_index_add(
	const char *pathname,
	const char *section)
{
	char *e;
	char *index_data;
	char **sections, **newsections;
	int n, newn, i;

	e = localfs_file_section_info_index_data_pathname_get(pathname,
							     &index_data);
	if (e != NULL)
		return (e);
	e = localfs_file_section_info_index_get2(index_data, &n, &sections);
	if (e != NULL && e != GFARM_ERR_NO_SUCH_OBJECT)
		goto free_index_data;

	for (i = 0; i < n; i++) {
		if (strcmp(sections[i], section) == 0) {
			e = GFARM_ERR_ALREADY_EXISTS;
			goto free_sections;
		}
	}
	newn = n + 1;
	GFARM_MALLOC_ARRAY(newsections, newn);
	if (newsections == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto free_sections;
	}
	
	for (i = 0; i < n; i++) {
		newsections[i] = sections[i];
	}
	newsections[i] = (char *)section; /* add */
	e = localfs_file_section_info_index_replace2(index_data, newn,
						    newsections);
	free(newsections);
free_sections:
	if (n > 0)
		gfarm_strarray_free(sections);
free_index_data:
	free(index_data);
	return (e);
}

static char *
localfs_file_section_info_index_remove(
	const char *pathname,
	const char *section)
{
	char *e;
	char *index_data;
	char **sections, **newsections;
	int n, newn, i, j, found;

	e = localfs_file_section_info_index_data_pathname_get(pathname,
							      &index_data);
	if (e != NULL)
		return (e);
	e = localfs_file_section_info_index_get2(index_data, &n, &sections);
	if (e != NULL)
		goto free_index_data;
	found = 0;
	for (i = 0; i < n; i++) {
		if (strcmp(sections[i], section) == 0) {
			found = 1;
			break;
		}
	}
	if (found == 0) {
		e = GFARM_ERR_INVALID_ARGUMENT;
		goto free_sections;
	}
	newn = n - 1;
	GFARM_MALLOC_ARRAY(newsections, newn);
	if (newsections == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto free_sections;
	}
	
	for (i = 0, j = 0; i < n; i++) {
		if (strcmp(sections[i], section) != 0)
			newsections[j++] = sections[i];
	}
	e = localfs_file_section_info_index_replace2(index_data, newn,
						    newsections);
	free(newsections);
free_sections:
	gfarm_strarray_free(sections);
free_index_data:
	free(index_data);
	return (e);
}

static char *
localfs_file_section_info_data_pathname_get(
	const char *pathname, const char *section,
	char **si_datap,
	int checkexist)
{
	char *e = NULL;
	char *tmp, *si_data;

	tmp = join_path(pathinfodir, pathname);
	if (tmp == NULL)
		return (GFARM_ERR_NO_MEMORY);
	si_data = join_str3(tmp, SECTIONINFO_AFFIX, section);
	free(tmp);
	if (si_data == NULL)
		return (GFARM_ERR_NO_MEMORY);
	if (checkexist > OPEN_NOCHECK) {
		e = is_file(si_data);
		if (e == NULL && checkexist == OPEN_FAILEXIST)
			e = GFARM_ERR_ALREADY_EXISTS;
		else if (e != NULL && checkexist == OPEN_NEEDEXIST)
			e = GFARM_ERR_NO_SUCH_OBJECT;
		else
			e = NULL;
	}
	if (e != NULL)
		free(si_data);
	else
		*si_datap = si_data;
	return (e);
}

static char *
localfs_file_section_info_data_open(
	const char *pathname, const char *section,
	const char *mode, int checkexist, FILE **fpp)
{
	char *e = NULL;
	char *si_data;
	FILE *fp = NULL;

	e = localfs_file_section_info_data_pathname_get(pathname, section,
						       &si_data, checkexist);
	if (e != NULL)
		return (e);
	fp = my_fopen(si_data, mode);
	if (fp == NULL)
		e = gfarm_errno_to_error(errno);
	else
		*fpp = fp;
	free(si_data);
	return (e);
}

static char *
localfs_file_section_info_validate(
	struct gfarm_file_section_info *info)
{
	if (info->pathname == NULL || info->section == NULL ||
	    info->checksum == NULL || info->checksum_type == NULL)
		return (GFARM_ERR_INVALID_ARGUMENT);
	else
		return (NULL);
}

static char * localfs_file_section_info_remove(const char *, const char *);

static char *
localfs_file_section_info_get(
	const char *pathname, const char *section,
	struct gfarm_file_section_info *info,
	int *ncopyhostsp, char ***copyhostsp)
{
	char *e;
	FILE *fp;

	e = localfs_file_section_info_data_open(pathname, section,
					       "r", OPEN_NEEDEXIST, &fp);
	if (e != NULL)
		return (e);

	info->pathname = strdup(pathname);
	info->section = strdup(section);
	info->filesize = line_value_get_long(fp, FILESIZE, 0); /* 1 */
	info->checksum = line_value_get(fp, CHECKSUM); /* 2 */
	info->checksum_type = line_value_get(fp, CHECKSUMTYPE); /* 3 */

	if (ncopyhostsp != NULL)
		*ncopyhostsp = 0;
	if (ncopyhostsp != NULL && copyhostsp != NULL) {
		char *tmp;
		tmp = line_value_get(fp, SECTIONCOPIES); /* 4 */
		if (tmp != NULL) {
			str_to_words(tmp, ncopyhostsp, copyhostsp);
			free(tmp);
		}
	}
	fclose(fp);

	/* XXX check error (invalid format) */
	e = localfs_file_section_info_validate(info);
	if (e != NULL)
		localfs_file_section_info_remove(pathname, section);

	return (e);
}

static char *
localfs_file_section_info_data_write(
	FILE *fp,
	struct gfarm_file_section_info *info,
	int ncopyhosts, char **copyhosts)
{
	line_value_put(fp, FILESIZE, ltoa(info->filesize));    /* 1 */
	line_value_put(fp, CHECKSUM, info->checksum);          /* 2 */
	line_value_put(fp, CHECKSUMTYPE, info->checksum_type); /* 3 */

	if (ncopyhosts > 0 && copyhosts != NULL) {
		char *tmp;
		tmp = str_array_to_str_with_space(copyhosts, 0, ncopyhosts);
		line_value_put(fp, SECTIONCOPIES, tmp); /* 4 */
		if (tmp != NULL)
			free(tmp);
	}
	my_fclose(fp);
	return (NULL);
}

static char *
localfs_file_section_info_set(
	char *pathname, char *section,
	struct gfarm_file_section_info *info)
{
	char *e;
	FILE *fp;
#ifdef CREATE_FSCI_WITHOUT_FSI
	int ncopies = 0;
	char **copies;
#endif
	struct gfarm_file_section_info fsi;
	char *pi_data;

	fsi = *info;
	fsi.pathname = pathname;
	fsi.section = section;
	e = localfs_file_section_info_validate(&fsi);
	if (e != NULL)
		return (e);

	e = localfs_path_info_data_pathname_get(pathname, &pi_data,
						NOTCREATE_DIR);
	if (e != NULL) {
		return (e);
	} else {
		if (is_file(pi_data) != NULL)
			e = GFARM_ERR_NO_SUCH_OBJECT;
		free(pi_data);
		if (e != NULL)
			return (e);
	}

#ifdef CREATE_FSCI_WITHOUT_FSI
	/* XXX useless processing for file_section_copy_info_set */
	e = localfs_file_section_info_get(pathname, section, &fsi,
					 &ncopies, &copies);
	if (e == NULL) {
		if (strcmp(fsi.checksum, "") == 0) {
			gfarm_file_section_info_free(&fsi);
			/* do nothing */
		} else {
			gfarm_file_section_info_free(&fsi);
			return (GFARM_ERR_ALREADY_EXISTS);
		}
	} else if (e != GFARM_ERR_NO_SUCH_OBJECT)
		return (e);
	/* else: e == GFARM_ERR_NO_SUCH_OBJECT: do nothing */

	e = localfs_file_section_info_data_open(pathname, section, "w",
					       OPEN_NOCHECK, &fp);
	if (e != NULL)
		return (e);
	e = localfs_file_section_info_data_write(fp, info, ncopies, copies);
	if (ncopies > 0)
		gfarm_strarray_free(copies);

#else  /* normal */
	e = localfs_file_section_info_data_open(pathname, section, "w",
					       OPEN_FAILEXIST, &fp);
	if (e != NULL)
		return (e);
	e = localfs_file_section_info_data_write(fp, info, 0, NULL);
#endif
	if (e != NULL) {
		(void)localfs_file_section_info_index_remove(pathname, section);
		return (e);
	} else {
		e = localfs_file_section_info_index_add(pathname, section);
		if (e != NULL && e != GFARM_ERR_ALREADY_EXISTS)
			return (e);
		return (NULL);
	}
}

/* ncopies < 0: file_section_copy info is not modified */
static char *
localfs_file_section_info_replace(
	char *pathname, char *section,
	struct gfarm_file_section_info *info,
	int ncopies, char **copies)
{
	char *e;
	FILE *fp;
	struct gfarm_file_section_info fsi; /* tmp */
	int ncopies2 = 0;
	char **copies2;

	fsi = *info;
	fsi.pathname = pathname;
	fsi.section = section;
	e = localfs_file_section_info_validate(&fsi);
	if (e != NULL)
		return (e);

	if (ncopies < 0) {
		/* not modify file_section_copy infos */
		e = localfs_file_section_info_get(pathname, section, &fsi,
						 &ncopies2, &copies2);
		if (e != NULL)
			return (e);
		gfarm_file_section_info_free(&fsi);
		ncopies = ncopies2;
		copies = copies2;
	} else if (ncopies == 0) /* delete all file_section_copy info */
		copies = NULL;

	e = localfs_file_section_info_data_open(pathname, section, "w",
					       OPEN_NEEDEXIST, &fp);
	if (e != NULL)
		goto free_copies2;
	e = localfs_file_section_info_data_write(fp, info, ncopies, copies);
free_copies2:
	if (ncopies2 > 0)
		gfarm_strarray_free(copies2);
	return (e);
}

static char *
localfs_file_section_info_remove(
	const char *pathname,
	const char *section)
{
	char *e;
	char *si_data;

	e = localfs_file_section_info_index_remove(pathname, section);
	if (e != NULL && e != GFARM_ERR_NO_SUCH_OBJECT)
		return (e);

	/* e == NULL || e == GFARM_ERR_NO_SUCH_OBJECT */
	e = localfs_file_section_info_data_pathname_get(pathname, section,
							&si_data,
							OPEN_NEEDEXIST);
	if (e != NULL)
		return (e);
	if (unlink(si_data) == 0)
		e = NULL;
	else
		e = gfarm_errno_to_error(errno);
	free(si_data);
	return (e);
}

static char *
gfarm_localfs_file_section_info_get(
	const char *pathname,
	const char *section,
	struct gfarm_file_section_info *info)
{
	char *e;
	if ((e = gfarm_localfs_check()) != NULL)
		return (e);
	return localfs_file_section_info_get(pathname, section, info,
					    NULL, NULL);
}

static char *
gfarm_localfs_file_section_info_set(
	char *pathname,
	char *section,
	struct gfarm_file_section_info *info)
{
	char *e;
	if ((e = gfarm_localfs_check()) != NULL)
		return (e);
	return localfs_file_section_info_set(pathname, section, info);
}

static char *
gfarm_localfs_file_section_info_replace(
	char *pathname,
	char *section,
	struct gfarm_file_section_info *info)
{
	char *e;
	if ((e = gfarm_localfs_check()) != NULL)
		return (e);

	return localfs_file_section_info_replace(pathname, section, info,
						-1, NULL);
}

static char *
gfarm_localfs_file_section_info_remove(
	const char *pathname,
	const char *section)
{
	char *e;
	if ((e = gfarm_localfs_check()) != NULL)
		return (e);
	return localfs_file_section_info_remove(pathname, section);
}

static char *
localfs_file_section_info_get_all_by_file(
	const char *pathname,
	int *np,
	struct gfarm_file_section_info **infosp)
{
	char *e;
	int i;
	char **sections;
	struct gfarm_file_section_info *infos;

	e = localfs_file_section_info_index_get(pathname, np, &sections);
	if (e == NULL && *np > 0) {
		GFARM_MALLOC_ARRAY(infos, *np);
		if (infos == NULL) {
			e = GFARM_ERR_NO_MEMORY;
			goto free_sections;
		}
		for (i = 0; i < *np; i++) {
			e = localfs_file_section_info_get(pathname,
							  sections[i],
							  &infos[i],
							  NULL, NULL);
		}
		if (e != NULL)
			free(infos);
		else
			*infosp = infos;
	free_sections:
		gfarm_strarray_free(sections);
	} else if (*np == 0)
		e = GFARM_ERR_NO_SUCH_OBJECT;
	return (e);
}

static char *
gfarm_localfs_file_section_info_get_all_by_file(
	const char *pathname,
	int *np,
	struct gfarm_file_section_info **infosp)
{
	char *e;
	if ((e = gfarm_localfs_check()) != NULL)
		return (e);

	return localfs_file_section_info_get_all_by_file(pathname, np, infosp);
}

static char *
localfs_file_section_info_remove_all_by_file(
	const char *pathname)
{
	char *e, *e_save = NULL;
	int n, i;
	struct gfarm_file_section_info *infos;

	e = localfs_file_section_info_get_all_by_file(pathname, &n, &infos);
	if (e == NULL) {
		for (i = 0; i < n; i++) {
			e = localfs_file_section_info_remove(pathname,
							     infos[i].section);
			if (e != GFARM_ERR_NO_SUCH_OBJECT && e_save == NULL)
				e_save = e;
		}
		gfarm_file_section_info_free_all(n, infos);
	}
	return (e);
}

/**********************************************************************/

/* used by gfarm_file_section_copy_info_does_exist */
static char *
gfarm_localfs_file_section_copy_info_get(
	const char *pathname,
	const char *section,
	const char *hostname,
	struct gfarm_file_section_copy_info *info)
{
	char *e;
	struct gfarm_file_section_info fsi; /* not use */
	int nhosts, i;
	char **hosts;

	if ((e = gfarm_localfs_check()) != NULL)
		return (e);
	e = localfs_file_section_info_get(pathname, section, &fsi,
					 &nhosts, &hosts);
	if (e != NULL)
		return (e);
	e = GFARM_ERR_NO_SUCH_OBJECT;
	for (i = 0; i < nhosts; i++) {
		if (strcmp(hostname, hosts[i]) == 0) {
			info->pathname = strdup(pathname);
			info->section = strdup(section);
			info->hostname = strdup(hostname);
			e = NULL;
		}
	}
	if (nhosts > 0)
		gfarm_strarray_free(hosts);
	gfarm_file_section_info_free(&fsi);
	return (e);
}

static char *
gfarm_localfs_file_section_copy_info_set(
	char *pathname,
	char *section,
	char *hostname,
	struct gfarm_file_section_copy_info *info)
{
	char *e;
	struct gfarm_file_section_info fsi;
	int nhosts, i;
	char **hosts, **tmp;
	int dummymode = 0;

	if ((e = gfarm_localfs_check()) != NULL)
		return (e);

	e = localfs_file_section_info_get(pathname, section, &fsi,
					 &nhosts, &hosts);
#ifdef CREATE_FSCI_WITHOUT_FSI
	if (e == GFARM_ERR_NO_SUCH_OBJECT) {
		char *dummy = "";

		fsi.filesize = 0;
		fsi.checksum = dummy;
		fsi.checksum_type = dummy;
		(void)localfs_file_section_info_set(pathname, section, &fsi);
		e = NULL;
		nhosts = 0;
		dummymode = 1;
	}
#endif
	if (e == NULL) {
		/* if (nhosts == 0) do nothing; */
		for (i = 0; i < nhosts; i++) {
			if (strcmp(hostname, hosts[i]) == 0) {
				e = GFARM_ERR_ALREADY_EXISTS;
				break;
			}
		}
		if (e == NULL) { /* add hostname */
			GFARM_MALLOC_ARRAY(tmp, nhosts + 1);
			for (i = 0; i < nhosts; i++) {
				tmp[i] = hosts[i];
			}
			tmp[i] = hostname;
			e = localfs_file_section_info_replace(
				pathname, section,
				&fsi, nhosts + 1, tmp);
			free(tmp);
		}
		if (nhosts > 0)
			gfarm_strarray_free(hosts);
		if (dummymode == 0)
			gfarm_file_section_info_free(&fsi);
	}
	return (e);
}

static char *
gfarm_localfs_file_section_copy_info_remove(
	const char *pathname,
	const char *section,
	const char *hostname)
{
	char *e;
	struct gfarm_file_section_info fsi;
	int nhosts, i, j;
	char **hosts, **tmp;

	if ((e = gfarm_localfs_check()) != NULL)
		return (e);
	e = localfs_file_section_info_get(pathname, section, &fsi,
					 &nhosts, &hosts);
	if (nhosts == 0)
		e = GFARM_ERR_NO_SUCH_OBJECT;
	else if (e == NULL) {
		e = GFARM_ERR_NO_SUCH_OBJECT;
		for (i = 0; i < nhosts; i++) {
			if (strcmp(hostname, hosts[i]) == 0) {
				e = NULL;
				break;
			}
		}
		if (e == NULL) { /* delete hostname */
			if (nhosts - 1 == 0) {
				e = localfs_file_section_info_replace(
					(char*)pathname, (char*)section,
					&fsi, 0, NULL); /* delete all fsci */
			} else {
				GFARM_MALLOC_ARRAY(tmp, nhosts - 1);
				j = 0;
				for (i = 0; i < nhosts; i++) {
					if (strcmp(hostname, hosts[i]) != 0)
						tmp[j++] = hosts[i];
				}
				tmp[j] = (char*)hostname;
				e = localfs_file_section_info_replace(
					(char*)pathname,
					(char*)section,
					&fsi, j, tmp);
				free(tmp);
			}
		}
		gfarm_strarray_free(hosts);
		gfarm_file_section_info_free(&fsi);
	}
	return (e);
}

static char *
gfarm_localfs_file_section_copy_info_get_all_by_file(
	const char *pathname,
	int *np,
	struct gfarm_file_section_copy_info **infosp)
{
	char *e, *e2 = NULL;
	int i, j, nfsis, nhosts, nlist;
	struct gfarm_file_section_info *fsis;
	struct gfarm_file_section_info fsi;
	char **hosts;
	gfarm_stringlist list;
	struct gfarm_file_section_copy_info *infos;

	if ((e = gfarm_localfs_check()) != NULL)
		return (e);

	e = localfs_file_section_info_get_all_by_file(pathname, &nfsis, &fsis);
	if (e != NULL)
		return (e);
	e = gfarm_stringlist_init(&list);
	if (e != NULL)
		goto free_fsis;
	for (i = 0; i < nfsis; i++) {
		/* XXX */
		e = localfs_file_section_info_get(
			pathname, fsis[i].section, &fsi, &nhosts, &hosts);
		if (e != NULL) {
			e2 = e;
			continue;
		}
		for (j = 0; j < nhosts; j++) {
			/* XXX */
			gfarm_stringlist_add(&list, strdup(fsis[i].section));
			gfarm_stringlist_add(&list, strdup(hosts[j]));
			free(hosts[j]);
		}
		if (nhosts > 0)
			free(hosts);
		gfarm_file_section_info_free(&fsi);
	}
	nlist = gfarm_stringlist_length(&list);
	if (nlist % 2 != 0) {
		e = GFARM_ERR_INPUT_OUTPUT;
		goto free_list;
	}
	*np = nlist / 2;
	GFARM_MALLOC_ARRAY(infos, *np);
	if (infos == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto free_list;
	}
	for (i = 0; i < *np; i++) {
		infos[i].pathname = strdup(pathname);
		infos[i].section = strdup(
			gfarm_stringlist_elem(&list, 2 * i));
		infos[i].hostname = strdup(
			gfarm_stringlist_elem(&list, 2 * i + 1));
	}
	*infosp = infos;
free_list:
	gfarm_stringlist_free_deeply(&list);

free_fsis:
	for (i = 0; i < nfsis; i++) {
		gfarm_file_section_info_free(&fsis[i]);
	}
	free(fsis);
	if  (e == NULL && e2 != NULL)
		e = e2;
	return (e);
}

static char *
gfarm_localfs_file_section_copy_info_get_all_by_section(
	const char *pathname,
	const char *section,
	int *np,
	struct gfarm_file_section_copy_info **infosp)
{
	char *e;
	struct gfarm_file_section_info fsi; /* not use */
	int nhosts, i;
	char **hosts;
	struct gfarm_file_section_copy_info *infos;

	if ((e = gfarm_localfs_check()) != NULL)
		return (e);

	e = localfs_file_section_info_get(pathname, section, &fsi,
					 &nhosts, &hosts);
	if (e != NULL)
		return (e);
	if (nhosts == 0) {
		e = GFARM_ERR_NO_SUCH_OBJECT;
		goto end;
	}

	GFARM_MALLOC_ARRAY(infos, nhosts);
	if (infos == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gfarm_strarray_free(hosts);
		goto end;
	}
	*np = nhosts;
	for (i = 0; i < nhosts; i++) {
		infos[i].pathname = strdup(pathname);
		infos[i].section = strdup(section);
		infos[i].hostname = hosts[i];/* not use gfarm_strarray_free */
	}
	*infosp = infos;
	free(hosts);
end:
	gfarm_file_section_info_free(&fsi);
	return (e);
}

/* XXX : Is this needed? */
static char *
gfarm_localfs_file_section_copy_info_get_all_by_host(
	const char *hostname,
	int *np,
	struct gfarm_file_section_copy_info **infosp)
{
	char *e;
	if ((e = gfarm_localfs_check()) != NULL)
		return (e);
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

/**********************************************************************/

const struct gfarm_metadb_internal_ops gfarm_localfs_metadb_ops = {
	gfarm_localfs_initialize,
	gfarm_localfs_terminate,

	gfarm_localfs_host_info_get,
	gfarm_localfs_host_info_remove_hostaliases,
	gfarm_localfs_host_info_set,
	gfarm_localfs_host_info_replace,
	gfarm_localfs_host_info_remove,
	gfarm_localfs_host_info_get_all,
	gfarm_localfs_host_info_get_by_name_alias,
	gfarm_localfs_host_info_get_allhost_by_architecture,

	gfarm_localfs_path_info_get,
	gfarm_localfs_path_info_set,
	gfarm_localfs_path_info_replace,
	gfarm_localfs_path_info_remove,
	gfarm_localfs_path_info_get_all_foreach,

	gfarm_localfs_file_section_info_get,
	gfarm_localfs_file_section_info_set,
	gfarm_localfs_file_section_info_replace,
	gfarm_localfs_file_section_info_remove,
	gfarm_localfs_file_section_info_get_all_by_file,

	gfarm_localfs_file_section_copy_info_get,
	gfarm_localfs_file_section_copy_info_set,
	gfarm_localfs_file_section_copy_info_remove,
	gfarm_localfs_file_section_copy_info_get_all_by_file,
	gfarm_localfs_file_section_copy_info_get_all_by_section,
	gfarm_localfs_file_section_copy_info_get_all_by_host,
};
