/*
 * $Id$
 */

typedef struct gfarm_pfunc gfarm_pfunc_t;
typedef struct gfarm_pfunc_cmd gfarm_pfunc_cmd_t;

enum pfunc_result {
	PFUNC_RESULT_OK,
	PFUNC_RESULT_NG,
	PFUNC_RESULT_SKIP,
	PFUNC_RESULT_BUSY_REMOVE_REPLICA,
	PFUNC_RESULT_END,
	PFUNC_RESULT_FATAL
};

gfarm_error_t gfarm_pfunc_init_fork(gfarm_pfunc_t **, int, int, gfarm_int64_t,
	int, int, void (*)(void *), void (*)(enum pfunc_result, void *),
	void (*)(void *));
gfarm_error_t gfarm_pfunc_start(gfarm_pfunc_t *);
gfarm_error_t gfarm_pfunc_cmd_add(gfarm_pfunc_t *, gfarm_pfunc_cmd_t *);
gfarm_error_t gfarm_pfunc_terminate(gfarm_pfunc_t *);
gfarm_error_t gfarm_pfunc_stop(gfarm_pfunc_t *);
gfarm_error_t gfarm_pfunc_join(gfarm_pfunc_t *);

gfarm_error_t gfarm_pfunc_replicate(
	gfarm_pfunc_t *, const char *, const char *, int, gfarm_off_t,
	const char *, int, void *, int, int);

gfarm_error_t gfarm_pfunc_remove_replica(
	gfarm_pfunc_t *, const char *,
	const char *, int, gfarm_off_t, void *);
gfarm_error_t gfarm_pfunc_copy(
	gfarm_pfunc_t *,
	const char *, const char *, int, gfarm_off_t,
	const char *, const char *, int, void *, int, int);
