struct gfs_pio_internal_cksum_info;

#ifdef GFARM_USE_GFS_PIO_INTERNAL_CKSUM_INFO
struct gfs_pio_internal_cksum_info {
	gfarm_off_t filesize;

	char *cksum_type;
	gfarm_int32_t cksum_get_flags, cksum_set_flags;
	/* the followings are available when GFS_PIO_MD_FLAG_DIGEST_FINISH */
	size_t cksum_len;
	char cksum[
	    EVP_MAX_MD_SIZE * 2 + 1 > GFM_PROTO_CKSUM_MAXLEN ?
	    EVP_MAX_MD_SIZE * 2 + 1 : GFM_PROTO_CKSUM_MAXLEN];
};
#endif

struct gfm_connection;
/* XXX should have metadata server as an argument */
gfarm_error_t gfm_create_fd(const char *, int, gfarm_mode_t,
	struct gfm_connection **, int *, int *,
	gfarm_ino_t *, gfarm_uint64_t *, char **,
	struct gfs_pio_internal_cksum_info *);
gfarm_error_t gfm_open_fd(const char *, int,
	struct gfm_connection **, int *, int *, char **,
	gfarm_ino_t *, gfarm_uint64_t *,
	struct gfs_pio_internal_cksum_info *);
gfarm_error_t gfm_fhopen_fd(gfarm_ino_t, gfarm_uint64_t, int,
	struct gfm_connection **, int *, int *,
	struct gfs_pio_internal_cksum_info *);
gfarm_error_t gfm_close_fd(struct gfm_connection *, int,
	struct gfs_pio_internal_cksum_info *);
