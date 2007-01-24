/*
 * Filesystem tests
 *
 * This program is based on test.c from FUSE project.
 *     http://fuse.cvs.sourceforge.net/fuse/fuse/test/
 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <utime.h>
#include <errno.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <signal.h>

static char testfile[] = "./__testfile";
static char testfile2[] = "./__testfile2";
static char testdir[] = "./__testdir";
static char testdir2[] = "./__testdir2";
static char testname[256];
static char testdata[] = "abcdefghijklmnopqrstuvwxyz";
static char testdata2[] = "1234567890-=qwertyuiop[]\asdfghjkl;'zxcvbnm,./";
static const char *testdir_files[] = { "f1", "f2", NULL};
static char zerodata[4096];
static int testdatalen = sizeof(testdata) - 1;
static int testdata2len = sizeof(testdata2) - 1;

#define MAX_ENTRIES 1024

static void
test_perror(const char *func, const char *msg)
{
	fprintf(stdout, "NG [%s] %s(): %s: %s\n", testname, func, msg,
		strerror(errno));
}

static void
test_error(const char *func, const char *msg, ...)
	__attribute__ ((format (printf, 2, 3)));

static void
start_test(const char *fmt, ...)
	__attribute__ ((format (printf, 1, 2)));

static void
test_error(const char *func, const char *msg, ...)
{
	va_list ap;
	fprintf(stdout, "NG [%s] %s(): ", testname, func);
	va_start(ap, msg);
	vfprintf(stdout, msg, ap);
	va_end(ap);
	fprintf(stdout, "\n");
}

static void
success(void)
{
	fprintf(stdout, "OK [%s]\n", testname);
}

static void
start_test(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsprintf(testname, fmt, ap);
	va_end(ap);
}

#define PERROR(msg) test_perror(__FUNCTION__, msg)
#define ERROR(msg, args...) test_error(__FUNCTION__, msg, ##args)

static int
check_size(const char *path, int len)
{
	struct stat stbuf;
	int res = stat(path, &stbuf);
	if (res == -1) {
		PERROR("stat");
		return -1;
	}
	if (stbuf.st_size != len) {
		ERROR("length %u instead of %u",
		      (int) stbuf.st_size, (int) len);
		return -1;
	}
	return 0;
}

static int
check_type(const char *path, mode_t type)
{
	struct stat stbuf;
	int res = lstat(path, &stbuf);
	if (res == -1) {
		PERROR("lstat");
		return -1;
	}
	if ((stbuf.st_mode & S_IFMT) != type) {
		ERROR("type 0%o instead of 0%o", stbuf.st_mode & S_IFMT, type);
		return -1;
	}
	return 0;
}

static int
check_mode(const char *path, mode_t mode)
{
	struct stat stbuf;
	int res = lstat(path, &stbuf);
	if (res == -1) {
		PERROR("lstat");
		return -1;
	}
	if ((stbuf.st_mode & 07777) != mode) {
		ERROR("mode 0%o instead of 0%o", stbuf.st_mode & 07777, mode);
		return -1;
	}
	return 0;
}

static int
check_times(const char *path, time_t atime, time_t mtime)
{
	int err = 0;
	struct stat stbuf;
	int res = lstat(path, &stbuf);
	if (res == -1) {
		PERROR("lstat");
		return -1;
	}
	if (stbuf.st_atime != atime) {
		ERROR("different atime");
		err--;
	}
	if (stbuf.st_mtime != mtime) {
		ERROR("different mtime");
		err--;
	}
	if (err)
		return -1;
	return 0;
}

static int
check_nlink(const char *path, nlink_t nlink)
{
	struct stat stbuf;
	int res = lstat(path, &stbuf);
	if (res == -1) {
		PERROR("lstat");
		return -1;
	}
	if (stbuf.st_nlink != nlink) {
		ERROR("nlink %i instead of %i", stbuf.st_nlink, nlink);
		return -1;
	}
	return 0;
}

static int
check_nonexist(const char *path)
{
	struct stat stbuf;
	int res = lstat(path, &stbuf);
	if (res == 0) {
		ERROR("file should not exist");
		return -1;
	}
	if (errno != ENOENT) {
		ERROR("file should not exist: %s", strerror(errno));
		return -1;
	}
	return 0;
}

static int
check_buffer(const char *buf, const char *data, unsigned len)
{
	if (memcmp(buf, data, len) != 0) {
		ERROR("data mismatch");
		return -1;
	}
	return 0;
}

#define check_data(p, d, o, l)  do_check_data(p, #p, d, o, l)

static int
do_check_data(const char *path, const char *path_str,
	      const char *data, int offset, unsigned len)
{
	char buf[4096];
	int res;
	int fd = open(path, O_RDONLY);
	if (fd == -1) {
		PERROR("open");
		return -1;
	}
	if (lseek(fd, offset, SEEK_SET) == (off_t) -1) {
		PERROR("lseek");
		close(fd);
		return -1;
	}
	while (len) {
		int rdlen = len < sizeof(buf) ? len : sizeof(buf);
		res = read(fd, buf, rdlen);
		if (res == -1) {
			PERROR("read");
			close(fd);
			return -1;
		}
		if (res != rdlen) {
			ERROR("%s: short read: %u instead of %u",
			      path_str, res, rdlen);
			close(fd);
			return -1;
		}
		if (check_buffer(buf, data, rdlen) != 0) {
			close(fd);
			return -1;
		}
		data += rdlen;
		len -= rdlen;
	}
	res = close(fd);
	if (res == -1) {
		PERROR("close");
		return -1;
	}
	return 0;
}

static int
check_dir_contents(const char *path, const char **contents)
{
	int i;
	int res;
	int err = 0;
	int found[MAX_ENTRIES];
	const char *cont[MAX_ENTRIES];
	DIR *dp;
	for (i = 0; contents[i]; i++) {
		assert(i < MAX_ENTRIES - 3);
		found[i] = 0;
		cont[i] = contents[i];
	}
	found[i] = 0;
	cont[i++] = ".";
	found[i] = 0;
	cont[i++] = "..";
	cont[i] = NULL;
	dp = opendir(path);
	if (dp == NULL) {
		PERROR("opendir");
		return -1;
	}
	memset(found, 0, sizeof(found));
	while(1) {
		struct dirent *de;
		errno = 0;
		de = readdir(dp);
		if (de == NULL) {
			if (errno) {
				PERROR("readdir");
				closedir(dp);
				return -1;
			}
			break;
		}
		for (i = 0; cont[i] != NULL; i++) {
			assert(i < MAX_ENTRIES);
			if (strcmp(cont[i], de->d_name) == 0) {
				if (found[i]) {
					ERROR("duplicate entry <%s>",
					      de->d_name);
					err--;
				} else
					found[i] = 1;
				break;
			}
		}
		if (!cont[i]) {
			ERROR("unexpected entry <%s>", de->d_name);
			err --;
		}
	}
	for (i = 0; cont[i] != NULL; i++) {
		if (!found[i]) {
			ERROR("missing entry <%s>", cont[i]);
			err--;
		}
	}
	res = closedir(dp);
	if (res == -1) {
		PERROR("closedir");
		return -1;
	}
	if (err)
		return -1;
	return 0;
}

static int
create_file(const char *path, const char *data, int len)
{
	int res;
	int fd;
	unlink(path);
	fd = creat(path, 0644);
	if (fd == -1) {
		PERROR("creat");
		return -1;
	}
	if (len) {
		res = write(fd, data, len);
		if (res == -1) {
			PERROR("write");
			close(fd);
			return -1;
		}
		if (res != len) {
			ERROR("write is short: %u instead of %u", res, len);
			close(fd);
			return -1;
		}
	}
	res = close(fd);
	if (res == -1) {
		PERROR("close");
		return -1;
	}
	res = check_type(path, S_IFREG);
	if (res == -1)
		return -1;
	res = check_mode(path, 0644);
	if (res == -1)
		return -1;
	res = check_nlink(path, 1);
	if (res == -1)
		return -1;
	res = check_size(path, len);
	if (res == -1)
		return -1;
	if (len) {
		res = check_data(path, data, 0, len);
		if (res == -1)
			return -1;
	}
	return 0;
}

static int
cleanup_dir(const char *path, const char **dir_files, int quiet)
{
	int i;
	int err = 0;
	for (i = 0; dir_files[i]; i++) {
		int res;
		char fpath[1024];
		sprintf(fpath, "%s/%s", path, dir_files[i]);
		res = unlink(fpath);
		if (res == -1 && !quiet) {
			PERROR("unlink");
			err --;
		}
	}
	if (err)
		return -1;
	return 0;
}

static int
create_dir(const char *path, const char **dir_files)
{
	int res;
	int i;
	rmdir(path);
	res = mkdir(path, 0755);
	if (res == -1) {
		PERROR("mkdir");
		return -1;
	}
	res = check_type(path, S_IFDIR);
	if (res == -1)
		return -1;
	res = check_mode(path, 0755);
	if (res == -1)
		return -1;
	for (i = 0; dir_files[i]; i++) {
		char fpath[1024];
		sprintf(fpath, "%s/%s", path, dir_files[i]);
		res = create_file(fpath, "", 0);
		if (res == -1) {
			cleanup_dir(path, dir_files, 1);
			return -1;
		}
	}
	res = check_dir_contents(path, dir_files);
	if (res == -1) {
		cleanup_dir(path, dir_files, 1);
		return -1;
	}
	return 0;
}

static int
test_truncate(int len)
{
	const char *data = testdata;
	int datalen = testdatalen;
	int res;
	start_test("truncate(%u)", (int) len);
	res = create_file(testfile, data, datalen);
	if (res == -1)
		return -1;
	res = truncate(testfile, len);
	if (res == -1) {
		PERROR("truncate");
		return -1;
	}
	res = check_size(testfile, len);
	if (res == -1)
		return -1;
	if (len > 0) {
		if (len <= datalen) {
			res = check_data(testfile, data, 0, len);
			if (res == -1)
				return -1;
		} else {
			res = check_data(testfile, data, 0, datalen);
			if (res == -1)
				return -1;
			res = check_data(testfile, zerodata, datalen,
					 len - datalen);
			if (res == -1)
				return -1;
		}
	}
	res = unlink(testfile);
	if (res == -1) {
		PERROR("unlink");
		return -1;
	}
	res = check_nonexist(testfile);
	if (res == -1)
		return -1;
	success();
	return 0;
}

static int
test_ftruncate(int len, int mode)
{
	const char *data = testdata;
	int datalen = testdatalen;
	int res;
	int fd;
	start_test("ftruncate(%u) mode=0%03o", len, mode);
	res = create_file(testfile, data, datalen);
	if (res == -1)
		return -1;
	fd = open(testfile, O_WRONLY);
	if (fd == -1) {
		PERROR("open");
		return -1;
	}
	res = fchmod(fd, mode);
	if (res == -1) {
		PERROR("fchmod");
		close(fd);
		return -1;
	}
	res = check_mode(testfile, mode);
	if (res == -1) {
		close(fd);
		return -1;
	}
	res = ftruncate(fd, len);
	if (res == -1) {
		PERROR("ftruncate");
		close(fd);
		return -1;
	}
	close(fd);
	res = check_size(testfile, len);
	if (res == -1)
		return -1;
	if (len > 0) {
		if (len <= datalen) {
			res = check_data(testfile, data, 0, len);
			if (res == -1)
				return -1;
		} else {
			res = check_data(testfile, data, 0, datalen);
			if (res == -1)
				return -1;
			res = check_data(testfile, zerodata, datalen,
					 len - datalen);
			if (res == -1)
				return -1;
		}
	}
	res = unlink(testfile);
	if (res == -1) {
		PERROR("unlink");
		return -1;
	}
	res = check_nonexist(testfile);
	if (res == -1)
		return -1;
	success();
	return 0;
}

static int
test_utime(void)
{
	struct utimbuf utm;
	time_t atime = 987631200;
	time_t mtime = 123116400;
	int res;
	start_test("utime");
	res = create_file(testfile, NULL, 0);
	if (res == -1)
		return -1;
	utm.actime = atime;
	utm.modtime = mtime;
	res = utime(testfile, &utm);
	if (res == -1) {
		PERROR("utime");
		return -1;
	}
	res = check_times(testfile, atime, mtime);
	if (res == -1) {
		return -1;
	}
	res = unlink(testfile);
	if (res == -1) {
		PERROR("unlink");
		return -1;
	}
	res = check_nonexist(testfile);
	if (res == -1)
		return -1;
	success();
	return 0;
}

static int
test_create(void)
{
	const char *data = testdata;
	int datalen = testdatalen;
	int err = 0;
	int res;
	int fd;
	start_test("create");
	unlink(testfile);
	fd = creat(testfile, 0644);
	if (fd == -1) {
		PERROR("creat");
		return -1;
	}
	res = write(fd, data, datalen);
	if (res == -1) {
		PERROR("write");
		close(fd);
		return -1;
	}
	if (res != datalen) {
		ERROR("write is short: %u instead of %u", res, datalen);
		close(fd);
		return -1;
	}
	res = close(fd);
	if (res == -1) {
		PERROR("close");
		return -1;
	}
	res = check_type(testfile, S_IFREG);
	if (res == -1)
		return -1;
	err += check_mode(testfile, 0644);
	err += check_nlink(testfile, 1);
	err += check_size(testfile, datalen);
	err += check_data(testfile, data, 0, datalen);
	res = unlink(testfile);
	if (res == -1) {
		PERROR("unlink");
		return -1;
	}
	res = check_nonexist(testfile);
	if (res == -1)
		return -1;
	if (err)
		return -1;
	success();
	return 0;
}

#define test_open(exist, flags, mode)  do_test_open(exist, flags, #flags, mode)

static int
do_test_open(int exist, int flags, const char *flags_str, int mode)
{
	char buf[4096];
	const char *data = testdata;
	int datalen = testdatalen;
	unsigned currlen = 0;
	int err = 0;
	int res;
	int fd;
	off_t off;

	start_test("open(%s, %s, 0%03o)", exist ? "+" : "-", flags_str, mode);
	unlink(testfile);
	if (exist) {
		res = create_file(testfile, testdata2, testdata2len);
		if (res == -1)
			return -1;
		currlen = testdata2len;
	}
	fd = open(testfile, flags, mode);
	if ((flags & O_CREAT) && (flags & O_EXCL) && exist) {
		if (fd != -1) {
			ERROR("open should have failed");
			close(fd);
			return -1;
		} else if (errno == EEXIST)
			goto succ;
	}
	if (!(flags & O_CREAT) && !exist) {
		if (fd != -1) {
			ERROR("open should have failed");
			close(fd);
			return -1;
		} else if (errno == ENOENT)
			goto succ;
	}
	if (fd == -1) {
		PERROR("open");
		return -1;
	}
	if (flags & O_TRUNC)
		currlen = 0;
	err += check_type(testfile, S_IFREG);
	if (exist)
		err += check_mode(testfile, 0644);
	else
		err += check_mode(testfile, mode);
	err += check_nlink(testfile, 1);
	err += check_size(testfile, currlen);
	if (exist && !(flags & O_TRUNC) && (mode & 0400))
		err += check_data(testfile, testdata2, 0, testdata2len);
	res = write(fd, data, datalen);
	if ((flags & O_ACCMODE) != O_RDONLY) {
		if (res == -1) {
			PERROR("write");
			err --;
		} else if (res != datalen) {
			ERROR("write is short: %u instead of %u",
			      res, datalen);
			err --;
		} else {
			if (datalen > (int) currlen)
				currlen = datalen;
			err += check_size(testfile, currlen);
			if (mode & 0400) {
				err += check_data(testfile, data, 0, datalen);
				if (exist && !(flags & O_TRUNC) &&
				    testdata2len > datalen)
					err += check_data(
						testfile,
						testdata2 + datalen,
						datalen,
						testdata2len - datalen);
			}
		}
	} else {
		if (res != -1) {
			ERROR("write should have failed");
			err --;
		} else if (errno != EBADF) {
			PERROR("write");
			err --;
		}
	}
	off = lseek(fd, SEEK_SET, 0);
	if (off == (off_t) -1) {
		PERROR("lseek");
		err--;
	} else if (off != 0) {
		ERROR("offset should have returned 0");
		err --;
	}
	res = read(fd, buf, sizeof(buf));
	if ((flags & O_ACCMODE) != O_WRONLY) {
		if (res == -1) {
			PERROR("read");
			err--;
		} else {
			int readsize = currlen < sizeof(buf) ?
				currlen : sizeof(buf);
			if (res != readsize) {
				ERROR("read is short: %i instead of %u",
				      res, readsize);
				err--;
			} else {
				if ((flags & O_ACCMODE) != O_RDONLY) {
					err += check_buffer(buf, data,
							    datalen);
					if (exist && !(flags & O_TRUNC) &&
					    testdata2len > datalen)
						err += check_buffer(
							buf + datalen,
							testdata2 + datalen,
							testdata2len - datalen
							);
				} else if (exist && !(flags & O_TRUNC))
					/* (added) O_RDONLY only */
					err += check_buffer(
						buf, testdata2, testdata2len);
			}
		}
	} else {
		if (res != -1) {
			ERROR("read should have failed");
			err --;
		} else if (errno != EBADF) {
			PERROR("read");
			err --;
		}
	}
	res = close(fd);
	if (res == -1) {
		PERROR("close");
		return -1;
	}
	res = unlink(testfile);
	if (res == -1) {
		PERROR("unlink");
		return -1;
	}
	res = check_nonexist(testfile);
	if (res == -1)
		return -1;
	if (err)
		return -1;
