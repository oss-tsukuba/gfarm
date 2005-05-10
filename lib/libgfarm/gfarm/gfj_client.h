/*
 * $Id$
 */

struct gfarm_job_info {
	int total_nodes;
	char *user;
	char *job_type;
	char *originate_host;
	char *gfarm_url_for_scheduling;
	int argc;
	char **argv;

	/* per node information */
	struct gfarm_job_node_info {
		char *hostname;
		int pid;
		enum gfarm_job_node_state {
			GFJ_NODE_NONE
		} state;
	} *nodes;
};

void gfarm_job_info_clear(struct gfarm_job_info *, int);
void gfarm_job_info_free_contents(struct gfarm_job_info *, int);

struct gfm_connection;

gfarm_error_t gfj_initialize(void);

gfarm_error_t gfj_client_lock_register(struct gfm_connection *);
gfarm_error_t gfj_client_unlock_register(struct gfm_connection *);
gfarm_error_t gfj_client_register(struct gfm_connection *,
			  struct gfarm_job_info *, int, int *job_idp);
gfarm_error_t gfj_client_unregister(struct gfm_connection *, int);
gfarm_error_t gfj_client_list(struct gfm_connection *, char *, int *, int **);
gfarm_error_t gfj_client_info(struct gfm_connection *, int, int *,
		      struct gfarm_job_info *);

/* convenience function */
gfarm_error_t gfarm_user_job_register(struct gfm_connection *,
	int, char **, char *, char *, int, char **, int *);
