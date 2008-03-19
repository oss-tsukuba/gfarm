#include <stdio.h> /* for config.h */
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <time.h>
#include "hash.h"
#include <gfarm/gfarm_config.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>
#include <gfarm/host_info.h>
#include <gfarm/user_info.h>
#include <gfarm/group_info.h>
#include "gfp_xdr.h"
#include "io_fd.h"
#include "sockopt.h"
#include "auth.h"
#include "config.h"
#include "conn_hash.h"
#include "gfm_proto.h"
#include "gfj_client.h"
#include "gfm_client.h"

struct gfm_connection {
	struct gfp_xdr *conn;
	enum gfarm_auth_method auth_method;
	struct gfarm_hash_entry *hash_entry;
};

#define SERVER_HASHTAB_SIZE	3079	/* prime number */

static struct gfarm_hash_table *gfm_server_hashtab = NULL;

struct gfp_xdr *
gfm_client_connection_conn(struct gfm_connection *gfm_server)
{
	return (gfm_server->conn);
}

int
gfm_client_connection_fd(struct gfm_connection *gfm_server)
{
	return (gfp_xdr_fd(gfm_server->conn));
}

enum gfarm_auth_method
gfm_client_connection_auth_method(struct gfm_connection *gfm_server)
{
	return (gfm_server->auth_method);
}

static gfarm_error_t
gfm_client_connection0(const char *hostname, int port,
	struct gfm_connection *gfm_server)
{
	gfarm_error_t e;
	char *host_fqdn;
	int sock;
	struct hostent *hp;
	struct sockaddr_in peer_addr;

