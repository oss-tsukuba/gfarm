/*
 * $Id$
 */

char *gfarm_foreach_directory_hierarchy(
	char *(*)(char *, struct gfs_stat *, void *),
	char *(*)(char *, struct gfs_stat *, void *),
	char *(*)(char *, struct gfs_stat *, void *),
	char *, void *);

char *gfarm_foreach_directory_add_file(char *, struct gfs_stat *, void *);
