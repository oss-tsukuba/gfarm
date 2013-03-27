#include <gfarm/gflog.h>
#include "gfsk.h"
#include "gfsk_fs.h"
#include "sys/socket.h"
#include <errno.h>
#include <net/sock.h>
#include <linux/sched.h>
#include <linux/syscalls.h>
#include <linux/slab.h>
#include <linux/personality.h> /* for STICKY_TIMEOUTS */
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/fs.h>
#include <linux/fsnotify.h>
#include <linux/net.h>
#include <linux/rcupdate.h>
#include <linux/hrtimer.h>


static inline int
gfsk_fd2sock(int fd, struct socket **sockp)
{
	int err;
	struct file *file;

	*sockp = NULL;
	if (!(err = gfsk_fd2file(fd, &file))) {
		if (!file->f_dentry || !file->f_dentry->d_inode) {
			err = -EBADF;
		} else if (!S_ISSOCK(file->f_dentry->d_inode->i_mode)) {
			err = -ENOTSOCK;
		} else {
			*sockp = SOCKET_I(file->f_dentry->d_inode);
		}
	}
	return (err);
}
static inline void
gfsk_sockput(struct socket *sock)
{
	if (sock && sock->file)
		fput(sock->file);
}
/*
 * Estimate expected accuracy in ns from a timeval.
 *
 * After quite a bit of churning around, we've settled on
 * a simple thing of taking 0.1% of the timeout as the
 * slack, with a cap of 100 msec.
 * "nice" tasks get a 0.5% slack instead.
 *
 * Consider this comment an open invitation to come up with even
 * better solutions..
 */

#define MAX_SLACK	(100 * NSEC_PER_MSEC)

static long __estimate_accuracy(struct timespec *tv)
{
	long slack;
	int divfactor = 1000;

	if (tv->tv_sec < 0)
		return (0);

	if (task_nice(current) > 0)
		divfactor = divfactor / 5;

	if (tv->tv_sec > MAX_SLACK / (NSEC_PER_SEC/divfactor))
		return (MAX_SLACK);

	slack = tv->tv_nsec / divfactor;
	slack += tv->tv_sec * (NSEC_PER_SEC/divfactor);

	if (slack > MAX_SLACK)
		return (MAX_SLACK);

	return (slack);
}

static long estimate_accuracy(struct timespec *tv)
{
	unsigned long ret;
	struct timespec now;

	/*
	* Realtime tasks get a slack of 0 for obvious reasons.
	*/

	if (rt_task(current))
		return (0);

	ktime_get_ts(&now);
	now = timespec_sub(*tv, now);
	ret = __estimate_accuracy(&now);
	if (ret < current->timer_slack_ns)
		return (current->timer_slack_ns);
	return (ret);
}

static inline unsigned int
do_pollfd(struct pollfd *pollfd, poll_table *pwait)
{
	unsigned int mask;
	int fd;
	struct file *file;

	fd = pollfd->fd;
	if (fd < 0) {
		mask = 0;
	} else if (gfsk_fd2file(fd, &file)) {
		mask = POLLNVAL;
	} else {
		mask = DEFAULT_POLLMASK;
		if (file->f_op && file->f_op->poll) {
			if (pwait)
				pwait->key = pollfd->events |
						POLLERR | POLLHUP;
			mask = file->f_op->poll(file, pwait);
		}
		/* Mask out unneeded events. */
		mask &= pollfd->events | POLLERR | POLLHUP;
		fput(file);
	}
	pollfd->revents = mask;

	return (mask);
}

