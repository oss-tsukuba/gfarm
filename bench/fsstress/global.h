/*
 * Copyright (c) 2000 Silicon Graphics, Inc.  All Rights Reserved.
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * 
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 * 
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 * 
 * http://www.sgi.com 
 * 
 * For further information regarding this notice, see: 
 * 
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */
 
#ifndef GLOBAL_H
#define GLOBAL_H

/* xfs-specific includes */

#if defined(NO_XFS)
# include "xfscompat.h"
#else
# include <libxfs.h>
# include <attributes.h>
#endif

/* libc includes */

#ifdef __FreeBSD__
#include <sys/types.h>
#include <sys/param.h>
#endif
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <string.h>

#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__CYGWIN__)
typedef off_t	off64_t;
#define stat64	stat
#define lseek64	lseek
#define lstat64	lstat
#define fstat64	fstat
#define ftruncate64	ftruncate
#define truncate64	truncate
#define readdir64	readdir

static __inline
void *
memalign(int blksize, int bytes)
{
    void *ptr;
    int blkmask;
    static int pagesize;

    if (pagesize == 0)
	pagesize = getpagesize();
    if (blksize < pagesize)
	blksize = pagesize;
    blkmask = blksize - 1;
    ptr = malloc((bytes + blkmask) & ~blkmask);
    bzero(ptr, bytes);
    return(ptr);
}

#endif /* defined(__FreeBSD__) || defined(__NetBSD__) || defined(__CYGWIN__) */

#ifdef __FreeBSD__
#define fdatasync	fsync
typedef long	ptrdiff_t;
#endif

#endif /* GLOBAL_H */
