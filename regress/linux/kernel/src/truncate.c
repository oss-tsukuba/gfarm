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

int main(int argc, char *argv[])
{
	if (argc < 2) {
		printf("Usage: %s filename\n", argv[0]);
		return (1);
	}
	char *filename = argv[1];

	int fd = open(filename, O_CREAT | O_TRUNC | O_RDWR, 0644);
	if (fd < 0) {
		perror("open() failed\n");
		return (1);
	} else {
		printf("open(%s, O_CREAT|O_TRUNC) OK\n", filename);
	}

	int ret = 0, ret2;
	ssize_t wrotelen = write(fd, writedata, writelen);
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

	off_t newlen = writelen / 2;
	ret2 = ftruncate(fd, newlen);
	if (ret == 0) {
		printf("truncate(%ld) OK\n", newlen);
	} else {
		perror("truncate()\n");
		ret = 1;
	}

	char readdata[writelen + 1];
	readdata[writelen] = '\0';
	ssize_t readlen = pread(fd, readdata, writelen, 0);
	if (readlen == newlen) {
		printf("read() OK, readdata=%s\n", readdata);
	} else if (readlen < 0) {
		perror("read() failed\n");
		ret = 1;
	} else {
		printf("NG readlen=%lu, must be %ld\n", readlen, newlen);
		ret = 1;
	}

	ret2 = ftruncate(fd, writelen);
	if (ret == 0) {
		printf("truncate(%ld) 2 OK\n", writelen);
	} else {
		perror("truncate() 2\n");
		ret = 1;
	}

	memset(readdata, 0, sizeof(readdata));
	readlen = pread(fd, readdata, writelen, 0);
	if (readlen == writelen) {
		printf("read() OK, readdata=%s\n", readdata);
		int i;
		for (i = 0; i < (int)writelen; i++) {
			printf("%d 0x%x (%c)\n", i, readdata[i], readdata[i]);
		}
		if (strlen(readdata) == newlen) {
			printf("hole OK\n");
		} else {
			printf("hole NG\n");
			ret = 1;
		}
	} else if (readlen < 0) {
		perror("read() failed\n");
		ret = 1;
	} else {
		printf("NG readlen=%lu, must be %ld\n", readlen, newlen);
		ret = 1;
	}

	if (close(fd) == 0) {
		printf("close(%s) OK\n", filename);
	} else {
		perror("close() failed\n");
		ret = 1;
	}

	printf("ret=%d\n", ret);
	return (ret);
}
