/* xstat using old-style Unix stat system call.
   Copyright (C) 1991,95,96,97,98,2000 Free Software Foundation, Inc.
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stddef.h>
#include <sys/stat.h>
#include "kernel_stat.h"

#include <sys/syscall.h>

#define NEEDS_XSTAT_CONV
#include "xstatconv.c"

/* Get information about the file NAME in BUF.  */
int
gfs_hook_syscall_xstat (int vers, const char *name, struct stat *buf)
{
  struct kernel_stat kbuf;
  int result;

  if (vers == _STAT_VER_KERNEL)
    return syscall (SYS_stat, name, (struct kernel_stat *) buf);

  result = syscall (SYS_stat, name, &kbuf);
  if (result == 0)
    result = xstat_conv (vers, &kbuf, buf);

  return result;
}
