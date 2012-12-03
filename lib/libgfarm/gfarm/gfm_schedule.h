struct gfarm_host_sched_info;
struct gfs_file;

gfarm_error_t gfm_schedule_file(struct gfs_file *, int *,
	struct gfarm_host_sched_info **);
