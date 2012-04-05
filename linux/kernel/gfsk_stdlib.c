#include <gfarm/gflog.h>
#include "gfsk.h"
#include "gfsk_fs.h"
#include <errno.h>
#include <stdio.h>
#include <pthread.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/syscalls.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/personality.h> /* for STICKY_TIMEOUTS */
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/fs.h>
#include <linux/rcupdate.h>
#include <linux/hrtimer.h>
#include <linux/ctype.h>
#ifdef __arch_um__
#include <linux/delay.h>
#endif

static DEFINE_MUTEX(gmutex);
#define GMUTEX_LOCK()	mutex_lock(&gmutex)
#define GMUTEX_UNLOCK()	mutex_unlock(&gmutex)

void
pthread_once(pthread_once_t  *once_control,  void  (*init_routine)(void))
{
	GMUTEX_LOCK();
	if (!*once_control) {
		*once_control = 1;
		init_routine();
	}
	GMUTEX_UNLOCK();
}

FILE *
fopen(const char *path, const char *mode)
{
	int	i, err = -EINVAL;
	FILE	*fp = NULL;
	if (!path) {
		gflog_error(0, "no path");
	} else if (*mode != 'r') {
		gflog_error(0, "support only read mode but '%s'", mode);
	} else {
		for (i = 0; i < GFSK_FBUF_MAX; i++) {
			struct gfsk_fbuf *fbp = &gfsk_fsp->gf_mdata.m_fbuf[i];
			if (!fbp->f_name.d_len)
				continue;
			if (strcmp(fbp->f_name.d_buf, path))
				continue;
			if (!(fp = kmalloc(sizeof(*fp), GFP_KERNEL))) {
				err = -ENOMEM;
				break;
			}
			fp->io_rptr = fp->io_buf = fbp->f_buf.d_buf;
			fp->io_end = fp->io_buf + fbp->f_buf.d_len;
			err = 0;
			break;
		}
	}
	if (err)
		errno = -err;
	return (fp);
}

int
fclose(FILE *fp)
{
	kfree(fp);
	return (0);
}

char *
fgets(char *buf, int n, FILE *fp)
{
	char *s = buf, *cp = fp->io_rptr;
	int	l;

	if (fp->io_rptr >= fp->io_end) {
		return (NULL);
	}
	n--;
	for (l = 0; l < n && cp < fp->io_end; l++) {
		if (*cp == '\n') {
			cp++;
			break;
		}
		if (*cp == '\r' && *(cp+1) == '\n') {
			cp += 2;
			break;
		}
		*buf++ = *cp++;
	}
	*buf = 0;
	fp->io_rptr = cp;
	return (s);
}

unsigned int
sleep(unsigned int seconds)
{
	msleep(seconds * 1000);
	return (0);
}
unsigned long int
strtoul(const char *nptr, char **endptr, int base)
{
	unsigned long res;
	int	ret;

	if (endptr)
		*endptr = (char *)nptr;
	if ((ret = strict_strtoul(nptr, base, &res))) {
		errno = -ret;
		res = ULONG_MAX;
	} else if (endptr) {
		if (base == 16 && *nptr == '0'
		&& (*(nptr+1) == 'x' || *(nptr+1) == 'X'))
			nptr += 2;
		while (isxdigit(*nptr))
			nptr++;
		*endptr = (char *)nptr;
	}
	return (res);
}

long int
strtol(const char *nptr, char **endptr, int base)
{
	long resl;
	unsigned long res;
	int	sign = 1;
	char *ep;

	if (!endptr)
		endptr = &ep;
	if (*nptr == '-') {
		sign = -1;
		nptr++;
	}
	res = strtoul(nptr, endptr, base);
	if (*endptr == nptr && res == ULONG_MAX)
		resl = LONG_MIN;
	else
		resl = res * sign;

	return (resl);
}
