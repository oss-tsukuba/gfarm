/*
 * program to test [gfarm-developers:01294] and [gfarm-developers:01330]
 */

#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <gfarm/gfarm.h>

#define RETRY 1

int
check(const char *msg, gfarm_error_t e)
{
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", msg, gfarm_error_string(e));
		return (1);
	}
	return (0);
}

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	GFS_File gf;
	pid_t pid;
	int status, count;
	char *filename;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
		return 2;
	}
	filename = argv[1];

	e = gfarm_initialize(&argc, &argv);
	if (check("gfarm_initialize", e))
		return 1;

	for (count = 0; count < RETRY; count++) {
		(void)gfs_unlink(filename);
		e = gfs_pio_create(filename,
		    GFARM_FILE_WRONLY|GFARM_FILE_TRUNC, 0644, &gf);
		if (check("gfs_pio_create", e))
			return 1;
		e = gfs_pio_set_view_global(gf, 0);
		if (check("gfs_pio_set_view_global", e)) {
			gfs_pio_close(gf);
			return 1;
		}
		e = gfs_pio_close(gf);
		if (check("gfs_pio_close", e))
			break;
		pid = fork();
		if (pid == -1){
			perror("fork");
			break;
		} else if (pid == 0) {
			e = gfarm_terminate();
			if (check("child gfarm_terminate", e))
				_exit(1);
			e = gfarm_initialize(&argc, &argv);
			if (check("child gfarm_initialize", e))
				_exit(1);
			e = gfs_unlink(filename);
			printf("%d\n", count);
			if (check("child gfarm_initialize", e))
				_exit(1);
			_exit(0);
		}
		if (waitpid(pid, &status, 0) == -1)
			break;
		if (WIFSIGNALED(status) ||
		    (WIFEXITED(status) && WEXITSTATUS(status) != 0))
			break;
	}
	(void)gfs_unlink(filename);
	e = gfarm_terminate();
	if (check("gfarm_terminate", e))
		return 1;

	if (count < RETRY)
		return 1;
	return 0;
}
