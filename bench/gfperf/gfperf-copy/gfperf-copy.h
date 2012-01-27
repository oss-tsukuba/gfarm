/*
 * $Id$
 */


/* $id */
#include <sys/time.h>

#ifndef _GFPERF_COPY_H_
#define _GFPERF_COPY_H_

#define LOCAL_TO_GFARM 1
#define GFARM_TO_LOCAL 2
#define COPY_BUF_SIZE (4*1024*1024)
#define DEFAULT_BUF_SIZE (64*1024)
#define DEFAULT_BUF_SIZE_STRING "64K"
#define UNKNOWN_FSTYPE 0x65735546

extern int direction;
extern char *file_size_string;
extern long long file_size;
extern char *buf_size_string;
extern size_t buf_size;
extern char *src_url;
extern char *src_filename;
extern char *dst_url;
extern char *dst_filename;
extern int posix_flag;
extern char *gfsd_hostname;
extern char *gfarm2fs_mount_point;

gfarm_error_t do_libgfarm_test();
gfarm_error_t do_posix_test();

#endif
