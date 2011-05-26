/*
 * $Id$
 */

struct gfarm_metadb_server {
	char *name;
	char *clustername;
	int port;
	int flags;
	/* tflags is not stored in db */
	int tflags;
};

gfarm_error_t gfarm_metadb_server_new(struct gfarm_metadb_server **);
const char * gfarm_metadb_server_get_name(struct gfarm_metadb_server *);
void gfarm_metadb_server_set_name(struct gfarm_metadb_server *, char *);
int gfarm_metadb_server_get_port(struct gfarm_metadb_server *);
void gfarm_metadb_server_set_port(struct gfarm_metadb_server *, int);
int gfarm_metadb_server_is_master(struct gfarm_metadb_server *);
void gfarm_metadb_server_set_is_master(struct gfarm_metadb_server *, int);
int gfarm_metadb_server_is_self(struct gfarm_metadb_server *);
void gfarm_metadb_server_set_is_self(struct gfarm_metadb_server *, int);
int gfarm_metadb_server_is_sync_replication(struct gfarm_metadb_server *);
void gfarm_metadb_server_set_is_sync_replication(struct gfarm_metadb_server *,
	int);
void gfarm_metadb_server_free(struct gfarm_metadb_server *);
int gfarm_metadb_server_is_default_master(struct gfarm_metadb_server *);
void gfarm_metadb_server_set_is_default_master(struct gfarm_metadb_server *,
	int);
int gfarm_metadb_server_is_master_candidate(struct gfarm_metadb_server *);
void gfarm_metadb_server_set_is_master_candidate(struct gfarm_metadb_server *,
	int);
int gfarm_metadb_server_is_active(struct gfarm_metadb_server *);
void gfarm_metadb_server_set_is_active(struct gfarm_metadb_server *, int);