succ:
	success();
	return 0;
}

#define test_open_acc(flags, mode, err)  do_test_open_acc(flags, #flags, mode, err, #err)

static int
do_test_open_acc(int flags, const char *flags_str, int mode,
		 int err, const char *err_str)
{
	const char *data = testdata;
	int datalen = testdatalen;
	int res;
	int fd;
	start_test("open_acc(%s) mode=0%03o errno=%s", flags_str, mode,
		   err_str);
	unlink(testfile);
	res = create_file(testfile, data, datalen);
	if (res == -1)
		return -1;
	res = chmod(testfile, mode);
	if (res == -1) {
		PERROR("chmod");
		return -1;
	}
	res = check_mode(testfile, mode);
	if (res == -1)
		return -1;
	fd = open(testfile, flags);
	if (fd == -1) {
		if (err != errno) {
			PERROR("open");
			return -1;
		}
		res = check_size(testfile, datalen); /* added */
		if (res == -1)
			return -1;
		chmod(testfile, 0400);
		res = check_data(testfile, data, 0, datalen); /* added */
		if (res == -1)
			return -1;
	} else {
		if (err) {
			ERROR("open should have failed");
			close(fd);
			return -1;
		}
		close(fd);
		if (flags & O_TRUNC) { /* added */
			res = check_size(testfile, 0);
			if (res == -1)
				return -1;
		}
	}
	success();
	return 0;
}

