#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <gfarm/gfarm_config.h>

/*
 * getloadavg() function may require root user or kmem group privilege
 * on some OSes.
 */

#ifndef HAVE_GETLOADAVG

#if defined(__linux__)
int
getloadavg(double *loadavg, int n)
{
	int i, rv;
	char buffer[30];
	double ldavg[3];
	static int fd = -1;

	if (fd < 0) {
		if (fd != -2) /* known to be failed */
			fd = open("/proc/loadavg", O_RDONLY);
	}
	if (fd < 0) {
		fd = -2; /* known to be failed */
		return (-1);
	}
	lseek(fd, SEEK_SET, 0L);
	rv = read(fd, buffer, sizeof(buffer) - 1);
	if (rv <= 0)
		return (-1);
	buffer[rv] = '\0';
	ldavg[0] = ldavg[1] = ldavg[2] = 0.0;
	rv = sscanf(buffer, "%lf %lf %lf", &ldavg[0], &ldavg[1], &ldavg[2]);
	if (n > rv)
		n = rv;
	for (i = 0; i < n; i++)
		loadavg[i] = ldavg[i];
	return (n);
}
#endif /* __linux__ */

#if defined(__osf__)
#include <sys/table.h>

int
getloadavg(double *loadavg, int n)
{
	int i;
	struct tbl_loadavg ldavg;

	if (table(TBL_LOADAVG, 0, &ldavg, 1, sizeof(ldavg)) == -1)
		return (-1);
	if (ldavg.tl_lscale == 0) {
		for (i = 0; i < n; i++)
			loadavg[i] = ldavg.tl_avenrun.d[i];
	} else {
		for (i = 0; i < n; i++)
			loadavg[i] = (double)ldavg.tl_avenrun.l[i] /
			    ldavg.tl_lscale;
	}
	return (n);
}
#endif /* __osf__ */

#endif /* !HAVE_GETLOADAVG */

#ifdef LOADAVG_TEST
main()
{
	double loadavg[3];

	getloadavg(loadavg, 3);
	printf("%lf %lf %lf\n", loadavg[0], loadavg[1], loadavg[2]);
}
#endif
