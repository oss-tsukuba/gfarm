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
#include <linux/delay.h>	/* for msleep */
#include <linux/personality.h> /* for STICKY_TIMEOUTS */
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/fs.h>
#include <linux/rcupdate.h>
#include <linux/hrtimer.h>
#include <linux/ctype.h>
#include <linux/swab.h>
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
		gflog_error(GFARM_MSG_1004985, "no path");
	} else if (*mode != 'r') {
		gflog_error(GFARM_MSG_1004986,
			"support only read mode but '%s'", mode);
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
void
gfarm_nanosleep_by_timespec(const struct timespec *tsp)
{
	unsigned int msec = tsp->tv_sec * 1000 + tsp->tv_nsec / (1000 * 1000);
	msleep(msec);
}
void
gfarm_nanosleep(unsigned long long nsec)
{
	msleep(nsec / (1000 * 1000));
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
unsigned long long
strtoull(const char *nptr, char **endptr, int base)
{
	unsigned long long res;
	int	ret;

	if (endptr)
		*endptr = (char *)nptr;
	if ((ret = strict_strtoull(nptr, base, &res))) {
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

long long
strtoll(const char *nptr, char **endptr, int base)
{
	if (*nptr == '-')
		return (-strtoull(nptr + 1, endptr, base));
	else
		return (strtoull(nptr, endptr, base));
}
void
swab(const void *from, void *to, ssize_t n)
{
	switch (n) {
	case 2:
		*(__u16 *)to = swab16(*(__u16 *)from);
		break;
	case 4:
		*(__u32 *)to = swab16(*(__u32 *)from);
		break;
	case 8:
		*(__u64 *)to = swab16(*(__u64 *)from);
		break;
	}
}