int
poll(struct pollfd *pfds,  unsigned int nfds, int msec)
{
	struct timespec endtime , *end_time = &endtime;
	struct poll_wqueues wait;
	poll_table *pt = &wait.pt;
	ktime_t expire, *to = NULL;
	int timed_out = 0, count = 0;
	unsigned long slack = 0;
	struct pollfd *pfd, *pfd_end = pfds + nfds;

	poll_initwait(&wait);
	if (msec == 0) {
		end_time->tv_sec = end_time->tv_nsec = 0;
		pt = NULL;
		timed_out = 1;
	} else if (msec > 0) {
		long sec = msec / MSEC_PER_SEC;
		ktime_get_ts(end_time);
		end_time->tv_sec += sec;
		end_time->tv_nsec +=
			(msec - sec * MSEC_PER_SEC) * NSEC_PER_MSEC;
		if (end_time->tv_nsec > NSEC_PER_SEC) {
			end_time->tv_sec++;
			end_time->tv_nsec -= MSEC_PER_SEC;
		}
		slack = estimate_accuracy(end_time);
	} else {
		end_time = NULL;
	}


	for (;;) {
		for (pfd = pfds; pfd != pfd_end; pfd++) {
			if (do_pollfd(pfd, pt)) {
				count++;
				pt = NULL;
			}
		}
		/*
		 * All waiters have already been registered, so don't provide
		 * a poll_table to them on the next loop iteration.
		 */
		pt = NULL;
		if (!count) {
			count = wait.error;
			if (signal_pending(current))
				count = -EINTR;
		}
		if (count || timed_out)
			break;

		/*
		 * If this is the first loop and we have a timeout
		 * given, then we convert to ktime_t and set the to
		 * pointer to the expiry value.
		 */
		if (end_time && !to) {
			expire = timespec_to_ktime(*end_time);
			to = &expire;
		}

		if (!poll_schedule_timeout(&wait, TASK_INTERRUPTIBLE,
				to, slack))
			timed_out = 1;
	}
	poll_freewait(&wait);
	gkfs_syscall_ret(count);
	return (count);
}
#define POLLIN_SET (POLLRDNORM | POLLRDBAND | POLLIN | POLLHUP | POLLERR)
#define POLLOUT_SET (POLLWRBAND | POLLWRNORM | POLLOUT | POLLERR)
#define POLLEX_SET (POLLPRI)

int
select(int maxfds, fd_set *readfds, fd_set *writefds,
		  fd_set *exceptfds, struct timeval *timeout)
{
	int	err;
	int	i, j, nfds, set;
	int	msec;
#define MAXFD	32
	struct pollfd pfds[MAXFD];

	for (nfds = i = 0; i < maxfds; i++) {
		if ((readfds && FD_ISSET(i, readfds))
		|| (writefds && FD_ISSET(i, writefds))
		|| (exceptfds && FD_ISSET(i, exceptfds)))
			nfds++;
	}
	if (nfds > MAXFD) {
		gflog_error(GFARM_MSG_UNFIXED, "Too many fds %d", nfds);
		err = -ENOMEM;
		goto end_ret;
	}
	memset(pfds, 0, sizeof(pfds));
	for (j = i = 0; i < maxfds; i++) {
		set = 0;
		if (readfds && FD_ISSET(i, readfds)) {
			set = 1;
			pfds[j].events |= POLLIN_SET;
		}
		if (writefds && FD_ISSET(i, writefds)) {
			set = 1;
			pfds[j].events |= POLLOUT_SET;
		}
		if (exceptfds && FD_ISSET(i, exceptfds)) {
			set = 1;
			pfds[j].events |= POLLEX_SET;
		}
		if (set) {
			pfds[j].fd = i;
			j++;
		}
	}
	if (timeout) {
		msec = timeout->tv_sec * MSEC_PER_SEC +
				timeout->tv_usec / USEC_PER_MSEC;
	} else
		msec = -1;
	err = poll(pfds, nfds, msec);
	if (readfds)
		FD_ZERO(readfds);
	if (writefds)
		FD_ZERO(writefds);
	if (exceptfds)
		FD_ZERO(exceptfds);
	if (err > 0) {
		for (i = 0; i < nfds; i++) {
			short events;
			j = pfds[i].fd;
			if (readfds && ((events = pfds[i].events & POLLIN_SET)
				 & pfds[i].revents)) {
				FD_SET(j, readfds);
			}
			if (writefds && ((events = pfds[i].events & POLLOUT_SET)
				 & pfds[i].revents)) {
				FD_SET(j, writefds);
			}
			if (exceptfds && ((events = pfds[i].events & POLLEX_SET)
				 & pfds[i].revents)) {
				FD_SET(j, exceptfds);
			}
		}
	}
end_ret:
	gkfs_syscall_ret(err);
	return (err);
}

