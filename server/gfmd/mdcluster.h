/*
 * $Id$
 */

struct mdcluster;
struct mdhost;

const char *mdcluster_get_name(struct mdcluster *);
void mdcluster_init(void);
gfarm_error_t mdcluster_get_or_create_by_mdhost(struct mdhost *);
void mdcluster_remove_mdhost(struct mdhost *);
