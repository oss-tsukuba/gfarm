#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

int
main(int argc, char *argv[])
{
	char *f = argv[1];
	int fd, r;

	fd = open(f, O_WRONLY);
	r = write(fd, "XXX", 3);
	close(fd);
	return (0);
}
