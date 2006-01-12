/* xstat64 using old-style Unix stat system call.
   Copyright (C) 1991,95,96,97,98,99,2000,2001 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the GNU C Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#include <errno.h>
#include <stddef.h>
#include <unistd.h>

#ifndef __USE_LARGEFILE64
#define __USE_LARGEFILE64
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include "kernel_stat.h"

#include <sys/syscall.h>

#include "hooks_subr.h"

#if defined(__i386__)
#define NEEDS_XSTAT64_CONV
#endif

#ifdef NEEDS_XSTAT64_CONV
#include "xstatconv.c"
#endif

/* Get information about the file NAME in BUF.  */

int
gfs_hook_syscall_xstat64 (int vers, const char *name, struct stat64 *buf)
{
#ifdef NEEDS_XSTAT64_CONV
  int result;

# if defined SYS_stat64
  static int no_stat64 = 0;

  if (! no_stat64)
    {
      int saved_errno = errno;
      result = syscall (SYS_stat64, name, buf);

      if (result != -1 || errno != ENOSYS)
	{
#  if defined _HAVE_STAT64___ST_INO && __ASSUME_ST_INO_64_BIT == 0
	  if (result != -1 && buf->__st_ino != (__ino_t) buf->st_ino)
	    buf->st_ino = buf->__st_ino;
#  endif
	  return result;
	}

      errno = saved_errno;
      no_stat64 = 1;
    }
# endif

  {
    struct kernel_stat kbuf;

    result = syscall (SYS_stat, name, &kbuf);
    if (result == 0)
      result = xstat64_conv (vers, &kbuf, buf);
  }

  return result;
#else /* NEEDS_XSTAT64_CONV */
  return syscall (SYS_stat, name, buf);
#endif
}
