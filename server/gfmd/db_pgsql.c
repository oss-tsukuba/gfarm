/*
 * Copyright (c) 2003-2006 National Institute of Advanced
 * Industrial Science and Technology (AIST).  All rights reserved.
 *
 * Copyright (c) 2006 National Institute of Informatics in Japan,
 * All rights reserved.
 *
 * This file or a portion of this file is licensed under the terms of
 * the NAREGI Public License, found at
 * http://www.naregi.org/download/index.html.
 * If you redistribute this file, with or without modifications, you
 * must include this notice in the file.
 */

/*
 * $Id$
 */

#include <stdlib.h>
#include <string.h>
#include <libpq-fe.h>
#include <netinet/in.h>
#include <gfarm/gfarm.h>

#include "gfutil.h"

#include "config.h"
#include "metadb_common.h"

#include "db_common.h"
#include "db_access.h"
#include "db_ops.h"

#define GFARM_PGSQL_ERRCODE_UNIQUE_VIOLATION "23505"

/**********************************************************************/

static PGconn *conn = NULL;

static char *
gfarm_pgsql_make_conninfo(const char **varnames, char **varvalues, int n,
	char *others)
{
	int i;
	size_t length = 0;
	char *v, *conninfo, *p;

	/* count necessary string length */
	for (i = 0; i < n; i++) {
		if ((v = varvalues[i]) != NULL) {
			if (i > 1)
				length++; /* space */
			length += strlen(varnames[i]) + 3; /* var='' */
			for (; *v != '\0'; v++) {
				if (*v == '\'' || *v == '\\')
					++length;
				++length;
			}
		}
	}
	if (others != NULL) {
		if (length > 0)
			++length; /* space */
		length += strlen(others);
	}
	++length; /* '\0' */

	conninfo = malloc(length);
	if (conninfo == NULL)
		return (NULL);

	p = conninfo;
	for (i = 0; i < n; i++) {
		if ((v = varvalues[i]) != NULL) {
			if (i > 1)
				*p++ = ' ';
			p += sprintf(p, "%s='", varnames[i]);
			for (; *v != '\0'; v++) {
				if (*v == '\'' || *v == '\\')
					*p++ = '\\';
				*p++ = *v;
			}
			*p++ = '\'';
		}
	}
	if (others != NULL) {
		if (p > conninfo)
			*p++ = ' ';
		p += sprintf(p, "%s", others);
	}
	*p++ = '\0';
	return (conninfo);
}

gfarm_error_t
gfarm_pgsql_initialize(void)
{
	int port;
	static const char *varnames[] = {
		"host", "port", "dbname", "user", "password"
	};
	char *varvalues[GFARM_ARRAY_LENGTH(varnames)];
	char *e, *conninfo;

	/*
	 * sanity check:
	 * all parameters can be NULL,
	 * in such cases, enviroment variables will be used.
	 */
	if (gfarm_postgresql_server_port != NULL) {
		port = strtol(gfarm_postgresql_server_port, &e, 0);
		if (e == gfarm_postgresql_server_port ||
		    port <= 0 || port >= 65536) {
			gflog_error("gfmd.conf: illegal value in "
			    "postgresql_serverport (%s)",
			    gfarm_postgresql_server_port);
			return (GFARM_ERR_INVALID_ARGUMENT);
		}
	}

	varvalues[0] = gfarm_postgresql_server_name;
	varvalues[1] = gfarm_postgresql_server_port;
	varvalues[2] = gfarm_postgresql_dbname;
	varvalues[3] = gfarm_postgresql_user;
	varvalues[4] = gfarm_postgresql_password;
	conninfo = gfarm_pgsql_make_conninfo(
	    varnames, varvalues, GFARM_ARRAY_LENGTH(varnames),
	    gfarm_postgresql_conninfo);
	if (conninfo == NULL)
		return (GFARM_ERR_NO_MEMORY);

	/*
	 * initialize PostgreSQL
	 */

	/* open a connection */
	conn = PQconnectdb(conninfo);
	free(conninfo);

	if (PQstatus(conn) != CONNECTION_OK) {
		/* PQerrorMessage's return value will be freed in PQfinish() */
		gflog_error("connecting PostgreSQL: %s", PQerrorMessage(conn));
		return (GFARM_ERR_CONNECTION_REFUSED);
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfarm_pgsql_terminate(void)
{
	/* close and free connection resources */
	PQfinish(conn);

	return (GFARM_ERR_NO_ERROR);
}


/**********************************************************************/

uint64_t
gfarm_hton64(uint64_t h64)
{
#ifdef WORDS_BIGENDIAN
	return (h64); /* same order */
#else
	return (htonl((uint32_t)((h64 >> 32) & 0xffffffff)) |
	    ((uint64_t)htonl((uint32_t)h64 & 0xffffffff) << 32));
#endif
}

uint64_t
gfarm_ntoh64(uint64_t n64)
{
	return (gfarm_hton64(n64));
}

/**********************************************************************/

static gfarm_error_t
host_info_get_one(
	PGresult *res,
	int startrow,
	int nhostaliases,
	struct gfarm_host_info *info)
{
	int i;

	info->hostname = strdup(
		PQgetvalue(res, startrow, PQfnumber(res, "hostname")));
	info->port = ntohl(
	    *((uint32_t *)PQgetvalue(res, startrow, PQfnumber(res, "port"))));
	info->architecture = strdup(
		PQgetvalue(res, startrow, PQfnumber(res, "architecture")));
	info->ncpu = ntohl(
	    *((uint32_t *)PQgetvalue(res, startrow, PQfnumber(res, "ncpu"))));
	info->flags = ntohl(
	    *((uint32_t *)PQgetvalue(res, startrow, PQfnumber(res, "flags"))));
	info->nhostaliases = nhostaliases;
	info->hostaliases =
	    malloc(sizeof(*(info->hostaliases)) * (nhostaliases + 1));
	for (i = 0; i < nhostaliases; i++) {
		info->hostaliases[i] = strdup(
			PQgetvalue(res, startrow + i,
				   PQfnumber(res, "hostalias")));
	}
	info->hostaliases[info->nhostaliases] = NULL;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
hostaliases_remove(const char *hostname)
{
	PGresult *res;
	const char *paramValues[1];
	gfarm_error_t e = GFARM_ERR_NO_ERROR;

 retry:
	paramValues[0] = hostname;
	res = PQexecParams(conn,
		"DELETE FROM HostAliases WHERE hostname = $1",
		1, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0); /* dummy parameter for result format */

	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		if (PQstatus(conn) == CONNECTION_BAD) {
			PQreset(conn);
			if (PQstatus(conn) == CONNECTION_OK) {
				PQclear(res);
				goto retry;
			}
		}
		gflog_error("hostaliases_remove: %s",
		    PQresultErrorMessage(res));
		e = GFARM_ERR_UNKNOWN;
	}
	PQclear(res);
	return (e);
}

static gfarm_error_t
hostaliases_set(struct gfarm_host_info *info)
{
	PGresult *res;
	const char *paramValues[2];
	char *s;
	int i;
	gfarm_error_t e = GFARM_ERR_NO_ERROR;

	if (info->hostaliases != NULL) {
		for (i = 0; i < info->nhostaliases; i++) {
			paramValues[0] = info->hostname;
			paramValues[1] = info->hostaliases[i];
			res = PQexecParams(conn,
				"INSERT INTO Hostaliases (hostname, hostalias)"
				    " VALUES ($1, $2)",
				2, /* number of params */
				NULL, /* param types */
				paramValues,
				NULL, /* param lengths */
				NULL, /* param formats */
				0); /* dummy parameter for result format */
			if (PQresultStatus(res) != PGRES_COMMAND_OK) {
				s = PQresultErrorMessage(res);
				/* XXX */
				if (strstr(
				    s,
				    "duplicate key violates unique constraint")
				    != NULL) {
					e = GFARM_ERR_ALREADY_EXISTS;
				} else {
					e = GFARM_ERR_UNKNOWN;
				}
				gflog_info("hostaliases_set: %s", s);
				PQclear(res);
				return (e);
			}
			PQclear(res);
		}
	}
	return (e);
}

static void
gfarm_pgsql_host_add(struct gfarm_host_info *info)
{
	PGresult *res;
	const char *paramValues[5];
	gfarm_error_t e;
	char *s;
	char port[GFARM_INT32STRLEN + 1];
	char ncpu[GFARM_INT32STRLEN + 1];
	char flags[GFARM_INT32STRLEN + 1];

 retry:
	res = PQexec(conn, "BEGIN");
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		if (PQstatus(conn) == CONNECTION_BAD) {
			PQreset(conn);
			if (PQstatus(conn) == CONNECTION_OK) {
				PQclear(res);
				goto retry;
			}
		}
		gflog_error("host_add BEGIN: %s",
		    PQresultErrorMessage(res));
		e = GFARM_ERR_UNKNOWN;
		PQclear(res);
		goto end;
	}
	PQclear(res);

	paramValues[0] = info->hostname;
	sprintf(port, "%d", info->port);
	paramValues[1] = port;
	paramValues[2] = info->architecture;
	sprintf(ncpu, "%d", info->ncpu);
	paramValues[3] = ncpu;
	sprintf(flags, "%d", info->flags);
	paramValues[4] = flags;
	res = PQexecParams(conn,
		"INSERT INTO Host (hostname, port, architecture, ncpu, flags) "
		    "VALUES ($1, $2, $3, $4, $5)",
		5, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0); /* dummy parameter for result format */
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		s = PQresultErrorMessage(res);
		/* XXX */
		if (strstr(
		    s, "duplicate key violates unique constraint") != NULL) {
			e = GFARM_ERR_ALREADY_EXISTS;
		} else {
			e = GFARM_ERR_UNKNOWN;
		}
		gflog_error("host_add INSERT: %s", s);
		PQclear(res);
		goto end;
	}
	PQclear(res);

	e = hostaliases_set(info);
 end:
	if (e == GFARM_ERR_NO_ERROR)
		res = PQexec(conn, "COMMIT");
	else
		res = PQexec(conn, "ROLLBACK");
	PQclear(res);

	free(info);
}