	hp = gethostbyname(hostname);
	if (hp == NULL || hp->h_addrtype != AF_INET)
		return (GFARM_ERR_UNKNOWN_HOST);
	memset(&peer_addr, 0, sizeof(peer_addr));
	memcpy(&peer_addr.sin_addr, hp->h_addr,
	       sizeof(peer_addr.sin_addr));
	peer_addr.sin_family = hp->h_addrtype;
	peer_addr.sin_port = htons(port);

	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock == -1)
		return (gfarm_errno_to_error(errno));
	fcntl(sock, F_SETFD, 1); /* automatically close() on exec(2) */

	/* XXX - how to report setsockopt(2) failure ? */
	gfarm_sockopt_apply_by_name_addr(sock, hp->h_name,
	    (struct sockaddr *)&peer_addr);

	if (connect(sock, (struct sockaddr *)&peer_addr, sizeof(peer_addr))
	    < 0) {
		close(sock);
		return (gfarm_errno_to_error(errno));
	}
	e = gfp_xdr_new_socket(sock, &gfm_server->conn);
	if (e != GFARM_ERR_NO_ERROR) {
		close(sock);
		return (e);
	}
	/*
	 * the reason why we call strdup() is because
	 * gfarm_auth_request() may break static work area of `*hp'
	 */
	host_fqdn = strdup(hp->h_name);
	if (host_fqdn == NULL) {
		gfp_xdr_free(gfm_server->conn);
		return (GFARM_ERR_NO_MEMORY);
	}
	e = gfarm_auth_request(gfm_server->conn,
	    GFM_SERVICE_TAG, host_fqdn,
	    (struct sockaddr *)&peer_addr, gfarm_get_auth_id_type(),
	    &gfm_server->auth_method);
	free(host_fqdn);
	if (e != GFARM_ERR_NO_ERROR) {
		gfp_xdr_free(gfm_server->conn);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfm_client_connection_acquire(const char *hostname, int port,
	struct gfm_connection **gfm_serverp)
{
	gfarm_error_t e;
	struct gfarm_hash_entry *entry;
	struct gfm_connection *gfm_server;
	int created;

	e = gfp_conn_hash_enter(&gfm_server_hashtab, SERVER_HASHTAB_SIZE,
	    sizeof(*gfm_server),
	    hostname, port, gfarm_get_global_username(),
	    &entry, &created);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	gfm_server = gfarm_hash_entry_data(entry);
	if (created) {
		e = gfm_client_connection0(hostname, port, gfm_server);
		if (e != GFARM_ERR_NO_ERROR) {
			gfp_conn_hash_purge(gfm_server_hashtab, entry);
			return (e);
		}
		gfm_server->hash_entry = entry;
	}
	*gfm_serverp = gfm_server;
	return (GFARM_ERR_NO_ERROR);
}

void
gfm_client_connection_free(struct gfm_connection *gfm_server)
{
	gfp_xdr_free(gfm_server->conn);
	gfp_conn_hash_purge(gfm_server_hashtab, gfm_server->hash_entry);
}

gfarm_error_t
gfm_client_rpc_request(struct gfm_connection *gfm_server, int command,
		       const char *format, ...)
{
	va_list ap;
	gfarm_error_t e;

	va_start(ap, format);
	e = gfp_xdr_vrpc_request(gfm_server->conn, command, &format, &ap);
	va_end(ap);
	return (e);
}

gfarm_error_t
gfm_client_rpc_result(struct gfm_connection *gfm_server, int just,
		      const char *format, ...)
{
	va_list ap;
	gfarm_error_t e;
	int errcode;

	e = gfp_xdr_flush(gfm_server->conn);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	va_start(ap, format);
	e = gfp_xdr_vrpc_result(gfm_server->conn, just,
	    &errcode, &format, &ap);
	va_end(ap);

	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (errcode != 0) {
		/*
		 * We just use gfarm_error_t as the errcode,
		 * Note that GFARM_ERR_NO_ERROR == 0.
		 */
		return (errcode);
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfm_client_rpc(struct gfm_connection *gfm_server, int just, int command,
	       const char *format, ...)
{
	va_list ap;
	gfarm_error_t e;
	int errcode;

	va_start(ap, format);
	e = gfp_xdr_vrpc(gfm_server->conn, just,
			 command, &errcode, &format, &ap);
	va_end(ap);

	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (errcode != 0) {
		/*
		 * We just use gfarm_error_t as the errcode,
		 * Note that GFARM_ERR_NO_ERROR == 0.
		 */
		return (errcode);
	}
	return (GFARM_ERR_NO_ERROR);
}

/*
 * host/user/group metadata
 */

static gfarm_error_t
get_nhosts(struct gfm_connection *gfm_server,
	int nhosts, struct gfarm_host_info *hosts)
{
	gfarm_error_t e;
	int i, eof;
	gfarm_int32_t naliases;

	for (i = 0; i < nhosts; i++) {
		if ((e = gfp_xdr_recv(gfm_server->conn, 0, &eof, "ssiiii",
		    &hosts[i].hostname, &hosts[i].architecture,
		    &hosts[i].ncpu, &hosts[i].port, &hosts[i].flags,
		    &naliases)) != GFARM_ERR_NO_ERROR)
			return (e); /* XXX */
		if (eof)
			return (GFARM_ERR_PROTOCOL); /* XXX */
		/* XXX FIXME */
		hosts[i].nhostaliases = 0;
		hosts[i].hostaliases = NULL;
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfm_client_host_info_send_common(struct gfm_connection *gfm_server,
	int op, const struct gfarm_host_info *host)
{
	return (gfm_client_rpc(gfm_server, 0,
	    op, "ssiii/",
	    host->hostname, host->architecture,
	    host->ncpu, host->port, host->flags));
}

gfarm_error_t
gfm_client_host_info_get_all(struct gfm_connection *gfm_server,
	int *nhostsp, struct gfarm_host_info **hostsp)
{
	gfarm_error_t e;
	gfarm_int32_t nhosts;
	struct gfarm_host_info *hosts;

	if ((e = gfm_client_rpc(gfm_server, 0, GFM_PROTO_HOST_INFO_GET_ALL,
	    "/i", &nhosts)) != GFARM_ERR_NO_ERROR)
		return (e);
	GFARM_MALLOC_ARRAY(hosts, nhosts);
	if (hosts == NULL) /* XXX this breaks gfm protocol */
		return (GFARM_ERR_NO_MEMORY);
	if ((e = get_nhosts(gfm_server, nhosts, hosts)) != GFARM_ERR_NO_ERROR)
		return (e);
	*nhostsp = nhosts;
	*hostsp = hosts;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfm_client_host_info_get_by_architecture(struct gfm_connection *gfm_server,
	const char *architecture,
	int *nhostsp, struct gfarm_host_info **hostsp)
{
	gfarm_error_t e;
	int nhosts;

	if ((e = gfm_client_rpc(gfm_server, 0,
	    GFM_PROTO_HOST_INFO_GET_BY_ARCHITECTURE, "s/i", architecture,
	    &nhosts)) != GFARM_ERR_NO_ERROR)
		return (e);
	*nhostsp = nhosts;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfm_client_host_info_get_by_names_common(struct gfm_connection *gfm_server,
	int op, int nhosts, const char **names,
	gfarm_error_t *errors, struct gfarm_host_info *hosts)
{
	gfarm_error_t e;
	int i;

	if ((e = gfm_client_rpc_request(gfm_server, op, "i", nhosts)) !=
	    GFARM_ERR_NO_ERROR)
		return (e);
	for (i = 0; i < nhosts; i++) {
		e = gfp_xdr_send(gfm_server->conn, "s", names[i]);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
	}
	if ((e = gfm_client_rpc_result(gfm_server, 0, "")) !=
	    GFARM_ERR_NO_ERROR)
		return (e);
	for (i = 0; i < nhosts; i++) {
		e = gfm_client_rpc_result(gfm_server, 0, "");
		errors[i] = e != GFARM_ERR_NO_ERROR ?
		    e : get_nhosts(gfm_server, 1, &hosts[i]);
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfm_client_host_info_get_by_names(struct gfm_connection *gfm_server,
	int nhosts, const char **names,
	gfarm_error_t *errors, struct gfarm_host_info *hosts)
{
	return (gfm_client_host_info_get_by_names_common(
	    gfm_server, GFM_PROTO_HOST_INFO_GET_BY_NAMES,
	    nhosts, names, errors, hosts));
}

gfarm_error_t
gfm_client_host_info_get_by_namealiases(struct gfm_connection *gfm_server,
	int nhosts, const char **names,
	gfarm_error_t *errors, struct gfarm_host_info *hosts)
{
	return (gfm_client_host_info_get_by_names_common(
	    gfm_server, GFM_PROTO_HOST_INFO_GET_BY_NAMEALIASES,
	    nhosts, names, errors, hosts));
}

gfarm_error_t
gfm_client_host_info_set(struct gfm_connection *gfm_server,
	const struct gfarm_host_info *host)
{
	return (gfm_client_host_info_send_common(gfm_server,
	    GFM_PROTO_HOST_INFO_SET, host));
}

gfarm_error_t
gfm_client_host_info_modify(struct gfm_connection *gfm_server,
	const struct gfarm_host_info *host)
{
	return (gfm_client_host_info_send_common(gfm_server,
	    GFM_PROTO_HOST_INFO_MODIFY, host));
}

gfarm_error_t
gfm_client_host_info_remove(struct gfm_connection *gfm_server,
	const char *hostname)
{
	return (gfm_client_rpc(gfm_server, 0,
	    GFM_PROTO_HOST_INFO_REMOVE, "s/", hostname));
}

static gfarm_error_t
get_nusers(struct gfm_connection *gfm_server,
	int nusers, struct gfarm_user_info *users)
{
	gfarm_error_t e;
	int i, eof;

	for (i = 0; i < nusers; i++) {
		if ((e = gfp_xdr_recv(gfm_server->conn, 0, &eof, "ssss",
		    &users[i].username, &users[i].realname,
		    &users[i].homedir, &users[i].gsi_dn)) !=
		    GFARM_ERR_NO_ERROR)
			return (e); /* XXX */
		if (eof)
			return (GFARM_ERR_PROTOCOL); /* XXX */
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfm_client_user_info_send_common(struct gfm_connection *gfm_server,
	int op, const struct gfarm_user_info *user)
{
	return (gfm_client_rpc(gfm_server, 0,
	    op, "ssss/",
	    user->username, user->realname, user->homedir, user->gsi_dn));
}

gfarm_error_t
gfm_client_user_info_get_all(struct gfm_connection *gfm_server,
	int *nusersp, struct gfarm_user_info **usersp)
{
	gfarm_error_t e;
	int nusers;
	struct gfarm_user_info *users;

	if ((e = gfm_client_rpc(gfm_server, 0, GFM_PROTO_USER_INFO_GET_ALL,
	    "/i", &nusers)) != GFARM_ERR_NO_ERROR)
		return (e);
	GFARM_MALLOC_ARRAY(users, nusers);
	if (users == NULL) /* XXX this breaks gfm protocol */
		return (GFARM_ERR_NO_MEMORY);
	if ((e = get_nusers(gfm_server, nusers, users)) != GFARM_ERR_NO_ERROR)
		return (e);
	*nusersp = nusers;
	*usersp = users;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfm_client_user_info_get_by_names(struct gfm_connection *gfm_server,
	int nusers, const char **names,
	gfarm_error_t *errors, struct gfarm_user_info *users)
{
	gfarm_error_t e;
	int i;

	if ((e = gfm_client_rpc_request(gfm_server,
	    GFM_PROTO_USER_INFO_GET_BY_NAMES, "i", nusers)) !=
	    GFARM_ERR_NO_ERROR)
		return (e);
	for (i = 0; i < nusers; i++) {
		e = gfp_xdr_send(gfm_server->conn, "s", names[i]);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
	}
	if ((e = gfm_client_rpc_result(gfm_server, 0, "")) !=
	    GFARM_ERR_NO_ERROR)
		return (e);
	for (i = 0; i < nusers; i++) {
		e = gfm_client_rpc_result(gfm_server, 0, "");
		errors[i] = e != GFARM_ERR_NO_ERROR ?
		    e : get_nusers(gfm_server, 1, &users[i]);
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfm_client_user_info_get_by_gsi_dn(struct gfm_connection *gfm_server,
	const char *gsi_dn, struct gfarm_user_info *user)
{
	return (gfm_client_rpc(gfm_server, 0,
	    GFM_PROTO_USER_INFO_GET_BY_GSI_DN, "s/ssss", gsi_dn,
	    &user->username, &user->realname, &user->homedir, &user->gsi_dn));
}

gfarm_error_t
gfm_client_user_info_set(struct gfm_connection *gfm_server,
	const struct gfarm_user_info *user)
{
	return (gfm_client_user_info_send_common(gfm_server,
	    GFM_PROTO_USER_INFO_SET, user));
}

gfarm_error_t
gfm_client_user_info_modify(struct gfm_connection *gfm_server,
	const struct gfarm_user_info *user)
{
	return (gfm_client_user_info_send_common(gfm_server,
	    GFM_PROTO_USER_INFO_MODIFY, user));
}

gfarm_error_t
gfm_client_user_info_remove(struct gfm_connection *gfm_server,
	const char *username)
{
	return (gfm_client_rpc(gfm_server, 0,
	    GFM_PROTO_USER_INFO_REMOVE, "s/", username));
}

static gfarm_error_t
get_ngroups(struct gfm_connection *gfm_server,
	int ngroups, struct gfarm_group_info *groups)
{
	gfarm_error_t e;
	int i, j, eof;

	for (i = 0; i < ngroups; i++) {
		if ((e = gfp_xdr_recv(gfm_server->conn, 0, &eof, "si",
		    &groups[i].groupname, &groups[i].nusers)) !=
		    GFARM_ERR_NO_ERROR)
			return (e); /* XXX */
		if (eof)
			return (GFARM_ERR_PROTOCOL); /* XXX */
		GFARM_MALLOC_ARRAY(groups[i].usernames, groups[i].nusers);
		 /* XXX this breaks gfm protocol */
		if (groups[i].usernames == NULL)
			return (GFARM_ERR_NO_MEMORY); /* XXX */
		for (j = 0; j < groups[i].nusers; j++) {
			if ((e = gfp_xdr_recv(gfm_server->conn, 0, &eof, "s",
			    &groups[i].usernames[j])) != GFARM_ERR_NO_ERROR)
				return (e); /* XXX */
			if (eof)
				return (GFARM_ERR_PROTOCOL); /* XXX */
		}
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfm_client_group_info_send_common(struct gfm_connection *gfm_server,
	int op, const struct gfarm_group_info *group)
{
	gfarm_error_t e;
	int i;

	if ((e = gfm_client_rpc_request(gfm_server, op, "si",
	    group->groupname, group->nusers)) != GFARM_ERR_NO_ERROR)
		return (e);
	for (i = 0; i < group->nusers; i++) {
		if ((e = gfp_xdr_send(gfm_server->conn,
		    "s", group->usernames[i])) != GFARM_ERR_NO_ERROR)
			return (e);
	}
	return (gfm_client_rpc_result(gfm_server, 0, ""));
}

gfarm_error_t
gfm_client_group_info_get_all(struct gfm_connection *gfm_server,
	int *ngroupsp, struct gfarm_group_info **groupsp)
{
	gfarm_error_t e;
	int ngroups;
	struct gfarm_group_info *groups;

	if ((e = gfm_client_rpc(gfm_server, 0, GFM_PROTO_GROUP_INFO_GET_ALL,
	    "/i", &ngroups)) != GFARM_ERR_NO_ERROR)
		return (e);
	GFARM_MALLOC_ARRAY(groups, ngroups);
	if (groups == NULL) /* XXX this breaks gfm protocol */
		return (GFARM_ERR_NO_MEMORY);
	if ((e = get_ngroups(gfm_server, ngroups, groups)) !=
	    GFARM_ERR_NO_ERROR)
		return (e);
	*ngroupsp = ngroups;
	*groupsp = groups;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfm_client_group_info_get_by_names(struct gfm_connection *gfm_server,
	int ngroups, const char **group_names,
	gfarm_error_t *errors, struct gfarm_group_info *groups)
{
	gfarm_error_t e;
	int i;

	if ((e = gfm_client_rpc_request(gfm_server,
	    GFM_PROTO_GROUP_INFO_GET_BY_NAMES, "i", ngroups)) !=
	    GFARM_ERR_NO_ERROR)
		return (e);
	for (i = 0; i < ngroups; i++) {
		e = gfp_xdr_send(gfm_server->conn, "s", group_names[i]);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
	}
	if ((e = gfm_client_rpc_result(gfm_server, 0, "")) !=
	    GFARM_ERR_NO_ERROR)
		return (e);
	for (i = 0; i < ngroups; i++) {
		e = gfm_client_rpc_result(gfm_server, 0, "");
		errors[i] = e != GFARM_ERR_NO_ERROR ?
		    e : get_ngroups(gfm_server, 1, &groups[i]);
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfm_client_group_info_set(struct gfm_connection *gfm_server,
	const struct gfarm_group_info *group)
{
	return (gfm_client_group_info_send_common(gfm_server,
	    GFM_PROTO_GROUP_INFO_SET, group));
}

gfarm_error_t
gfm_client_group_info_modify(struct gfm_connection *gfm_server,
	const struct gfarm_group_info *group)
{
	return (gfm_client_group_info_send_common(gfm_server,
	    GFM_PROTO_GROUP_INFO_MODIFY, group));
}

gfarm_error_t
gfm_client_group_info_remove(struct gfm_connection *gfm_server,
	const char *groupname)
{
	return (gfm_client_rpc(gfm_server, 0,
	    GFM_PROTO_GROUP_INFO_REMOVE, "s/", groupname));
}

gfarm_error_t
gfm_client_group_info_users_op_common(struct gfm_connection *gfm_server,
	int op, const char *groupname,
	int nusers, const char **usernames, gfarm_error_t *errors)
{
	gfarm_error_t e;
	int i;

	if ((e = gfm_client_rpc_request(gfm_server, op, "si",
	    groupname, nusers)) != GFARM_ERR_NO_ERROR)
		return (e);
	for (i = 0; i < nusers; i++) {
		if ((e = gfp_xdr_send(gfm_server->conn, "s", usernames[i]))
		    != GFARM_ERR_NO_ERROR)
			return (e);
	}
	if ((e = gfm_client_rpc_result(gfm_server, 0, "")) !=
	    GFARM_ERR_NO_ERROR)
		return (e);
	for (i = 0; i < nusers; i++) {
		errors[i] = gfm_client_rpc_result(gfm_server, 0, "");
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfm_client_group_info_add_users(struct gfm_connection *gfm_server,
	const char *groupname,
	int nusers, const char **usernames, gfarm_error_t *errors)
{
	return (gfm_client_group_info_users_op_common(gfm_server,
	    GFM_PROTO_GROUP_INFO_ADD_USERS, groupname, nusers, usernames,
	    errors));
}

gfarm_error_t
gfm_client_group_info_remove_users(struct gfm_connection *gfm_server,
	const char *groupname,
	int nusers, const char **usernames, gfarm_error_t *errors)
{
	return (gfm_client_group_info_users_op_common(gfm_server,
	    GFM_PROTO_GROUP_INFO_ADD_USERS, groupname, nusers, usernames,
	    errors));
}

gfarm_error_t
gfm_client_group_names_get_by_users(struct gfm_connection *gfm_server,
	int nusers, const char **usernames,
	gfarm_error_t *errors, struct gfarm_group_names *assignments)
{
	gfarm_error_t e;
	int i, j;

	if ((e = gfm_client_rpc_request(gfm_server,
	    GFM_PROTO_GROUP_NAMES_GET_BY_USERS, "i", nusers)) !=
	    GFARM_ERR_NO_ERROR)
		return (e);
	for (i = 0; i < nusers; i++) {
		if ((e = gfp_xdr_send(gfm_server->conn, "s", usernames[i]))
		    != GFARM_ERR_NO_ERROR)
			return (e);
	}
	if ((e = gfm_client_rpc_result(gfm_server, 0, "")) !=
	    GFARM_ERR_NO_ERROR)
		return (e);
	for (i = 0; i < nusers; i++) {
		errors[i] = e = gfm_client_rpc_result(gfm_server, 0, "i",
		    &assignments->ngroups);
		if (e == GFARM_ERR_NO_ERROR) {
			GFARM_MALLOC_ARRAY(assignments->groupnames,
			    assignments->ngroups);
			if (assignments->groupnames == NULL) {
				errors[i] = GFARM_ERR_NO_MEMORY;
			} else {
				for (j = 0; j < assignments->ngroups; j++) {
					int eof;

					errors[i] = gfp_xdr_recv(
					    gfm_server->conn, 0, &eof, "s",
					    &assignments->groupnames[j]);
					if (errors[i] != GFARM_ERR_NO_ERROR)
						break;
					if (eof)
						errors[i] = GFARM_ERR_PROTOCOL;
					if (errors[i] != GFARM_ERR_NO_ERROR)
						break;
				}
			}
		}
	}
	return (GFARM_ERR_NO_ERROR);
}

/*
 * gfs from client
 */

gfarm_error_t
gfm_client_compound_begin_request(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc_request(gfm_server, GFM_PROTO_COMPOUND_BEGIN,
	    ""));
}

gfarm_error_t
gfm_client_compound_begin_result(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc_result(gfm_server, 0, ""));
}

gfarm_error_t
gfm_client_compound_end_request(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc_request(gfm_server, GFM_PROTO_COMPOUND_END,
	    ""));
}

gfarm_error_t
gfm_client_compound_end_result(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc_result(gfm_server, 0, ""));
}

gfarm_error_t
gfm_client_compound_on_error_request(struct gfm_connection *gfm_server,
	gfarm_error_t error)
{
	return (gfm_client_rpc_request(gfm_server, GFM_PROTO_COMPOUND_ON_ERROR,
	    "i", error));
}

gfarm_error_t
gfm_client_compound_on_error_result(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc_result(gfm_server, 0, ""));
}

gfarm_error_t
gfm_client_get_fd_request(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc_request(gfm_server, GFM_PROTO_GET_FD, ""));
}

gfarm_error_t
gfm_client_get_fd_result(struct gfm_connection *gfm_server, gfarm_int32_t *fdp)
{
	return (gfm_client_rpc_result(gfm_server, 0, "i", fdp));
}

gfarm_error_t
gfm_client_put_fd_request(struct gfm_connection *gfm_server, gfarm_int32_t fd)
{
	return (gfm_client_rpc_request(gfm_server, GFM_PROTO_PUT_FD,
	    "i", fd));
}

gfarm_error_t
gfm_client_put_fd_result(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc_result(gfm_server, 0, ""));
}

gfarm_error_t
gfm_client_save_fd_request(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc_request(gfm_server, GFM_PROTO_SAVE_FD, ""));
}

gfarm_error_t
gfm_client_save_fd_result(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc_result(gfm_server, 0, ""));
}

gfarm_error_t
gfm_client_restore_fd_request(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc_request(gfm_server, GFM_PROTO_RESTORE_FD, ""));
}

gfarm_error_t
gfm_client_restore_fd_result(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc_result(gfm_server, 0, ""));
}

gfarm_error_t
gfm_client_create_request(struct gfm_connection *gfm_server,
	const char *name, gfarm_uint32_t flags, gfarm_uint32_t mode)
{
	return (gfm_client_rpc_request(gfm_server, GFM_PROTO_CREATE,
	    "sii", name, flags, mode));
}

gfarm_error_t
gfm_client_create_result(struct gfm_connection *gfm_server,
	gfarm_ino_t *inump, gfarm_uint64_t *genp, gfarm_mode_t *modep)
{
	return (gfm_client_rpc_result(gfm_server, 0, "lli",
	    inump, genp, modep));
}

gfarm_error_t
gfm_client_open_request(struct gfm_connection *gfm_server,
	const char *name, size_t namelen, gfarm_uint32_t flags)
{
	return (gfm_client_rpc_request(gfm_server, GFM_PROTO_OPEN,
	    "Si", name, namelen, flags));
}

gfarm_error_t
gfm_client_open_result(struct gfm_connection *gfm_server,
	gfarm_ino_t *inump, gfarm_uint64_t *genp, gfarm_mode_t *modep)
{
	return (gfm_client_rpc_result(gfm_server, 0, "lli",
	    inump, genp, modep));
}

gfarm_error_t
gfm_client_open_root_request(struct gfm_connection *gfm_server,
	gfarm_uint32_t flags)
{
	return (gfm_client_rpc_request(gfm_server, GFM_PROTO_OPEN_ROOT, "i",
	    flags));
}

gfarm_error_t
gfm_client_open_root_result(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc_result(gfm_server, 0, ""));
}

gfarm_error_t
gfm_client_open_parernt_request(struct gfm_connection *gfm_server,
	gfarm_uint32_t flags)
{
	return (gfm_client_rpc_request(gfm_server, GFM_PROTO_OPEN_PARENT, "i",
	    flags));
}

gfarm_error_t
gfm_client_open_parent_result(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc_result(gfm_server, 0, ""));
}

gfarm_error_t
gfm_client_close_request(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc_request(gfm_server, GFM_PROTO_CLOSE, ""));
}

gfarm_error_t
gfm_client_close_result(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc_result(gfm_server, 0, ""));
}

gfarm_error_t
gfm_client_verify_type_request(struct gfm_connection *gfm_server,
	gfarm_int32_t type)
{
	return (gfm_client_rpc_request(gfm_server, GFM_PROTO_VERIFY_TYPE,
	    "i", type));
}

gfarm_error_t
gfm_client_verify_type_result(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc_result(gfm_server, 0, ""));
}

gfarm_error_t
gfm_client_verify_type_not_request(struct gfm_connection *gfm_server,
	gfarm_int32_t type)
{
	return (gfm_client_rpc_request(gfm_server, GFM_PROTO_VERIFY_TYPE_NOT,
	    "i", type));
}

gfarm_error_t
gfm_client_verify_type_not_result(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc_result(gfm_server, 0, ""));
}

gfarm_error_t
gfm_client_bequeath_fd_request(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc_request(gfm_server, GFM_PROTO_BEQUEATH_FD, ""));
}

gfarm_error_t
gfm_client_bequeath_fd_result(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc_result(gfm_server, 0, ""));
}

gfarm_error_t
gfm_client_inherit_fd_request(struct gfm_connection *gfm_server,
	gfarm_int32_t fd)
{
	return (gfm_client_rpc_request(gfm_server, GFM_PROTO_INHERIT_FD, "i",
	    fd));
}

gfarm_error_t
gfm_client_inherit_fd_result(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc_result(gfm_server, 0, ""));
}

gfarm_error_t
gfm_client_fstat_request(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc_request(gfm_server, GFM_PROTO_FSTAT, ""));
}

