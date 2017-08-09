#include <stdlib.h>
#include "gfsk_fs.h"
#include <gfarm/gfarm_config.h>
#include <gfarm/gflog.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <linux/file.h>
#include <linux/mutex.h>

struct file;
#define FD_ARRAY_SIZE BITS_PER_LONG
struct gfsk_fdset {
	struct file *fd_farray[FD_ARRAY_SIZE];
};
struct gfsk_fdstruct {
	struct mutex fd_lock;
	int     fd_nextfd;
	int     fd_maxfd;
	int     fd_fdsetcnt;
	struct gfsk_fdset **fd_fdsetp;
	struct gfsk_fdset *fd_dflt_fdsetarray[1];
	struct gfsk_fdset fd_dflt_fdset;
};
#define GFSK_FDSET_LOCK(fdsp)	mutex_lock(&(fdsp)->fd_lock)
#define GFSK_FDSET_UNLOCK(fdsp)		mutex_unlock(&(fdsp)->fd_lock)

#define	GFSK_STP	(gfsk_fsp->gf_fdstructp)

int
gfsk_fdset_init(void)
{
	struct gfsk_fdstruct *fdsp;

	if (GFSK_STP) {
		gflog_error(GFARM_MSG_1004906, "already set gf_fdsetp");
		return (0);
	}
	if (!GFARM_MALLOC(fdsp)) {
		gflog_error(GFARM_MSG_1004907, "can't allocate gf_fdsetp");
		return (-ENOMEM);
	}
	mutex_init(&fdsp->fd_lock);
	fdsp->fd_nextfd = 0;
	fdsp->fd_maxfd = BITS_PER_LONG;
	fdsp->fd_fdsetcnt = 1;
	fdsp->fd_fdsetp = fdsp->fd_dflt_fdsetarray;
	fdsp->fd_dflt_fdsetarray[0] = &fdsp->fd_dflt_fdset;
	memset(&fdsp->fd_dflt_fdset, 0, sizeof(fdsp->fd_dflt_fdset));
	GFSK_STP = fdsp;
	return (0);
}

void
gfsk_fdset_fini(void)
{
	struct gfsk_fdstruct *fdsp = GFSK_STP;
	int a;

	if (!fdsp)
		return;
	if (fdsp->fd_fdsetcnt > 1) {
		for (a = 1; a < fdsp->fd_fdsetcnt; a++) {
			free(fdsp->fd_fdsetp[a]);
		}
		free(fdsp->fd_fdsetp);
	}
	mutex_destroy(&fdsp->fd_lock);
	free(fdsp);
	GFSK_STP = NULL;
}

void
gfsk_fdset_umount(void)
{
	struct gfsk_fdstruct *fdsp = GFSK_STP;
	int	a, i;

	if (!fdsp)
		return;
	for (a = 0; a < fdsp->fd_fdsetcnt; a++) {
		for (i = 0; i < FD_ARRAY_SIZE; i++) {
			struct file *file = fdsp->fd_fdsetp[a]->fd_farray[i];
			if (file) {
				fput(file);
				fdsp->fd_fdsetp[a]->fd_farray[i] = NULL;
			}
		}
	}
}

/*
 *  up file ref count
 */
int
gfsk_fd_file_set(struct file *file)
{
	int err = -ENOMEM;
	struct gfsk_fdstruct *fdsp = GFSK_STP;
	int	i, a, m, fd = -1;
	struct gfsk_fdset *fdp, **fdpp;

	GFSK_FDSET_LOCK(fdsp);
	i = fdsp->fd_nextfd;
	a = i / FD_ARRAY_SIZE;
	for (; a < fdsp->fd_fdsetcnt; a++) {
		fdp = fdsp->fd_fdsetp[a];
		m = (a+1) * FD_ARRAY_SIZE;
		for (; i < m; i++) {
			if (!fdp->fd_farray[i]) {
				goto ok;
			}
		}
		i = 0;
	}
	if (!GFARM_MALLOC_ARRAY(fdpp, fdsp->fd_fdsetcnt + 1)) {
		gflog_error(GFARM_MSG_1004908, "can't allocate fd_fdsetpp");
		goto ng;
	}
	memcpy(fdpp, fdsp->fd_fdsetp, sizeof(fdp) * a);
	if (fdsp->fd_fdsetp != fdsp->fd_dflt_fdsetarray) {
		free(fdsp->fd_fdsetp);
	}
	fdsp->fd_fdsetp = fdpp;
	if (!GFARM_MALLOC(fdp)) {
		gflog_error(GFARM_MSG_1004909, "can't allocate gfsk_fdset");
		goto ng;
	}
	memset(fdp, 0, sizeof(*fdp));
	fdsp->fd_fdsetp[a] = fdp;
	fdsp->fd_fdsetcnt = a + 1;
	fdsp->fd_maxfd = fdsp->fd_fdsetcnt * FD_ARRAY_SIZE;
ok:
	fdp->fd_farray[i] = file;
	get_file(file);
	fd = a * FD_ARRAY_SIZE + i;
	fdsp->fd_nextfd = fd + 1;
	err = fd;
ng:
	GFSK_FDSET_UNLOCK(fdsp);
	return (err);
}

/*
 *  dec file ref count
 */
int
gfsk_fd_unset(int fd)
{
	int err = 0;
	struct gfsk_fdstruct *fdsp = GFSK_STP;
	int	i, a;
	struct file *file = 0;

	GFSK_FDSET_LOCK(fdsp);
	if (fd  < fdsp->fd_maxfd) {
		a = fd / FD_ARRAY_SIZE;
		i = fd - a * FD_ARRAY_SIZE;
		file = fdsp->fd_fdsetp[a]->fd_farray[i];
		fdsp->fd_fdsetp[a]->fd_farray[i] = NULL;
		if (fdsp->fd_nextfd > fd)
			fdsp->fd_nextfd = fd;
	}
	GFSK_FDSET_UNLOCK(fdsp);
	if (!file)
		err = -EBADFD;
	else
		fput(file);

	return (err);
}

/*
 *  inc file ref count
 */
int
gfsk_fd2file(int fd, struct file **res)
{
	int err = 0;
	struct gfsk_fdstruct *fdsp = GFSK_STP;
	int	i, a;
	struct file *file = 0;

	GFSK_FDSET_LOCK(fdsp);
	if (fd >= 0 && fd  < fdsp->fd_maxfd) {
		a = fd / FD_ARRAY_SIZE;
		i = fd - a * FD_ARRAY_SIZE;
		file = fdsp->fd_fdsetp[a]->fd_farray[i];
		if (file) {
			get_file(file);
			*res = file;
		}
	}
	GFSK_FDSET_UNLOCK(fdsp);
	if (!file) {
		err = -EBADFD;
		*res = NULL;
	}

	return (err);
}