static void
gfarm_pgsql_host_modify(struct db_host_modify_arg *arg)
{
	struct gfarm_host_info *info = &arg->hi;
	PGresult *res;
	const char *paramValues[5];
	gfarm_error_t e;
	char port[GFARM_INT32STRLEN + 1];
	char ncpu[GFARM_INT32STRLEN + 1];
	char flags[GFARM_INT32STRLEN + 1];

 retry:
	res = PQexec(conn, "BEGIN");
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		if (PQstatus(conn) == CONNECTION_BAD) {
			PQreset(conn);
			if (PQstatus(conn) == CONNECTION_OK) {
				PQclear(res);
				goto retry;
			}
		}
		gflog_error("host_modify BEGIN: %s",
		    PQresultErrorMessage(res));
		e = GFARM_ERR_UNKNOWN;
		PQclear(res);
		goto end;
	}
	PQclear(res);

	paramValues[0] = info->hostname;
	sprintf(port, "%d", info->port);
	paramValues[1] = port;
	paramValues[2] = info->architecture;
	sprintf(ncpu, "%d", info->ncpu);
	paramValues[3] = ncpu;
	sprintf(flags, "%d", info->flags);
	paramValues[4] = flags;
	res = PQexecParams(conn,
		"UPDATE Host "
		    "SET port = $2, architecture = $3, ncpu = $4, flags = $5 "
		    "WHERE hostname = $1",
		5, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0); /* dummy parameter for result format */
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		gflog_error("host_modify UPDATE: %s",
		    PQresultErrorMessage(res));
		e = GFARM_ERR_UNKNOWN;
		PQclear(res);
		goto end;
	}
	if (strtol(PQcmdTuples(res), NULL, 0) == 0) {
		e = GFARM_ERR_NO_SUCH_OBJECT;
		PQclear(res);
		goto end;
	}
	PQclear(res);

	e = hostaliases_remove(info->hostname);
	if (e != GFARM_ERR_NO_ERROR)
		goto end;
	e = hostaliases_set(info);
 end:
	if (e == GFARM_ERR_NO_ERROR)
		res = PQexec(conn, "COMMIT");
	else
		res = PQexec(conn, "ROLLBACK");
	PQclear(res);

	free(arg);
}

static void
gfarm_pgsql_host_remove(char *hostname)
{
	PGresult *res;
	const char *paramValues[1];
	gfarm_error_t e;

 retry:
	paramValues[0] = hostname;
	res = PQexecParams(conn,
		"DELETE FROM Host WHERE hostname = $1",
		1, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0);  /* dummy parameter for result format */

	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		if (PQstatus(conn) == CONNECTION_BAD) {
			PQreset(conn);
			if (PQstatus(conn) == CONNECTION_OK) {
				PQclear(res);
				goto retry;
			}
		}
		gflog_error("host_remove DELETE: %s",
		    PQresultErrorMessage(res));
		e = GFARM_ERR_UNKNOWN;
	} else if (strtol(PQcmdTuples(res), NULL, 0) == 0)
		e = GFARM_ERR_NO_SUCH_OBJECT;
	PQclear(res);

	free(hostname);
}

static gfarm_error_t
host_info_get_all(
	char *csql,
	char *isql,
	int nparams,
	const char **paramValues,
	int *np,
	struct gfarm_host_info **infosp)
{
	PGresult *res, *ires, *cres;
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	int i, startrow;
	struct gfarm_host_info *ip;

 retry:
	res = PQexec(conn, "BEGIN");
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		if (PQstatus(conn) == CONNECTION_BAD) {
			PQreset(conn);
			if (PQstatus(conn) == CONNECTION_OK) {
				PQclear(res);
				goto retry;
			}
		}
		gflog_error("host_info_get_all BEGIN: %s",
		    PQresultErrorMessage(res));
		e = GFARM_ERR_UNKNOWN;
		PQclear(res);
		goto end;
	}
	PQclear(res);

	cres = PQexecParams(conn,
		csql,
		nparams,
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		1); /* ask for binary results */
	if (PQresultStatus(cres) != PGRES_TUPLES_OK) {
		gflog_error("host_info_get_all count: %s",
		    PQresultErrorMessage(res));
		e = GFARM_ERR_UNKNOWN;
		PQclear(cres);
		goto clear_cres;
	}
	*np = PQntuples(cres); /* number of hosts */
	if (*np == 0) {
		e = GFARM_ERR_NO_SUCH_OBJECT;
		goto clear_cres;
	}
	ip = malloc(sizeof(*ip) * *np);
	if (ip == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto clear_cres;
	}

	ires = PQexecParams(conn,
		isql,
		nparams,
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		1); /* ask for binary results */
	if (PQresultStatus(ires) != PGRES_TUPLES_OK) {
		gflog_error("host_info_get_all information: %s",
		    PQresultErrorMessage(res));
		e = GFARM_ERR_UNKNOWN;
		free(ip);
		goto clear_ires;
	}

	startrow = 0;
	for (i = 0; i < PQntuples(cres); i++) {
		int nhostaliases;

		nhostaliases = gfarm_ntoh64(*((uint64_t *)PQgetvalue(cres,
						i,
						PQfnumber(cres, "count"))));
		gfarm_base_host_info_ops.clear(&ip[i]);
		e = host_info_get_one(ires, startrow, nhostaliases, &ip[i]);
		startrow += (nhostaliases == 0 ? 1 : nhostaliases);
	}
	*infosp = ip;

 clear_ires:
	PQclear(ires);
 clear_cres:
	PQclear(cres);
 end:
	res = PQexec(conn, "END");
	PQclear(res);

	return (e);
}

static gfarm_error_t
gfarm_pgsql_host_load(void *closure,
	void (*callback)(void *, struct gfarm_host_info *))
{
	gfarm_error_t e;
	int i, n;
	struct gfarm_host_info *infos;

	e = host_info_get_all(
		"SELECT Host.hostname, count(hostalias) "
		    "FROM Host LEFT OUTER JOIN HostAliases "
			"ON Host.hostname = HostAliases.hostname "
		    "GROUP BY Host.hostname "
		    "ORDER BY Host.hostname",

		"SELECT Host.hostname, port, architecture, ncpu, flags, "
				"hostalias "
		    "FROM Host LEFT OUTER JOIN HostAliases "
			"ON Host.hostname = HostAliases.hostname "
		    "ORDER BY Host.hostname, hostalias",

		0,
		NULL,
		&n,
		&infos);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	for (i = 0; i < n; i++)
		(*callback)(closure, &infos[i]);

	free(infos);
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

static void
user_info_set_field(
	PGresult *res,
	int row,
	struct gfarm_user_info *info)
{
	info->username = strdup(
		PQgetvalue(res, row, PQfnumber(res, "username")));
	info->homedir = strdup(
		PQgetvalue(res, row, PQfnumber(res, "homedir")));
	info->realname = strdup(
		PQgetvalue(res, row, PQfnumber(res, "realname")));
	info->gsi_dn = strdup(
		PQgetvalue(res, row, PQfnumber(res, "gsiDN")));
}

static void
gfarm_pgsql_user_add(struct gfarm_user_info *info)
{
	PGresult *res;
	const char *paramValues[4];
	gfarm_error_t e;

 retry:
	paramValues[0] = info->username;
	paramValues[1] = info->homedir;
	paramValues[2] = info->realname;
	paramValues[3] = info->gsi_dn;
	res = PQexecParams(conn,
		"INSERT INTO GfarmUser (username, homedir, realname, gsiDN) "
		     "VALUES ($1, $2, $3, $4)",
		4, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0); /* dummy parameter for result format */
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		if (PQstatus(conn) == CONNECTION_BAD) {
			PQreset(conn);
			if (PQstatus(conn) == CONNECTION_OK) {
				PQclear(res);
				goto retry;
			}
		}
		if (strcmp(
			   PQresultErrorField(res, PG_DIAG_SQLSTATE),
			   GFARM_PGSQL_ERRCODE_UNIQUE_VIOLATION) == 0) {
			e = GFARM_ERR_ALREADY_EXISTS;
		} else {
			e = GFARM_ERR_UNKNOWN;
		}
		gflog_error("user_add INSERT: %s",
		    PQresultErrorMessage(res));
	}
	PQclear(res);

	free(info);
}

static void
gfarm_pgsql_user_modify(struct db_user_modify_arg *arg)
{
	struct gfarm_user_info *info = &arg->ui;
	PGresult *res;
	const char *paramValues[4];
	gfarm_error_t e = GFARM_ERR_NO_ERROR;

 retry:
	paramValues[0] = info->username;
	paramValues[1] = info->homedir;
	paramValues[2] = info->realname;
	paramValues[3] = info->gsi_dn;
	res = PQexecParams(conn,
		"UPDATE GfarmUser "
		     "SET homedir = $2, realname = $3, gsiDN = $4 "
		     "WHERE username = $1",
		4, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0); /* dummy parameter for result format */
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		if (PQstatus(conn) == CONNECTION_BAD) {
			PQreset(conn);
			if (PQstatus(conn) == CONNECTION_OK) {
				PQclear(res);
				goto retry;
			}
		}
		gflog_error("user_modify UPDATE: %s",
		    PQresultErrorMessage(res));
		e = GFARM_ERR_UNKNOWN;
	} else if (strtol(PQcmdTuples(res), NULL, 0) == 0) {
		e = GFARM_ERR_NO_SUCH_OBJECT;
	}
	PQclear(res);

	free(arg);
}

