gfarm_error_t gfs_pio_set_view_default(GFS_File);
#if 0 /* not yet in gfarm v2 */
gfarm_error_t gfs_pio_set_view_global(GFS_File, int);
#endif /* not yet in gfarm v2 */
char *gfs_pio_url(GFS_File);
struct gfs_connection;
gfarm_error_t gfs_pio_open_local_section(GFS_File, struct gfs_connection *);
gfarm_error_t gfs_pio_open_remote_section(GFS_File, struct gfs_connection *);
gfarm_error_t gfs_pio_internal_set_view_section(GFS_File, char *);
gfarm_error_t gfs_pio_reconnect(GFS_File);
gfarm_error_t gfs_pio_view_fd(GFS_File gf, int *fdp);
gfarm_error_t gfs_pio_create_igen(const char *url, int flags, gfarm_mode_t mode,
	GFS_File *gfp, gfarm_ino_t *inop, gfarm_uint64_t *genp);
gfarm_error_t gfs_pio_append(GFS_File gf, void *buffer, int size, int *np,
	gfarm_off_t *offp, gfarm_off_t *fsizep);
