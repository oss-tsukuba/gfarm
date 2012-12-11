/*
 * Copyright (c) 2003-2006 National Institute of Advanced
 * Industrial Science and Technology (AIST).  All rights reserved.
 *
 * Copyright (c) 2006 National Institute of Informatics in Japan,
 * All rights reserved.
 *
 * This file or a portion of this file is licensed under the terms of
 * the NAREGI Public License, found at
 * http://www.naregi.org/download/index.html.
 * If you redistribute this file, with or without modifications, you
 * must include this notice in the file.
 */

struct gfm_connection;
struct gfs_connection;
struct gfarm_host_sched_info;
struct gfs_file; /* GFS_File */

gfarm_error_t gfarm_schedule_select_host(struct gfm_connection *,
	int, struct gfarm_host_sched_info *, int, char **, int *);
gfarm_error_t gfarm_schedule_host_cache_purge(struct gfs_connection *);
void gfarm_schedule_host_cache_reset(struct gfm_connection *, int,
	struct gfarm_host_sched_info *);
/* XXX - defined in gfs_pio_section.c */
gfarm_error_t gfarm_schedule_file(struct gfs_file *, char **, gfarm_int32_t *);

gfarm_uint64_t gfarm_schedule_host_used(const char *, int, const char *);
void gfarm_schedule_host_unused(const char *, int, const char *, gfarm_uint64_t);

int gfm_host_is_in_local_net(struct gfm_connection *, const char *);

#if 0 /* not yet in gfarm v2 */

int gfarm_is_active_fsnode(void);
int gfarm_is_active_fsnode_to_write(file_offset_t);

#endif
