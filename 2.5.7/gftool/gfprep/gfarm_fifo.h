/*
 * $Id$
 */

typedef struct gfarm_fifo gfarm_fifo_t;

gfarm_error_t gfarm_fifo_init(gfarm_fifo_t **, size_t, size_t,
			      void (*)(void *, int, void *),
			      void (*)(void *, int, void *));
gfarm_error_t gfarm_fifo_wait_to_finish(gfarm_fifo_t *);
void gfarm_fifo_free(gfarm_fifo_t *);
gfarm_error_t gfarm_fifo_enter(gfarm_fifo_t *, void *);
int gfarm_fifo_can_get(gfarm_fifo_t *);
gfarm_error_t gfarm_fifo_checknext(gfarm_fifo_t *, void *);
gfarm_error_t gfarm_fifo_pending(gfarm_fifo_t *);
gfarm_error_t gfarm_fifo_delete(gfarm_fifo_t *, void *);
