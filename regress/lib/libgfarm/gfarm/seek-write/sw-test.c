#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gfarm/gfarm.h>

#define TMPFILE "testfile"

char *
gfs_pio_pread(GFS_File gf, char *buf, size_t size, file_offset_t off, int *lenp)
{
	char *e;
	file_offset_t o;

	e = gfs_pio_seek(gf, off, 0, &o);
	if (e != NULL)
		return (e);
	e = gfs_pio_read(gf, buf, size, lenp);

	return (e);
}

char *
gfs_pio_pwrite(GFS_File gf, char *buf, size_t size, file_offset_t off, int *lenp)
{
	char *e;
	file_offset_t o;

	e = gfs_pio_seek(gf, off, 0, &o);
	if (e != NULL)
		return (e);
	e = gfs_pio_write(gf, buf, size, lenp);

	return (e);
}

#define BSIZE 4096

#if 0
#define POS 1024
#else
#define POS 10
#endif

int
main(int argc, char **argv)
{
	char *e;
	GFS_File gf;
	int flags;
	char b1[BSIZE], b2[BSIZE], *tmpfile = TMPFILE;
	unsigned long i;
	int len, len2, exit_code = 2;

	e = gfarm_initialize(&argc, &argv);
	if (e != NULL)
		goto err;
	if (argc > 1)
		tmpfile = argv[1];

	for (i = 0; i < BSIZE; i++) {
		b1[i] = (char) i;
	}
	flags = GFARM_FILE_RDWR|GFARM_FILE_TRUNC;
#if 0
	flags |= GFARM_FILE_UNBUFFERED;
#endif

	e = gfs_pio_create(tmpfile, flags, 0600, &gf);
	if (e != NULL)
		goto err;
	e = gfs_pio_set_view_index(gf, 1, 0, NULL, 0);
	if (e != NULL)
		goto err;

	e = gfs_pio_pwrite(gf, b1, 1, POS - 2, &len);
	if (e != NULL)
		goto err;
	e = gfs_pio_pread(gf, b2, 2, POS - 2, &len); /* toward */
	if (e != NULL)
		goto err;
	e = gfs_pio_pwrite(gf, b1, 1, POS - 1, &len); /* go back */
	if (e != NULL)
		goto err;
	e = gfs_pio_pwrite(gf, b1, 2, POS, &len); /* cannot write correctly */
	if (e != NULL)
		goto err;
	e = gfs_pio_pread(gf, b2, 10, POS, &len2); /* check */
	if (e != NULL)
		goto err;

	if (memcmp(b1, b2, 2) == 0 && len == len2) {
		fprintf(stderr, "OK\n");
		exit_code = 0;
	} else {
		fprintf(stderr, "NG: write=%d, read=%d\n", len, len2);
		exit_code = 1;
	}
	gfs_pio_close(gf);

err:
	gfs_unlink(tmpfile);
	if (e != NULL)
		fprintf(stderr, "error: %s\n", e);
	return (exit_code);
}
