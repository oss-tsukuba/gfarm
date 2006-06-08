/*
 * $Id$
 *
 * The reason that this source file is separated from hooks_64.c is that
 * llseek/lseek64 needs special syscall() prototype to return 64bit value.
 */

#if defined(sun) && (defined(__svr4__) || defined(__SVR4))
#define OS_SOLARIS	1
#endif

#ifndef OS_SOLARIS
/* Solaris version is defined in sysdep/solaris/llseek.S */

#define syscall syscall_original /* avoid "int syscall()" function prototype */

#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>

#undef syscall

#include "hooks_subr.h"
#include <gfarm/gfarm_config.h>

off64_t syscall();

/*
 * XXX - not really tested.
 */

off64_t
gfs_hook_syscall_lseek64(int filedes, off64_t offset, int whence)
{
#if defined(SYS_lseek64)
	return (syscall(SYS_lseek64, filedes, offset, whence));
#elif defined(SYS__llseek) /* linux */
	int rv;
	off64_t result;

	rv = syscall(SYS__llseek, filedes, (int)(offset >> 32), (int)offset,
	    &result, whence);
	return (rv ? rv : result);
#elif defined(__linux__) && SIZEOF_LONG == 8 && defined(__NR_lseek)
	return (syscall(__NR_lseek, filedes, offset, whence));
#else
#error do not know how to implement lseek64
#endif
}

#endif /* !OS_SOLARIS */