gfarm_error_t
gfm_client_fstat_result(struct gfm_connection *gfm_server, struct gfs_stat *st)
{
	return (gfm_client_rpc_result(gfm_server, 0, "llilsslllilili",
	    &st->st_ino, &st->st_gen, &st->st_mode, &st->st_nlink,
	    &st->st_user, &st->st_group, &st->st_size, &st->st_ncopy,
	    &st->st_atimespec.tv_sec, &st->st_atimespec.tv_nsec,
	    &st->st_mtimespec.tv_sec, &st->st_mtimespec.tv_nsec,
	    &st->st_ctimespec.tv_sec, &st->st_ctimespec.tv_nsec));
}

gfarm_error_t
gfm_client_futimes_request(struct gfm_connection *gfm_server,
	gfarm_int64_t atime_sec, gfarm_int32_t atime_nsec,
	gfarm_int64_t mtime_sec, gfarm_int32_t mtime_nsec)
{
	return (gfm_client_rpc_request(gfm_server, GFM_PROTO_FUTIMES,
	    "lili", atime_sec, atime_nsec, mtime_sec, mtime_nsec));
}

gfarm_error_t
gfm_client_futimes_result(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc_result(gfm_server, 0, ""));
}

gfarm_error_t
gfm_client_fchmod_request(struct gfm_connection *gfm_server, gfarm_mode_t mode)
{
	return (gfm_client_rpc_request(gfm_server, GFM_PROTO_FCHMOD,
	    "i", mode));
}

