/*
 * $Id$
 *
 * The reason that this source file is separated from hooks_64.c is that
 * llseek/lseek64 needs special syscall() prototype to return 64bit value.
 */

#define syscall syscall_original /* avoid "int syscall()" function prototype */

#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>

#undef syscall

#include "hooks_subr.h"

off64_t syscall();

/*
 * XXX - not really tested.
 */

off64_t
gfs_hook_syscall_lseek64(int filedes, off64_t offset, int whence)
{
#if defined(SYS_lseek64)
	return (syscall(SYS_lseek64, filedes, offset, whence));
#elif defined(SYS_llseek)
	return (syscall(SYS_llseek, filedes, (int)(offset >> 32), (int)offset,
	    whence));
#elif defined(SYS__llseek) /* linux */
	int rv;
	off64_t result;

	rv = syscall(SYS__llseek, filedes, (int)(offset >> 32), (int)offset,
	    &result, whence);
	return (rv ? rv : result);
#else
#error do not know how to implement lseek64
#endif
}
