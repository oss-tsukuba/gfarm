/*
 * $Id$
 */

struct gfarm_metadb_server {
	char *name;
	char *clustername;
	int port;
	int flags;
	/* tflags is not stored in db */
	int tflags; /* needs struct gfarm_filesystem::mutex to access */
};

gfarm_error_t gfarm_metadb_server_new(struct gfarm_metadb_server **,
	char *, int);
const char * gfarm_metadb_server_get_name(struct gfarm_metadb_server *);
int gfarm_metadb_server_get_port(struct gfarm_metadb_server *);
void gfarm_metadb_server_free(struct gfarm_metadb_server *);
int gfarm_metadb_server_is_default_master(struct gfarm_metadb_server *);
void gfarm_metadb_server_set_is_default_master(struct gfarm_metadb_server *,
	int);
int gfarm_metadb_server_is_master_candidate(struct gfarm_metadb_server *);
void gfarm_metadb_server_set_is_master_candidate(struct gfarm_metadb_server *,
	int);

/*
 * APIs which need struct gfarm_filesystem::mutex to access
 */

struct gfarm_filesystem;

int gfarm_metadb_server_is_master(
	struct gfarm_filesystem *, struct gfarm_metadb_server *);
void gfarm_metadb_server_set_is_master(
	struct gfarm_filesystem *, struct gfarm_metadb_server *, int);
int gfarm_metadb_server_is_self(
	struct gfarm_filesystem *, struct gfarm_metadb_server *);
void gfarm_metadb_server_set_is_self(
	struct gfarm_filesystem *, struct gfarm_metadb_server *, int);
int gfarm_metadb_server_is_sync_replication(
	struct gfarm_filesystem *, struct gfarm_metadb_server *);
void gfarm_metadb_server_set_is_sync_replication(
	struct gfarm_filesystem *, struct gfarm_metadb_server *, int);
int gfarm_metadb_server_is_active(
	struct gfarm_filesystem *, struct gfarm_metadb_server *);
void gfarm_metadb_server_set_is_active(
	struct gfarm_filesystem *, struct gfarm_metadb_server *, int);
int gfarm_metadb_server_seqnum_is_unknown(
	struct gfarm_filesystem *, struct gfarm_metadb_server *);
void gfarm_metadb_server_set_seqnum_is_unknown(
	struct gfarm_filesystem *, struct gfarm_metadb_server *);
int gfarm_metadb_server_seqnum_is_ok(
	struct gfarm_filesystem *, struct gfarm_metadb_server *);
void gfarm_metadb_server_set_seqnum_is_ok(
	struct gfarm_filesystem *, struct gfarm_metadb_server *);
int gfarm_metadb_server_seqnum_is_out_of_sync(
	struct gfarm_filesystem *, struct gfarm_metadb_server *);
void gfarm_metadb_server_set_seqnum_is_out_of_sync(
	struct gfarm_filesystem *, struct gfarm_metadb_server *);
int gfarm_metadb_server_seqnum_is_error(
	struct gfarm_filesystem *, struct gfarm_metadb_server *);
void gfarm_metadb_server_set_seqnum_is_error(
	struct gfarm_filesystem *, struct gfarm_metadb_server *);
int gfarm_metadb_server_seqnum_is_behind(
	struct gfarm_filesystem *, struct gfarm_metadb_server *);
void gfarm_metadb_server_set_seqnum_is_behind(
	struct gfarm_filesystem *, struct gfarm_metadb_server *);
int gfarm_metadb_server_is_memory_owned_by_fs_unlocked(
	struct gfarm_metadb_server *);
int gfarm_metadb_server_is_memory_owned_by_fs(
	struct gfarm_filesystem *, struct gfarm_metadb_server *);
void gfarm_metadb_server_set_is_memory_owned_by_fs_unlocked(
	struct gfarm_metadb_server *, int);
void gfarm_metadb_server_set_is_memory_owned_by_fs(
	struct gfarm_filesystem *, struct gfarm_metadb_server *, int);
int gfarm_metadb_server_is_removed_unlocked(
	struct gfarm_metadb_server *);
int gfarm_metadb_server_is_removed(
	struct gfarm_filesystem *, struct gfarm_metadb_server *);
void gfarm_metadb_server_set_is_removed_unlocked(
	struct gfarm_metadb_server *, int);
void gfarm_metadb_server_set_is_removed(
	struct gfarm_filesystem *, struct gfarm_metadb_server *, int);