gfarm_error_t
gfm_client_fchmod_result(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc_result(gfm_server, 0, ""));
}

gfarm_error_t
gfm_client_fchown_request(struct gfm_connection *gfm_server,
	const char *user, const char *group)
{
	return (gfm_client_rpc_request(gfm_server, GFM_PROTO_FCHOWN, "ss",
	    user == NULL ? "" : user,
	    group == NULL ? "" : group));
}

gfarm_error_t
gfm_client_fchown_result(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc_result(gfm_server, 0, ""));
}

gfarm_error_t
gfm_client_cksum_get_request(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc_request(gfm_server,
	    GFM_PROTO_CKSUM_GET, ""));
}

gfarm_error_t
gfm_client_cksum_get_result(struct gfm_connection *gfm_server,
	char **cksum_typep, size_t bufsize, size_t *cksum_lenp, char *cksum,
	gfarm_int32_t *flagsp)
{
	return (gfm_client_rpc_result(gfm_server, 0, "sbi",
	    cksum_typep, bufsize, cksum_lenp, cksum, flagsp));
}

gfarm_error_t
gfm_client_cksum_set_request(struct gfm_connection *gfm_server,
	char *cksum_type, size_t cksum_len, const char *cksum,
	gfarm_int32_t flags, gfarm_int64_t mtime_sec, gfarm_int32_t mtime_nsec)
{
	return (gfm_client_rpc_request(gfm_server,
	    GFM_PROTO_CKSUM_SET, "sbili", cksum_type, cksum_len, cksum,
	    flags, mtime_sec, mtime_nsec));
}

