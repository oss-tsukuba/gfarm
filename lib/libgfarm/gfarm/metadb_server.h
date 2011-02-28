/*
 * $Id$
 */

struct gfarm_metadb_server;

gfarm_error_t gfarm_metadb_server_new(struct gfarm_metadb_server **);
const char * gfarm_metadb_server_get_name(struct gfarm_metadb_server *);
void gfarm_metadb_server_set_name(struct gfarm_metadb_server *, char *);
int gfarm_metadb_server_get_port(struct gfarm_metadb_server *);
void gfarm_metadb_server_set_port(struct gfarm_metadb_server *, int);
int gfarm_metadb_server_is_master(struct gfarm_metadb_server *);
void gfarm_metadb_server_set_is_master(struct gfarm_metadb_server *, int);
int gfarm_metadb_server_is_self(struct gfarm_metadb_server *);
void gfarm_metadb_server_set_is_self(struct gfarm_metadb_server *, int);
void gfarm_metadb_server_free(struct gfarm_metadb_server *);
