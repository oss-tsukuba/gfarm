extern int gf_profile;
#define gfs_profile(x) if (gf_profile) { x; }

extern double gfs_pio_set_view_section_time;
extern double gfs_stat_time;
extern double gfs_unlink_time;

void gfs_display_timers(void);