static int
test_symlink(void)
{
	char buf[1024];
	const char *data = testdata;
	int datalen = testdatalen;
	int linklen = strlen(testfile);
	int err = 0;
	int res;
	start_test("symlink");
	res = create_file(testfile, data, datalen);
	if (res == -1)
		return -1;
	unlink(testfile2);
	res = symlink(testfile, testfile2);
	if (res == -1) {
		PERROR("symlink");
		return -1;
	}
	res = check_type(testfile2, S_IFLNK);
	if (res == -1)
		return -1;
	err += check_mode(testfile2, 0777);
	err += check_nlink(testfile2, 1);
	res = readlink(testfile2, buf, sizeof(buf));
	if (res == -1) {
		PERROR("readlink");
		err--;
	}
	if (res != linklen) {
		ERROR("short readlink: %u instead of %u", res, linklen);
		err--;
	}
	if (memcmp(buf, testfile, linklen) != 0) {
	    ERROR("link mismatch");
	    err--;
	}
	err += check_size(testfile2, datalen);
	err += check_data(testfile2, data, 0, datalen);
	res = unlink(testfile2);
	if (res == -1) {
		PERROR("unlink");
		return -1;
	}
	res = check_nonexist(testfile2);
	if (res == -1)
		return -1;
	if (err)
		return -1;
	success();
	return 0;
}

