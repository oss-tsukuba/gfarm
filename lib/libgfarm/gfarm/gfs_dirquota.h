/* the following should be moved to <gfarm/gfs.h>, perhaps? */
gfarm_error_t gfs_dirquota_add(const char *, const char *, const char *);
gfarm_error_t gfs_dirquota_get(const char *, struct gfarm_dirset_info *,
	struct gfarm_quota_limit_info *,
	struct gfarm_quota_subject_info *, struct gfarm_quota_subject_time *,
	gfarm_uint64_t *);
