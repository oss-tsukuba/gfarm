#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/file.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
	if (argc < 4) {
		printf("Usage: %s filename [se] waitsec\n", argv[0]);
		return (1);
	}
	char *filename = argv[1];
	char opech = argv[2][0];
	int waitsec = atoi(argv[3]);
	int ope;

	switch (opech) {
	case 's':
		ope = LOCK_SH;
		break;
	case 'e':
		ope = LOCK_EX;
		break;
	default:
		printf("unknown opech: %c\n", opech);
		return (1);
	}

	int fd = open(filename, O_CREAT, 0644);
	if (fd < 0) {
		perror("open() failed\n");
		return (1);
	} else {
		printf("open(%s, O_CREAT) OK\n", filename);
	}

	int ret = flock(fd, ope);
	if (ret == 0) {
		printf("flock(%d, %d) OK, wait %d sec before unlock\n", fd, ope,
				waitsec);
		/* unlock after waitsec sec */
		sleep(waitsec);
		ret = flock(fd, LOCK_UN);
		if (ret == 0) {
			printf("flock(%d, LOCK_UN) OK\n", fd);
		} else {
			perror("flock(LOCK_UN) failed\n");
		}
	} else {
		perror("flock() failed\n");
	}

	if (close(fd) == 0) {
		printf("close(%s) OK\n", filename);
	} else {
		perror("close() failed\n");
	}

	printf("ret=%d\n", ret);
	return (ret);
}
