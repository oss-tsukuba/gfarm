/*
 * $Id$
 */

gfarm_error_t gfarm_foreach_directory_hierarchy(
	gfarm_error_t (*)(char *, struct gfs_stat *, void *),
	gfarm_error_t (*)(char *, struct gfs_stat *, void *),
	gfarm_error_t (*)(char *, struct gfs_stat *, void *),
	char *, void *);
