/*
 * $Id$
 */

struct mdcluster;
struct mdhost;

const char *mdcluster_get_name(struct mdcluster *);
struct mdcluster *mdcluster_lookup(const char *);
void mdcluster_init(void);
gfarm_error_t mdcluster_get_or_create_by_mdhost(struct mdhost *);
void mdcluster_remove_mdhost(struct mdhost *);
void mdcluster_foreach(int (*)(struct mdcluster *, void *), void *);
void mdcluster_foreach_mdhost(struct mdcluster *, int (*)(struct mdhost *,
	void *), void *);
