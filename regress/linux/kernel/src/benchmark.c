#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/file.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/mman.h>

static char filename[1024];
static char *gfarmfile;
static long iosizeMB;
static size_t iosize;
static char *writeBuffer;
static char *readBuffer;
static int callGfstat = 0;

int init(void) {
	writeBuffer = malloc(iosize);
	if (writeBuffer == NULL) {
		perror("malloc");
		return 1;
	}
	memset(writeBuffer, 0x41424344, iosize);

	readBuffer = malloc(iosize);
	if (readBuffer == NULL) {
		perror("malloc");
		free(writeBuffer);
		return 1;
	}
	return 0;
}

void fini(void) {
	free(writeBuffer);
	free(readBuffer);
}

struct timeval startTv, endTv;

void start(char *testname) {
	printf("START <%s>\n", testname);
	if (gettimeofday(&startTv, NULL) != 0) {
		perror("gettimeofday()");
	}
}

void end(char *testname) {
	printf("END   <%s>\n", testname);
	if (gettimeofday(&endTv, NULL) == 0) {
		double elapse = (endTv.tv_sec + 1e-6 * endTv.tv_usec)
				- (startTv.tv_sec + 1e-6 * startTv.tv_usec);
		printf("elapsed=%.6f [sec], %.3f[MB/sec]\n\n", elapse,
				iosizeMB / elapse);
	} else {
		perror("gettimeofday()");
	}
}

void checkGfstat() {
	if (callGfstat) {
		char command[1024];
		snprintf(command, sizeof(command), "/usr/local/bin/gfstat %s",
				gfarmfile);
		system(command);
		printf("\n");
	}
}

void runTest(char *testname, void (*benchfunc)(void)) {
	start(testname);
	benchfunc();
	end(testname);
}

void createwrite(void) {
	int fd = open(filename, O_CREAT | O_RDWR, 0644);
	if (fd < 0) {
		perror("open() failed\n");
		return;
	}
	ssize_t wrotelen = write(fd, writeBuffer, iosize);
	if (wrotelen < 0) {
		perror("pwrite()");
	} else if (wrotelen != iosize) {
		printf("wrotelen=%ld, iosize=%ld\n", wrotelen, iosize);
	}
	checkGfstat();
	if (close(fd) != 0) {
		perror("close()");
	}
}

void openwrite(void) {
	int fd = open(filename, O_RDWR);
	if (fd < 0) {
		perror("open() failed\n");
		return;
	}
	ssize_t wrotelen = write(fd, writeBuffer, iosize);
	if (wrotelen < 0) {
		perror("pwrite()");
	} else if (wrotelen != iosize) {
		printf("wrotelen=%ld, iosize=%ld\n", wrotelen, iosize);
	}
	checkGfstat();
	if (close(fd) != 0) {
		perror("close()");
	}
}

void openread(void) {
	int fd = open(filename, O_RDONLY);
	if (fd < 0) {
		perror("open() failed\n");
		return;
	}
	ssize_t readlen = read(fd, readBuffer, iosize);
	if (readlen < 0) {
		perror("pread()");
	} else if (readlen != iosize) {
		printf("readlen=%ld, iosize=%ld\n", readlen, iosize);
	}
	checkGfstat();
	if (close(fd) != 0) {
		perror("close()");
	}
}

void openmmapwrite(void) {
	int fd = open(filename, O_RDWR);
	if (fd < 0) {
		perror("open() failed\n");
		return;
	}
	void *addr = mmap(NULL, iosize, PROT_WRITE, MAP_SHARED, fd, 0);
	if (addr == MAP_FAILED) {
		perror("mmap");
		goto quit;
	}
	memmove(addr, writeBuffer, iosize);
	if (munmap(addr, iosize) != 0) {
		perror("munmap");
	}
	checkGfstat();
	quit: if (close(fd) != 0) {
		perror("close()");
	}
}

void openmmapread(void) {
	int fd = open(filename, O_RDWR);
	if (fd < 0) {
		perror("open() failed\n");
		return;
	}
	void *addr = mmap(NULL, iosize, PROT_READ, MAP_SHARED, fd, 0);
	if (addr == MAP_FAILED) {
		perror("mmap");
		goto quit;
	}
	memmove(readBuffer, addr, iosize);
	if (munmap(addr, iosize) != 0) {
		perror("munmap");
	}
	checkGfstat();
	quit: if (close(fd) != 0) {
		perror("close()");
	}
}

int main(int argc, char *argv[]) {
	if (argc < 4) {
		printf("Usage: %s mountpoint filename IOsize_MB [read]\n", argv[0]);
		return 1;
	}
	snprintf(filename, sizeof(filename), "%s/%s", argv[1], argv[2]);
	gfarmfile = argv[2];
	iosizeMB = strtol(argv[3], NULL, 10);
	iosize = 1024 * 1024 * iosizeMB;
	int doReadTest = (argc >= 5 && strcmp(argv[4], "read") == 0);
	printf("filename=%s, I/O size=%lu[MB], doReadTest=%d\n", filename, iosizeMB,
			doReadTest);

	if (init() != 0) {
		return 1;
	}

	checkGfstat();

	int ret = 0;
	if (!doReadTest) {
		ret = unlink(filename);
		if (ret == 0) {
			printf("\"%s\" was removed before benchmark\n", filename);
			checkGfstat();
		} else if (errno != ENOENT) {
			perror("unlink()");
			goto quit;
		} else {
			ret = 0;
		}
		//
		runTest("creat,write,close", createwrite);
		runTest("open,write,close", openwrite);
		runTest("open,mmapWrite,close", openmmapwrite);
	}
	runTest("open,read,close", openread);
	runTest("open,mmapRead,close", openmmapread);

	quit: fini();
	return ret;
}
