#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

int mmaptest(char *filename, int mmapflags, char *writedata, char *updatedata,
		int updateoffset, char *bufdata) {
	if (strlen(writedata) < strlen(updatedata)) {
		printf("updatedata must not be longer than writedata\n");
		return 1;
	}
	if (strlen(bufdata) != strlen(writedata)) {
		printf("bufdata must be same length aswritedata\n");
		return 1;
	}
	if (updateoffset < 0 || strlen(writedata) <= updateoffset) {
		printf("invalid updateoffset: %d\n", updateoffset);
		return 1;
	}

	int fd = open(filename, O_CREAT | O_TRUNC | O_RDWR, 0644);
	if (fd < 0) {
		perror("open() failed\n");
		return 1;
	} else {
		printf("open(%s, O_CREAT|O_TRUNC) OK\n", filename);
	}

	int ret = 0;
	size_t writelen = strlen(writedata);
	ssize_t wrotelen = pwrite(fd, writedata, writelen, 0);
	if (wrotelen == writelen) {
		printf("pwrite() OK, writedata=%s\n", writedata);
	} else if (wrotelen < 0) {
		perror("pwrite() failed\n");
		ret = 1;
	} else {
		printf("writelen=%ld, but wrotelen=%lu\n", writelen, wrotelen);
		ret = 1;
	}

	size_t mmaplen = writelen;
	void *addr = mmap(NULL, mmaplen, PROT_READ | PROT_WRITE, mmapflags, fd, 0);
	if (addr == MAP_FAILED) {
		ret = 1;
		perror("mmap() failed\n");
	} else {
		printf("mmap(%s) OK\n",
				(mmapflags == MAP_SHARED ? "MAP_SHARED" : "MAP_PRIVATE"));
	}

	char mreaddata[mmaplen + 1];
	memcpy(mreaddata, addr, mmaplen);
	mreaddata[mmaplen] = '\0';
	if (memcmp(writedata, mreaddata, mmaplen) == 0) {
		printf("mread() OK, mreaddata=%s\n", mreaddata);
	} else {
		printf("mread() failed, mreaddata=%s must be %s\n", mreaddata,
				writedata);
		ret = 1;
	}

	memcpy(mreaddata + updateoffset, updatedata, strlen(updatedata));
	memcpy(addr, mreaddata, mmaplen);
	printf("memcpy() OK, new mreaddata=%s\n", mreaddata);

	char preaddata[mmaplen + 1];
	preaddata[mmaplen] = '\0';
	ssize_t readlen = pread(fd, preaddata, writelen, 0);
	if (readlen == writelen) {
		printf("pread() OK, preaddata=%s, bufdata=%s\n", preaddata, bufdata);
		if (memcmp(preaddata, bufdata, writelen) == 0) {
			printf("pread() data OK\n");
		} else {
			printf("pread() data NG\n");
			ret = 1;
		}
	} else if (readlen < 0) {
		perror("pread() failed\n");
		ret = 1;
	} else {
		printf("readlen=%ld, but wrotelen=%lu\n", readlen, wrotelen);
		ret = 1;
	}

	if (addr != MAP_FAILED) {
		ret = munmap(addr, mmaplen);
		if (ret == 0) {
			printf("munmap() OK\n");
		} else {
			perror("munmap() failed\n");
			ret = 1;
		}
	}

	if (close(fd) == 0) {
		printf("close(%s) OK\n", filename);
	} else {
		perror("close() failed\n");
		ret = 1;
	}
	return ret;
}

int main(int argc, char *argv[]) {
	if (argc < 2) {
		printf("Usage: %s filename\n", argv[0]);
		return 1;
	}
	char *filename = argv[1];

	int ret = mmaptest(filename, MAP_SHARED, "12345678", "abcd", 2, "12abcd78");
	ret += mmaptest(filename, MAP_PRIVATE, "12345678", "abcd", 2, "12345678");

	printf("ret=%d\n", ret);
	return ret;
}
