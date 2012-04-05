/*
 * $Id$
 */

struct gfs_file_list;

/* gfs_pio.c */
struct gfs_file_list *gfs_pio_file_list_alloc(void);
void gfs_pio_file_list_free(struct gfs_file_list *);
void gfs_pio_file_list_add(struct gfs_file_list *, GFS_File);
void gfs_pio_file_list_remove(struct gfs_file_list *, GFS_File);
void gfs_pio_file_list_foreach(struct gfs_file_list *,
	int (*)(struct gfs_file *, void *), void *);