static int
test_link(void)
{
	const char *data = testdata;
	int datalen = testdatalen;
	int err = 0;
	int res;
	start_test("link");
	res = create_file(testfile, data, datalen);
	if (res == -1)
		return -1;
	unlink(testfile2);
	res = link(testfile, testfile2);
	if (res == -1) {
		PERROR("link");
		return -1;
	}
	res = check_type(testfile2, S_IFREG);
	if (res == -1)
		return -1;
	err += check_mode(testfile2, 0644);
	err += check_nlink(testfile2, 2);
	err += check_size(testfile2, datalen);
	err += check_data(testfile2, data, 0, datalen);
	res = unlink(testfile);
	if (res == -1) {
		PERROR("unlink");
		return -1;
	}
	res = check_nonexist(testfile);
	if (res == -1)
		return -1;
	err += check_nlink(testfile2, 1);
	res = unlink(testfile2);
	if (res == -1) {
		PERROR("unlink");
		return -1;
	}
	res = check_nonexist(testfile2);
	if (res == -1)
		return -1;
	if (err)
		return -1;
	success();
	return 0;
}

static int
test_rename_file(void)
{
	const char *data = testdata;
	int datalen = testdatalen;
	int err = 0;
	int res;
	start_test("rename file");
	res = create_file(testfile, data, datalen);
	if (res == -1)
		return -1;
	unlink(testfile2);
	res = rename(testfile, testfile2);
	if (res == -1) {
		PERROR("rename");
		return -1;
	}
	res = check_nonexist(testfile);
	if (res == -1)
		return -1;
	res = check_type(testfile2, S_IFREG);
	if (res == -1)
		return -1;
	err += check_mode(testfile2, 0644);
	err += check_nlink(testfile2, 1);
	err += check_size(testfile2, datalen);
	err += check_data(testfile2, data, 0, datalen);
	res = unlink(testfile2);
	if (res == -1) {
		PERROR("unlink");
		return -1;
	}
	res = check_nonexist(testfile2);
	if (res == -1)
		return -1;
	if (err)
		return -1;
	success();
	return 0;
}

