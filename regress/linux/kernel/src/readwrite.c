#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/file.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

const static char *writedata = "abcdefgh";
const static size_t writelen = sizeof(writedata);

int readwrite_test(char *filename)
{
	int fd = open(filename, O_CREAT | O_TRUNC | O_RDWR, 0644);
	if (fd < 0) {
		perror("open() failed\n");
		return (1);
	} else {
		printf("open(%s, O_CREAT|O_TRUNC) OK\n", filename);
	}

	int i = 0, ret = 0;
	for (i = 0; i < (int) writelen; i++) {
		ssize_t wrotelen = write(fd, writedata + i, 1);
		if (wrotelen == 1) {
			printf("write() OK, %d: writedata=%c\n",
			i, writedata[i]);
		} else if (wrotelen < 0) {
			perror("write() failed\n");
			ret = 1;
		} else {
			printf("NG writelen=1, but wrotelen=%lu\n", wrotelen);
			ret = 1;
		}
	}

	off_t seekoff = lseek(fd, 0, SEEK_SET);
	if (seekoff == 0) {
		printf("lseek() OK\n");
	} else {
		perror("lseek() NG\n");
		ret = 1;
	}

	if (close(fd) == 0) {
		printf("close(%s) OK\n", filename);
	} else {
		perror("close() failed\n");
		ret = 1;
	}

	fd = open(filename, O_RDWR);
	if (fd < 0) {
		perror("open() failed\n");
		return (1);
	} else {
		printf("open(%s, O_RDWR) OK\n", filename);
	}

	char readdata[writelen + 1];
	readdata[writelen] = '\0';
	for (i = 0; i < (int) writelen; i++) {
		ssize_t readlen = read(fd, readdata + i, 1);
		if (readlen == 1) {
			printf("read() OK, %d: readdata=%c\n", i, readdata[i]);
		} else if (readlen < 0) {
			perror("read() failed\n");
			ret = 1;
		} else {
			printf("NG readlen=%lu, must be 1\n", readlen);
			ret = 1;
		}
	}

	if (close(fd) == 0) {
		printf("close(%s) OK\n", filename);
	} else {
		perror("close() failed\n");
		ret = 1;
	}

	return (ret);
}

int appendwrite_test(char *filename)
{
	int fd = open(filename, O_CREAT | O_TRUNC | O_RDWR, 0644);
	if (fd < 0) {
		perror("open() failed\n");
		return (1);
	} else {
		printf("open(%s, O_CREAT|O_TRUNC) OK\n", filename);
	}

	int ret = 0;
	ssize_t wrotelen = write(fd, writedata, writelen);
	if (wrotelen == writelen) {
		printf("write() OK, writedata=%s\n", writedata);
	} else if (wrotelen < 0) {
		perror("write() failed\n");
		ret = 1;
	} else {
		printf("NG writelen=%ld, but wrotelen=%lu\n",
			writelen, wrotelen);
		ret = 1;
	}

	if (close(fd) == 0) {
		printf("close(%s) OK\n", filename);
	} else {
		perror("close() failed\n");
		ret = 1;
	}

	fd = open(filename, O_APPEND | O_RDWR);
	if (fd < 0) {
		perror("open() failed\n");
		return (1);
	} else {
		printf("open(%s, O_APPEND | O_RDWR) OK\n", filename);
	}

	wrotelen = write(fd, writedata, writelen);
	if (wrotelen == writelen) {
		printf("write()2 OK, writedata=%s\n", writedata);
	} else if (wrotelen < 0) {
		perror("write()2 failed\n");
		ret = 1;
	} else {
		printf("NG writelen=%ld, but wrotelen=%lu\n",
			writelen, wrotelen);
		ret = 1;
	}

	off_t seekoff = lseek(fd, 0, SEEK_SET);
	if (seekoff == 0) {
		printf("lseek() OK\n");
	} else {
		perror("lseek() NG\n");
		ret = 1;
	}

	size_t filesize = 2 * writelen;
	char readdata[filesize + 1];
	readdata[filesize] = '\0';
	ssize_t readlen = read(fd, readdata, filesize);
	if (readlen == filesize) {
		printf("read() OK, readdata=%s\n", readdata);
	} else if (readlen < 0) {
		perror("read() failed\n");
		ret = 1;
	} else {
		printf("NG readlen=%lu, must be %ld\n", readlen, filesize);
		ret = 1;
	}

	if (close(fd) == 0) {
		printf("close(%s) OK\n", filename);
	} else {
		perror("close() failed\n");
		ret = 1;
	}

	return (ret);
}