int
close(int fd)
{
	int err;
	struct file *file;

	if (!(err = gfsk_fd2file(fd, &file))) {
		gfsk_fd_unset(fd);
		fput(file);
	}
	gkfs_syscall_ret(err);
	return (err);
}
ssize_t
read(int fd, void *buf, size_t count)
{
	int err;
	struct file *file;

	if (!(err = gfsk_fd2file(fd, &file))) {
		GFSK_CTX_DECLARE_SAVE;
		mm_segment_t oldfs = get_fs();
		loff_t	pos;

		GFSK_CTX_CLEAR();
		pos = file->f_pos;
		set_fs(KERNEL_DS);
		if (file->f_op->read)
			err = file->f_op->read(file, buf, count, &pos);
		else
			err = do_sync_read(file, buf, count, &pos);
		if (err > 0) {
			fsnotify_access(file->f_path.dentry);
			add_rchar(current, err);
			file->f_pos = pos;
		}
		set_fs(oldfs);
		fput(file);
		GFSK_CTX_RESTORE();
	}
	gkfs_syscall_ret(err);
	return (err);
}

ssize_t
write(int fd, void *buf, size_t count)
{
	int err;
	struct file *file;

	if (!(err = gfsk_fd2file(fd, &file))) {
		GFSK_CTX_DECLARE_SAVE;
		GFSK_CTX_CLEAR();
		if (!(file->f_mode & FMODE_WRITE)) {
			err = -EBADF;
		} else {
			mm_segment_t oldfs = get_fs();
			loff_t	pos;
			pos = file->f_pos;
			set_fs(KERNEL_DS);
			if (file->f_op->write)
				err = file->f_op->write(file, buf, count, &pos);
			else
				err = do_sync_write(file, buf, count, &pos);
			if (err > 0) {
				fsnotify_modify(file->f_path.dentry);
				add_wchar(current, err);
				file->f_pos = pos;
			}
			set_fs(oldfs);

		}
		fput(file);
		GFSK_CTX_RESTORE();
	}
	gkfs_syscall_ret(err);
	return (err);
}
ssize_t
pread(int fd, void *buf, size_t count, off_t offset)
{
	int err;
	struct file *file;

	if (!(err = gfsk_fd2file(fd, &file))) {
		GFSK_CTX_DECLARE_SAVE;
		mm_segment_t oldfs = get_fs();
		loff_t	pos = offset;
		GFSK_CTX_CLEAR();
		set_fs(KERNEL_DS);
		if (file->f_op->read)
			err = file->f_op->read(file, buf, count, &pos);
		else
			err = do_sync_read(file, buf, count, &pos);
		if (err > 0) {
			fsnotify_modify(file->f_path.dentry);
			add_wchar(current, err);
		}
		set_fs(oldfs);
		fput(file);
		GFSK_CTX_RESTORE();
	}
	gkfs_syscall_ret(err);
	return (err);
}
ssize_t
pwrite(int fd, void *buf, size_t count, off_t offset)
{
	int err;
	struct file *file;

	if (!(err = gfsk_fd2file(fd, &file))) {
		GFSK_CTX_DECLARE_SAVE;
		GFSK_CTX_CLEAR();
		if (!(file->f_mode & FMODE_WRITE)) {
			err = -EBADF;
		} else {
			mm_segment_t oldfs = get_fs();
			loff_t	pos = offset;
			set_fs(KERNEL_DS);
			if (file->f_op->write)
				err = file->f_op->write(file, buf, count, &pos);
			else
				err = do_sync_write(file, buf, count, &pos);
			if (err > 0) {
				fsnotify_modify(file->f_path.dentry);
				add_wchar(current, err);
			}
			set_fs(oldfs);

		}
		fput(file);
		GFSK_CTX_RESTORE();
	}
	gkfs_syscall_ret(err);
	return (err);
}
off_t
lseek(int fd, off_t offset, int whence)
{
	int err;
	struct file *file;

	if (!(err = gfsk_fd2file(fd, &file))) {
		GFSK_CTX_DECLARE_SAVE;
		GFSK_CTX_CLEAR();
		err = vfs_llseek(file, offset, whence);
		fput(file);
		GFSK_CTX_RESTORE();
	}
	gkfs_syscall_ret(err);
	return (err);
}
int
fdatasync(int fd)
{
	int err;
	struct file *file;

	if (!(err = gfsk_fd2file(fd, &file))) {
		GFSK_CTX_DECLARE_SAVE;
		GFSK_CTX_CLEAR();
		err = vfs_fsync(file, file->f_path.dentry, 1);
		fput(file);
		GFSK_CTX_RESTORE();
	}
	gkfs_syscall_ret(err);
	return (err);
}