static void
gfarm_pgsql_user_remove(char *username)
{
	PGresult *res;
	const char *paramValues[1];
	gfarm_error_t e;

 retry:
	paramValues[0] = username;
	res = PQexecParams(conn,
		"DELETE FROM GfarmUser WHERE username = $1",
		1, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0);  /* dummy parameter for result format */
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		if (PQstatus(conn) == CONNECTION_BAD) {
			PQreset(conn);
			if (PQstatus(conn) == CONNECTION_OK) {
				PQclear(res);
				goto retry;
			}
		}
		gflog_error("user_remove DELETE: %s",
		    PQresultErrorMessage(res));
		e = GFARM_ERR_UNKNOWN;
	} else if (strtol(PQcmdTuples(res), NULL, 0) == 0)
		e = GFARM_ERR_NO_SUCH_OBJECT;
	PQclear(res);

	free(username);
}

static gfarm_error_t
user_info_get_all(
	const char *sql,
	int nparams,
	const char **paramValues,
	int *np,
	struct gfarm_user_info **infosp)
{
	PGresult *res;
	gfarm_error_t e;
	struct gfarm_user_info *ip;
	int i;

 retry:
	res = PQexecParams(conn,
		sql,
		nparams,
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		1); /* ask for binary results */
	if (PQresultStatus(res) != PGRES_TUPLES_OK) {
		if (PQstatus(conn) == CONNECTION_BAD) {
			PQreset(conn);
			if (PQstatus(conn) == CONNECTION_OK) {
				PQclear(res);
				goto retry;
			}
		}
		gflog_error("user_info_get_all information: %s",
		    PQresultErrorMessage(res));
		e = GFARM_ERR_UNKNOWN;
		PQclear(res);
		return (e);
	}
	*np = PQntuples(res);
	if (*np == 0) {
		PQclear(res);
		return (GFARM_ERR_NO_SUCH_OBJECT);
	}
	ip = malloc(sizeof(*ip) * *np);
	if (ip == NULL) {
		PQclear(res);
		return (GFARM_ERR_NO_MEMORY);
	}
	for (i = 0; i < *np; i++) {
		gfarm_base_user_info_ops.clear(&ip[i]);
		user_info_set_field(res, i, &ip[i]);
	}
	*infosp = ip;
	PQclear(res);
	return (GFARM_ERR_NO_ERROR);
}


static gfarm_error_t
gfarm_pgsql_user_load(void *closure,
	void (*callback)(void *, struct gfarm_user_info *))
{
	gfarm_error_t e;
	int i, n;
	struct gfarm_user_info *infos;

	e = user_info_get_all(
		"SELECT * FROM GfarmUser",
		0,
		NULL,
		&n,
		&infos);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	for (i = 0; i < n; i++)
		(*callback)(closure, &infos[i]);

	free(infos);
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

static gfarm_error_t
group_info_get_one(
	PGresult *res,
	int startrow,
	int nusers,
	struct gfarm_group_info *info)
{
	int i;

	info->groupname = strdup(
		PQgetvalue(res, startrow, PQfnumber(res, "groupname")));
	info->nusers = nusers;
	info->usernames =
	    malloc(sizeof(*(info->usernames)) * (nusers + 1));
	for (i = 0; i < nusers; i++) {
		info->usernames[i] = strdup(
			PQgetvalue(res, startrow + i,
				   PQfnumber(res, "username")));
	}
	info->usernames[info->nusers] = NULL;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
grpassign_remove(const char *groupname)
{
	PGresult *res;
	const char *paramValues[1];
	gfarm_error_t e = GFARM_ERR_NO_ERROR;

 retry:
	paramValues[0] = groupname;
	res = PQexecParams(conn,
		"DELETE FROM GfarmGroupAssignment WHERE groupname = $1",
		1, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0); /* dummy parameter for result format */

	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		if (PQstatus(conn) == CONNECTION_BAD) {
			PQreset(conn);
			if (PQstatus(conn) == CONNECTION_OK) {
				PQclear(res);
				goto retry;
			}
		}
		gflog_error("grpassign_remove: %s",
		    PQresultErrorMessage(res));
		e = GFARM_ERR_UNKNOWN;
	}
	PQclear(res);
	return (e);
}

static gfarm_error_t
grpassign_set(struct gfarm_group_info *info)
{
	PGresult *res;
	const char *paramValues[2];
	char *s;
	int i;
	gfarm_error_t e = GFARM_ERR_NO_ERROR;

	if (info->usernames != NULL) {
		for (i = 0; i < info->nusers; i++) {
			paramValues[0] = info->groupname;
			paramValues[1] = info->usernames[i];
			res = PQexecParams(conn,
				"INSERT INTO GfarmGroupAssignment "
				    " (groupname, username)"
				    " VALUES ($1, $2)",
				2, /* number of params */
				NULL, /* param types */
				paramValues,
				NULL, /* param lengths */
				NULL, /* param formats */
				0); /* dummy parameter for result format */
			if (PQresultStatus(res) != PGRES_COMMAND_OK) {
				s = PQresultErrorMessage(res);
				/* XXX */
				if (strstr(
				    s,
				    "duplicate key violates unique constraint")
				    != NULL) {
					e = GFARM_ERR_ALREADY_EXISTS;
				} else {
					e = GFARM_ERR_UNKNOWN;
				}
				gflog_info("grpassign_set: %s", s);
				PQclear(res);
				return (e);
			}
			PQclear(res);
		}
	}
	return (e);
}

static void
gfarm_pgsql_group_add(struct gfarm_group_info *info)
{
	PGresult *res;
	const char *paramValues[1];
	gfarm_error_t e;
	char *s;

 retry:
	res = PQexec(conn, "BEGIN");
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		if (PQstatus(conn) == CONNECTION_BAD) {
			PQreset(conn);
			if (PQstatus(conn) == CONNECTION_OK) {
				PQclear(res);
				goto retry;
			}
		}
		gflog_error("group_add BEGIN: %s",
		    PQresultErrorMessage(res));
		e = GFARM_ERR_UNKNOWN;
		PQclear(res);
		goto end;
	}
	PQclear(res);

	paramValues[0] = info->groupname;
	res = PQexecParams(conn,
		"INSERT INTO GfarmGroup (groupname) VALUES ($1)",
		1, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0); /* dummy parameter for result format */
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		s = PQresultErrorMessage(res);
		/* XXX */
		if (strstr(
		    s, "duplicate key violates unique constraint") != NULL) {
			e = GFARM_ERR_ALREADY_EXISTS;
		} else {
			e = GFARM_ERR_UNKNOWN;
		}
		gflog_error("group_add INSERT: %s", s);
		PQclear(res);
		goto end;
	}
	PQclear(res);

	e = grpassign_set(info);
 end:
	if (e == GFARM_ERR_NO_ERROR)
		res = PQexec(conn, "COMMIT");
	else
		res = PQexec(conn, "ROLLBACK");
	PQclear(res);

	free(info);
}

static void
gfarm_pgsql_group_modify(struct db_group_modify_arg *arg)
{
	struct gfarm_group_info *info = &arg->gi;
	PGresult *res;
	gfarm_error_t e;

 retry:
	res = PQexec(conn, "BEGIN");
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		if (PQstatus(conn) == CONNECTION_BAD) {
			PQreset(conn);
			if (PQstatus(conn) == CONNECTION_OK) {
				PQclear(res);
				goto retry;
			}
		}
		gflog_error("group_modify BEGIN: %s",
		    PQresultErrorMessage(res));
		e = GFARM_ERR_UNKNOWN;
		PQclear(res);
		goto end;
	}
	PQclear(res);

	e = grpassign_remove(info->groupname);
	if (e != GFARM_ERR_NO_ERROR)
		goto end;
	e = grpassign_set(info);
 end:
	if (e == GFARM_ERR_NO_ERROR)
		res = PQexec(conn, "COMMIT");
	else
		res = PQexec(conn, "ROLLBACK");
	PQclear(res);

	free(arg);
}

static void
gfarm_pgsql_group_remove(char *groupname)
{
	PGresult *res;
	const char *paramValues[1];
	gfarm_error_t e;

 retry:
	paramValues[0] = groupname;
	res = PQexecParams(conn,
		"DELETE FROM GfarmGroup WHERE groupname = $1",
		1, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0);  /* dummy parameter for result format */

	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		if (PQstatus(conn) == CONNECTION_BAD) {
			PQreset(conn);
			if (PQstatus(conn) == CONNECTION_OK) {
				PQclear(res);
				goto retry;
			}
		}
		gflog_error("group_remove DELETE: %s",
		    PQresultErrorMessage(res));
		e = GFARM_ERR_UNKNOWN;
	} else if (strtol(PQcmdTuples(res), NULL, 0) == 0)
		e = GFARM_ERR_NO_SUCH_OBJECT;
	PQclear(res);

	free(groupname);
}

static gfarm_error_t
group_info_get_all(
	char *csql,
	char *isql,
	int nparams,
	const char **paramValues,
	int *np,
	struct gfarm_group_info **infosp)
{
	PGresult *res, *ires, *cres;
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	int i, startrow;
	struct gfarm_group_info *ip;

