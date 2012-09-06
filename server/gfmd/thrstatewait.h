struct gfarm_thr_statewait {
	pthread_mutex_t mutex;
	pthread_cond_t arrived;
	int arrival;
	gfarm_error_t result;
};

void gfarm_thr_statewait_initialize(struct gfarm_thr_statewait *,
	const char *);
gfarm_error_t gfarm_thr_statewait_wait(struct gfarm_thr_statewait *,
	const char *);
void gfarm_thr_statewait_signal(struct gfarm_thr_statewait *, gfarm_error_t,
	const char *);
void gfarm_thr_statewait_terminate(struct gfarm_thr_statewait *,
	const char *);
void gfarm_thr_statewait_reset(struct gfarm_thr_statewait *, const char *);
