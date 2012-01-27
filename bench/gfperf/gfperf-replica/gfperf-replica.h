/*
 * $Id$
 */


/* $id */
#include <sys/time.h>

#ifndef _GFPERF_REPLICA_H_
#define _GFPERF_REPLICA_H_

#define LOCAL_TO_GFARM 1
#define GFARM_TO_LOCAL 2
#define COPY_BUF_SIZE (4*1024*1024)
#define DEFAULT_BUF_SIZE (64*1024)
#define DEFAULT_BUF_SIZE_STRING "64K"

extern char *from_gfsd_name;
extern char *to_gfsd_name;
extern char *testdir;
extern char *testdir_filename;
extern long long file_size;
extern char *file_size_string;
extern int loop_count;
extern int parallel_flag;
extern char *group_name;

gfarm_error_t do_test();

#endif