 retry:
	res = PQexec(conn, "BEGIN");
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		if (PQstatus(conn) == CONNECTION_BAD) {
			PQreset(conn);
			if (PQstatus(conn) == CONNECTION_OK) {
				PQclear(res);
				goto retry;
			}
		}
		gflog_error("group_info_get_all BEGIN: %s",
		    PQresultErrorMessage(res));
		e = GFARM_ERR_UNKNOWN;
		PQclear(res);
		goto end;
	}
	PQclear(res);

	cres = PQexecParams(conn,
		csql,
		nparams,
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		1); /* ask for binary results */
	if (PQresultStatus(cres) != PGRES_TUPLES_OK) {
		gflog_error("group_info_get_all count: %s",
		    PQresultErrorMessage(res));
		e = GFARM_ERR_UNKNOWN;
		PQclear(cres);
		goto clear_cres;
	}
	*np = PQntuples(cres); /* number of groups */
	if (*np == 0) {
		e = GFARM_ERR_NO_SUCH_OBJECT;
		goto clear_cres;
	}
	ip = malloc(sizeof(*ip) * *np);
	if (ip == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto clear_cres;
	}

	ires = PQexecParams(conn,
		isql,
		nparams,
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		1); /* ask for binary results */
	if (PQresultStatus(ires) != PGRES_TUPLES_OK) {
		gflog_error("group_info_get_all information: %s",
		    PQresultErrorMessage(res));
		e = GFARM_ERR_UNKNOWN;
		free(ip);
		goto clear_ires;
	}

	startrow = 0;
	for (i = 0; i < PQntuples(cres); i++) {
		int nusers;

		nusers = gfarm_ntoh64(*((uint64_t *)PQgetvalue(cres,
						i,
						PQfnumber(cres, "count"))));
		gfarm_base_group_info_ops.clear(&ip[i]);
		e = group_info_get_one(ires, startrow, nusers, &ip[i]);
		startrow += (nusers == 0 ? 1 : nusers);
	}
	*infosp = ip;

 clear_ires:
	PQclear(ires);
 clear_cres:
	PQclear(cres);
 end:
	res = PQexec(conn, "END");
	PQclear(res);

	return (e);
}

static gfarm_error_t
gfarm_pgsql_group_load(void *closure,
	void (*callback)(void *, struct gfarm_group_info *))
{
	gfarm_error_t e;
	int i, n;
	struct gfarm_group_info *infos;

	e = group_info_get_all(
		"SELECT GfarmGroup.groupname, count(username) "
		    "FROM GfarmGroup LEFT OUTER JOIN GfarmGroupAssignment "
		    "ON GfarmGroup.groupname = GfarmGroupAssignment.groupname "
		    "GROUP BY GfarmGroup.groupname "
		    "ORDER BY GfarmGroup.groupname",

		"SELECT GfarmGroup.groupname, username "
		    "FROM GfarmGroup LEFT OUTER JOIN GfarmGroupAssignment "
		    "ON GfarmGroup.groupname = GfarmGroupAssignment.groupname "
		    "ORDER BY GfarmGroup.groupname, username",

		0,
		NULL,
		&n,
		&infos);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	for (i = 0; i < n; i++)
		(*callback)(closure, &infos[i]);

	free(infos);
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

static void
gfarm_pgsql_inode_add(struct gfs_stat *info)
{
	PGresult *res;
	const char *paramValues[13];
	gfarm_error_t e;
	char inumber[GFARM_INT64STRLEN + 1];
	char igen[GFARM_INT64STRLEN + 1];
	char nlink[GFARM_INT64STRLEN + 1];
	char size[GFARM_INT64STRLEN + 1];
	char mode[GFARM_INT32STRLEN + 1];
	char atimesec[GFARM_INT64STRLEN + 1];
	char atimensec[GFARM_INT32STRLEN + 1];
	char mtimesec[GFARM_INT64STRLEN + 1];
	char mtimensec[GFARM_INT32STRLEN + 1];
	char ctimesec[GFARM_INT64STRLEN + 1];
	char ctimensec[GFARM_INT32STRLEN + 1];

 retry:
	sprintf(inumber, "%" GFARM_PRId64, info->st_ino);
	paramValues[0] = inumber;
	sprintf(igen, "%" GFARM_PRId64, info->st_gen);
	paramValues[1] = igen;
	sprintf(nlink, "%" GFARM_PRId64, info->st_nlink);
	paramValues[2] = nlink;
	sprintf(size, "%" GFARM_PRId64, info->st_size);
	paramValues[3] = size;
	sprintf(mode, "%d", info->st_mode);
	paramValues[4] = mode;
	paramValues[5] = info->st_user;
	paramValues[6] = info->st_group;
	sprintf(atimesec, "%" GFARM_PRId64, info->st_atimespec.tv_sec);
	paramValues[7] = atimesec;
	sprintf(atimensec, "%d", info->st_atimespec.tv_nsec);
	paramValues[8] = atimensec;
	sprintf(mtimesec, "%" GFARM_PRId64, info->st_mtimespec.tv_sec);
	paramValues[9] = mtimesec;
	sprintf(mtimensec, "%d", info->st_mtimespec.tv_nsec);
	paramValues[10] = mtimensec;
	sprintf(ctimesec, "%" GFARM_PRId64, info->st_ctimespec.tv_sec);
	paramValues[11] = ctimesec;
	sprintf(ctimensec, "%d", info->st_ctimespec.tv_nsec);
	paramValues[12] = ctimensec;

	res = PQexecParams(conn,
		"INSERT INTO INode (inumber, igen, nlink, size, mode, "
			           "username, groupname, "
				   "atimesec, atimensec, "
				   "mtimesec, mtimensec, "
				   "ctimesec, ctimensec) "
		    "VALUES ($1, $2, $3, $4, $5, "
			    "$6, $7, $8, $9 ,$10, $11, $12, $13)",
		13, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0); /* dummy parameter for result format */
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		if (PQstatus(conn) == CONNECTION_BAD) {
			PQreset(conn);
			if (PQstatus(conn) == CONNECTION_OK) {
				PQclear(res);
				goto retry;
			}
		}
		if (strcmp(
			   PQresultErrorField(res, PG_DIAG_SQLSTATE),
			   GFARM_PGSQL_ERRCODE_UNIQUE_VIOLATION) == 0) {
			e = GFARM_ERR_ALREADY_EXISTS;
		} else {
			e = GFARM_ERR_UNKNOWN;
		}
		gflog_error("inode_add INSERT: %s",
		    PQresultErrorMessage(res));
	}
	PQclear(res);

	free(info);
}

static void
gfarm_pgsql_inode_modify(struct gfs_stat *info)
{
	PGresult *res;
	const char *paramValues[13];
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	char inumber[GFARM_INT64STRLEN + 1];
	char igen[GFARM_INT64STRLEN + 1];
	char nlink[GFARM_INT64STRLEN + 1];
	char size[GFARM_INT64STRLEN + 1];
	char mode[GFARM_INT32STRLEN + 1];
	char atimesec[GFARM_INT64STRLEN + 1];
	char atimensec[GFARM_INT32STRLEN + 1];
	char mtimesec[GFARM_INT64STRLEN + 1];
	char mtimensec[GFARM_INT32STRLEN + 1];
	char ctimesec[GFARM_INT64STRLEN + 1];
	char ctimensec[GFARM_INT32STRLEN + 1];

 retry:
	sprintf(inumber, "%" GFARM_PRId64, info->st_ino);
	paramValues[0] = inumber;
	sprintf(igen, "%" GFARM_PRId64, info->st_gen);
	paramValues[1] = igen;
	sprintf(nlink, "%" GFARM_PRId64, info->st_nlink);
	paramValues[2] = nlink;
	sprintf(size, "%" GFARM_PRId64, info->st_size);
	paramValues[3] = size;
	sprintf(mode, "%d", info->st_mode);
	paramValues[4] = mode;
	paramValues[5] = info->st_user;
	paramValues[6] = info->st_group;
	sprintf(atimesec, "%" GFARM_PRId64, info->st_atimespec.tv_sec);
	paramValues[7] = atimesec;
	sprintf(atimensec, "%d", info->st_atimespec.tv_nsec);
	paramValues[8] = atimensec;
	sprintf(mtimesec, "%" GFARM_PRId64, info->st_mtimespec.tv_sec);
	paramValues[9] = mtimesec;
	sprintf(mtimensec, "%d", info->st_mtimespec.tv_nsec);
	paramValues[10] = mtimensec;
	sprintf(ctimesec, "%" GFARM_PRId64, info->st_ctimespec.tv_sec);
	paramValues[11] = ctimesec;
	sprintf(ctimensec, "%d", info->st_ctimespec.tv_nsec);
	paramValues[12] = ctimensec;

	res = PQexecParams(conn,
		"Update INode SET igen = $2, nlink = $3, size = $4, "
				"mode = $5, username = $6, groupname = $7, "
				"atimesec = $8,  atimensec = $9, "
				"mtimesec = $10, mtimensec = $11, "
				"ctimesec = $12, ctimensec = $13 "
		    "WHERE inumber = $1",
		13, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0); /* dummy parameter for result format */
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		if (PQstatus(conn) == CONNECTION_BAD) {
			PQreset(conn);
			if (PQstatus(conn) == CONNECTION_OK) {
				PQclear(res);
				goto retry;
			}
		}
		gflog_error("inode_modify UPDATE: %s",
		    PQresultErrorMessage(res));
			e = GFARM_ERR_UNKNOWN;
	} else if (strtol(PQcmdTuples(res), NULL, 0) == 0)
		e = GFARM_ERR_NO_SUCH_OBJECT;
	PQclear(res);

	free(info);
}