int
fsync(int fd)
{
	int err;
	struct file *file;

	if (!(err = gfsk_fd2file(fd, &file))) {
		GFSK_CTX_DECLARE_SAVE;
		GFSK_CTX_CLEAR();
		err = vfs_fsync(file, file->f_path.dentry, 0);
		fput(file);
		GFSK_CTX_RESTORE();
	}
	gkfs_syscall_ret(err);
	return (err);
}
int
fstat(int fd, struct stat *buf)
{
	int err;
	struct file *file;

	if (!(err = gfsk_fd2file(fd, &file))) {
		struct kstat stat;
		GFSK_CTX_DECLARE_SAVE;
		GFSK_CTX_CLEAR();
		err = vfs_getattr(file->f_path.mnt, file->f_path.dentry, &stat);
		fput(file);
		GFSK_CTX_RESTORE();
		if (!err) {
			memset(buf, 0, sizeof(*buf));
			buf->st_ino = stat.ino;
			buf->st_mode = stat.mode;
			buf->st_nlink = stat.nlink;
			buf->st_size = stat.size;
			buf->st_atime = stat.atime.tv_sec;
			buf->st_mtime = stat.mtime.tv_sec;
			buf->st_ctime = stat.ctime.tv_sec;
			buf->st_atime_nsec = stat.atime.tv_nsec;
			buf->st_mtime_nsec = stat.mtime.tv_nsec;
			buf->st_ctime_nsec = stat.ctime.tv_nsec;
			buf->st_blocks = stat.blocks;
			buf->st_blksize = stat.blksize;
		}
	}
	gkfs_syscall_ret(err);
	return (err);
}
int
ftruncate(int fd, off_t length)
{
	int err;
	struct file *file;

	if (!(err = gfsk_fd2file(fd, &file))) {
		struct iattr newattrs;
		struct dentry *dentry = file->f_path.dentry;
		GFSK_CTX_DECLARE_SAVE;
		GFSK_CTX_CLEAR();
		GFSK_CTX_RESTORE();

		newattrs.ia_size = length;
		newattrs.ia_file = file;
		newattrs.ia_valid = ATTR_SIZE|ATTR_MTIME|ATTR_CTIME|ATTR_FILE;
		mutex_lock(&dentry->d_inode->i_mutex);
		err = notify_change(dentry, &newattrs);
		mutex_unlock(&dentry->d_inode->i_mutex);
		fput(file);
		GFSK_CTX_RESTORE();
	}
	gkfs_syscall_ret(err);
	return (err);
}
static void
gfsk_set_close_on_exec(int fd, int on)
{
	struct files_struct *files = current->files;
	struct fdtable *fdt;

	spin_lock(&files->file_lock);
	fdt = files_fdtable(files);
	if (on)
		FD_SET(fd, fdt->close_on_exec);
	else
		FD_CLR(fd, fdt->close_on_exec);
	spin_unlock(&files->file_lock);
}
int
fcntl(int fd, int cmd, long arg)
{
#define SETFL_MASK (O_APPEND | O_NONBLOCK | O_NDELAY | O_DIRECT | O_NOATIME)
	int err;
	struct file *file;

	if (!(err = gfsk_fd2file(fd, &file))) {
		switch (cmd) {
		case F_SETFD:
			gfsk_set_close_on_exec(fd, arg & FD_CLOEXEC);
			break;
		case F_GETFL:
			err = file->f_flags;
			break;
		case F_SETFL:
			spin_lock(&file->f_lock);
			file->f_flags = (arg & SETFL_MASK)
					| (file->f_flags & ~SETFL_MASK);
			spin_unlock(&file->f_lock);
			break;
		default:
			err = -EINVAL;
			break;
		}
		fput(file);
	}
	gkfs_syscall_ret(err);
	return (err);
}

/*-------------------------------------------------------------------------*/
int
socket(int domain, int type, int protocol)
{
	int err;
	struct socket *sock;
	int	flags;

	flags = type & (O_CLOEXEC | O_NONBLOCK);
	if ((err = sock_create_kern(domain, type & SOCK_TYPE_MASK,
		protocol, &sock)))
		;
	else if ((err = sock_map_fd(sock, flags)) < 0)
		sock_release(sock);
	else {
		struct file *file;
		int fd = err;

		file = fget(fd);
		err = gfsk_fd_file_set(file);
		fput(file);
		sys_close(fd);
	}
	gkfs_syscall_ret(err);
	return (err);
}

int
bind(int fd, const struct sockaddr *addr, socklen_t addrlen)
{
	int	err;
	struct socket *sock;

	if (!(err = gfsk_fd2sock(fd, &sock))) {
		err = kernel_bind(sock, (struct sockaddr *)addr, addrlen);
		gfsk_sockput(sock);
	}
	gkfs_syscall_ret(err);
	return (err);
}

