/*
 * $Id$
 */

struct gfarm_timespec;

int gfarm_timespec_cmp(
	const struct gfarm_timespec *, const struct gfarm_timespec *);
void gfarm_timespec_add(struct gfarm_timespec *, const struct gfarm_timespec *);
void gfarm_timespec_sub(struct gfarm_timespec *, const struct gfarm_timespec *);
void gfarm_timespec_add_nanosec(struct gfarm_timespec *, long);