static gfarm_error_t
pgsql_inode_uint64_modify(struct db_inode_uint64_modify_arg *arg,
	const char *sql)
{
	PGresult *res;
	const char *paramValues[2];
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	char inumber[GFARM_INT64STRLEN + 1];
	char uint64[GFARM_INT64STRLEN + 1];

 retry:
	sprintf(inumber, "%" GFARM_PRId64, arg->inum);
	paramValues[0] = inumber;
	sprintf(uint64, "%" GFARM_PRId64, arg->uint64);
	paramValues[1] = uint64;

	res = PQexecParams(conn,
		sql,
		2, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0); /* dummy parameter for result format */
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		if (PQstatus(conn) == CONNECTION_BAD) {
			PQreset(conn);
			if (PQstatus(conn) == CONNECTION_OK) {
				PQclear(res);
				goto retry;
			}
		}
		gflog_error("inode_modify %s: %s", sql,
		    PQresultErrorMessage(res));
		e = GFARM_ERR_UNKNOWN;
	} else if (strtol(PQcmdTuples(res), NULL, 0) == 0)
		e = GFARM_ERR_NO_SUCH_OBJECT;
	PQclear(res);

	free(arg);
	return (e);
}

static gfarm_error_t
pgsql_inode_uint32_modify(struct db_inode_uint32_modify_arg *arg,
	const char *sql)
{
	PGresult *res;
	const char *paramValues[2];
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	char inumber[GFARM_INT64STRLEN + 1];
	char uint32[GFARM_INT32STRLEN + 1];

 retry:
	sprintf(inumber, "%" GFARM_PRId64, arg->inum);
	paramValues[0] = inumber;
	sprintf(uint32, "%d", arg->uint32);
	paramValues[1] = uint32;

	res = PQexecParams(conn,
		sql,
		2, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0); /* dummy parameter for result format */
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		if (PQstatus(conn) == CONNECTION_BAD) {
			PQreset(conn);
			if (PQstatus(conn) == CONNECTION_OK) {
				PQclear(res);
				goto retry;
			}
		}
		gflog_error("inode_modify %s: %s", sql,
		    PQresultErrorMessage(res));
		e = GFARM_ERR_UNKNOWN;
	} else if (strtol(PQcmdTuples(res), NULL, 0) == 0)
		e = GFARM_ERR_NO_SUCH_OBJECT;
	PQclear(res);

	free(arg);
	return (e);
}

static gfarm_error_t
pgsql_inode_string_modify(struct db_inode_string_modify_arg *arg,
	const char *sql)
{
	PGresult *res;
	const char *paramValues[2];
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	char inumber[GFARM_INT64STRLEN + 1];

 retry:
	sprintf(inumber, "%" GFARM_PRId64, arg->inum);
	paramValues[0] = inumber;
	paramValues[1] = arg->string;

	res = PQexecParams(conn,
		sql,
		2, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0); /* dummy parameter for result format */
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		if (PQstatus(conn) == CONNECTION_BAD) {
			PQreset(conn);
			if (PQstatus(conn) == CONNECTION_OK) {
				PQclear(res);
				goto retry;
			}
		}
		gflog_error("inode_modify %s: %s", sql,
		    PQresultErrorMessage(res));
		e = GFARM_ERR_UNKNOWN;
	} else if (strtol(PQcmdTuples(res), NULL, 0) == 0)
		e = GFARM_ERR_NO_SUCH_OBJECT;
	PQclear(res);

	free(arg);
	return (e);
}

static gfarm_error_t
pgsql_inode_timespec_modify(struct db_inode_timespec_modify_arg *arg,
	const char *sql)
{
	PGresult *res;
	const char *paramValues[3];
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	char inumber[GFARM_INT64STRLEN + 1];
	char sec[GFARM_INT64STRLEN + 1];
	char nsec[GFARM_INT32STRLEN + 1];

 retry:
	sprintf(inumber, "%" GFARM_PRId64, arg->inum);
	paramValues[0] = inumber;
	sprintf(sec, "%" GFARM_PRId64, arg->time.tv_sec);
	paramValues[1] = sec;
	sprintf(nsec, "%d", arg->time.tv_nsec);
	paramValues[2] = nsec;

	res = PQexecParams(conn,
		sql,
		3, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0); /* dummy parameter for result format */
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		if (PQstatus(conn) == CONNECTION_BAD) {
			PQreset(conn);
			if (PQstatus(conn) == CONNECTION_OK) {
				PQclear(res);
				goto retry;
			}
		}
		gflog_error("inode_modify %s: %s", sql,
		    PQresultErrorMessage(res));
		e = GFARM_ERR_UNKNOWN;
	} else if (strtol(PQcmdTuples(res), NULL, 0) == 0)
		e = GFARM_ERR_NO_SUCH_OBJECT;
	PQclear(res);

	free(arg);
	return (e);
}

static void
gfarm_pgsql_inode_nlink_modify(struct db_inode_uint64_modify_arg *arg)
{
	pgsql_inode_uint64_modify(arg,
	    "Update INode SET nlink = $2 WHERE inumber = $1");
}

static void
gfarm_pgsql_inode_size_modify(struct db_inode_uint64_modify_arg *arg)
{
	pgsql_inode_uint64_modify(arg,
	    "Update INode SET size = $2 WHERE inumber = $1");
}

static void
gfarm_pgsql_inode_mode_modify(struct db_inode_uint32_modify_arg *arg)
{
	pgsql_inode_uint32_modify(arg,
	    "Update INode SET mode = $2 WHERE inumber = $1");
}

static void
gfarm_pgsql_inode_user_modify(struct db_inode_string_modify_arg *arg)
{
	pgsql_inode_string_modify(arg,
	    "Update INode SET username = $2 WHERE inumber = $1");
}

static void
gfarm_pgsql_inode_group_modify(struct db_inode_string_modify_arg *arg)
{
	pgsql_inode_string_modify(arg,
	    "Update INode SET groupname = $2 WHERE inumber = $1");
}

static void
gfarm_pgsql_inode_atime_modify(struct db_inode_timespec_modify_arg *arg)
{
	pgsql_inode_timespec_modify(arg,
	  "Update INode SET atimesec = $2, atimensec = $3 WHERE inumber = $1");
}

static void
gfarm_pgsql_inode_mtime_modify(struct db_inode_timespec_modify_arg *arg)
{
	pgsql_inode_timespec_modify(arg,
	  "Update INode SET mtimesec = $2, mtimensec = $3 WHERE inumber = $1");
}

static void
gfarm_pgsql_inode_ctime_modify(struct db_inode_timespec_modify_arg *arg)
{
	pgsql_inode_timespec_modify(arg,
	  "Update INode SET ctimesec = $2, ctimensec = $3 WHERE inumber = $1");
}

static char *
get_value_from_varchar_copy_binary(char **bufp)
{
	size_t len;
	char *p;

	if (*(int32_t *)*bufp == -1) { /* NULL */
		*bufp += sizeof(uint32_t);
		/* XXX - stopgap for NULL value */
		return (NULL);
	}

	len = ntohl(*(uint32_t *)*bufp);
	*bufp += sizeof(uint32_t);
	p = malloc(len + 1);
	strncpy(p, *bufp, len);
	p[len] = '\0';
	*bufp += len;
	return (p);
}

static uint32_t
get_value_from_integer_copy_binary(char **bufp)
{
	uint32_t val;

	if (*(int32_t *)*bufp == -1) { /* NULL */
		*bufp += sizeof(uint32_t);
		/* XXX - stopgap for NULL value */
		return (0);
	}

	*bufp += sizeof(uint32_t);
	val = ntohl(*(uint32_t *)*bufp);
	*bufp += sizeof(uint32_t);
	return (val);
}

static uint64_t
get_value_from_int8_copy_binary(char **bufp)
{
	uint64_t val;

	if (*(int32_t *)*bufp == -1) { /* NULL */
		*bufp += sizeof(uint32_t);
		/* XXX - stopgap for NULL value */
		return (0);
	}

	*bufp += sizeof(uint32_t);
	val = gfarm_ntoh64(*(uint64_t *)*bufp);
	*bufp += sizeof(uint64_t);
	return (val);
}

static void
inode_info_set_field_from_copy_binary(
	char *buf,
	struct gfs_stat *info)
{
	buf += 2; /* skip the number of fields */

	info->st_ino = get_value_from_int8_copy_binary(&buf);
	info->st_gen = get_value_from_int8_copy_binary(&buf);
	info->st_nlink = get_value_from_int8_copy_binary(&buf);
	info->st_size = get_value_from_int8_copy_binary(&buf);
	info->st_ncopy = 0;
	info->st_mode = get_value_from_integer_copy_binary(&buf);
	info->st_user = get_value_from_varchar_copy_binary(&buf);
	info->st_group = get_value_from_varchar_copy_binary(&buf);
	info->st_atimespec.tv_sec =
		get_value_from_int8_copy_binary(&buf);
	info->st_atimespec.tv_nsec =
		get_value_from_integer_copy_binary(&buf);
	info->st_mtimespec.tv_sec =
		get_value_from_int8_copy_binary(&buf);
	info->st_mtimespec.tv_nsec =
		get_value_from_integer_copy_binary(&buf);
	info->st_ctimespec.tv_sec =
		get_value_from_int8_copy_binary(&buf);
	info->st_ctimespec.tv_nsec =
		get_value_from_integer_copy_binary(&buf);
}

#define COPY_BINARY_SIGNATURE_LEN		11
#define COPY_BINARY_FLAGS_FIELD_LEN		4
#define COPY_BINARY_HEADER_EXTENSION_AREA_LEN	4
#define COPY_BINARY_HEADER_LEN (COPY_BINARY_SIGNATURE_LEN + \
	    COPY_BINARY_FLAGS_FIELD_LEN +COPY_BINARY_HEADER_EXTENSION_AREA_LEN)
