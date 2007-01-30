#define GFS_LOCAL_FILE_BUFSIZE	16384

/* metadb_misc.c */
struct gfarm_file_section_info;
struct gfarm_file_section_copy_info;
char *gfarm_foreach_copy(
	char *(*)(struct gfarm_file_section_copy_info *, void *),
	const char *, const char *, void *, int *);
char *gfarm_foreach_section(
	char *(*)(struct gfarm_file_section_info *, void *),
	const char *, void *,
	char *(*)(struct gfarm_file_section_info *, void *));

#define GFS_DEFAULT_DIGEST_NAME	"md5"
#define GFS_DEFAULT_DIGEST_MODE	EVP_md5()

char *gfs_file_section_info_check_checksum_unknown(
	struct gfarm_file_section_info *);
char *gfs_file_section_info_check_busy(struct gfarm_file_section_info *);
char *gfs_file_section_check_busy(char *, char *);
char *gfs_file_section_set_checksum_unknown(char *, char *, file_offset_t);
char *gfs_file_section_set_busy(char *, char *, file_offset_t);

/* gfs_client_apply.c */
struct gfs_connection;
char *gfs_client_apply_all_hosts(
	char *(*)(struct gfs_connection *, void *), void *, char *, int,
	int *);

/* gfs_client_dir.c */
struct gfs_connection;
char *gfs_client_mk_parent_dir(struct gfs_connection *, char *);
char *gfs_client_link_faulttolerant(const char *, char *, char *,
	struct gfs_connection **, char **);

/* gfs_dir.c */
char *gfs_realpath_canonical(const char *, char **);
char *gfs_get_ino(const char *, unsigned long *);

/* url.c */
struct gfs_stat;
struct gfarm_path_info;
char *gfs_stat_access(struct gfs_stat *, int);
char *gfarm_path_info_access(struct gfarm_path_info *, int);
char *gfarm_path_expand_home(const char *, char **);
char *gfarm_path_dir(const char *);
char *gfarm_path_dirname(const char *);

/* gfs_chmod.c */
char *gfs_chmod_internal(struct gfarm_path_info *, gfarm_mode_t, char **);

/* gfs_unlink.c */
char *gfs_unlink_replica_internal(const char *, const char *, const char *);
char *gfs_unlink_check_perm(char *);
char *gfs_unlink_internal(const char *);
char *gfs_unlink_section_internal(const char *, const char *);
char *gfs_unlink_every_other_replicas(
	const char *, const char *, const char *);

/* gfs_stat.c */
char *gfs_stat_size_canonical_path(char *, file_offset_t *, int *);
char *gfs_stat_canonical_path(char *, struct gfs_stat *);

/* gfs_replicate.c */
char *gfarm_fabricate_mode_for_replication(struct gfs_stat *, gfarm_mode_t *);
char *gfarm_file_section_replicate_from_to_local_with_locking(
	struct gfarm_file_section_info *, gfarm_mode_t, char *, char *,
	char **);
char *gfarm_file_section_replicate_to_local_with_locking(
	struct gfarm_file_section_info *, gfarm_mode_t, char **);

/* gfs_pio_section.c */
void gfs_pio_unset_calc_digest(GFS_File);
char *gfarm_redirect_file(int, char *, GFS_File *);