int
connect(int fd, const struct sockaddr *addr, socklen_t addrlen)
{
	int	err;
	struct socket *sock;

	if (!(err = gfsk_fd2sock(fd, &sock))) {
		err = kernel_connect(sock, (struct sockaddr *)addr, addrlen, 0);
		gfsk_sockput(sock);
	}
	gkfs_syscall_ret(err);
	return (err);
}

ssize_t
send(int fd, const void *buf, size_t len, int flags)
{
	return (sendto(fd, buf, len, flags, NULL, 0));
}
ssize_t
sendto(int fd, const void *buf, size_t len, int flags,
	const struct sockaddr *addr, socklen_t addrlen)
{
	int err;
	struct socket *sock;

	if (!(err = gfsk_fd2sock(fd, &sock))) {
		struct msghdr msg = { .msg_flags = flags,
				.msg_name = (void *)addr,
				.msg_namelen = addrlen };
		struct kvec vec = { .iov_base = (void *)buf, .iov_len = len};

		if (sock->file->f_flags & O_NONBLOCK)
			msg.msg_flags |= MSG_DONTWAIT;
		err = kernel_sendmsg(sock, &msg, &vec, 1, len);
		gfsk_sockput(sock);
	}
	gkfs_syscall_ret(err);
	return (err);
}

ssize_t
recvfrom(int fd, void *buf, size_t len, int flags,
	struct sockaddr *addr, socklen_t *addrlen)
{
	int err;
	struct socket *sock;

	if (!(err = gfsk_fd2sock(fd, &sock))) {
		struct msghdr msg = { .msg_flags = flags,
				.msg_name = addr,
				.msg_namelen = addrlen ? *addrlen : 0 };
		struct kvec vec = { .iov_base = (void *)buf, .iov_len = len};

		if (sock->file->f_flags & O_NONBLOCK)
			msg.msg_flags |= MSG_DONTWAIT;
		err = kernel_recvmsg(sock, &msg, &vec, 1, len, msg.msg_flags);
		if (addrlen)
			*addrlen = msg.msg_namelen;
		gfsk_sockput(sock);
	}
	gkfs_syscall_ret(err);
	return (err);
}
ssize_t
recvmsg(int fd, struct msghdr *msg, int flags)
{
	int err;
	struct socket *sock;

	if (!(err = gfsk_fd2sock(fd, &sock))) {
		size_t len;

		len = iov_length(msg->msg_iov, msg->msg_iovlen);
		if (sock->file->f_flags & O_NONBLOCK)
			flags |= MSG_DONTWAIT;
		err = kernel_recvmsg(sock, msg, (struct kvec *)msg->msg_iov,
			msg->msg_iovlen, len, flags);
		gfsk_sockput(sock);
	}
	gkfs_syscall_ret(err);
	return (err);
}

int
getsockopt(int fd, int level, int optname, void *optval, socklen_t *optlen)
{
	int err;
	struct socket *sock;

	if (!(err = gfsk_fd2sock(fd, &sock))) {
		err = kernel_getsockopt(sock, level, optname, optval, optlen);
		gfsk_sockput(sock);
	}
	gkfs_syscall_ret(err);
	return (err);
}

int
setsockopt(int fd, int level, int optname, const void *optval, socklen_t optlen)
{
	int err;
	struct socket *sock;

	if (!(err = gfsk_fd2sock(fd, &sock))) {
		err = kernel_setsockopt(sock, level, optname,
			(char *)optval, optlen);
		gfsk_sockput(sock);
	}
	gkfs_syscall_ret(err);
	return (err);
}

int
getsockname(int fd, struct sockaddr *addr, socklen_t *addrlen)
{
	int err;
	struct socket *sock;

	if (!(err = gfsk_fd2sock(fd, &sock))) {
		err = kernel_getsockname(sock, addr, addrlen);
		gfsk_sockput(sock);
	}
	gkfs_syscall_ret(err);
	return (err);
}
int
getpeername(int fd, struct sockaddr *addr, socklen_t *addrlen)
{
	int err;
	struct socket *sock;

	if (!(err = gfsk_fd2sock(fd, &sock))) {
		err = kernel_getpeername(sock, addr, addrlen);
		gfsk_sockput(sock);
	}
	gkfs_syscall_ret(err);
	return (err);
}