#define COPY_BINARY_TRAILER_LEN			2
#define PQ_GET_COPY_DATA_ERROR			-2

static const char binary_signature[COPY_BINARY_SIGNATURE_LEN] =
	"PGCOPY\n\377\r\n\0";

static gfarm_error_t
gfarm_pgsql_inode_load(
	void *closure,
	void (*callback)(void *, struct gfs_stat *))
{
	PGresult *res;
	gfarm_error_t e;
	char *buf, *bp;
	int ret;
	struct gfs_stat info;

 retry:
	res = PQexec(conn,
		"COPY INode TO STDOUT BINARY");
	if (PQresultStatus(res) != PGRES_COPY_OUT) {
		if (PQstatus(conn) == CONNECTION_BAD) {
			PQreset(conn);
			if (PQstatus(conn) == CONNECTION_OK) {
				PQclear(res);
				goto retry;
			}
		}

		gflog_error("pgsql_inode_load COPY INode: %s",
		    PQresultErrorMessage(res));
		e = GFARM_ERR_UNKNOWN;
		PQclear(res);
		return (e);
	}
	PQclear(res);

	ret = PQgetCopyData(conn, &buf,	0);
	if (ret < COPY_BINARY_SIGNATURE_LEN ||
	    memcmp(buf, binary_signature, COPY_BINARY_SIGNATURE_LEN) != 0) {
		gflog_fatal("gfarm_pgsql_inode_load: "
		    "Fatal Error, COPY INode signature not recognized");
	}
	if (ret > COPY_BINARY_HEADER_LEN + COPY_BINARY_TRAILER_LEN) {
		bp = buf + COPY_BINARY_HEADER_LEN;
		while (ret > COPY_BINARY_TRAILER_LEN) {
			if (buf) {
				gfarm_base_gfs_stat_ops.clear(&info);
				inode_info_set_field_from_copy_binary(bp,
					&info);
				if (gfarm_base_gfs_stat_ops.validate(&info))
					(*callback)(closure, &info);
				PQfreemem(buf);
			}
			ret = PQgetCopyData(conn, &buf, 0);
			bp = buf;
		}
	}
	if (ret == PQ_GET_COPY_DATA_ERROR) {
		gflog_error("pgsql_inode_load COPY INode DATA_ERROR: %s",
		    PQresultErrorMessage(res));
		e = GFARM_ERR_UNKNOWN;
		return (e);
	}
	if (ret == COPY_BINARY_TRAILER_LEN ||
	    ret == COPY_BINARY_HEADER_LEN + COPY_BINARY_TRAILER_LEN) {
		ret = PQgetCopyData(conn, &buf, 0);
		PQfreemem(buf);
	}
	res = PQgetResult(conn);
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		gflog_error("pgsql_inode_load COPY INode result: %s",
		    PQresultErrorMessage(res));
		e = GFARM_ERR_UNKNOWN;
		return (e);
	}
	PQclear(res);
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

static void
gfarm_pgsql_file_info_add(struct db_inode_cksum_arg *arg)
{
	PGresult *res;
	const char *paramValues[3];
	gfarm_error_t e;
	char inumber[GFARM_INT64STRLEN + 1];

 retry:
	sprintf(inumber, "%" GFARM_PRId64, arg->inum);
	paramValues[0] = inumber;
	paramValues[1] = arg->type;
	paramValues[2] = arg->sum;
	res = PQexecParams(conn,
		"INSERT INTO FileInfo (inumber, checksumType, checksum) "
		     "VALUES ($1, $2, $3)",
		3, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0); /* dummy parameter for result format */
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		if (PQstatus(conn) == CONNECTION_BAD) {
			PQreset(conn);
			if (PQstatus(conn) == CONNECTION_OK) {
				PQclear(res);
				goto retry;
			}
		}
		if (strcmp(
			   PQresultErrorField(res, PG_DIAG_SQLSTATE),
			   GFARM_PGSQL_ERRCODE_UNIQUE_VIOLATION) == 0) {
			e = GFARM_ERR_ALREADY_EXISTS;
		} else {
			e = GFARM_ERR_UNKNOWN;
		}
		gflog_error("cksum_add INSERT: %s",
		    PQresultErrorMessage(res));
	}
	PQclear(res);

	free(arg);
}

static void
gfarm_pgsql_file_info_modify(struct db_inode_cksum_arg *arg)
{
	PGresult *res;
	const char *paramValues[3];
	gfarm_error_t e;
	char inumber[GFARM_INT64STRLEN + 1];

 retry:
	sprintf(inumber, "%" GFARM_PRId64, arg->inum);
	paramValues[0] = inumber;
	paramValues[1] = arg->type;
	paramValues[2] = arg->sum;
	res = PQexecParams(conn,
		"UPDATE FileInfo SET checksumType = $2, checksum = $3 "
		    "WHERE inumber = $1",
		3, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0); /* dummy parameter for result format */
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		if (PQstatus(conn) == CONNECTION_BAD) {
			PQreset(conn);
			if (PQstatus(conn) == CONNECTION_OK) {
				PQclear(res);
				goto retry;
			}
		}
		gflog_error("cksum_modify UPDATE: %s",
		    PQresultErrorMessage(res));
		e = GFARM_ERR_UNKNOWN;
	} else if (strtol(PQcmdTuples(res), NULL, 0) == 0)
		e = GFARM_ERR_NO_SUCH_OBJECT;
	PQclear(res);

	free(arg);
}

static void
gfarm_pgsql_file_info_remove(struct db_inode_inum_arg *arg)
{
	PGresult *res;
	const char *paramValues[1];
	gfarm_error_t e;
	char inumber[GFARM_INT64STRLEN + 1];

 retry:
	sprintf(inumber, "%" GFARM_PRId64, arg->inum);
	paramValues[0] = inumber;
	res = PQexecParams(conn,
		"DELETE FROM FileInfo WHERE inumber = $1",
		1, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0);  /* dummy parameter for result format */
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		if (PQstatus(conn) == CONNECTION_BAD) {
			PQreset(conn);
			if (PQstatus(conn) == CONNECTION_OK) {
				PQclear(res);
				goto retry;
			}
		}
		gflog_error("cksum_remove DELETE: %s",
		    PQresultErrorMessage(res));
		e = GFARM_ERR_UNKNOWN;
	} else if (strtol(PQcmdTuples(res), NULL, 0) == 0)
		e = GFARM_ERR_NO_SUCH_OBJECT;
	PQclear(res);

	free(arg);
}

static void
file_info_set_field_from_copy_binary(
	char *buf,
	struct db_inode_cksum_arg *info)
{
	buf += 2; /* skip the number of fields */

	info->inum = get_value_from_int8_copy_binary(&buf);
	info->type = get_value_from_varchar_copy_binary(&buf);
	info->sum = get_value_from_varchar_copy_binary(&buf);
	info->len = strlen(info->sum);
}

static gfarm_error_t
gfarm_pgsql_file_info_load(
	void *closure,
	void (*callback)(void *, gfarm_ino_t, char *, size_t, char *))
{
	PGresult *res;
	gfarm_error_t e;
	char *buf, *bp;
	int ret;
	struct db_inode_cksum_arg info;

 retry:
	res = PQexec(conn,
		"COPY FileInfo TO STDOUT BINARY");
	if (PQresultStatus(res) != PGRES_COPY_OUT) {
		if (PQstatus(conn) == CONNECTION_BAD) {
			PQreset(conn);
			if (PQstatus(conn) == CONNECTION_OK) {
				PQclear(res);
				goto retry;
			}
		}

		gflog_error("pgsql_cksum_load COPY FileInfo: %s",
		    PQresultErrorMessage(res));
		e = GFARM_ERR_UNKNOWN;
		PQclear(res);
		return (e);
	}
	PQclear(res);

	ret = PQgetCopyData(conn, &buf,	0);
	if (ret < COPY_BINARY_SIGNATURE_LEN ||
	    memcmp(buf, binary_signature, COPY_BINARY_SIGNATURE_LEN) != 0) {
		gflog_fatal("gfarm_pgsql_cksum_load: "
		    "Fatal Error, COPY FileInfo signature not recognized");
	}
	if (ret > COPY_BINARY_HEADER_LEN + COPY_BINARY_TRAILER_LEN) {
		bp = buf + COPY_BINARY_HEADER_LEN;
		while (ret > COPY_BINARY_TRAILER_LEN) {
			if (buf) {
				db_base_inode_cksum_arg_ops.clear(&info);
				file_info_set_field_from_copy_binary(bp,
					&info);
				if (db_base_inode_cksum_arg_ops.validate(&info))
					(*callback)(closure, info.inum,
					    info.type, info.len, info.sum);
				PQfreemem(buf);
			}
			ret = PQgetCopyData(conn, &buf, 0);
			bp = buf;
		}
	}
	if (ret == PQ_GET_COPY_DATA_ERROR) {
		gflog_error("pgsql_cksum_load COPY FileInfo DATA_ERROR: %s",
		    PQresultErrorMessage(res));
		e = GFARM_ERR_UNKNOWN;
		return (e);
	}
	if (ret == COPY_BINARY_TRAILER_LEN ||
	    ret == COPY_BINARY_HEADER_LEN + COPY_BINARY_TRAILER_LEN) {
		ret = PQgetCopyData(conn, &buf, 0);
		PQfreemem(buf);
	}
	res = PQgetResult(conn);
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		gflog_error("pgsql_cksum_load COPY FileInfo result: %s",
		    PQresultErrorMessage(res));
		e = GFARM_ERR_UNKNOWN;
		return (e);
	}
	PQclear(res);
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

