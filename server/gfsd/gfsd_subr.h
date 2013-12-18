/* need #include <gfarm/gfarm_config.h> to see HAVE_GETLOADAVG */

extern int debug_mode;
extern struct gfm_connection *gfm_server;
extern const char READONLY_CONFIG_FILE[];
extern int gfarm_spool_root_len;
extern char *canonical_self_name;

#ifndef HAVE_GETLOADAVG
int getloadavg(double *, int);
#endif

int gfsd_statfs(char *, gfarm_int32_t *,
	gfarm_off_t *, gfarm_off_t *, gfarm_off_t *,
	gfarm_off_t *, gfarm_off_t *, gfarm_off_t *,
	int *);

void gfsd_spool_check();

#define fatal_metadb_proto(msg_no, diag, proto, e) \
	fatal_metadb_proto_full(msg_no, __FILE__, __LINE__, __func__, \
	    diag, proto, e)

void fatal_metadb_proto_full(int,
	const char *, int, const char *,
	const char *, const char *, gfarm_error_t);

#define fatal(msg_no, ...) \
	fatal_full(msg_no, __FILE__, __LINE__, __func__, __VA_ARGS__)

void fatal_full(int, const char *, int, const char *,
	const char *, ...) GFLOG_PRINTF_ARG(5, 6);

void gfsd_local_path(gfarm_ino_t, gfarm_uint64_t, const char *, char **);
int gfsd_create_ancestor_dir(char *);
gfarm_error_t gfm_client_replica_lost(gfarm_ino_t, gfarm_uint64_t);
