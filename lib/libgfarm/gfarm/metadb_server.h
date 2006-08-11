/*
 * This module provides interface to gfarm metadata in two ways:
 * - gfm protocol: for gfsd,
 *	via lib/libgfarm/gfarm/metadb_server.c
 * - function call: for gfmd,
 *	via server/gfmd/metadb_if.c
 *
 * gfarm clients shouldn't use this interface, because this doesn't
 * take metadb server as its argument, and cannot cope with multple
 * metadata servers.
 */

/* XXX FIXME: the following interface is used by gfarm client for now, too. */
/* this interface is only used by gfsd */
struct gfm_connection;
void gfarm_metadb_set_server(struct gfm_connection *);

/* XXX FIXME: the following interface is used by gfarm client for now, too. */
struct gfarm_host_info;
gfarm_error_t gfarm_host_info_get_by_name_alias(const char *,
	struct gfarm_host_info *);

gfarm_error_t gfarm_metadb_verify_username(const char *);