static void
gfarm_pgsql_filecopy_add(struct db_filecopy_arg *arg)
{
	PGresult *res;
	const char *paramValues[2];
	gfarm_error_t e;
	char inumber[GFARM_INT64STRLEN + 1];

 retry:
	sprintf(inumber, "%" GFARM_PRId64, arg->inum);
	paramValues[0] = inumber;
	paramValues[1] = arg->hostname;
	res = PQexecParams(conn,
		"INSERT INTO FileCopy (inumber, hostname) VALUES ($1, $2)",
		2, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0); /* dummy parameter for result format */
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		if (PQstatus(conn) == CONNECTION_BAD) {
			PQreset(conn);
			if (PQstatus(conn) == CONNECTION_OK) {
				PQclear(res);
				goto retry;
			}
		}
		if (strcmp(
			   PQresultErrorField(res, PG_DIAG_SQLSTATE),
			   GFARM_PGSQL_ERRCODE_UNIQUE_VIOLATION) == 0) {
			e = GFARM_ERR_ALREADY_EXISTS;
		} else {
			e = GFARM_ERR_UNKNOWN;
		}
		gflog_error("filecopy_add INSERT: %s",
		    PQresultErrorMessage(res));
	}
	PQclear(res);

	free(arg);
}

static void
gfarm_pgsql_filecopy_remove(struct db_filecopy_arg *arg)
{
	PGresult *res;
	const char *paramValues[2];
	gfarm_error_t e;
	char inumber[GFARM_INT64STRLEN + 1];

 retry:
	sprintf(inumber, "%" GFARM_PRId64, arg->inum);
	paramValues[0] = inumber;
	paramValues[1] = arg->hostname;
	res = PQexecParams(conn,
		"DELETE FROM FileCopy WHERE inumber = $1 AND hostname = $2",
		2, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0);  /* dummy parameter for result format */
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		if (PQstatus(conn) == CONNECTION_BAD) {
			PQreset(conn);
			if (PQstatus(conn) == CONNECTION_OK) {
				PQclear(res);
				goto retry;
			}
		}
		gflog_error("filecopy_remove DELETE: %s",
		    PQresultErrorMessage(res));
		e = GFARM_ERR_UNKNOWN;
	} else if (strtol(PQcmdTuples(res), NULL, 0) == 0)
		e = GFARM_ERR_NO_SUCH_OBJECT;
	PQclear(res);

	free(arg);
}

static void
filecopy_set_field_from_copy_binary(
	char *buf,
	struct db_filecopy_arg *info)
{
	buf += 2; /* skip the number of fields */

	info->inum = get_value_from_int8_copy_binary(&buf);
	info->hostname = get_value_from_varchar_copy_binary(&buf);
}

static gfarm_error_t
gfarm_pgsql_filecopy_load(
	void *closure,
	void (*callback)(void *, gfarm_ino_t, char *))
{
	PGresult *res;
	gfarm_error_t e;
	char *buf, *bp;
	int ret;
	struct db_filecopy_arg info;

 retry:
	res = PQexec(conn,
		"COPY FileCopy TO STDOUT BINARY");
	if (PQresultStatus(res) != PGRES_COPY_OUT) {
		if (PQstatus(conn) == CONNECTION_BAD) {
			PQreset(conn);
			if (PQstatus(conn) == CONNECTION_OK) {
				PQclear(res);
				goto retry;
			}
		}

		gflog_error("pgsql_filecopy_load COPY FileCopy: %s",
		    PQresultErrorMessage(res));
		e = GFARM_ERR_UNKNOWN;
		PQclear(res);
		return (e);
	}
	PQclear(res);

	ret = PQgetCopyData(conn, &buf,	0);
	if (ret < COPY_BINARY_SIGNATURE_LEN ||
	    memcmp(buf, binary_signature, COPY_BINARY_SIGNATURE_LEN) != 0) {
		gflog_fatal("gfarm_pgsql_filecopy_load: "
		    "Fatal Error, COPY FileCopy signature not recognized");
	}
	if (ret > COPY_BINARY_HEADER_LEN + COPY_BINARY_TRAILER_LEN) {
		bp = buf + COPY_BINARY_HEADER_LEN;
		while (ret > COPY_BINARY_TRAILER_LEN) {
			if (buf) {
				db_base_filecopy_arg_ops.clear(&info);
				filecopy_set_field_from_copy_binary(bp,
					&info);
				if (db_base_filecopy_arg_ops.validate(&info))
					(*callback)(closure, info.inum,
					    info.hostname);
				PQfreemem(buf);
			}
			ret = PQgetCopyData(conn, &buf, 0);
			bp = buf;
		}
	}
	if (ret == PQ_GET_COPY_DATA_ERROR) {
		gflog_error("pgsql_filecopy_load COPY FileCopy DATA_ERROR: %s",
		    PQresultErrorMessage(res));
		e = GFARM_ERR_UNKNOWN;
		return (e);
	}
	if (ret == COPY_BINARY_TRAILER_LEN ||
	    ret == COPY_BINARY_HEADER_LEN + COPY_BINARY_TRAILER_LEN) {
		ret = PQgetCopyData(conn, &buf, 0);
		PQfreemem(buf);
	}
	res = PQgetResult(conn);
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		gflog_error("pgsql_filecopy_load COPY FileCopy result: %s",
		    PQresultErrorMessage(res));
		e = GFARM_ERR_UNKNOWN;
		return (e);
	}
	PQclear(res);
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

static void
gfarm_pgsql_deadfilecopy_add(struct db_deadfilecopy_arg *arg)
{
	PGresult *res;
	const char *paramValues[3];
	gfarm_error_t e;
	char inumber[GFARM_INT64STRLEN + 1];
	char igen[GFARM_INT64STRLEN + 1];

 retry:
	sprintf(inumber, "%" GFARM_PRId64, arg->inum);
	paramValues[0] = inumber;
	sprintf(igen, "%" GFARM_PRId64, arg->igen);
	paramValues[1] = igen;
	paramValues[2] = arg->hostname;
	res = PQexecParams(conn,
		"INSERT INTO DeadFileCopy (inumber, igen, hostname) "
			"VALUES ($1, $2, $3)",
		3, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0); /* dummy parameter for result format */
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		if (PQstatus(conn) == CONNECTION_BAD) {
			PQreset(conn);
			if (PQstatus(conn) == CONNECTION_OK) {
				PQclear(res);
				goto retry;
			}
		}
		if (strcmp(
			   PQresultErrorField(res, PG_DIAG_SQLSTATE),
			   GFARM_PGSQL_ERRCODE_UNIQUE_VIOLATION) == 0) {
			e = GFARM_ERR_ALREADY_EXISTS;
		} else {
			e = GFARM_ERR_UNKNOWN;
		}
		gflog_error("deadfilecopy_add INSERT: %s",
		    PQresultErrorMessage(res));
	}
	PQclear(res);

	free(arg);
}

static void
gfarm_pgsql_deadfilecopy_remove(struct db_deadfilecopy_arg *arg)
{
	PGresult *res;
	const char *paramValues[3];
	gfarm_error_t e;
	char inumber[GFARM_INT64STRLEN + 1];
	char igen[GFARM_INT64STRLEN + 1];

 retry:
	sprintf(inumber, "%" GFARM_PRId64, arg->inum);
	paramValues[0] = inumber;
	sprintf(igen, "%" GFARM_PRId64, arg->igen);
	paramValues[1] = igen;
	paramValues[2] = arg->hostname;
	res = PQexecParams(conn,
		"DELETE FROM DeadFileCopy "
			"WHERE inumber = $1 AND igen = $2 AND hostname = $3",
		3, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0);  /* dummy parameter for result format */
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		if (PQstatus(conn) == CONNECTION_BAD) {
			PQreset(conn);
			if (PQstatus(conn) == CONNECTION_OK) {
				PQclear(res);
				goto retry;
			}
		}
		gflog_error("deadfilecopy_remove DELETE: %s",
		    PQresultErrorMessage(res));
		e = GFARM_ERR_UNKNOWN;
	} else if (strtol(PQcmdTuples(res), NULL, 0) == 0)
		e = GFARM_ERR_NO_SUCH_OBJECT;
	PQclear(res);

	free(arg);
}

static void
deadfilecopy_set_field_from_copy_binary(
	char *buf,
	struct db_deadfilecopy_arg *info)
{
	buf += 2; /* skip the number of fields */

	info->inum = get_value_from_int8_copy_binary(&buf);
	info->igen = get_value_from_int8_copy_binary(&buf);
	info->hostname = get_value_from_varchar_copy_binary(&buf);
}

