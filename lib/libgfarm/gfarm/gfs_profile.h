#ifndef __KERNEL__
#define gfs_profile(x) if (gfarm_ctxp->profile) { x; }
#else /* __KERNEL__ */
#define gfs_profile(x) 
#endif /* __KERNEL__ */

void gfs_profile_set(void);
void gfs_profile_unset(void);

/* profile related subroutines: called from gfs_pio_display() */
void gfs_pio_display_timers(void);
void gfs_pio_section_display_timers(void);
void gfs_stat_display_timers(void);
void gfs_unlink_display_timers(void);
void gfs_xattr_display_timers(void);
