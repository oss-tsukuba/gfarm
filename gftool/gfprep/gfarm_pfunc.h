/*
 * $Id$
 */

typedef struct gfarm_pfunc gfarm_pfunc_t;
typedef struct gfarm_pfunc_cmd gfarm_pfunc_cmd_t;

gfarm_error_t gfarm_pfunc_start(gfarm_pfunc_t **, int, int, gfarm_int64_t,
				int, void (*)(void *), void (*)(int, void *));
gfarm_error_t gfarm_pfunc_cmd_add(gfarm_pfunc_t *, gfarm_pfunc_cmd_t *);
gfarm_error_t gfarm_pfunc_interrupt(gfarm_pfunc_t *);
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
