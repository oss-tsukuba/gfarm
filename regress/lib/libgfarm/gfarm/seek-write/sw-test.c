#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gfarm/gfarm.h>

#define TMPFILE "testfile"

char *
gfs_pio_pread(GFS_File gf, char *buf, size_t size, off_t off, int *lenp)
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
gfs_pio_pwrite(GFS_File gf, char *buf, size_t size, off_t off, int *lenp)
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

//#define POS 1024
#define POS 10

int
main()
{
	char *e;
	GFS_File gf;
	int flags;
	char b1[BSIZE], b2[BSIZE];
	unsigned long i;
	int len, len2;

	e = gfarm_initialize(NULL,NULL);
	if (e != NULL)
		goto err;
	for (i = 0; i < BSIZE; i++) {
		b1[i] = (char) i;
	}
	flags = GFARM_FILE_RDWR|GFARM_FILE_TRUNC;
//	flags |= GFARM_FILE_UNBUFFERED;

	e = gfs_pio_create(TMPFILE, flags, 0600, &gf);
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

	if (memcmp(b1, b2, 2) == 0 && len == len2)
		printf("OK\n");
	else
		printf("NG: write=%d, read=%d\n", len, len2);

	gfs_pio_close(gf);

	return (0);
err:
	printf("%s", e);
	return (1);
}