static int
test_rename_dir(void)
{
	int err = 0;
	int res;
	start_test("rename dir");
	res = create_dir(testdir, testdir_files);
	if (res == -1)
		return -1;
	rmdir(testdir2);
	res = rename(testdir, testdir2);
	if (res == -1) {
		PERROR("rename");
		cleanup_dir(testdir, testdir_files, 1);
		return -1;
	}
	res = check_nonexist(testdir);
	if (res == -1) {
		cleanup_dir(testdir, testdir_files, 1);
		return -1;
	}
	res = check_type(testdir2, S_IFDIR);
	if (res == -1) {
		cleanup_dir(testdir2, testdir_files, 1);
		return -1;
	}
	err += check_mode(testdir2, 0755);
	err += check_dir_contents(testdir2, testdir_files);
	err += cleanup_dir(testdir2, testdir_files, 0);
	res = rmdir(testdir2);
	if (res == -1) {
		PERROR("rmdir");
		return -1;
	}
	res = check_nonexist(testdir2);
	if (res == -1)
		return -1;
	if (err)
		return -1;
	success();
	return 0;
}

static int
test_mkfifo(void)
{
	int res;
	int err = 0;
	start_test("mkfifo");
	unlink(testfile);
	res = mkfifo(testfile, 0644);
	if (res == -1) {
		PERROR("mkfifo");
		return -1;
	}
	res = check_type(testfile, S_IFIFO);
	if (res == -1)
		return -1;
	err += check_mode(testfile, 0644);
	err += check_nlink(testfile, 1);
	res = unlink(testfile);
	if (res == -1) {
		PERROR("unlink");
		return -1;
	}
	res = check_nonexist(testfile);
	if (res == -1)
		return -1;
	if (err)
		return -1;
	success();
	return 0;
}

static int
test_mkdir(void)
{
	int res;
	int err = 0;
	const char *dir_contents[] = {NULL};
	start_test("mkdir");
	rmdir(testdir);
	res = mkdir(testdir, 0755);
	if (res == -1) {
		PERROR("mkdir");
		return -1;
	}
	res = check_type(testdir, S_IFDIR);
	if (res == -1)
		return -1;
	err += check_mode(testdir, 0755);
	err += check_nlink(testdir, 2);
	err += check_dir_contents(testdir, dir_contents);
	res = rmdir(testdir);
	if (res == -1) {
		PERROR("rmdir");
		return -1;
	}
	res = check_nonexist(testdir);
	if (res == -1)
		return -1;
	if (err)
		return -1;
	success();
	return 0;
}