gfarm_error_t
gfm_client_cksum_set_result(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc_result(gfm_server, 0, ""));
}

void
gfarm_host_sched_info_free(int nhosts, struct gfarm_host_sched_info *infos)
{
	int i;

	for (i = 0; i < nhosts; i++)
		free(infos[i].host);
	free(infos);
}

static gfarm_error_t
get_schedule_result(struct gfm_connection *gfm_server,
	int *nhostsp, struct gfarm_host_sched_info **infosp)
{
	gfarm_error_t e;
	gfarm_int32_t i, nhosts, loadavg;
	struct gfarm_host_sched_info *infos;
	int eof;

	if ((e = gfm_client_rpc_result(gfm_server, 0, "i", &nhosts)) !=
	    GFARM_ERR_NO_ERROR)
		return (e);
	GFARM_MALLOC_ARRAY(infos, nhosts);
	if (infos == NULL) /* XXX this breaks gfm protocol */
		return (GFARM_ERR_NO_MEMORY);

	for (i = 0; i < nhosts; i++) {
		if ((e = gfp_xdr_recv(gfm_server->conn, 0, &eof, "siiillllii",
		    &infos[i].host, &infos[i].port, &infos[i].ncpu,
		    &loadavg, &infos[i].cache_time,
		    &infos[i].disk_used, &infos[i].disk_avail,
		    &infos[i].rtt_cache_time,
		    &infos[i].rtt_usec, &infos[i].flags)) !=
		    GFARM_ERR_NO_ERROR)
			return (e); /* XXX */
		if (eof)
			return (GFARM_ERR_PROTOCOL); /* XXX */
		infos[i].loadavg = (float)loadavg / GFM_PROTO_LOADAVG_FSCALE;
	}

	*nhostsp = nhosts;
	*infosp = infos;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfm_client_schedule_file_request(struct gfm_connection *gfm_server,
	const char *domain)
{
	return (gfm_client_rpc_request(gfm_server,
	    GFM_PROTO_SCHEDULE_FILE, "s", domain));
}

gfarm_error_t
gfm_client_schedule_file_result(struct gfm_connection *gfm_server,
	int *nhostsp, struct gfarm_host_sched_info **infosp)
{
	return (get_schedule_result(gfm_server, nhostsp, infosp));
}

gfarm_error_t
gfm_client_schedule_file_with_program_request(
	struct gfm_connection *gfm_server, const char *domain)
{
	return (gfm_client_rpc_request(gfm_server,
	    GFM_PROTO_SCHEDULE_FILE_WITH_PROGRAM, "s", domain));
}

gfarm_error_t
gfm_client_schedule_file_with_program_result(struct gfm_connection *gfm_server,
	int *nhostsp, struct gfarm_host_sched_info **infosp)
{
	return (get_schedule_result(gfm_server, nhostsp, infosp));
}

gfarm_error_t
gfm_client_remove_request(struct gfm_connection *gfm_server, const char *name)
{
	return (gfm_client_rpc_request(gfm_server, GFM_PROTO_REMOVE, "s",
	    name));
}

gfarm_error_t
gfm_client_remove_result(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc_result(gfm_server, 0, ""));
}

gfarm_error_t
gfm_client_rename_request(struct gfm_connection *gfm_server,
	const char *src_name, const char *target_name)
{
	return (gfm_client_rpc_request(gfm_server, GFM_PROTO_RENAME, "ss",
	    src_name, target_name));
}

gfarm_error_t
gfm_client_rename_result(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc_result(gfm_server, 0, ""));
}

gfarm_error_t
gfm_client_flink_request(struct gfm_connection *gfm_server,
	const char *target_name)
{
	return (gfm_client_rpc_request(gfm_server, GFM_PROTO_FLINK, "s",
	    target_name));
}

gfarm_error_t
gfm_client_flink_result(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc_result(gfm_server, 0, ""));
}

gfarm_error_t
gfm_client_mkdir_request(struct gfm_connection *gfm_server,
	const char *target_name, gfarm_mode_t mode)
{
	return (gfm_client_rpc_request(gfm_server, GFM_PROTO_MKDIR, "si",
	    target_name, mode));
}

gfarm_error_t
gfm_client_mkdir_result(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc_result(gfm_server, 0, ""));
}

gfarm_error_t
gfm_client_symlink_request(struct gfm_connection *gfm_server,
	const char *target_path, const char *link_name)
{
	return (gfm_client_rpc_request(gfm_server, GFM_PROTO_SYMLINK, "ss",
	    target_path, link_name));
}

gfarm_error_t
gfm_client_symlink_result(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc_result(gfm_server, 0, ""));
}

gfarm_error_t
gfm_client_readlink_request(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc_request(gfm_server, GFM_PROTO_READLINK, ""));
}

gfarm_error_t
gfm_client_readlink_result(struct gfm_connection *gfm_server, char **linkp)
{
	return (gfm_client_rpc_result(gfm_server, 0, "s", linkp));
}

gfarm_error_t
gfm_client_getdirpath_request(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc_request(gfm_server, GFM_PROTO_GETDIRPATH, ""));
}

gfarm_error_t
gfm_client_getdirpath_result(struct gfm_connection *gfm_server, char **pathp)
{
	return (gfm_client_rpc_result(gfm_server, 0, "s", pathp));
}