static gfarm_error_t
gfarm_pgsql_deadfilecopy_load(
	void *closure,
	void (*callback)(void *, gfarm_ino_t, gfarm_uint64_t, char *))
{
	PGresult *res;
	gfarm_error_t e;
	char *buf, *bp;
	int ret;
	struct db_deadfilecopy_arg info;

 retry:
	res = PQexec(conn,
		"COPY DeadFileCopy TO STDOUT BINARY");
	if (PQresultStatus(res) != PGRES_COPY_OUT) {
		if (PQstatus(conn) == CONNECTION_BAD) {
			PQreset(conn);
			if (PQstatus(conn) == CONNECTION_OK) {
				PQclear(res);
				goto retry;
			}
		}

		gflog_error("pgsql_deadfilecopy_load COPY DeadFileCopy: %s",
		    PQresultErrorMessage(res));
		e = GFARM_ERR_UNKNOWN;
		PQclear(res);
		return (e);
	}
	PQclear(res);

	ret = PQgetCopyData(conn, &buf,	0);
	if (ret < COPY_BINARY_SIGNATURE_LEN ||
	    memcmp(buf, binary_signature, COPY_BINARY_SIGNATURE_LEN) != 0) {
		gflog_fatal("gfarm_pgsql_deadfilecopy_load: "
		    "Fatal Error, COPY DeadFileCopy signature not recognized");
	}
	if (ret > COPY_BINARY_HEADER_LEN + COPY_BINARY_TRAILER_LEN) {
		bp = buf + COPY_BINARY_HEADER_LEN;
		while (ret > COPY_BINARY_TRAILER_LEN) {
			if (buf) {
				db_base_deadfilecopy_arg_ops.clear(&info);
				deadfilecopy_set_field_from_copy_binary(bp,
					&info);
				if (db_base_deadfilecopy_arg_ops.validate(&info))
					(*callback)(closure, info.inum,
					    info.igen, info.hostname);
				PQfreemem(buf);
			}
			ret = PQgetCopyData(conn, &buf, 0);
			bp = buf;
		}
	}
	if (ret == PQ_GET_COPY_DATA_ERROR) {
		gflog_error(
		    "pgsql_deadfilecopy_load COPY DeadFileCopy DATA_ERROR: %s",
		    PQresultErrorMessage(res));
		e = GFARM_ERR_UNKNOWN;
		return (e);
	}
	if (ret == COPY_BINARY_TRAILER_LEN ||
	    ret == COPY_BINARY_HEADER_LEN + COPY_BINARY_TRAILER_LEN) {
		ret = PQgetCopyData(conn, &buf, 0);
		PQfreemem(buf);
	}
	res = PQgetResult(conn);
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		gflog_error(
		    "pgsql_deadfilecopy_load COPY DeadFileCopy result: %s",
		    PQresultErrorMessage(res));
		e = GFARM_ERR_UNKNOWN;
		return (e);
	}
	PQclear(res);
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

static void
gfarm_pgsql_direntry_add(struct db_direntry_arg *arg)
{
	PGresult *res;
	const char *paramValues[3];
	gfarm_error_t e;
	char dir_inumber[GFARM_INT64STRLEN + 1];
	char entry_inumber[GFARM_INT64STRLEN + 1];

 retry:
	sprintf(dir_inumber, "%" GFARM_PRId64, arg->dir_inum);
	paramValues[0] = dir_inumber;
	paramValues[1] = arg->entry_name;
	sprintf(entry_inumber, "%" GFARM_PRId64, arg->entry_inum);
	paramValues[2] = entry_inumber;
	res = PQexecParams(conn,
		"INSERT INTO DirEntry (dirINumber, entryName, entryINumber) "
			"VALUES ($1, $2, $3)",
		3, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0); /* dummy parameter for result format */
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		if (PQstatus(conn) == CONNECTION_BAD) {
			PQreset(conn);
			if (PQstatus(conn) == CONNECTION_OK) {
				PQclear(res);
				goto retry;
			}
		}
		if (strcmp(
			   PQresultErrorField(res, PG_DIAG_SQLSTATE),
			   GFARM_PGSQL_ERRCODE_UNIQUE_VIOLATION) == 0) {
			e = GFARM_ERR_ALREADY_EXISTS;
		} else {
			e = GFARM_ERR_UNKNOWN;
		}
		gflog_error("direntry_add INSERT: %s",
		    PQresultErrorMessage(res));
	}
	PQclear(res);

	free(arg);
}

static void
gfarm_pgsql_direntry_remove(struct db_direntry_remove_arg *arg)
{
	PGresult *res;
	const char *paramValues[2];
	gfarm_error_t e;
	char dir_inumber[GFARM_INT64STRLEN + 1];

 retry:
	sprintf(dir_inumber, "%" GFARM_PRId64, arg->dir_inum);
	paramValues[0] = dir_inumber;
	paramValues[1] = arg->entry_name;
	res = PQexecParams(conn,
		"DELETE FROM DirEntry "
			"WHERE dirINumber = $1 AND entryName = $2",
		2, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0);  /* dummy parameter for result format */
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		if (PQstatus(conn) == CONNECTION_BAD) {
			PQreset(conn);
			if (PQstatus(conn) == CONNECTION_OK) {
				PQclear(res);
				goto retry;
			}
		}
		gflog_error("direntry_remove DELETE: %s",
		    PQresultErrorMessage(res));
		e = GFARM_ERR_UNKNOWN;
	} else if (strtol(PQcmdTuples(res), NULL, 0) == 0)
		e = GFARM_ERR_NO_SUCH_OBJECT;
	PQclear(res);

	free(arg);
}

static void
direntry_set_field_from_copy_binary(
	char *buf,
	struct db_direntry_arg *info)
{
	buf += 2; /* skip the number of fields */

	info->dir_inum = get_value_from_int8_copy_binary(&buf);
	info->entry_name = get_value_from_varchar_copy_binary(&buf);
	info->entry_len = strlen(info->entry_name);
	info->entry_inum = get_value_from_int8_copy_binary(&buf);
}

static gfarm_error_t
gfarm_pgsql_direntry_load(
	void *closure,
	void (*callback)(void *, gfarm_ino_t, char *, int, gfarm_ino_t))
{
	PGresult *res;
	gfarm_error_t e;
	char *buf, *bp;
	int ret;
	struct db_direntry_arg info;

 retry:
	res = PQexec(conn,
		"COPY DirEntry TO STDOUT BINARY");
	if (PQresultStatus(res) != PGRES_COPY_OUT) {
		if (PQstatus(conn) == CONNECTION_BAD) {
			PQreset(conn);
			if (PQstatus(conn) == CONNECTION_OK) {
				PQclear(res);
				goto retry;
			}
		}

		gflog_error("pgsql_direntry_load COPY DirEntry: %s",
		    PQresultErrorMessage(res));
		e = GFARM_ERR_UNKNOWN;
		PQclear(res);
		return (e);
	}
	PQclear(res);

	ret = PQgetCopyData(conn, &buf,	0);
	if (ret < COPY_BINARY_SIGNATURE_LEN ||
	    memcmp(buf, binary_signature, COPY_BINARY_SIGNATURE_LEN) != 0) {
		gflog_fatal("gfarm_pgsql_direntry_load: "
		    "Fatal Error, COPY DirEntry signature not recognized");
	}
	if (ret > COPY_BINARY_HEADER_LEN + COPY_BINARY_TRAILER_LEN) {
		bp = buf + COPY_BINARY_HEADER_LEN;
		while (ret > COPY_BINARY_TRAILER_LEN) {
			if (buf) {
				db_base_direntry_arg_ops.clear(&info);
				direntry_set_field_from_copy_binary(bp,
					&info);
				if (db_base_direntry_arg_ops.validate(&info))
					(*callback)(closure, info.dir_inum,
					    info.entry_name, info.entry_len,
					    info.entry_inum);
				PQfreemem(buf);
			}
			ret = PQgetCopyData(conn, &buf, 0);
			bp = buf;
		}
	}
	if (ret == PQ_GET_COPY_DATA_ERROR) {
		gflog_error(
		    "pgsql_direntry_load COPY DirEntry DATA_ERROR: %s",
		    PQresultErrorMessage(res));
		e = GFARM_ERR_UNKNOWN;
		return (e);
	}
	if (ret == COPY_BINARY_TRAILER_LEN ||
	    ret == COPY_BINARY_HEADER_LEN + COPY_BINARY_TRAILER_LEN) {
		ret = PQgetCopyData(conn, &buf, 0);
		PQfreemem(buf);
	}
	res = PQgetResult(conn);
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		gflog_error(
		    "pgsql_direntry_load COPY DirEntry result: %s",
		    PQresultErrorMessage(res));
		e = GFARM_ERR_UNKNOWN;
		return (e);
	}
	PQclear(res);
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

const struct db_ops db_pgsql_ops = {
	gfarm_pgsql_initialize,
	gfarm_pgsql_terminate,

	gfarm_pgsql_host_add,
	gfarm_pgsql_host_modify,
	gfarm_pgsql_host_remove,
	gfarm_pgsql_host_load,

	gfarm_pgsql_user_add,
	gfarm_pgsql_user_modify,
	gfarm_pgsql_user_remove,
	gfarm_pgsql_user_load,

	gfarm_pgsql_group_add,
	gfarm_pgsql_group_modify,
	gfarm_pgsql_group_remove,
	gfarm_pgsql_group_load,

	gfarm_pgsql_inode_add,
	gfarm_pgsql_inode_modify,
	gfarm_pgsql_inode_nlink_modify,
	gfarm_pgsql_inode_size_modify,
	gfarm_pgsql_inode_mode_modify,
	gfarm_pgsql_inode_user_modify,
	gfarm_pgsql_inode_group_modify,
	gfarm_pgsql_inode_atime_modify,
	gfarm_pgsql_inode_mtime_modify,
	gfarm_pgsql_inode_ctime_modify,
	/* inode_remove: never remove any inode to keep inode->i_gen */
	gfarm_pgsql_inode_load,

	/* cksum */
	gfarm_pgsql_file_info_add,
	gfarm_pgsql_file_info_modify,
	gfarm_pgsql_file_info_remove,
	gfarm_pgsql_file_info_load,

	gfarm_pgsql_filecopy_add,
	gfarm_pgsql_filecopy_remove,
	gfarm_pgsql_filecopy_load,

	gfarm_pgsql_deadfilecopy_add,
	gfarm_pgsql_deadfilecopy_remove,
	gfarm_pgsql_deadfilecopy_load,

	gfarm_pgsql_direntry_add,
	gfarm_pgsql_direntry_remove,
	gfarm_pgsql_direntry_load,
};