#define test_mmap(prot, flags)  do_test_mmap(prot, #prot, flags, #flags)

static int
do_test_mmap(int prot, const char *prot_str, int flags, const char *flags_str)
{
	int res;
	int err = 0;
	int fd;
	unsigned currlen = 0;
	char *m;
	static char testdata3[] = "_1234567890-=qwertyuiop[]\asdfghjkl;'zxcvbnm,.";
	start_test("mmap(%s, %s)", prot_str, flags_str);
	unlink(testfile);
	res = create_file(testfile, testdata2, testdata2len);
	if (res == -1)
		return -1;
	currlen = testdata2len;
	fd = open(testfile, O_RDWR);
	if (fd == -1) {
		PERROR("open");
		return -1;
	}
	m = mmap(0, currlen, prot, flags, fd, 0);
	close(fd);
	if ((void *) m == MAP_FAILED) {
		PERROR("mmap");
		return -1;
	}
	if (prot & PROT_READ) {
		err += check_buffer(m, testdata2, currlen);
	}
	if (prot & PROT_WRITE) {
		memcpy(m, testdata3, currlen);
		err += check_buffer(m, testdata3, currlen);
	}
	munmap(m, currlen);
	err += check_nlink(testfile, 1);
	err += check_size(testfile, currlen);
	if (err) {
		return -1;
	} else {
		success();
		return 0;
	}
}

#define BSIZE 1024

static int
test_seek_eof(int pos)
{
	int res;
	int fd;
	int i;
	char b1[BSIZE], b2[BSIZE];
	start_test("seek_EOF offset=%d", pos);
	unlink(testfile);
	fd = open(testfile, O_CREAT | O_RDWR, 0644);
	if (fd == -1) {
		PERROR("open");
		return -1;
	}
	for (i = 0; i < BSIZE; i++)
		b1[i] = (char) i;
	pwrite(fd, b1, 1, pos - 2);
	pread(fd, b2, 2, pos - 2); /* toward */
	pwrite(fd, b1, 1, pos - 1); /* go back */
	pwrite(fd, b1, 2, pos); /* Can this write correctly? */
	res = pread(fd, b2, BSIZE, pos); /* check */
	if (res != 2) {
		ERROR("cannot write correctly");
		return -1;
	}
	success();
	return 0;
}

static int
test_open_size()
{
	int res;
	int fd;
	char b = '0';
	start_test("open size");
	unlink(testfile);
	res = create_file(testfile, testdata, testdatalen);
	if (res == -1)
		return -1;
	fd = open(testfile, O_RDWR | O_CREAT | O_TRUNC, 0600);
	if (fd == -1) {
		PERROR("open");
		return -1;
	}
	res = check_size(testfile, 0);
	if (res == -1)
		return -1;
	res = write(fd, &b, 1);
	if (res == -1) {
		PERROR("write");
		return -1;
	}
	res = check_size(testfile, 1);
	if (res == -1)
		return -1;
	lseek(fd, 65536, SEEK_SET);
	res = write(fd, &b, 1);
	res = check_size(testfile, 65537);
	if (res == -1)
		return -1;
	close(fd);
	success();
	return 0;
}

static int
test_open_rename()
{
	int res, err = 0;
	int fd1, fd2;
	struct utimbuf utm;
	time_t atime = 987631200;
	time_t mtime = 123116400;
	start_test("open rename utime close");
	unlink(testfile);
	unlink(testfile2);
	res = create_file(testfile, testdata, testdatalen);
	if (res == -1)
		return -1;
	fd1 = open(testfile, O_RDWR);
	if (fd1 == -1) {
		PERROR("open_1");
		return -1;
	}
	res = rename(testfile, testfile2);
	if (res == -1) {
		PERROR("rename");
		close(fd1);
		return -1;
	}
	res = check_nonexist(testfile);
	if (res == -1)
		return -1;
	fd2 = open(testfile, O_RDWR | O_CREAT | O_EXCL, 0600);
	if (fd2 == -1) {
		PERROR("open_2");
		close(fd1);
		return -1;
	}
	res = write(fd2, testdata2, testdata2len);
	if (res == -1) {
		PERROR("write");
		err += -1;
	} else if (res != testdata2len) {
		ERROR("write is short: %u instead of %u", res, testdata2len);
		err += -1;
	}
	utm.actime = atime;
	utm.modtime = mtime;
	res = utime(testfile, &utm);
	res = 0;
	if (res == -1) {
		PERROR("utime_1");
		err += -1;
	}
	res = utime(testfile2, &utm);
	if (res == -1) {
		PERROR("utime_2");
		err += -1;
	}
	close(fd1);
	close(fd2);
	err += check_times(testfile, atime, mtime);
	err += check_times(testfile2, atime, mtime);
	err += check_data(testfile, testdata2, 0, testdata2len);
	err += check_data(testfile2, testdata, 0, testdatalen);
	if (err)
		return -1;
	success();
	return 0;
}