gfarm_error_t
gfm_client_getdirents_request(struct gfm_connection *gfm_server,
	gfarm_int32_t n_entries)
{
	return (gfm_client_rpc_request(gfm_server, GFM_PROTO_GETDIRENTS, "i",
	    n_entries));
}

gfarm_error_t
gfm_client_getdirents_result(struct gfm_connection *gfm_server,
	int *n_entriesp, struct gfs_dirent *dirents)
{
	gfarm_error_t e;
	int eof, i;
	gfarm_int32_t n, type;
	size_t sz;

	e = gfm_client_rpc_result(gfm_server, 0, "i", &n);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	for (i = 0; i < n; i++) {
		e = gfp_xdr_recv(gfm_server->conn, 0, &eof, "bil",
		    sizeof(dirents[i].d_name) - 1, &sz, dirents[i].d_name,
		    &type,
		    &dirents[i].d_fileno);
		if (e != GFARM_ERR_NO_ERROR || eof) {
			if (e == GFARM_ERR_NO_ERROR)
				e = GFARM_ERR_PROTOCOL;
			return (e);
		}
		if (sz >= sizeof(dirents[i].d_name) - 1)
			sz = sizeof(dirents[i].d_name) - 1;
		dirents[i].d_name[sz] = '\0';
		dirents[i].d_namlen = sz;
		dirents[i].d_type = type;
		/* XXX */
		dirents[i].d_reclen =
		    sizeof(dirents[i]) - sizeof(dirents[i].d_name) + sz;
	}
	*n_entriesp = n;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfm_client_seek_request(struct gfm_connection *gfm_server,
	gfarm_off_t offset, gfarm_int32_t whence)
{
	return (gfm_client_rpc_request(gfm_server, GFM_PROTO_SEEK, "li",
	    offset, whence));
}

gfarm_error_t
gfm_client_seek_result(struct gfm_connection *gfm_server, gfarm_off_t *offsetp)
{
	return (gfm_client_rpc_result(gfm_server, 0, "l", offsetp));
}

/*
 * gfs from gfsd
 */

gfarm_error_t
gfm_client_reopen_request(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc_request(gfm_server, GFM_PROTO_REOPEN, ""));
}

gfarm_error_t
gfm_client_reopen_result(struct gfm_connection *gfm_server,
	gfarm_ino_t *ino_p, gfarm_uint64_t *gen_p, gfarm_int32_t *modep,
	gfarm_int32_t *flagsp, gfarm_int32_t *to_create_p)
{
	return (gfm_client_rpc_result(gfm_server, 0, "lliii", ino_p, gen_p,
	    modep, flagsp, to_create_p));
}

gfarm_error_t
gfm_client_close_read_request(struct gfm_connection *gfm_server,
	gfarm_int64_t atime_sec, gfarm_int32_t atime_nsec)
{
	return (gfm_client_rpc_request(gfm_server, GFM_PROTO_CLOSE_READ,
	    "li", atime_sec, atime_nsec));
}

gfarm_error_t
gfm_client_close_read_result(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc_result(gfm_server, 0, ""));
}

gfarm_error_t
gfm_client_close_write_request(struct gfm_connection *gfm_server,
	gfarm_off_t size,
	gfarm_int64_t atime_sec, gfarm_int32_t atime_nsec,
	gfarm_int64_t mtime_sec, gfarm_int32_t mtime_nsec)
{
	return (gfm_client_rpc_request(gfm_server, GFM_PROTO_CLOSE_WRITE,
	    "llili", size, atime_sec, atime_nsec, mtime_sec, mtime_nsec));
}

gfarm_error_t
gfm_client_close_write_result(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc_result(gfm_server, 0, ""));
}

gfarm_error_t
gfm_client_lock_request(struct gfm_connection *gfm_server,
	gfarm_off_t start, gfarm_off_t len,
	gfarm_int32_t type, gfarm_int32_t whence)
{
	return (gfm_client_rpc_request(gfm_server, GFM_PROTO_LOCK, "llii",
	    start, len, type, whence));
}

gfarm_error_t
gfm_client_lock_result(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc_result(gfm_server, 0, ""));
}

gfarm_error_t
gfm_client_trylock_request(struct gfm_connection *gfm_server,
	gfarm_off_t start, gfarm_off_t len,
	gfarm_int32_t type, gfarm_int32_t whence)
{
	return (gfm_client_rpc_request(gfm_server, GFM_PROTO_TRYLOCK, "llii",
	    start, len, type, whence));
}

gfarm_error_t
gfm_client_trylock_result(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc_result(gfm_server, 0, ""));
}

gfarm_error_t
gfm_client_unlock_request(struct gfm_connection *gfm_server,
	gfarm_off_t start, gfarm_off_t len,
	gfarm_int32_t type, gfarm_int32_t whence)
{
	return (gfm_client_rpc_request(gfm_server, GFM_PROTO_UNLOCK, "llii",
	    start, len, type, whence));
}

gfarm_error_t
gfm_client_unlock_result(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc_result(gfm_server, 0, ""));
}

gfarm_error_t
gfm_client_lock_info_request(struct gfm_connection *gfm_server,
	gfarm_off_t start, gfarm_off_t len,
	gfarm_int32_t type, gfarm_int32_t whence)
{
	return (gfm_client_rpc_request(gfm_server, GFM_PROTO_LOCK_INFO, "llii",
	    start, len, type, whence));
}

gfarm_error_t
gfm_client_lock_info_result(struct gfm_connection *gfm_server,
	gfarm_off_t *startp, gfarm_off_t *lenp, gfarm_int32_t *typep,
	char **hostp, gfarm_pid_t *pidp)
{
	return (gfm_client_rpc_result(gfm_server, 0, "llisl",
	    startp, lenp, typep, hostp, pidp));
}

gfarm_error_t
gfm_client_switch_back_channel(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc(gfm_server, 0,
	    GFM_PROTO_SWITCH_BACK_CHANNEL, "/"));
}

/*
 * gfs_pio from client
 */

