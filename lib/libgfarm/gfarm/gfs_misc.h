/* gfs_dir.c */
char *gfs_realpath_canonical(const char *, char **);
char *gfs_get_ino(const char *, long *);

/* url.c */
struct gfs_stat;
struct gfarm_path_info;
char *gfs_stat_access(struct gfs_stat *, int);
char *gfarm_path_info_access(struct gfarm_path_info *, int);
char *gfarm_path_expand_home(const char *, char **);

/* gfs_unlink.c */
char *gfs_unlink_replica_internal(const char *, const char *, const char *);

/* gfs_pio_misc.c */
char *gfs_stat_size_canonical_path(char *, file_offset_t *, int *);
char *gfs_stat_canonical_path(char *, struct gfs_stat *);

char *gfarm_fabricate_mode_for_replication(struct gfs_stat *, gfarm_mode_t *);
char *gfarm_file_section_replicate_from_to_local_with_locking(
	struct gfarm_file_section_info *, gfarm_mode_t, char *, char *,
	char **);
char *gfarm_file_section_replicate_to_local_with_locking(
	struct gfarm_file_section_info *, gfarm_mode_t, char **);
