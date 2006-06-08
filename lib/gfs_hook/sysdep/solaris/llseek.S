/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved	*/


/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"@(#)llseek.s	1.7	05/06/08 SMI"

	.file	"llseek.s"

/*
 * C library -- llseek
 * offset_t llseek(int fildes, offset_t offset, int whence);
 */

#include <sys/asm_linkage.h>

#if 0
	ANSI_PRAGMA_WEAK(llseek,function)
#endif

#define __cerror	_cerror
#define __cerror64	_cerror64

#if __STDC__
# define CONCAT(x,y)	x ## y
#else
# define CONCAT(x,y)	x/**/y
#endif

#if 0
#include "SYS.h"
#elif defined(sparc)
#include "sparc/SYS.h"
#elif defined(sparcv9)
#include "sparcv9/SYS.h"
#elif defined(i386)
#include "i386/SYS.h"
#elif defined(amd64) || defined(x86_64)
#include "amd64/SYS.h"
#endif

#if CLONGSIZE == 8	/* from sys/asm_linkage.h */
	/*
	 * For _LP64, llseek() is the same as lseek().
	 */
	SYSCALL2_RVAL1(gfs_hook_syscall_lseek64,lseek)
#else
#  if 0
	SYSCALL64(llseek)
#  else
        ENTRY(gfs_hook_syscall_lseek64)
        SYSTRAP_64RVAL(llseek)
        SYSCERROR64
#  endif
#endif
	RET
	SET_SIZE(gfs_hook_syscall_lseek64)
