#ifndef _UNISTD_H_
#define _UNISTD_H_
#include <linux/kernel.h>
#include <linux/unistd.h>

# ifndef __socklen_t_defined
typedef int socklen_t;
#  define __socklen_t_defined
# endif

#define sysconf(x)	-1

#define getpid()	(current->pid)
#define getuid()	current_uid()
#define geteuid()	current_euid()

unsigned int sleep(unsigned int seconds);
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
ssize_t pread(int fd, void *buf, size_t count, off_t offset);
ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset);
int close(int fd);
off_t lseek(int fd, off_t offset, int whence);
int ftruncate(int fd, off_t length);
int fsync(int fd);
int fdatasync(int fd);

int gethostname(char *name, size_t len);

void swab(const void *from, void *to, ssize_t n);

#endif /* _UNISTD_H_ */

