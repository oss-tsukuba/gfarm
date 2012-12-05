/*
 * $Id$
 */

struct peer;
gfarm_error_t gfm_server_fsngroup_get_all(
	struct peer *, int, int);
gfarm_error_t gfm_server_fsngroup_get_by_names(
	struct peer *, int, int);
gfarm_error_t gfm_server_fsngroup_modify(
	struct peer *, int, int);

#define macro_stringify(X)	stringify(X)
#define stringify(X)	#X