static int
test_open_unlink()
{
	int res;
	int fd;
	char b = '0';
	start_test("open unlink close");
	unlink(testfile);
	res = create_file(testfile, testdata, testdatalen);
	if (res == -1)
		return -1;
	fd = open(testfile, O_RDWR);
	if (fd == -1) {
		PERROR("open");
		return -1;
	}
	res = unlink(testfile); /* FUSE: RENAME is called */
	if (res == -1) {
		PERROR("unlink");
		close(fd);
		return -1;
	}
	/* for a filesystem which update the metadata at CLOSE only */
	write(fd, &b, 1); 
	res = check_nonexist(testfile);
	if (res == -1)
		return -1;
	res = fchmod(fd, 0700);  /* FUSE: renamed hidden file. */
	if (res == -1)
		PERROR("fchmod");
	close(fd);
	res = check_nonexist(testfile);
	if (res == -1)
		return -1;
	success();
	return 0;
}

static int
test_open_utime()
{
	int res;
	int fd;
	char b = '0';
	struct utimbuf utm;
	time_t atime = 987631200;
	time_t mtime = 123116400;
	start_test("open utime close");
	unlink(testfile);
	res = create_file(testfile, testdata, testdatalen);
	if (res == -1)
		return -1;
	fd = open(testfile, O_RDWR);
	if (fd == -1) {
		PERROR("open");
		return -1;
	}
	write(fd, &b, 1);  /* update metadata */
	utm.actime = atime;
	utm.modtime = mtime;
	res = utime(testfile, &utm);
	if (res == -1) {
		PERROR("utime");
		close(fd);
		return -1;
	}
	res = check_times(testfile, atime, mtime);
	if (res == -1)
		return -1;
	close(fd);
	res = check_times(testfile, atime, mtime);
	if (res == -1)
		return -1;
	success();
	return 0;
}

static int
test_open_chmod(mode_t mode1, mode_t mode2)
{
	int res;
	int fd;
	char b = '0';
	start_test("chmod(0%o) open chmod(0%o) close", mode1, mode2);
	unlink(testfile);
	res = create_file(testfile, testdata, testdatalen);
	if (res == -1)
		return -1;
	res = chmod(testfile, mode1);
	if (res == -1) {
		PERROR("chmod_1");
		return -1;
	}
	res = check_mode(testfile, mode1);
	if (res == -1)
		return -1;
	fd = open(testfile, O_RDWR);
	if (fd == -1) {
		PERROR("open");
		return -1;
	}
	write(fd, &b, 1);  /* update metadata */
	res = chmod(testfile, mode2);
	if (res == -1) {
		PERROR("chmod_2");
		close(fd);
		return -1;
	}
	res = check_mode(testfile, mode2);
	close(fd);
	if (res == -1)
		return -1;
	res = check_mode(testfile, mode2);
	if (res == -1)
		return -1;
	success();
	return 0;
}

#define test_open_open(f, s)   do_test_open_open(0, f, #f, s, #s)
#define test_creat_open(f, s)  do_test_open_open(1, f, #f, s, #s)

static int
do_test_open_open(int creat_mode,
		  int flags_first, const char *str_first,
		  int flags_second, const char *str_second)
{
	int res;
	int fd1, fd2;
	unlink(testfile);
	if (creat_mode) {
		start_test("creat(%s) open(%s)", str_first, str_second);
		fd1 = open(testfile, O_CREAT | flags_first, 0644);
		if (fd1 == -1) {
			PERROR("open_1");
			return -1;
		}
		res = write(fd1, testdata, testdatalen);
		if (res == -1) {
			PERROR("write");
			close(fd1);
			return -1;
		}
		if (res != testdatalen) {
			ERROR("write is short: %u instead of %u",
			      res, testdatalen);
			close(fd1);
			return -1;
		}
	} else {
		start_test("open(%s) open(%s)", str_first, str_second);
		res = create_file(testfile, testdata, testdatalen);
		if (res == -1)
			return -1;
		fd1 = open(testfile, flags_first);
		if (fd1 == -1) {
			PERROR("open_1");
			return -1;
		}
	}
	fd2 = open(testfile, flags_second);
	if (fd2 == -1) {
		PERROR("open_2");
		close(fd1);
		return -1;
	}
	close(fd1);
	close(fd2);
	res = check_data(testfile, testdata, 0, testdatalen);
	if (res == -1)
		return -1;
	success();
	return 0;
}

static int
test_finalize(int err)
{
	unlink(testfile);
	unlink(testfile2);
	rmdir(testdir);
	rmdir(testdir2);
	if (err) {
		fprintf(stdout, "%i tests failed\n", -err);
		return 1;
	}
	return 0;
}

static void
signal_exit(int sig)
{
	int res;
	(void) sig;
	res = test_finalize(0);
	fprintf(stderr, "canceled\n");
	exit(res);
}

