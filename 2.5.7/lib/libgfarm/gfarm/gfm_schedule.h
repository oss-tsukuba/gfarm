struct gfarm_host_sched_info;

gfarm_error_t gfm_schedule_file(struct gfm_connection *, gfarm_int32_t,
	int *, struct gfarm_host_sched_info **);