int readwriteseek_test(char *filename, off_t offset, int whence, off_t ansoff,
		char *writedata2, char *answerdata)
{
	int fd = open(filename, O_CREAT | O_TRUNC | O_RDWR, 0644);
	if (fd < 0) {
		perror("open() failed\n");
		return (1);
	} else {
		printf("open(%s, O_CREAT|O_TRUNC) OK\n", filename);
	}

	int ret = 0;
	ssize_t wrotelen = write(fd, writedata, writelen);
	if (wrotelen == writelen) {
		printf("write() OK, writedata=%s\n", writedata);
	} else if (wrotelen < 0) {
		perror("write() failed\n");
		ret = 1;
	} else {
		printf("NG writelen=%ld, but wrotelen=%lu\n",
			writelen, wrotelen);
		ret = 1;
	}

	off_t seekoff = lseek(fd, 0, SEEK_SET);
	if (seekoff == 0) {
		printf("lseek() OK\n");
	} else {
		perror("lseek() NG\n");
		ret = 1;
	}

	char readdata[writelen + 1];
	readdata[writelen] = '\0';
	ssize_t readlen = read(fd, readdata, writelen);
	if (readlen == writelen) {
		printf("read() OK, readdata=%s\n", readdata);
		if (memcmp(readdata, writedata, writelen) == 0) {
			printf("read() data OK\n");
		} else {
			printf("read() data NG\n");
			ret = 1;
		}
	} else if (readlen < 0) {
		perror("read() failed\n");
		ret = 1;
	} else {
		printf("NG readlen=%ld, but wrotelen=%lu\n", readlen, wrotelen);
		ret = 1;
	}

	seekoff = lseek(fd, offset, whence);
	if (seekoff == ansoff) {
		printf("lseek() OK, offset=%ld, whence=%d, seekoff=%ld\n",
			offset, whence, seekoff);
	} else {
		perror("lseek() NG\n");
		ret = 1;
	}

	size_t writelen2 = strlen(writedata2);
	ssize_t wrotelen2 = write(fd, writedata2, writelen2);
	if (wrotelen2 == writelen2) {
		printf("write()2 OK, writedata2=%s\n", writedata2);
	} else if (wrotelen2 < 0) {
		perror("write()2 failed\n");
		ret = 1;
	} else {
		printf("writelen2=%ld, but wrotelen2=%lu\n",
			writelen2, wrotelen2);
		ret = 1;
	}

	size_t filesize = strlen(answerdata);
	char preaddata2[filesize + 1];
	preaddata2[filesize] = '\0';
	ssize_t readlen2 = pread(fd, preaddata2, filesize, 0);
	if (readlen2 == filesize) {
		printf("pread()2 OK, preaddata2=%s\n", preaddata2);
		if (memcmp(preaddata2, answerdata, filesize) == 0) {
			printf("pread()2 data OK\n");
		} else {
			printf("pread()2 data NG\n");
			ret = 1;
		}
	} else if (readlen2 < 0) {
		perror("pread()2 failed\n");
		ret = 1;
	} else {
		printf("NG readlen2=%ld, but filesize=%lu\n",
			readlen2, filesize);
		ret = 1;
	}

	if (close(fd) == 0) {
		printf("close(%s) OK\n", filename);
	} else {
		perror("close() failed\n");
		ret = 1;
	}

	return (ret);
}