int
main(int argc, char *argv[])
{
	int err = 0;
	struct sigaction sa;

	if (argc != 2) {
		fprintf(stderr, "usage: %s testdir\n", argv[0]);
		return 1;
	}
	umask(0);
	chdir(argv[1]);

	sa.sa_handler = signal_exit;
	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);

	err += test_create();
	err += test_symlink();
	err += test_link();
	err += test_mkfifo();
	err += test_mkdir();
	err += test_rename_file();
	err += test_rename_dir();
	err += test_utime();
	err += test_truncate(0);
	err += test_truncate(testdatalen / 2);
	err += test_truncate(testdatalen);
	err += test_truncate(testdatalen + 100);
	err += test_ftruncate(0, 0600);
	err += test_ftruncate(testdatalen / 2, 0600);
	err += test_ftruncate(testdatalen, 0600);
	err += test_ftruncate(testdatalen + 100, 0600);
	err += test_ftruncate(0, 0400);
	err += test_ftruncate(0, 0200);
	err += test_ftruncate(0, 0000);
	err += test_open(0, O_RDONLY, 0);
	err += test_open(1, O_RDONLY, 0);
	err += test_open(1, O_RDWR, 0);
	err += test_open(1, O_WRONLY, 0);
	err += test_open(0, O_RDWR | O_CREAT, 0600);
	err += test_open(1, O_RDWR | O_CREAT, 0600);
	err += test_open(0, O_RDWR | O_CREAT | O_TRUNC, 0600);
	err += test_open(1, O_RDWR | O_CREAT | O_TRUNC, 0600);
	err += test_open(0, O_RDONLY | O_CREAT, 0600);
	err += test_open(0, O_RDONLY | O_CREAT, 0400);
	err += test_open(0, O_RDONLY | O_CREAT, 0200);
	err += test_open(0, O_RDONLY | O_CREAT, 0000);
	err += test_open(0, O_WRONLY | O_CREAT, 0600);
	err += test_open(0, O_WRONLY | O_CREAT, 0400);
	err += test_open(0, O_WRONLY | O_CREAT, 0200);
	err += test_open(0, O_WRONLY | O_CREAT, 0000);
	err += test_open(0, O_RDWR | O_CREAT, 0400);
	err += test_open(0, O_RDWR | O_CREAT, 0200);
	err += test_open(0, O_RDWR | O_CREAT, 0000);
	err += test_open(0, O_RDWR | O_CREAT | O_EXCL, 0600);
	err += test_open(1, O_RDWR | O_CREAT | O_EXCL, 0600);
	err += test_open(0, O_RDWR | O_CREAT | O_EXCL, 0000);
	err += test_open(1, O_RDWR | O_CREAT | O_EXCL, 0000);
	err += test_open_acc(O_RDONLY, 0600, 0);
	err += test_open_acc(O_WRONLY, 0600, 0);
	err += test_open_acc(O_RDWR,   0600, 0);
	err += test_open_acc(O_RDONLY, 0400, 0);
	err += test_open_acc(O_RDONLY | O_TRUNC, 0400, EACCES);
	err += test_open_acc(O_WRONLY, 0400, EACCES);
	err += test_open_acc(O_RDWR,   0400, EACCES);
	err += test_open_acc(O_RDONLY, 0200, EACCES);
	err += test_open_acc(O_WRONLY, 0200, 0);
	err += test_open_acc(O_RDWR,   0200, EACCES);
	err += test_open_acc(O_RDONLY, 0000, EACCES);
	err += test_open_acc(O_WRONLY, 0000, EACCES);
	err += test_open_acc(O_RDWR,   0000, EACCES);

	/* ------ additional tests ------ */
	err += test_open_acc(O_RDONLY | O_TRUNC, 0600, 0);
	err += test_open_acc(O_RDONLY | O_TRUNC, 0200, EACCES);
	err += test_open_acc(O_WRONLY | O_TRUNC, 0200, 0);
	err += test_open_acc(O_RDWR   | O_TRUNC, 0200, EACCES);
	err += test_mmap(PROT_READ, MAP_SHARED);
	err += test_mmap(PROT_WRITE, MAP_SHARED);
	err += test_mmap(PROT_READ | PROT_WRITE, MAP_SHARED);
	err += test_seek_eof(10);
	err += test_open_size();
	err += test_open_rename();
	err += test_open_unlink();
	err += test_open_utime();
	err += test_open_chmod(0600, 0700);
	err += test_open_chmod(0700, 0600);
	err += test_open_open(O_RDONLY, O_WRONLY);
	err += test_open_open(O_RDONLY, O_RDWR);
	err += test_open_open(O_WRONLY, O_RDONLY);
	err += test_open_open(O_WRONLY, O_RDWR);
	err += test_open_open(O_RDWR, O_RDONLY);
	err += test_open_open(O_RDWR, O_WRONLY);
	/* err += test_creat_open(O_RDONLY, O_WRONLY); -- EBADF */
	/* err += test_creat_open(O_RDONLY, O_RDWR);   -- EBADF */
	err += test_creat_open(O_WRONLY, O_RDONLY);
	err += test_creat_open(O_WRONLY, O_RDWR);
	err += test_creat_open(O_RDWR, O_RDONLY);
	err += test_creat_open(O_RDWR, O_WRONLY);

	return test_finalize(err);
}