gfarm_error_t
gfm_client_glob(struct gfm_connection *gfm_server)
{
	/* XXX - NOT IMPLEMENTED */
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_client_schedule(struct gfm_connection *gfm_server)
{
	/* XXX - NOT IMPLEMENTED */
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_client_pio_open(struct gfm_connection *gfm_server)
{
	/* XXX - NOT IMPLEMENTED */
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_client_pio_set_paths(struct gfm_connection *gfm_server)
{
	/* XXX - NOT IMPLEMENTED */
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_client_pio_close(struct gfm_connection *gfm_server)
{
	/* XXX - NOT IMPLEMENTED */
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_client_pio_visit(struct gfm_connection *gfm_server)
{
	/* XXX - NOT IMPLEMENTED */
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

/*
 * misc operations from gfsd
 */

gfarm_error_t
gfm_client_hostname_set(struct gfm_connection *gfm_server, char *hostname)
{
	return (gfm_client_rpc(gfm_server, 0,
	    GFM_PROTO_HOSTNAME_SET, "s/", hostname));
}

/*
 * replica management from client
 */

gfarm_error_t
gfm_client_replica_list_by_name_request(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc_request(gfm_server,
	    GFM_PROTO_REPLICA_LIST_BY_NAME, ""));
}

gfarm_error_t
gfm_client_replica_list_by_name_result(struct gfm_connection *gfm_server,
	gfarm_int32_t *n_replicasp, char ***replica_hosts)
{
	gfarm_error_t e;
	int eof, i;
	gfarm_int32_t n;
	char **hosts;

	e = gfm_client_rpc_result(gfm_server, 0, "i", &n);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	GFARM_MALLOC_ARRAY(hosts, n);
	if (hosts == NULL)
		return (GFARM_ERR_NO_MEMORY); /* XXX not graceful */

	for (i = 0; i < n; i++) {
		e = gfp_xdr_recv(gfm_server->conn, 0, &eof, "s", &hosts[i]);
		if (e != GFARM_ERR_NO_ERROR || eof) {
			if (e == GFARM_ERR_NO_ERROR)
				e = GFARM_ERR_PROTOCOL;
			break;
		}
	}
	if (i < n) {
		for (; i >= 0; --i)
			free(hosts[i]);
		free(hosts);
		return (e);
	}
	*n_replicasp = n;
	*replica_hosts = hosts;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfm_client_replica_list_by_host_request(struct gfm_connection *gfm_server,
	const char *host, gfarm_int32_t port)
{
	return (gfm_client_rpc_request(gfm_server,
	    GFM_PROTO_REPLICA_LIST_BY_HOST, "si", host, port));
}

gfarm_error_t
gfm_client_replica_list_by_host_result(struct gfm_connection *gfm_server,
	gfarm_int32_t *n_replicasp, gfarm_ino_t **inodesp)
{
	gfarm_error_t e;
	int eof, i;
	gfarm_int32_t n;
	gfarm_ino_t *inodes;

	e = gfm_client_rpc_result(gfm_server, 0, "i", &n);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	GFARM_MALLOC_ARRAY(inodes, n);
	if (inodes == NULL)
		return (GFARM_ERR_NO_MEMORY); /* XXX not graceful */
	for (i = 0; i < n; i++) {
		e = gfp_xdr_recv(gfm_server->conn, 0, &eof, "l", &inodes[i]);
		if (e != GFARM_ERR_NO_ERROR || eof) {
			if (e == GFARM_ERR_NO_ERROR)
				e = GFARM_ERR_PROTOCOL;
			free(inodes);
			return (e);
		}
	}
	*n_replicasp = n;
	*inodesp = inodes;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfm_client_replica_remove_by_host_request(struct gfm_connection *gfm_server,
	const char *host, gfarm_int32_t port)
{
	return (gfm_client_rpc_request(gfm_server,
	    GFM_PROTO_REPLICA_REMOVE_BY_HOST, "si", host, port));
}

gfarm_error_t
gfm_client_replica_remove_by_host_result(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc_result(gfm_server, 0, ""));
}

/*
 * replica management from gfsd
 */

gfarm_error_t
gfm_client_replica_adding_request(struct gfm_connection *gfm_server,
	char *src_host)
{
	return (gfm_client_rpc_request(gfm_server,
	    GFM_PROTO_REPLICA_ADDING, "s", src_host));
}

gfarm_error_t
gfm_client_replica_adding_result(struct gfm_connection *gfm_server,
	gfarm_ino_t *ino_p, gfarm_uint64_t *gen_p,
	gfarm_int64_t *mtime_secp, gfarm_int32_t *mtime_nsecp)
{
	return (gfm_client_rpc_result(gfm_server, 0, "llli",
	    ino_p, gen_p, mtime_secp, mtime_nsecp));
}

gfarm_error_t
gfm_client_replica_added_request(struct gfm_connection *gfm_server,
	gfarm_int32_t flags, gfarm_int64_t mtime_sec, gfarm_int32_t mtime_nsec)
{
	return (gfm_client_rpc_request(gfm_server,
	    GFM_PROTO_REPLICA_ADDED, "ili", flags, mtime_sec, mtime_nsec));
}

gfarm_error_t
gfm_client_replica_added_result(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc_result(gfm_server, 0, ""));
}

gfarm_error_t
gfm_client_replica_remove_request(struct gfm_connection *gfm_server,
	gfarm_ino_t inum, gfarm_uint64_t gen)
{
	return (gfm_client_rpc_request(gfm_server,
	    GFM_PROTO_REPLICA_REMOVE, "ll", inum, gen));
}

gfarm_error_t
gfm_client_replica_remove_result(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc_result(gfm_server, 0, ""));
}

gfarm_error_t
gfm_client_replica_add_request(struct gfm_connection *gfm_server,
	gfarm_ino_t inum, gfarm_uint64_t gen, gfarm_off_t size)
{
	return (gfm_client_rpc_request(gfm_server,
	    GFM_PROTO_REPLICA_ADD, "lll", inum, gen, size));
}

gfarm_error_t
gfm_client_replica_add_result(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc_result(gfm_server, 0, ""));
}

/*
 * process management
 */

gfarm_error_t
gfm_client_process_alloc(struct gfm_connection *gfm_server,
	gfarm_int32_t keytype, const char *sharedkey, size_t sharedkey_size,
	gfarm_pid_t *pidp)
{
	return (gfm_client_rpc(gfm_server, 0,
	    GFM_PROTO_PROCESS_ALLOC, "ib/l",
	    keytype, sharedkey_size, sharedkey, pidp));
}

gfarm_error_t
gfm_client_process_alloc_child(struct gfm_connection *gfm_server,
	gfarm_int32_t parent_keytype, const char *parent_sharedkey,
	size_t parent_sharedkey_size, gfarm_pid_t parent_pid,
	gfarm_int32_t keytype, const char *sharedkey, size_t sharedkey_size,
	gfarm_pid_t *pidp)
{
	return (gfm_client_rpc(gfm_server, 0,
	    GFM_PROTO_PROCESS_ALLOC_CHILD, "iblib/l", parent_keytype,
	    parent_sharedkey_size, parent_sharedkey, parent_pid,
	    keytype, sharedkey_size, sharedkey, pidp));
}

gfarm_error_t
gfm_client_process_free(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc(gfm_server, 0, GFM_PROTO_PROCESS_FREE, ""));
}

gfarm_error_t
gfm_client_process_set(struct gfm_connection *gfm_server,
	gfarm_int32_t keytype, const char *sharedkey, size_t sharedkey_size,
	gfarm_pid_t pid)
{
	return (gfm_client_rpc(gfm_server, 0,
	    GFM_PROTO_PROCESS_SET, "ibl/",
	    keytype, sharedkey_size, sharedkey, pid));
}

/*
 * job management
 */

gfarm_error_t
gfj_client_lock_register(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc(gfm_server, 0, GFJ_PROTO_LOCK_REGISTER,
			       "/"));
}

gfarm_error_t
gfj_client_unlock_register(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc(gfm_server, 0, GFJ_PROTO_UNLOCK_REGISTER,
			       "/"));
}

gfarm_error_t
gfj_client_register(struct gfm_connection *gfm_server,
		    struct gfarm_job_info *job, int flags,
		    int *job_idp)
{
	gfarm_error_t e;
	int i;
	gfarm_int32_t job_id;

	e = gfm_client_rpc_request(gfm_server, GFJ_PROTO_REGISTER,
				   "iisssi",
				   (gfarm_int32_t)flags,
				   (gfarm_int32_t)job->total_nodes,
				   job->job_type,
				   job->originate_host,
				   job->gfarm_url_for_scheduling,
				   (gfarm_int32_t)job->argc);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	for (i = 0; i < job->argc; i++)
		e = gfp_xdr_send(gfm_server->conn, "s", job->argv[i]);
	for (i = 0; i < job->total_nodes; i++)
		e = gfp_xdr_send(gfm_server->conn, "s",
				   job->nodes[i].hostname);
	e = gfm_client_rpc_result(gfm_server, 0, "i", &job_id);
	if (e == GFARM_ERR_NO_ERROR)
		*job_idp = job_id;
	return (e);
}