int preadwrite_test(char *filename)
{
	int fd = open(filename, O_CREAT | O_TRUNC | O_RDWR, 0644);
	if (fd < 0) {
		perror("open() failed\n");
		return (1);
	} else {
		printf("open(%s, O_CREAT|O_TRUNC) OK\n", filename);
	}

	int ret = 0;
	ssize_t wrotelen = pwrite(fd, writedata, writelen, 0);
	if (wrotelen == writelen) {
		printf("pwrite() OK, writedata=%s\n", writedata);
	} else if (wrotelen < 0) {
		perror("pwrite() failed\n");
		ret = 1;
	} else {
		printf("NG writelen=%ld, but wrotelen=%lu\n",
			writelen, wrotelen);
		ret = 1;
	}

	ret = fsync(fd);
	if (ret == 0) {
		printf("fsync() OK\n");
	} else {
		perror("fsync() NG\n");
		ret = 1;
	}

	char preaddata[writelen + 1];
	preaddata[writelen] = '\0';
	ssize_t readlen = pread(fd, preaddata, writelen, 0);
	if (readlen == writelen) {
		printf("pread() OK, preaddata=%s\n", preaddata);
		if (memcmp(preaddata, writedata, writelen) == 0) {
			printf("pread() data OK\n");
		} else {
			printf("pread() data NG\n");
			ret = 1;
		}
	} else if (readlen < 0) {
		perror("pread() failed\n");
		ret = 1;
	} else {
		printf("NG readlen=%ld, but wrotelen=%lu\n", readlen, wrotelen);
		ret = 1;
	}

	char *writedata2 = "12345678";
	size_t writelen2 = strlen(writedata2);
	ssize_t wrotelen2 = pwrite(fd, writedata2, writelen2, writelen);
	if (wrotelen2 == writelen2) {
		printf("pwrite()2 OK, writedata2=%s\n", writedata2);
	} else if (wrotelen2 < 0) {
		perror("pwrite()2 failed\n");
		ret = 1;
	} else {
		printf("NG writelen2=%ld, but wrotelen2=%lu\n",
			writelen2, wrotelen2);
		ret = 1;
	}

	ret = fdatasync(fd);
	if (ret == 0) {
		printf("fdatasync() OK\n");
	} else {
		perror("fdatasync() NG\n");
		ret = 1;
	}

	size_t filesize = writelen + writelen2;
	char filedata[filesize + 1];
	strcpy(filedata, writedata);
	strcpy(filedata + writelen, writedata2);
	char preaddata2[filesize + 1];
	preaddata[filesize] = '\0';
	ssize_t readlen2 = pread(fd, preaddata2, filesize, 0);
	if (readlen2 == filesize) {
		printf("pread()2 OK, preaddata2=%s\n", preaddata2);
		if (memcmp(preaddata2, filedata, filesize) == 0) {
			printf("pread()2 data OK\n");
		} else {
			printf("pread()2 data NG\n");
			ret = 1;
		}
	} else if (readlen2 < 0) {
		perror("pread()2 failed\n");
		ret = 1;
	} else {
		printf("NG readlen2=%ld, but filesize=%lu\n",
			readlen2, filesize);
		ret = 1;
	}

	if (close(fd) == 0) {
		printf("close(%s) OK\n", filename);
	} else {
		perror("close() failed\n");
		ret = 1;
	}

	return (ret);
}

int main(int argc, char *argv[])
{
	if (argc < 2) {
		printf("Usage: readwrite filename\n");
		return (1);
	}
	char *filename = argv[1];

	int ret = 0;

	ret += readwrite_test(filename);
	ret += appendwrite_test(filename);

	off_t ansoff = writelen - 2;
	ret += readwriteseek_test(filename, writelen - 2, SEEK_SET, ansoff,
			"1234", "abcdef1234");
	ret += readwriteseek_test(filename, -2, SEEK_CUR, ansoff, "1234",
			"abcdef1234");
	ret += readwriteseek_test(filename, 0, SEEK_END, writelen, "1234",
			"abcdefgh1234");
	ret += preadwrite_test(filename);

	printf("ret=%d\n", ret);
	return (ret);
}
