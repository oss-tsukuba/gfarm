#ifndef __KERNEL__
#define gfs_profile(x) if (gfarm_ctxp->profile) { x; }
#define GFARM_KERNEL_UNUSE(x)
#define GFARM_KERNEL_UNUSE2(t1, t2)
#define GFARM_KERNEL_UNUSE3(t1, t2, t3)
#else /* __KERNEL__ */	/* gfs_profile :: never support */
#define gfs_profile(x)
#define GFARM_KERNEL_UNUSE(x)	(void)(x)
#define GFARM_KERNEL_UNUSE2(t1, t2)	((void)(t1), (void)(t2))
#define GFARM_KERNEL_UNUSE3(t1, t2, t3)	((void)(t1), (void)(t2), (void)(t3))
#endif /* __KERNEL__ */

void gfs_profile_set(void);
void gfs_profile_unset(void);

/* profile related subroutines: called from gfs_pio_display() */
struct gfs_profile_list {
	char *name, *format, *format_value, type;
	size_t offset;
};

void gfs_profile_display_timers(int, struct gfs_profile_list[], void *);
gfarm_error_t gfs_profile_value(const char *, int, struct gfs_profile_list[],
	void *, char *, size_t *);

void gfs_pio_display_timers(void);
void gfs_pio_section_display_timers(void);
void gfs_pio_local_display_timers(void);
void gfs_pio_remote_display_timers(void);
void gfs_stat_display_timers(void);
void gfs_unlink_display_timers(void);
void gfs_xattr_display_timers(void);

gfarm_error_t gfs_pio_profile_value(const char *, char *, size_t *);
gfarm_error_t gfs_pio_section_profile_value(const char *, char *, size_t *);
gfarm_error_t gfs_pio_local_profile_value(const char *, char *, size_t *);
gfarm_error_t gfs_pio_remote_profile_value(const char *, char *, size_t *);
gfarm_error_t gfs_stat_profile_value(const char *, char *, size_t *);
gfarm_error_t gfs_unlink_profile_value(const char *, char *, size_t *);
gfarm_error_t gfs_xattr_profile_value(const char *, char *, size_t *);