gfarm_error_t
gfj_client_unregister(struct gfm_connection *gfm_server, int job_id)
{
	return (gfm_client_rpc(gfm_server, 0, GFJ_PROTO_UNREGISTER, "i/",
			       job_id));
}

gfarm_error_t
gfj_client_list(struct gfm_connection *gfm_server, char *user,
		      int *np, int **jobsp)
{
	gfarm_error_t e;
	int i, n, eof, *jobs;
	gfarm_int32_t job_id;

	e = gfm_client_rpc(gfm_server, 0, GFJ_PROTO_LIST, "s/i", user, &n);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	GFARM_MALLOC_ARRAY(jobs, n);
	if (jobs == NULL)
		return (GFARM_ERR_NO_MEMORY);
	for (i = 0; i < n; i++) {
		e = gfp_xdr_recv(gfm_server->conn, 0, &eof, "i", &job_id);
		if (e != GFARM_ERR_NO_ERROR) {
			free(jobs);
			return (e);
		}
		if (eof) {
			free(jobs);
			return (GFARM_ERR_PROTOCOL);
		}
		jobs[i] = job_id;
	}
	*np = n;
	*jobsp = jobs;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfj_client_info_entry(struct gfp_xdr *conn,
		      struct gfarm_job_info *info)
{
	gfarm_error_t e;
	int eof, i;
	gfarm_int32_t total_nodes, argc, node_pid, node_state;
	
	e = gfp_xdr_recv(conn, 0, &eof, "issssi",
			   &total_nodes,
			   &info->user,
			   &info->job_type,
			   &info->originate_host,
			   &info->gfarm_url_for_scheduling,
			   &argc);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (eof)
		return (GFARM_ERR_PROTOCOL);
	GFARM_MALLOC_ARRAY(info->argv, argc + 1);
	GFARM_MALLOC_ARRAY(info->nodes, total_nodes);
	if (info->argv == NULL || info->nodes == NULL) {
		free(info->job_type);
		free(info->originate_host);
		free(info->gfarm_url_for_scheduling);
		if (info->argv != NULL)
			free(info->argv);
		if (info->nodes != NULL)
			free(info->nodes);
		return (GFARM_ERR_NO_MEMORY);
	}

	for (i = 0; i < argc; i++) {
		e = gfp_xdr_recv(conn, 0, &eof, "s", &info->argv[i]);
		if (e != GFARM_ERR_NO_ERROR || eof) {
			if (e == GFARM_ERR_NO_ERROR)
				e = GFARM_ERR_PROTOCOL;
			while (--i >= 0)
				free(info->argv[i]);
			free(info->job_type);
			free(info->originate_host);
			free(info->gfarm_url_for_scheduling);
			free(info->argv);
			free(info->nodes);
			return (e);
		}
	}
	info->argv[argc] = NULL;

	for (i = 0; i < total_nodes; i++) {
		e = gfp_xdr_recv(conn, 0, &eof, "sii",
				   &info->nodes[i].hostname,
				   &node_pid, &node_state);
		if (e != GFARM_ERR_NO_ERROR || eof) {
			if (e == GFARM_ERR_NO_ERROR)
				e = GFARM_ERR_PROTOCOL;
			while (--i >= 0)
				free(info->nodes[i].hostname);
			for (i = 0; i < argc; i++)
				free(info->argv[i]);
			free(info->job_type);
			free(info->originate_host);
			free(info->gfarm_url_for_scheduling);
			free(info->argv);
			free(info->nodes);
			return (e);
		}
		info->nodes[i].pid = node_pid;
		info->nodes[i].state = node_state;
	}
	info->total_nodes = total_nodes;
	info->argc = argc;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfj_client_info(struct gfm_connection *gfm_server, int n, int *jobs,
		      struct gfarm_job_info *infos)
{
	gfarm_error_t e;
	int i;

	e = gfm_client_rpc_request(gfm_server, GFJ_PROTO_INFO, "i", n);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	for (i = 0; i < n; i++) {
		e = gfp_xdr_send(gfm_server->conn, "i",
				   (gfarm_int32_t)jobs[i]);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
	}

	gfarm_job_info_clear(infos, n);
	for (i = 0; i < n; i++) {
		e = gfm_client_rpc_result(gfm_server, 0, "");
		if (e == GFARM_ERR_NO_SUCH_OBJECT)
			continue;
		if (e == GFARM_ERR_NO_ERROR)
			e = gfj_client_info_entry(gfm_server->conn, &infos[i]);
		if (e != GFARM_ERR_NO_ERROR) {
			gfarm_job_info_free_contents(infos, i - 1);
			return (e);
		}
	}
	return (GFARM_ERR_NO_ERROR);
}

void
gfarm_job_info_clear(struct gfarm_job_info *infos, int n)
{
	memset(infos, 0, sizeof(struct gfarm_job_info) * n);
}

void
gfarm_job_info_free_contents(struct gfarm_job_info *infos, int n)
{
	int i, j;
	struct gfarm_job_info *info;

	for (i = 0; i < n; i++) {
		info = &infos[i];
		if (info->user == NULL) /* this entry is not valid */
			continue;
		free(info->user);
		free(info->job_type);
		free(info->originate_host);
		free(info->gfarm_url_for_scheduling);
		for (j = 0; j < info->argc; j++)
			free(info->argv[j]);
		free(info->argv);
		for (j = 0; j < info->total_nodes; j++)
			free(info->nodes[j].hostname);
		free(info->nodes);
	}
}

/*
 * convenience function
 */
gfarm_error_t
gfarm_user_job_register(struct gfm_connection *gfm_server,
			int nusers, char **users,
			char *job_type, char *sched_file,
			int argc, char **argv,
			int *job_idp)
{
	gfarm_error_t e;
	int i;
	struct gfarm_job_info job_info;

	gfarm_job_info_clear(&job_info, 1);
	job_info.total_nodes = nusers;
	job_info.user = gfarm_get_global_username();
	job_info.job_type = job_type;
	e = gfarm_host_get_canonical_self_name(&job_info.originate_host);
	if (e == GFARM_ERR_UNKNOWN_HOST) {
		/*
		 * gfarm client doesn't have to be a compute user,
		 * so, we should allow non canonical name here.
		 */
		job_info.originate_host = gfarm_host_get_self_name();
	} else if (e != GFARM_ERR_NO_ERROR)
		return (e);
	job_info.gfarm_url_for_scheduling = sched_file;
	job_info.argc = argc;
	job_info.argv = argv;
	GFARM_MALLOC_ARRAY(job_info.nodes, nusers);
	if (job_info.nodes == NULL)
		return (GFARM_ERR_NO_MEMORY);
	for (i = 0; i < nusers; i++) {
		e = gfarm_host_get_canonical_name(users[i],
		    &job_info.nodes[i].hostname);
		if (e != GFARM_ERR_NO_ERROR) {
			while (--i >= 0)
				free(job_info.nodes[i].hostname);
			free(job_info.nodes);
			return (e);
		}
	}
	e = gfj_client_register(gfm_server, &job_info, 0, job_idp);
	for (i = 0; i < nusers; i++)
		free(job_info.nodes[i].hostname);
	free(job_info.nodes);
	return (e);
}
