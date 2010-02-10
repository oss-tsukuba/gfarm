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

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <libpq-fe.h>
#include <netinet/in.h>
#include <gfarm/gfarm.h>

#include "gfutil.h"

#include "config.h"
#include "metadb_common.h"
#include "xattr_info.h"
#include "quota_info.h"
#include "quota.h"

#include "db_common.h"
#include "db_access.h"
#include "db_ops.h"

/* ERROR:  duplicate key violates unique constraint */
#define GFARM_PGSQL_ERRCODE_UNIQUE_VIOLATION	"23505"

/* ERROR:  invalid XML content */
#define GFARM_PGSQL_ERRCODE_INVALID_XML_CONTENT	"2200N"

/* FATAL:  terminating connection due to administrator command */
#define GFARM_PGSQL_ERRCODE_ADMIN_SHUTDOWN	"57P01"

/**********************************************************************/

static PGconn *conn = NULL;
static int transaction_nesting = 0;
static int transaction_ok;

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

	GFARM_MALLOC_ARRAY(conninfo, length);
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
			gflog_error(GFARM_MSG_1000421,
			    "gfmd.conf: illegal value in "
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
		gflog_error(GFARM_MSG_1000422,
		    "connecting PostgreSQL: %s", PQerrorMessage(conn));
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

static int64_t
pgsql_get_int64(PGresult *res, int row, const char *field_name)
{
	uint64_t val;

	memcpy(&val, PQgetvalue(res, row, PQfnumber(res, field_name)),
	    sizeof(val));
	return (gfarm_ntoh64(val));
}

/* this interface is exported for a use from a private extension */
int32_t
pgsql_get_int32(PGresult *res, int row, const char *field_name)
{
	uint32_t val;

	memcpy(&val, PQgetvalue(res, row, PQfnumber(res, field_name)),
	    sizeof(val));
	return (ntohl(val));
}

/* this interface is exported for a use from a private extension */
char *
pgsql_get_string(PGresult *res, int row, const char *field_name)
{
	return (strdup(PQgetvalue(res, row, PQfnumber(res, field_name))));
}

static void *
pgsql_get_binary(PGresult *res, int row, const char *field_name, int *size)
{
	int col;
	void *src;
	char *dst;

	col = PQfnumber(res, field_name);
	if ((*size = PQgetlength(res, row, col)) <= 0) {
		return NULL;
	}
	if ((src = PQgetvalue(res, row, col)) == NULL) {
		return NULL;
	}
	GFARM_MALLOC_ARRAY(dst, *size);
	if (dst != NULL)
		memcpy(dst, src, *size);
	return dst;
}

/**********************************************************************/

static gfarm_error_t
gfarm_pgsql_check_misc(PGresult *res, const char *command, const char *diag)
{
	gfarm_error_t e;

	if (PQresultStatus(res) == PGRES_COMMAND_OK) {
		e = GFARM_ERR_NO_ERROR;
	} else {
		e = GFARM_ERR_UNKNOWN;
		gflog_error(GFARM_MSG_1000423, "%s: %s: %s", diag, command,
		    PQresultErrorMessage(res));
	}
	return (e);
}

static gfarm_error_t
gfarm_pgsql_check_select(PGresult *res, const char *command, const char *diag)
{
	gfarm_error_t e;

	if (PQresultStatus(res) == PGRES_TUPLES_OK) {
		e = GFARM_ERR_NO_ERROR;
	} else {
		e = GFARM_ERR_UNKNOWN;
		gflog_error(GFARM_MSG_1000424, "%s: %s: %s", diag, command,
		    PQresultErrorMessage(res));
	}
	return (e);
}

static gfarm_error_t
gfarm_pgsql_check_insert(PGresult *res, const char *command, const char *diag)
{
	gfarm_error_t e;

	if (PQresultStatus(res) == PGRES_COMMAND_OK) {
		e = GFARM_ERR_NO_ERROR;
	} else {
		char *err = PQresultErrorField(res, PG_DIAG_SQLSTATE);
		if (strcmp(err, GFARM_PGSQL_ERRCODE_UNIQUE_VIOLATION) == 0) {
			e = GFARM_ERR_ALREADY_EXISTS;
		} else if (strcmp(err, GFARM_PGSQL_ERRCODE_INVALID_XML_CONTENT) == 0) {
			e = GFARM_ERR_INVALID_ARGUMENT;
		} else {
			e = GFARM_ERR_UNKNOWN;
		}
		gflog_error(GFARM_MSG_1000425, "%s: %s: %s", diag, command,
		    PQresultErrorMessage(res));
	}
	return (e);
}

/* this interface is exported for a use from a private extension */
gfarm_error_t
gfarm_pgsql_check_update_or_delete(PGresult *res,
	const char *command, const char *diag)
{
	gfarm_error_t e;
	char *pge;

	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		if ((pge = PQresultErrorField(res, PG_DIAG_SQLSTATE))!= NULL &&
		    strcmp(pge, GFARM_PGSQL_ERRCODE_INVALID_XML_CONTENT) == 0)
			e = GFARM_ERR_INVALID_ARGUMENT;
		else
			e = GFARM_ERR_UNKNOWN;
		gflog_error(GFARM_MSG_1000426, "%s: %s: %s", diag, command,
		    PQresultErrorMessage(res));
	} else if (strtol(PQcmdTuples(res), NULL, 0) == 0) {
		e = GFARM_ERR_NO_SUCH_OBJECT;
		gflog_error(GFARM_MSG_1000427,
		    "%s: %s: %s", diag, command, "no such object");
	} else {
		e = GFARM_ERR_NO_ERROR;
	}
	return (e);
}

static gfarm_error_t
gfarm_pgsql_exec_and_log(const char *command, const char *diag)
{
	PGresult *res = PQexec(conn, command);
	gfarm_error_t e = gfarm_pgsql_check_misc(res, command, diag);

	PQclear(res);
	return (e);
}

static gfarm_error_t
gfarm_pgsql_exec_params_and_log(const char *command,
	int nParams,
	const Oid *paramTypes,
	const char *const *paramValues,
	const int *paramLengths,
	const int *paramFormats,
	int resultFormat,
	const char *diag)
{
	PGresult *res = PQexecParams(conn, command, nParams,
	    paramTypes, paramValues, paramLengths, paramFormats, resultFormat);
	gfarm_error_t e = gfarm_pgsql_check_misc(res, command, diag);

	PQclear(res);
	return (e);
}

static gfarm_error_t
gfarm_pgsql_insert_and_log(const char *command,
	int nParams,
	const Oid *paramTypes,
	const char *const *paramValues,
	const int *paramLengths,
	const int *paramFormats,
	int resultFormat,
	const char *diag)
{
	PGresult *res = PQexecParams(conn, command, nParams, paramTypes,
	    paramValues, paramLengths, paramFormats, resultFormat);
	gfarm_error_t e = gfarm_pgsql_check_insert(res, command, diag);

	PQclear(res);
	return (e);
}

/*
 * Return Value:
 *	1: indicate that caller should retry.
 *	0: unrecoverable error, caller should end.
 * Note:
 *	If return value is 1, this function calls PQclear(),
 *	otherwise the caller of this function should call PQclear().
 */
static int
pgsql_should_retry(PGresult *res, int *retry_countp)
{
	if (PQstatus(conn) == CONNECTION_BAD && (*retry_countp)++ <= 1) {
		PQreset(conn);
		if (PQstatus(conn) == CONNECTION_OK) {
			PQclear(res);
			return (1);
		}
	} else if (PQresultStatus(res) == PGRES_FATAL_ERROR &&
	    strcmp(PQresultErrorField(res, PG_DIAG_SQLSTATE),
	    GFARM_PGSQL_ERRCODE_ADMIN_SHUTDOWN) == 0 &&
	    (*retry_countp)++ == 0) {
		PQclear(res);
		return (1);
	}
	return (0);
}

static PGresult *
gfarm_pgsql_exec_params_with_retry(const char *command,
	int nParams,
	const Oid *paramTypes,
	const char *const *paramValues,
	const int *paramLengths,
	const int *paramFormats,
	int resultFormat)
{
	PGresult *res;
	int retry_count = 0;

	do {
		res = PQexecParams(conn, command, nParams,
		    paramTypes, paramValues, paramLengths, paramFormats,
		    resultFormat);
	} while (PQresultStatus(res) != PGRES_COMMAND_OK &&
	    pgsql_should_retry(res, &retry_count));
	return (res);
}

/* this interface is exported for a use from a private extension */
gfarm_error_t
gfarm_pgsql_start_with_retry(const char *diag)
{
	PGresult *res;
	int retry_count = 0;

	if (transaction_nesting++ > 0)
		return (GFARM_ERR_NO_ERROR);

	transaction_ok = 1;
	do {
		res = PQexec(conn, "START TRANSACTION");
	} while (PQresultStatus(res) != PGRES_COMMAND_OK &&
	    pgsql_should_retry(res, &retry_count));
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		gflog_error(GFARM_MSG_1000428, "%s transaction BEGIN: %s",
		    diag, PQresultErrorMessage(res));
		PQclear(res);
		return (GFARM_ERR_UNKNOWN);
	}
	PQclear(res);
	return (GFARM_ERR_NO_ERROR);
}

/* this interface is exported for a use from a private extension */
gfarm_error_t
gfarm_pgsql_commit(const char *diag)
{
	if (--transaction_nesting > 0)
		return (GFARM_ERR_NO_ERROR);

	assert(transaction_nesting == 0);
	
	return (gfarm_pgsql_exec_and_log(transaction_ok ? "COMMIT" : "ROLLBACK",
	    diag));
}

/* this interface is exported for a use from a private extension */
gfarm_error_t
gfarm_pgsql_rollback(const char *diag)
{
	transaction_ok = 0;

	return (gfarm_pgsql_commit(diag));
}

/* this interface is exported for a use from a private extension */
gfarm_error_t
gfarm_pgsql_insert_with_retry(const char *command,
	int nParams,
	const Oid *paramTypes,
	const char *const *paramValues,
	const int *paramLengths,
	const int *paramFormats,
	int resultFormat,
	const char *diag)
{
	PGresult *res = gfarm_pgsql_exec_params_with_retry(command, nParams,
	    paramTypes, paramValues, paramLengths, paramFormats, resultFormat);
	gfarm_error_t e = gfarm_pgsql_check_insert(res, command, diag);

	PQclear(res);
	return (e);
}

/* this interface is exported for a use from a private extension */
gfarm_error_t
gfarm_pgsql_update_or_delete_with_retry(const char *command,
	int nParams,
	const Oid *paramTypes,
	const char *const *paramValues,
	const int *paramLengths,
	const int *paramFormats,
	int resultFormat,
	const char *diag)
{
	PGresult *res = gfarm_pgsql_exec_params_with_retry(command, nParams,
	    paramTypes, paramValues, paramLengths, paramFormats, resultFormat);
	gfarm_error_t e =
		gfarm_pgsql_check_update_or_delete(res, command, diag);

	PQclear(res);
	return (e);
}

/* this interface is exported for a use from a private extension */
gfarm_error_t
gfarm_pgsql_generic_get_all(
	const char *sql,
	int nparams,
	const char **paramValues,
	int *np,
	void *resultsp,
	const struct gfarm_base_generic_info_ops *ops,
	gfarm_error_t (*set_fields)(PGresult *, int, void *),
	const char *diag)
{
	PGresult *res;
	gfarm_error_t e;
	int n, i, retry_count = 0;
	char *results;

	do {
		res = PQexecParams(conn, sql,
			nparams,
			NULL, /* param types */
			paramValues,
			NULL, /* param lengths */
			NULL, /* param formats */
			1); /* ask for binary results */
	} while (PQresultStatus(res) != PGRES_TUPLES_OK &&
	    pgsql_should_retry(res, &retry_count));
	e = gfarm_pgsql_check_select(res, sql, diag);
	if (e == GFARM_ERR_NO_ERROR) {
		n = PQntuples(res);
		if (n == 0) {
			e = GFARM_ERR_NO_SUCH_OBJECT;
		} else if ((results = malloc(ops->info_size * n)) == NULL) {
			e = GFARM_ERR_NO_MEMORY;
		} else {
			for (i = 0; i < n; i++) {
				(*ops->clear)(results + i * ops->info_size);
				(*set_fields)(res, i,
				    results + i * ops->info_size);
			}
			*np = n;
			*(char **)resultsp = results;
		}
	}
	PQclear(res);
	return (e);
}

/* this interface is exported for a use from a private extension */
gfarm_error_t
gfarm_pgsql_generic_grouping_get_all(
	const char *count_sql,
	const char *results_sql,
	int nparams,
	const char **paramValues,
	int *np,
	void *resultsp,
	const struct gfarm_base_generic_info_ops *ops,
	gfarm_error_t (*set_fields_with_grouping)(PGresult *, int,int, void *),
	const char *diag)
{
	PGresult *cres, *rres;
	gfarm_error_t e;
	int ngroups;
	char *results;

	if ((e = gfarm_pgsql_start_with_retry(diag)) != GFARM_ERR_NO_ERROR)
		return (e);

	cres = PQexecParams(conn,
		count_sql,
		nparams,
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		1); /* ask for binary results */
	e = gfarm_pgsql_check_select(cres, count_sql, diag);
	if (e != GFARM_ERR_NO_ERROR)
		;
	else if ((ngroups = PQntuples(cres)) == 0) {
		e = GFARM_ERR_NO_SUCH_OBJECT;
	} else if ((results = malloc(ops->info_size * ngroups)) == NULL) {
		e = GFARM_ERR_NO_MEMORY;
	} else {
		rres = PQexecParams(conn,
			results_sql,
			nparams,
			NULL, /* param types */
			paramValues,
			NULL, /* param lengths */
			NULL, /* param formats */
			1); /* ask for binary results */
		e = gfarm_pgsql_check_select(rres, results_sql, diag);
		if (e != GFARM_ERR_NO_ERROR) {
			free(results);
		} else {
			int i, startrow = 0;

			for (i = 0; i < ngroups; i++) {
				int nmembers =
				    pgsql_get_int64(cres, i, "COUNT");

				(*ops->clear)(results + i * ops->info_size);
				e = (*set_fields_with_grouping)(
				    rres, startrow, nmembers,
				    results + i * ops->info_size);
				if (e != GFARM_ERR_NO_ERROR) {
					while (--i >= 0) {
						(*ops->free)(results
						    + i * ops->info_size);
					}
					free(results);
					break;
				}
				startrow += (nmembers == 0 ? 1 : nmembers);
			}
			if (e == GFARM_ERR_NO_ERROR) {
				*np = ngroups;
				*(char **)resultsp = results;
			}
		}
		PQclear(rres);
	}
	PQclear(cres);

	if (e == GFARM_ERR_NO_ERROR)
		e = gfarm_pgsql_commit(diag);
	else
		gfarm_pgsql_rollback(diag);

	return (e);
}

/**********************************************************************/

#define COPY_BINARY(data, buf, residual, msg) { \
	if (sizeof(data) > residual) \
		gflog_fatal(GFARM_MSG_1000429, \
		    msg ": %d bytes needed, but only %d bytes", \
		    (int)sizeof(data), residual); \
	memcpy(&(data), buf, sizeof(data)); \
	buf += sizeof(data); \
	residual -= sizeof(data); \
}

#define COPY_INT32(int32, buf, residual, msg) { \
	assert(sizeof(int32) == sizeof(int32_t)); \
	COPY_BINARY(int32, buf, residual, msg); \
	int32 = ntohl(int32); \
}

static char *
get_value_from_varchar_copy_binary(const char **bufp, int *residualp)
{
	int32_t len;
	char *p;

	COPY_INT32(len, *bufp, *residualp, "metdb_pgsql: copy varchar");
	if (len == -1) /* NULL field */
		return (NULL);
	if (len < 0) /* We don't allow that long varchar */
		gflog_fatal(GFARM_MSG_1000430,
		    "metadb_pgsql: copy varchar length=%d", len);

	if (len > *residualp)
		gflog_fatal(GFARM_MSG_1000431,
		    "metadb_pgsql: copy varchar %d > %d",
		    len, *residualp);
	GFARM_MALLOC_ARRAY(p, len + 1);
	memcpy(p, *bufp, len);
	p[len] = '\0';
	*bufp += len;
	*residualp -= len;
	return (p);
}

static uint32_t
get_value_from_integer_copy_binary(const char **bufp, int *residualp)
{
	int32_t len;
	uint32_t val;

	COPY_INT32(len, *bufp, *residualp, "metadb_pgsql: copy int32 len");
	if (len == -1)
		return (0); /* stopgap for NULL field */
	if (len != sizeof(val))
		gflog_fatal(GFARM_MSG_1000432,
		    "metadb_pgsql: copy int32 length=%d", len);

	COPY_INT32(val, *bufp, *residualp, "metadb_pgsql: copy int32");
	return (val);
}

static uint64_t
get_value_from_int8_copy_binary(const char **bufp, int *residualp)
{
	int32_t len;
	uint64_t val;

	COPY_INT32(len, *bufp, *residualp, "metadb_pgsql: copy int64 len");
	if (len == -1)
		return (0); /* stopgap for NULL field */
	if (len != sizeof(val))
		gflog_fatal(GFARM_MSG_1000433,
		    "metadb_pgsql: copy int64 length=%d", len);

	COPY_BINARY(val, *bufp, *residualp, "metadb_pgsql: copy int64");
	val = gfarm_ntoh64(val);
	return (val);
}

#define COPY_BINARY_SIGNATURE_LEN		11
#define COPY_BINARY_FLAGS_FIELD_LEN		4
#define COPY_BINARY_HEADER_EXTENSION_AREA_LEN	4
#define COPY_BINARY_HEADER_LEN (COPY_BINARY_SIGNATURE_LEN + \
	    COPY_BINARY_FLAGS_FIELD_LEN+COPY_BINARY_HEADER_EXTENSION_AREA_LEN)
#define COPY_BINARY_TRAILER_LEN			2
#define PQ_GET_COPY_DATA_DONE			-1
#define PQ_GET_COPY_DATA_ERROR			-2

#define	COPY_BINARY_FLAGS_CRITICAL		0xffff0000
#define	COPY_BINARY_TRAILER_VALUE		-1

static gfarm_error_t
gfarm_pgsql_generic_load(
	const char *command,
	void *tmp_info, /* just used as a work area */
	void (*callback)(void *, void *),
	void *closure,
	const struct gfarm_base_generic_info_ops *base_ops,
	void (*set_fields_from_copy_binary)(const char *, int, void *),
	const char *diag)
{
	PGresult *res;
	char *buf, *bp;
	int ret;
	uint32_t header_flags, extension_area_len;
	int16_t trailer;
	int retry_count = 0;

	static const char binary_signature[COPY_BINARY_SIGNATURE_LEN] =
		"PGCOPY\n\377\r\n\0";

	do {
		res = PQexec(conn, command);
	} while (PQresultStatus(res) != PGRES_COPY_OUT &&
	    pgsql_should_retry(res, &retry_count));
	if (PQresultStatus(res) != PGRES_COPY_OUT) {
		gflog_error(GFARM_MSG_1000434, "%s: %s: %s", diag, command,
		    PQresultErrorMessage(res));
		PQclear(res);
		return (GFARM_ERR_UNKNOWN);
	}
	PQclear(res);

	ret = PQgetCopyData(conn, &buf,	0);
	if (ret < COPY_BINARY_HEADER_LEN + COPY_BINARY_TRAILER_LEN ||
	    memcmp(buf, binary_signature, COPY_BINARY_SIGNATURE_LEN) != 0) {
		gflog_fatal(GFARM_MSG_1000435, "%s: "
		    "Fatal Error, COPY file signature not recognized: %d",
		    diag, ret);
	}
	bp = buf;
	bp  += COPY_BINARY_SIGNATURE_LEN;
	ret -= COPY_BINARY_SIGNATURE_LEN;

	COPY_INT32(header_flags, bp, ret, "db_pgsql: COPY header flag");
	if (header_flags & COPY_BINARY_FLAGS_CRITICAL)
		gflog_fatal(GFARM_MSG_1000436, "%s: "
		    "Fatal Error, COPY file protocol incompatible: 0x%08x",
		    diag, header_flags);

	COPY_INT32(extension_area_len, bp, ret,
	    "db_pgsql: COPY extension area length");
	if (ret < extension_area_len)
		gflog_fatal(GFARM_MSG_1000437, "%s: "
		    "Fatal Error, COPY file extension_area too short: %d < %d",
		    diag, ret, extension_area_len);
	bp  += extension_area_len;
	ret -= extension_area_len;

	for (;;) {
		if (ret < COPY_BINARY_TRAILER_LEN)
			gflog_fatal(GFARM_MSG_1000438, "%s: "
			    "Fatal error, COPY file trailer too short: %d",
			    diag, ret);
		/* don't use COPY_BINARY() here to not proceed the pointer */
		memcpy(&trailer, bp, sizeof(trailer));
		trailer = ntohs(trailer);

		if (trailer == COPY_BINARY_TRAILER_VALUE) {
			PQfreemem(buf);
			/* make sure that the COPY is done */
			ret = PQgetCopyData(conn, &buf, 0);
			if (ret >= 0)
				gflog_fatal(GFARM_MSG_1000439, "%s: "
				    "Fatal error, COPY file data after trailer"
				    ": %d", diag, ret);
			break;
		}
		(*base_ops->clear)(tmp_info);
		(*set_fields_from_copy_binary)(bp, ret, tmp_info);
		if ((*base_ops->validate)(tmp_info))
			(*callback)(closure, tmp_info);
#if 0		/* the (*callback)() routine frees this memory */
		(*base_ops->free)(tmp_info);
#endif
		PQfreemem(buf);

		ret = PQgetCopyData(conn, &buf, 0);
		bp = buf;
		if (ret < 0) {
			gflog_warning(GFARM_MSG_1000440,
			    "%s: warning: COPY file expected end of data",
			    diag);
			break;
		}
	}
	if (buf != NULL)
		gflog_warning(GFARM_MSG_1000441,
		    "%s: warning: COPY file NULL expected, possibly leak?",
		    diag);
	if (ret == PQ_GET_COPY_DATA_ERROR) {
		gflog_error(GFARM_MSG_1000442,
		    "%s: data error: %s", diag, PQerrorMessage(conn));
		return (GFARM_ERR_UNKNOWN);
	}
	res = PQgetResult(conn);
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		gflog_error(GFARM_MSG_1000443,
		    "%s: failed: %s", diag, PQresultErrorMessage(res));
		PQclear(res);
		return (GFARM_ERR_UNKNOWN);
	}
	PQclear(res);
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

static gfarm_error_t
gfarm_pgsql_begin(void *arg)
{
	assert(transaction_nesting == 0);
	return (gfarm_pgsql_start_with_retry("pgsql_begin"));
}

static gfarm_error_t
gfarm_pgsql_end(void *arg)
{
	gfarm_error_t e = gfarm_pgsql_commit(
	    transaction_ok ? "gfarm_pgsql_end(OK)" : "gfarm_pgsql_end(NG)");

	assert(transaction_nesting == 0);
	return (e);
}

/**********************************************************************/

static gfarm_error_t
host_info_set_fields_with_grouping(
	PGresult *res, int startrow, int nhostaliases, void *vinfo)
{
	struct gfarm_host_info *info = vinfo;
	int i;

	info->hostname = pgsql_get_string(res, startrow, "hostname");
	info->port = pgsql_get_int32(res, startrow, "port");
	info->architecture = pgsql_get_string(res, startrow, "architecture");
	info->ncpu = pgsql_get_int32(res, startrow, "ncpu");
	info->flags = pgsql_get_int32(res, startrow, "flags");
	info->nhostaliases = nhostaliases;
	GFARM_MALLOC_ARRAY(info->hostaliases, nhostaliases + 1);
	for (i = 0; i < nhostaliases; i++) {
		info->hostaliases[i] = pgsql_get_string(res, startrow + i,
		    "hostalias");
	}
	info->hostaliases[info->nhostaliases] = NULL;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
hostaliases_remove(const char *hostname)
{
	/*
	 * We don't use check_modify_or_remove() but check_misc() here,
	 * because it's OK that there isn't any alias.
	 */
	const char *paramValues[1];

	paramValues[0] = hostname;
	return (gfarm_pgsql_exec_params_and_log(
		"DELETE FROM HostAliases WHERE hostname = $1",
		1, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0, /* ask for text results */
		"pgsql_hostaliases_remove"));
}

static gfarm_error_t
hostaliases_set(struct gfarm_host_info *info)
{
	const char *paramValues[2];
	int i;
	gfarm_error_t e;

	if (info->hostaliases == NULL)
		return (GFARM_ERR_NO_ERROR);
	for (i = 0; i < info->nhostaliases; i++) {
		paramValues[0] = info->hostname;
		paramValues[1] = info->hostaliases[i];
		e = gfarm_pgsql_insert_and_log(
			"INSERT INTO Hostaliases (hostname, hostalias)"
			    " VALUES ($1, $2)",
			2, /* number of params */
			NULL, /* param types */
			paramValues,
			NULL, /* param lengths */
			NULL, /* param formats */
			0, /* ask for text results */
			"pgsql_hostaliases_set");
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
pgsql_host_update(struct gfarm_host_info *info, const char *sql,
	gfarm_error_t (*check)(PGresult *, const char *, const char *),
	int remove_all_aliases_first,
	const char *diag)
{
	PGresult *res;
	const char *paramValues[5];
	gfarm_error_t e;
	char port[GFARM_INT32STRLEN + 1];
	char ncpu[GFARM_INT32STRLEN + 1];
	char flags[GFARM_INT32STRLEN + 1];

	if ((e = gfarm_pgsql_start_with_retry(diag)) != GFARM_ERR_NO_ERROR)
		return (e);

	paramValues[0] = info->hostname;
	sprintf(port, "%d", info->port);
	paramValues[1] = port;
	paramValues[2] = info->architecture;
	sprintf(ncpu, "%d", info->ncpu);
	paramValues[3] = ncpu;
	sprintf(flags, "%d", info->flags);
	paramValues[4] = flags;
	res = PQexecParams(conn,
		sql,
		5, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0); /* ask for text results */
	e = (*check)(res, sql, diag);
	PQclear(res);

	if (e == GFARM_ERR_NO_ERROR && remove_all_aliases_first)
		e = hostaliases_remove(info->hostname);

	if (e == GFARM_ERR_NO_ERROR)
		e = hostaliases_set(info);

	if (e == GFARM_ERR_NO_ERROR)
		e = gfarm_pgsql_commit(diag);
	else
		gfarm_pgsql_rollback(diag);
	return (e);
}

static gfarm_error_t
gfarm_pgsql_host_add(struct gfarm_host_info *info)
{
	gfarm_error_t e;

	e = pgsql_host_update(info,
		"INSERT INTO Host (hostname, port, architecture, ncpu, flags) "
		    "VALUES ($1, $2, $3, $4, $5)",
		gfarm_pgsql_check_insert, 0, "pgsql_host_add");

	free(info);
	return e;
}

static gfarm_error_t
gfarm_pgsql_host_modify(struct db_host_modify_arg *arg)
{
	gfarm_error_t e;
	/* XXX FIXME: should use modflags, add_aliases and del_aliases */

	e = pgsql_host_update(&arg->hi,
		"UPDATE Host "
		    "SET port = $2, architecture = $3, ncpu = $4, flags = $5 "
		    "WHERE hostname = $1",
		gfarm_pgsql_check_update_or_delete, 1, "pgsql_host_modify");

	free(arg);
	return e;
}

static gfarm_error_t
gfarm_pgsql_host_remove(char *hostname)
{
	gfarm_error_t e;
	const char *paramValues[1];

	paramValues[0] = hostname;
	e = gfarm_pgsql_update_or_delete_with_retry(
		"DELETE FROM Host WHERE hostname = $1",
		1, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0, /* ask for text results */
		"pgsql_host_remove");

	free(hostname);
	return e;
}

static gfarm_error_t
gfarm_pgsql_host_load(void *closure,
	void (*callback)(void *, struct gfarm_host_info *))
{
	gfarm_error_t e;
	int i, n;
	struct gfarm_host_info *infos;

	e = gfarm_pgsql_generic_grouping_get_all(
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

		0, NULL,
		&n, &infos,
		&gfarm_base_host_info_ops, host_info_set_fields_with_grouping,
		"pgsql_host_load");
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	for (i = 0; i < n; i++)
		(*callback)(closure, &infos[i]);

	free(infos);
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

static gfarm_error_t
user_info_set_field(PGresult *res, int row, void *vinfo)
{
	struct gfarm_user_info *info = vinfo;

	info->username = pgsql_get_string(res, row, "username");
	info->homedir = pgsql_get_string(res, row, "homedir");
	info->realname = pgsql_get_string(res, row, "realname");
	info->gsi_dn = pgsql_get_string(res, row, "gsiDN");
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
pgsql_user_call(struct gfarm_user_info *info, const char *sql,
	gfarm_error_t (*op)(const char *, int, const Oid *,
		const char *const *, const int *, const int *, int,
		const char *),
	const char *diag)
{
	const char *paramValues[4];

	paramValues[0] = info->username;
	paramValues[1] = info->homedir;
	paramValues[2] = info->realname;
	paramValues[3] = info->gsi_dn;
	return (*op)(
		sql,
		4, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0, /* ask for text results */
		diag);
}

static gfarm_error_t
gfarm_pgsql_user_add(struct gfarm_user_info *info)
{
	gfarm_error_t e;
	e = pgsql_user_call(info,
		"INSERT INTO GfarmUser (username, homedir, realname, gsiDN) "
		     "VALUES ($1, $2, $3, $4)",
		gfarm_pgsql_insert_with_retry,
		"pgsql_user_add");

	free(info);
	return e;
}

static gfarm_error_t
gfarm_pgsql_user_modify(struct db_user_modify_arg *arg)
{
	gfarm_error_t e;
	e = pgsql_user_call(&arg->ui,
		"UPDATE GfarmUser "
		     "SET homedir = $2, realname = $3, gsiDN = $4 "
		     "WHERE username = $1",
		gfarm_pgsql_update_or_delete_with_retry,
		"pgsql_user_modify");

	free(arg);
	return e;
}

static gfarm_error_t
gfarm_pgsql_user_remove(char *username)
{
	gfarm_error_t e;
	const char *paramValues[1];

	paramValues[0] = username;
	e = gfarm_pgsql_update_or_delete_with_retry(
		"DELETE FROM GfarmUser WHERE username = $1",
		1, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0, /* ask for text results */
		"pgsql_user_remove");

	free(username);
	return e;
}

static gfarm_error_t
gfarm_pgsql_user_load(void *closure,
	void (*callback)(void *, struct gfarm_user_info *))
{
	gfarm_error_t e;
	int i, n;
	struct gfarm_user_info *infos;

	e = gfarm_pgsql_generic_get_all(
		"SELECT * FROM GfarmUser",
		0, NULL,
		&n, &infos,
		&gfarm_base_user_info_ops, user_info_set_field,
		"pgsql_user_load");
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	for (i = 0; i < n; i++)
		(*callback)(closure, &infos[i]);

	free(infos);
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

static gfarm_error_t
group_info_set_fields_with_grouping(
	PGresult *res, int startrow, int nusers, void *vinfo)
{
	struct gfarm_group_info *info = vinfo;
	int i;

	info->groupname = pgsql_get_string(res, startrow, "groupname");
	info->nusers = nusers;
	GFARM_MALLOC_ARRAY(info->usernames, nusers + 1);
	for (i = 0; i < nusers; i++) {
		info->usernames[i] =
		    pgsql_get_string(res, startrow + i, "username");
	}
	info->usernames[info->nusers] = NULL;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
grpassign_remove(const char *groupname)
{
	/*
	 * We don't use check_modify_or_remove() but check_misc() here,
	 * because it's OK that there isn't any grpassign.
	 */
	const char *paramValues[1];

	paramValues[0] = groupname;
	return (gfarm_pgsql_exec_params_and_log(
		"DELETE FROM GfarmGroupAssignment WHERE groupname = $1",
		1, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0, /* ask for text results */
		"pgsql_grpassign_remove"));
}

static gfarm_error_t
grpassign_set(struct gfarm_group_info *info)
{
	const char *paramValues[2];
	int i;
	gfarm_error_t e;

	if (info->usernames == NULL)
		return (GFARM_ERR_NO_ERROR);
	for (i = 0; i < info->nusers; i++) {
		paramValues[0] = info->groupname;
		paramValues[1] = info->usernames[i];
		e = gfarm_pgsql_insert_and_log(
			"INSERT INTO "
			    "GfarmGroupAssignment (groupname, username) "
			    "VALUES ($1, $2)",
			2, /* number of params */
			NULL, /* param types */
			paramValues,
			NULL, /* param lengths */
			NULL, /* param formats */
			0, /* ask for text results */
			"pgsql_grpassign_set");
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_pgsql_group_add(struct gfarm_group_info *info)
{
	const char *paramValues[1];
	gfarm_error_t e;
	static const char msg[] = "pgsql_group_add";

	if ((e = gfarm_pgsql_start_with_retry(msg))
	    == GFARM_ERR_NO_ERROR) {

		paramValues[0] = info->groupname;
		e = gfarm_pgsql_insert_and_log(
			"INSERT INTO GfarmGroup (groupname) VALUES ($1)",
			1, /* number of params */
			NULL, /* param types */
			paramValues,
			NULL, /* param lengths */
			NULL, /* param formats */
			0, /* ask for text results */
			msg);

		if (e == GFARM_ERR_NO_ERROR)
			e = grpassign_set(info);

		if (e == GFARM_ERR_NO_ERROR)
			e = gfarm_pgsql_commit(msg);
		else
			gfarm_pgsql_rollback(msg);
	}

	free(info);
	return (e);
}

static gfarm_error_t
gfarm_pgsql_group_modify(struct db_group_modify_arg *arg)
{
	struct gfarm_group_info *info = &arg->gi;
	gfarm_error_t e;
	static const char msg[] = "gfarm_pgsql_group_modify";

	if ((e = gfarm_pgsql_start_with_retry("pgsql_group_modify"))
	    == GFARM_ERR_NO_ERROR) {

		e = grpassign_remove(info->groupname);

		if (e == GFARM_ERR_NO_ERROR)
			e = grpassign_set(info);

		if (e == GFARM_ERR_NO_ERROR)
			e = gfarm_pgsql_commit(msg);
		else
			gfarm_pgsql_rollback(msg);
	}
	free(arg);
	return (e);
}

static gfarm_error_t
gfarm_pgsql_group_remove(char *groupname)
{
	gfarm_error_t e;
	const char *paramValues[1];

	paramValues[0] = groupname;
	e = gfarm_pgsql_update_or_delete_with_retry(
		"DELETE FROM GfarmGroup WHERE groupname = $1",
		1, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0, /* ask for text results */
		"pgsql_group_remove");

	free(groupname);
	return e;
}

static gfarm_error_t
gfarm_pgsql_group_load(void *closure,
	void (*callback)(void *, struct gfarm_group_info *))
{
	gfarm_error_t e;
	int i, n;
	struct gfarm_group_info *infos;

	e = gfarm_pgsql_generic_grouping_get_all(
		"SELECT GfarmGroup.groupname, count(username) "
		    "FROM GfarmGroup LEFT OUTER JOIN GfarmGroupAssignment "
		    "ON GfarmGroup.groupname = GfarmGroupAssignment.groupname "
		    "GROUP BY GfarmGroup.groupname "
		    "ORDER BY GfarmGroup.groupname",

		"SELECT GfarmGroup.groupname, username "
		    "FROM GfarmGroup LEFT OUTER JOIN GfarmGroupAssignment "
		    "ON GfarmGroup.groupname = GfarmGroupAssignment.groupname "
		    "ORDER BY GfarmGroup.groupname, username",

		0, NULL,
		&n, &infos,
		&gfarm_base_group_info_ops,
		group_info_set_fields_with_grouping,
		"pgsql_group_load");
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	for (i = 0; i < n; i++)
		(*callback)(closure, &infos[i]);

	free(infos);
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

static gfarm_error_t
pgsql_inode_call(struct gfs_stat *info, const char *sql,
	gfarm_error_t (*op)(const char *, int, const Oid *,
		const char *const *, const int *, const int *, int,
		const char *),
	const char *diag)
{
	gfarm_error_t e;
	const char *paramValues[13];
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

	e = (*op)(
		sql,
		13, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0, /* ask for text results */
		diag);

	free(info);
	return e;
}

static gfarm_error_t
gfarm_pgsql_inode_add(struct gfs_stat *info)
{
	return pgsql_inode_call(info,
		"INSERT INTO INode (inumber, igen, nlink, size, mode, "
			           "username, groupname, "
				   "atimesec, atimensec, "
				   "mtimesec, mtimensec, "
				   "ctimesec, ctimensec) "
		    "VALUES ($1, $2, $3, $4, $5, "
			    "$6, $7, $8, $9 ,$10, $11, $12, $13)",
		gfarm_pgsql_insert_with_retry,
		"pgsql_inode_add");
}

static gfarm_error_t
gfarm_pgsql_inode_modify(struct gfs_stat *info)
{
	return pgsql_inode_call(info,
		"UPDATE INode SET igen = $2, nlink = $3, size = $4, "
				"mode = $5, username = $6, groupname = $7, "
				"atimesec = $8,  atimensec = $9, "
				"mtimesec = $10, mtimensec = $11, "
				"ctimesec = $12, ctimensec = $13 "
		    "WHERE inumber = $1",
		gfarm_pgsql_update_or_delete_with_retry,
		"pgsql_inode_modify");
}

static gfarm_error_t
pgsql_inode_uint64_call(struct db_inode_uint64_modify_arg *arg,
	const char *sql, const char *diag)
{
	gfarm_error_t e;
	const char *paramValues[2];
	char inumber[GFARM_INT64STRLEN + 1];
	char uint64[GFARM_INT64STRLEN + 1];

	sprintf(inumber, "%" GFARM_PRId64, arg->inum);
	paramValues[0] = inumber;
	sprintf(uint64, "%" GFARM_PRId64, arg->uint64);
	paramValues[1] = uint64;

	e = gfarm_pgsql_update_or_delete_with_retry(
		sql,
		2, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0, /* ask for text results */
		diag);

	free(arg);
	return e;
}

static gfarm_error_t
pgsql_inode_uint32_call(struct db_inode_uint32_modify_arg *arg,
	const char *sql, const char *diag)
{
	gfarm_error_t e;
	const char *paramValues[2];
	char inumber[GFARM_INT64STRLEN + 1];
	char uint32[GFARM_INT32STRLEN + 1];

	sprintf(inumber, "%" GFARM_PRId64, arg->inum);
	paramValues[0] = inumber;
	sprintf(uint32, "%d", arg->uint32);
	paramValues[1] = uint32;

	e = gfarm_pgsql_update_or_delete_with_retry(
		sql,
		2, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0, /* ask for text results */
		diag);

	free(arg);
	return e;
}

static gfarm_error_t
pgsql_inode_string_call(struct db_inode_string_modify_arg *arg,
	const char *sql, const char *diag)
{
	gfarm_error_t e;
	const char *paramValues[2];
	char inumber[GFARM_INT64STRLEN + 1];

	sprintf(inumber, "%" GFARM_PRId64, arg->inum);
	paramValues[0] = inumber;
	paramValues[1] = arg->string;

	e = gfarm_pgsql_update_or_delete_with_retry(
		sql,
		2, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0, /* ask for text results */
		diag);

	free(arg);
	return e;
}

static gfarm_error_t
pgsql_inode_timespec_call(struct db_inode_timespec_modify_arg *arg,
	const char *sql, const char *diag)
{
	gfarm_error_t e;
	const char *paramValues[3];
	char inumber[GFARM_INT64STRLEN + 1];
	char sec[GFARM_INT64STRLEN + 1];
	char nsec[GFARM_INT32STRLEN + 1];

	sprintf(inumber, "%" GFARM_PRId64, arg->inum);
	paramValues[0] = inumber;
	sprintf(sec, "%" GFARM_PRId64, arg->time.tv_sec);
	paramValues[1] = sec;
	sprintf(nsec, "%d", arg->time.tv_nsec);
	paramValues[2] = nsec;

	e = gfarm_pgsql_update_or_delete_with_retry(
		sql,
		3, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0, /* ask for text results */
		diag);

	free(arg);
	return e;
}

static gfarm_error_t
gfarm_pgsql_inode_nlink_modify(struct db_inode_uint64_modify_arg *arg)
{
	return pgsql_inode_uint64_call(arg,
	    "UPDATE INode SET nlink = $2 WHERE inumber = $1",
	    "pgsql_inode_nlink_modify");
}

static gfarm_error_t
gfarm_pgsql_inode_size_modify(struct db_inode_uint64_modify_arg *arg)
{
	return pgsql_inode_uint64_call(arg,
	    "UPDATE INode SET size = $2 WHERE inumber = $1",
	    "pgsql_inode_size_modify");
}

static gfarm_error_t
gfarm_pgsql_inode_mode_modify(struct db_inode_uint32_modify_arg *arg)
{
	return pgsql_inode_uint32_call(arg,
	    "UPDATE INode SET mode = $2 WHERE inumber = $1",
	    "pgsql_inode_mode_modify");
}

static gfarm_error_t
gfarm_pgsql_inode_user_modify(struct db_inode_string_modify_arg *arg)
{
	return pgsql_inode_string_call(arg,
	    "UPDATE INode SET username = $2 WHERE inumber = $1",
	    "pgsql_inode_user_modify");
}

static gfarm_error_t
gfarm_pgsql_inode_group_modify(struct db_inode_string_modify_arg *arg)
{
	return pgsql_inode_string_call(arg,
	    "UPDATE INode SET groupname = $2 WHERE inumber = $1",
	    "pgsql_inode_group_modify");
}

static gfarm_error_t
gfarm_pgsql_inode_atime_modify(struct db_inode_timespec_modify_arg *arg)
{
	return pgsql_inode_timespec_call(arg,
	  "UPDATE INode SET atimesec = $2, atimensec = $3 WHERE inumber = $1",
	    "pgsql_inode_atime_modify");
}

static gfarm_error_t
gfarm_pgsql_inode_mtime_modify(struct db_inode_timespec_modify_arg *arg)
{
	return pgsql_inode_timespec_call(arg,
	  "UPDATE INode SET mtimesec = $2, mtimensec = $3 WHERE inumber = $1",
	  "pgsql_inode_mtime_modify");
}

static gfarm_error_t
gfarm_pgsql_inode_ctime_modify(struct db_inode_timespec_modify_arg *arg)
{
	return pgsql_inode_timespec_call(arg,
	  "UPDATE INode SET ctimesec = $2, ctimensec = $3 WHERE inumber = $1",
	  "pgsql_inode_ctime_modify");
}

static void
inode_info_set_fields_from_copy_binary(
	const char *buf, int residual, void *vinfo)
{
	struct gfs_stat *info = vinfo;
	uint16_t num_fields;

	COPY_BINARY(num_fields, buf, residual,
	    "pgsql_inode_load: field number");
	num_fields = ntohs(num_fields);
	if (num_fields < 13) /* allow fields addition in future */
		gflog_fatal(GFARM_MSG_1000444,
		    "pgsql_inode_load: fields = %d", num_fields);

	info->st_ino = get_value_from_int8_copy_binary(&buf, &residual);
	info->st_gen = get_value_from_int8_copy_binary(&buf, &residual);
	info->st_nlink = get_value_from_int8_copy_binary(&buf, &residual);
	info->st_size = get_value_from_int8_copy_binary(&buf, &residual);
	info->st_ncopy = 0;
	info->st_mode = get_value_from_integer_copy_binary(&buf, &residual);
	info->st_user = get_value_from_varchar_copy_binary(&buf, &residual);
	info->st_group = get_value_from_varchar_copy_binary(&buf, &residual);
	info->st_atimespec.tv_sec =
		get_value_from_int8_copy_binary(&buf, &residual);
	info->st_atimespec.tv_nsec =
		get_value_from_integer_copy_binary(&buf, &residual);
	info->st_mtimespec.tv_sec =
		get_value_from_int8_copy_binary(&buf, &residual);
	info->st_mtimespec.tv_nsec =
		get_value_from_integer_copy_binary(&buf, &residual);
	info->st_ctimespec.tv_sec =
		get_value_from_int8_copy_binary(&buf, &residual);
	info->st_ctimespec.tv_nsec =
		get_value_from_integer_copy_binary(&buf, &residual);
}

static gfarm_error_t
gfarm_pgsql_inode_load(
	void *closure,
	void (*callback)(void *, struct gfs_stat *))
{
	struct gfs_stat tmp_info;

	return (gfarm_pgsql_generic_load(
		"COPY INode TO STDOUT BINARY",
		&tmp_info, (void (*)(void *, void *))callback, closure,
		&gfarm_base_gfs_stat_ops,
		inode_info_set_fields_from_copy_binary,
		"pgsql_inode_load"));
}

/**********************************************************************/

static gfarm_error_t
pgsql_inode_cksum_call(struct db_inode_cksum_arg *arg, const char *sql,
	gfarm_error_t (*op)(const char *, int, const Oid *,
		const char *const *, const int *, const int *, int,
		const char *),
	const char *diag)
{
	gfarm_error_t e;
	const char *paramValues[3];
	char inumber[GFARM_INT64STRLEN + 1];

	sprintf(inumber, "%" GFARM_PRId64, arg->inum);
	paramValues[0] = inumber;
	paramValues[1] = arg->type;
	paramValues[2] = arg->sum;
	e = (*op)(
		sql,
		3, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0, /* ask for text results */
		diag);

	free(arg);
	return e;
}

static gfarm_error_t
pgsql_inode_inum_call(struct db_inode_inum_arg *arg, const char *sql,
	gfarm_error_t (*op)(const char *, int, const Oid *,
		const char *const *, const int *, const int *, int,
		const char *),
	const char *diag)
{
	gfarm_error_t e;
	const char *paramValues[1];
	char inumber[GFARM_INT64STRLEN + 1];

	sprintf(inumber, "%" GFARM_PRId64, arg->inum);
	paramValues[0] = inumber;
	e = (*op)(
		sql,
		1, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0, /* ask for text results */
		diag);

	free(arg);
	return e;
}

static gfarm_error_t
gfarm_pgsql_file_info_add(struct db_inode_cksum_arg *arg)
{
	return pgsql_inode_cksum_call(arg,
		"INSERT INTO FileInfo (inumber, checksumType, checksum) "
		     "VALUES ($1, $2, $3)",
		gfarm_pgsql_insert_with_retry, "pgsql_cksum_add");
}

static gfarm_error_t
gfarm_pgsql_file_info_modify(struct db_inode_cksum_arg *arg)
{
	return pgsql_inode_cksum_call(arg,
		"UPDATE FileInfo SET checksumType = $2, checksum = $3 "
		    "WHERE inumber = $1",
		gfarm_pgsql_update_or_delete_with_retry, "pgsql_cksum_modify");
}

static gfarm_error_t
gfarm_pgsql_file_info_remove(struct db_inode_inum_arg *arg)
{
	return pgsql_inode_inum_call(arg,
		"DELETE FROM FileInfo WHERE inumber = $1",
		gfarm_pgsql_update_or_delete_with_retry,
		"pgsql_cksum_remove");
}

static void
file_info_set_fields_from_copy_binary(
	const char *buf, int residual, void *vinfo)
{
	struct db_inode_cksum_arg *info = vinfo;
	uint16_t num_fields;

	COPY_BINARY(num_fields, buf, residual,
	    "pgsql_file_info_load: field number");
	num_fields = ntohs(num_fields);
	if (num_fields < 3) /* allow fields addition in future */
		gflog_fatal(GFARM_MSG_1000445,
		    "pgsql_file_info_load: fields = %d", num_fields);

	info->inum = get_value_from_int8_copy_binary(&buf, &residual);
	info->type = get_value_from_varchar_copy_binary(&buf, &residual);
	info->sum = get_value_from_varchar_copy_binary(&buf, &residual);
	info->len = strlen(info->sum);
}

static gfarm_error_t
gfarm_pgsql_file_info_load(
	void *closure,
	void (*callback)(void *, gfarm_ino_t, char *, size_t, char *))
{
	struct db_inode_cksum_arg tmp_info;
	struct db_inode_cksum_trampoline_closure c;

	c.closure = closure;
	c.callback = callback;

	return (gfarm_pgsql_generic_load(
		"COPY FileInfo TO STDOUT BINARY",
		&tmp_info, db_inode_cksum_callback_trampoline, &c,
		&db_base_inode_cksum_arg_ops,
		file_info_set_fields_from_copy_binary,
		"pgsql_cksum_load"));
}

/**********************************************************************/

static gfarm_error_t
pgsql_filecopy_call(struct db_filecopy_arg *arg, const char *sql,
	gfarm_error_t (*op)(const char *, int, const Oid *,
		const char *const *, const int *, const int *, int,
		const char *),
	const char *diag)
{
	gfarm_error_t e;
	const char *paramValues[2];
	char inumber[GFARM_INT64STRLEN + 1];

	sprintf(inumber, "%" GFARM_PRId64, arg->inum);
	paramValues[0] = inumber;
	paramValues[1] = arg->hostname;
	e = (*op)(
		sql,
		2, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0, /* ask for text results */
		diag);

	free(arg);
	return e;
}

static gfarm_error_t
gfarm_pgsql_filecopy_add(struct db_filecopy_arg *arg)
{
	return pgsql_filecopy_call(arg,
		"INSERT INTO FileCopy (inumber, hostname) VALUES ($1, $2)",
		gfarm_pgsql_insert_with_retry,
		"pgsql_filecopy_add");
}

static gfarm_error_t
gfarm_pgsql_filecopy_remove(struct db_filecopy_arg *arg)
{
	return pgsql_filecopy_call(arg,
		"DELETE FROM FileCopy WHERE inumber = $1 AND hostname = $2",
		gfarm_pgsql_update_or_delete_with_retry,
		"pgsql_filecopy_remove");
}

static void
filecopy_set_fields_from_copy_binary(
	const char *buf, int residual, void *vinfo)
{
	struct db_filecopy_arg *info = vinfo;
	uint16_t num_fields;

	COPY_BINARY(num_fields, buf, residual,
	    "pgsql_filecopy_load: field number");
	num_fields = ntohs(num_fields);
	if (num_fields < 2) /* allow fields addition in future */
		gflog_fatal(GFARM_MSG_1000446,
		    "pgsql_filecopy_load: fields = %d", num_fields);

	info->inum = get_value_from_int8_copy_binary(&buf, &residual);
	info->hostname = get_value_from_varchar_copy_binary(&buf, &residual);
}

static gfarm_error_t
gfarm_pgsql_filecopy_load(
	void *closure,
	void (*callback)(void *, gfarm_ino_t, char *))
{
	struct db_filecopy_arg tmp_info;
	struct db_filecopy_trampoline_closure c;

	c.closure = closure;
	c.callback = callback;

	return (gfarm_pgsql_generic_load(
		"COPY FileCopy TO STDOUT BINARY",
		&tmp_info, db_filecopy_callback_trampoline, &c,
		&db_base_filecopy_arg_ops,
		filecopy_set_fields_from_copy_binary,
		"pgsql_filecopy_load"));
}

/**********************************************************************/

static gfarm_error_t
pgsql_deadfilecopy_call(struct db_deadfilecopy_arg *arg, const char *sql,
	gfarm_error_t (*op)(const char *, int, const Oid *,
		const char *const *, const int *, const int *, int,
		const char *),
	const char *diag)
{
	gfarm_error_t e;
	const char *paramValues[3];
	char inumber[GFARM_INT64STRLEN + 1];
	char igen[GFARM_INT64STRLEN + 1];

	sprintf(inumber, "%" GFARM_PRId64, arg->inum);
	paramValues[0] = inumber;
	sprintf(igen, "%" GFARM_PRId64, arg->igen);
	paramValues[1] = igen;
	paramValues[2] = arg->hostname;
	e = (*op)(
		sql,
		3, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0, /* ask for text results */
		diag);

	free(arg);
	return e;
}

static gfarm_error_t
gfarm_pgsql_deadfilecopy_add(struct db_deadfilecopy_arg *arg)
{
	return pgsql_deadfilecopy_call(arg,
		"INSERT INTO DeadFileCopy (inumber, igen, hostname) "
			"VALUES ($1, $2, $3)",
		gfarm_pgsql_insert_with_retry,
		"pgsql_deadfilecopy_add");
}

static gfarm_error_t
gfarm_pgsql_deadfilecopy_remove(struct db_deadfilecopy_arg *arg)
{
	return pgsql_deadfilecopy_call(arg,
		"DELETE FROM DeadFileCopy "
			"WHERE inumber = $1 AND igen = $2 AND hostname = $3",
		gfarm_pgsql_update_or_delete_with_retry,
		"pgsql_deadfilecopy_remove");
}

static void
deadfilecopy_set_fields_from_copy_binary(
	const char *buf, int residual, void *vinfo)
{
	struct db_deadfilecopy_arg *info = vinfo;
	uint16_t num_fields;

	COPY_BINARY(num_fields, buf, residual,
	    "pgsql_deadfilecopy_load: field number");
	num_fields = ntohs(num_fields);
	if (num_fields < 3) /* allow fields addition in future */
		gflog_fatal(GFARM_MSG_1000447,
		    "pgsql_deadfilecopy_load: fields = %d",
		    num_fields);

	info->inum = get_value_from_int8_copy_binary(&buf, &residual);
	info->igen = get_value_from_int8_copy_binary(&buf, &residual);
	info->hostname = get_value_from_varchar_copy_binary(&buf, &residual);
}

static gfarm_error_t
gfarm_pgsql_deadfilecopy_load(
	void *closure,
	void (*callback)(void *, gfarm_ino_t, gfarm_uint64_t, char *))
{
	struct db_deadfilecopy_arg tmp_info;
	struct db_deadfilecopy_trampoline_closure c;

	c.closure = closure;
	c.callback = callback;

	return (gfarm_pgsql_generic_load(
		"COPY DeadFileCopy TO STDOUT BINARY",
		&tmp_info,
		db_deadfilecopy_callback_trampoline, &c,
		&db_base_deadfilecopy_arg_ops,
		deadfilecopy_set_fields_from_copy_binary,
		"pgsql_deadfilecopy_load"));
}

/**********************************************************************/

static gfarm_error_t
gfarm_pgsql_direntry_add(struct db_direntry_arg *arg)
{
	gfarm_error_t e;
	const char *paramValues[3];
	char dir_inumber[GFARM_INT64STRLEN + 1];
	char entry_inumber[GFARM_INT64STRLEN + 1];

	sprintf(dir_inumber, "%" GFARM_PRId64, arg->dir_inum);
	paramValues[0] = dir_inumber;
	paramValues[1] = arg->entry_name;
	sprintf(entry_inumber, "%" GFARM_PRId64, arg->entry_inum);
	paramValues[2] = entry_inumber;
	e = gfarm_pgsql_insert_with_retry(
		"INSERT INTO DirEntry (dirINumber, entryName, entryINumber) "
		"VALUES ($1, $2, $3)",
		3, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0, /* ask for text results */
		"direntry_add");

	free(arg);
	return e;
}

static gfarm_error_t
gfarm_pgsql_direntry_remove(struct db_direntry_arg *arg)
{
	gfarm_error_t e;
	const char *paramValues[2];
	char dir_inumber[GFARM_INT64STRLEN + 1];

	sprintf(dir_inumber, "%" GFARM_PRId64, arg->dir_inum);
	paramValues[0] = dir_inumber;
	paramValues[1] = arg->entry_name;
	e = gfarm_pgsql_update_or_delete_with_retry(
		"DELETE FROM DirEntry "
			"WHERE dirINumber = $1 AND entryName = $2",
		2, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0, /* ask for text results */
		"direntry_remove");

	free(arg);
	return e;
}

static void
direntry_set_fields_from_copy_binary(
	const char *buf, int residual, void *vinfo)
{
	struct db_direntry_arg *info = vinfo;
	uint16_t num_fields;

	COPY_BINARY(num_fields, buf, residual,
	    "pgsql_direntry_load: field number");
	num_fields = ntohs(num_fields);
	if (num_fields < 3) /* allow fields addition in future */
		gflog_fatal(GFARM_MSG_1000448,
		    "pgsql_direntry_load: fields = %d", num_fields);

	info->dir_inum = get_value_from_int8_copy_binary(&buf, &residual);
	info->entry_name = get_value_from_varchar_copy_binary(&buf, &residual);
	info->entry_len = strlen(info->entry_name);
	info->entry_inum = get_value_from_int8_copy_binary(&buf, &residual);
}

static gfarm_error_t
gfarm_pgsql_direntry_load(
	void *closure,
	void (*callback)(void *, gfarm_ino_t, char *, int, gfarm_ino_t))
{
	struct db_direntry_arg tmp_info;
	struct db_direntry_trampoline_closure c;

	c.callback = callback;
	c.closure = closure;

	return (gfarm_pgsql_generic_load(
		"COPY DirEntry TO STDOUT BINARY",
		&tmp_info, db_direntry_callback_trampoline, &c,
		&db_base_direntry_arg_ops,
		direntry_set_fields_from_copy_binary,
		"pgsql_direntry_load"));
}

/**********************************************************************/

static gfarm_error_t
pgsql_symlink_call(struct db_symlink_arg *arg, const char *sql,
	gfarm_error_t (*op)(const char *, int, const Oid *,
		const char *const *, const int *, const int *, int,
		const char *),
	const char *diag)
{
	gfarm_error_t e;
	const char *paramValues[2];
	char inumber[GFARM_INT64STRLEN + 1];

	sprintf(inumber, "%" GFARM_PRId64, arg->inum);
	paramValues[0] = inumber;
	paramValues[1] = arg->source_path;
	e = (*op)(
		sql,
		2, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0, /* ask for text results */
		diag);

	free(arg);
	return e;
}

static gfarm_error_t
gfarm_pgsql_symlink_add(struct db_symlink_arg *arg)
{
	return pgsql_symlink_call(arg,
		"INSERT INTO Symlink (inumber, sourcePath) VALUES ($1, $2)",
		gfarm_pgsql_insert_with_retry,
		"pgsql_symlink_add");
}

static gfarm_error_t
gfarm_pgsql_symlink_remove(struct db_inode_inum_arg *arg)
{
	return pgsql_inode_inum_call(arg,
		"DELETE FROM Symlink WHERE inumber = $1",
		gfarm_pgsql_update_or_delete_with_retry,
		"pgsql_symlink_remove");
}

static void
symlink_set_fields_from_copy_binary(
	const char *buf, int residual, void *vinfo)
{
	struct db_symlink_arg *info = vinfo;
	uint16_t num_fields;

	COPY_BINARY(num_fields, buf, residual,
	    "pgsql_symlink_load: field number");
	num_fields = ntohs(num_fields);
	if (num_fields < 2) /* allow fields addition in future */
		gflog_fatal(GFARM_MSG_1000449,
		    "pgsql_symlink_load: fields = %d", num_fields);

	info->inum = get_value_from_int8_copy_binary(&buf, &residual);
	info->source_path = get_value_from_varchar_copy_binary(&buf, &residual);
}

static gfarm_error_t
gfarm_pgsql_symlink_load(
	void *closure,
	void (*callback)(void *, gfarm_ino_t, char *))
{
	struct db_symlink_arg tmp_info;
	struct db_symlink_trampoline_closure c;

	c.closure = closure;
	c.callback = callback;

	return (gfarm_pgsql_generic_load(
		"COPY Symlink TO STDOUT BINARY",
		&tmp_info, db_symlink_callback_trampoline, &c,
		&db_base_symlink_arg_ops,
		symlink_set_fields_from_copy_binary,
		"pgsql_symlink_load"));
}
/**********************************************************************/

static gfarm_error_t
gfarm_pgsql_xattr_add(struct db_xattr_arg *arg)
{
	const char *paramValues[3];
	int paramLengths[3];
	int paramFormats[3];
	gfarm_error_t e;
	char inumber[GFARM_INT64STRLEN + 1];
	char *command;
	char *diag;

	sprintf(inumber, "%" GFARM_PRId64, arg->inum);
	paramValues[0] = inumber;
	paramValues[1] = arg->attrname;
	paramValues[2] = arg->value;
	paramLengths[0] = strlen(paramValues[0]);
	paramLengths[1] = strlen(paramValues[1]);
	paramLengths[2] = arg->size;
	paramFormats[0] = 0;
	paramFormats[1] = 0;

	if (arg->xmlMode) {
		command = "INSERT INTO XmlAttr (inumber, attrname, attrvalue) "
			"VALUES ($1, $2, $3)";
		diag = "pgsql_xmlattr_set";
		paramFormats[2] = 0; // as text
	} else {
		command = "INSERT INTO XAttr (inumber, attrname, attrvalue) "
			"VALUES ($1, $2, $3)";
		diag = "pgsql_xattr_set";
		paramFormats[2] = 1; // as binary
	}
	e = gfarm_pgsql_insert_and_log(
		command,
		3, /* number of params */
		NULL, /* param types */
		paramValues,
		paramLengths,
		paramFormats,
		0, /* ask for text results */
		diag);

	free(arg);
	return e;
}

static gfarm_error_t
gfarm_pgsql_xattr_modify(struct db_xattr_arg *arg)
{
	const char *paramValues[3];
	int paramLengths[3];
	int paramFormats[3];
	gfarm_error_t e;
	char inumber[GFARM_INT64STRLEN + 1];
	char *command;
	char *diag;

	sprintf(inumber, "%" GFARM_PRId64, arg->inum);
	paramValues[0] = inumber;
	paramValues[1] = arg->attrname;
	paramValues[2] = arg->value;
	paramLengths[0] = strlen(paramValues[0]);
	paramLengths[1] = strlen(paramValues[1]);
	paramLengths[2] = arg->size;
	paramFormats[0] = 0;
	paramFormats[1] = 0;

	if (arg->xmlMode) {
		command = "UPDATE XmlAttr SET attrvalue = $3 "
			"WHERE inumber = $1 and attrname=$2";
		diag = "pgsql_xmlattr_modify";
		paramFormats[2] = 0; // as text
	} else {
		command = "UPDATE XAttr SET attrvalue = $3 "
			"WHERE inumber = $1 and attrname=$2";
		diag = "pgsql_xattr_modify";
		paramFormats[2] = 1; // as binary
	}
	e = gfarm_pgsql_update_or_delete_with_retry(
		command,
		3, /* number of params */
		NULL, /* param types */
		paramValues,
		paramLengths,
		paramFormats,
		0, /* ask for text results */
		diag);

	free(arg);
	return e;
}

static gfarm_error_t
gfarm_pgsql_xattr_remove(struct db_xattr_arg *arg)
{
	gfarm_error_t e;
	const char *paramValues[2];
	int paramNum;
	char inumber[GFARM_INT64STRLEN + 1];
	char *diag;
	char *command;

	sprintf(inumber, "%" GFARM_PRId64, arg->inum);
	paramValues[0] = inumber;
	paramValues[1] = arg->attrname;
	paramNum = 2;
	if (arg->xmlMode) {
		command = "DELETE FROM XmlAttr "
			"WHERE inumber = $1 AND attrname = $2";
		diag = "pgsql_xmlattr_remove";
	} else {
		command = "DELETE FROM XAttr "
			"WHERE inumber = $1 AND attrname = $2";
		diag = "pgsql_xattr_remove";
	}
	e = gfarm_pgsql_update_or_delete_with_retry(
		command,
		paramNum,
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0, /* ask for text results */
		diag);

	free(arg);
	return e;
}

static gfarm_error_t
gfarm_pgsql_xattr_removeall(struct db_xattr_arg *arg)
{
	gfarm_error_t e;
	const char *paramValues[1];
	char inumber[GFARM_INT64STRLEN + 1];
	char *diag;
	char *command;

	sprintf(inumber, "%" GFARM_PRId64, arg->inum);
	paramValues[0] = inumber;
	if (arg->xmlMode) {
		command = "DELETE FROM XmlAttr "
			"WHERE inumber = $1";
		diag = "pgsql_xmlattr_removeall";
	} else {
		command = "DELETE FROM XAttr "
			"WHERE inumber = $1";
		diag = "pgsql_xattr_removeall";
	}
	e = gfarm_pgsql_update_or_delete_with_retry(
		command,
		1,
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0, /* ask for text results */
		diag);

	free(arg);
	return e;
}

static gfarm_error_t
pgsql_xattr_set_attrvalue_string(PGresult *res, int row, void *vinfo)
{
	struct xattr_info *info = vinfo;

	info->attrvalue = pgsql_get_string(res, row, "attrvalue");
	if (info->attrvalue != NULL) {
		// include '\0' in attrsize
		info->attrsize = strlen(info->attrvalue) + 1;
		return (GFARM_ERR_NO_ERROR);
	} else {
		info->attrsize = 0;
		return (GFARM_ERR_NO_MEMORY);
	}
}

static gfarm_error_t
pgsql_xattr_set_attrvalue_binary(PGresult *res, int row, void *vinfo)
{
	struct xattr_info *info = vinfo;

	info->attrvalue = pgsql_get_binary(res, row,
			"attrvalue", &info->attrsize);
	// NOTE: we allow attrsize==0, attrvalue==NULL
	if ((info->attrsize > 0) && (info->attrvalue == NULL))
		return (GFARM_ERR_NO_MEMORY);
	else
		return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_pgsql_xattr_get(struct db_xattr_arg *arg)
{
	gfarm_error_t e;
	const char *paramValues[2];
	char inumber[GFARM_INT64STRLEN + 1];
	char *diag;
	char *command;
	int n;
	struct xattr_info *vinfo;
	gfarm_error_t (*set_fields)(PGresult *, int, void *);

	sprintf(inumber, "%" GFARM_PRId64, arg->inum);
	paramValues[0] = inumber;
	paramValues[1] = arg->attrname;

	if (arg->xmlMode) {
		command = "SELECT attrvalue FROM XmlAttr "
			"WHERE inumber = $1 AND attrname = $2";
		diag = "pgsql_xmlattr_load";
		set_fields = pgsql_xattr_set_attrvalue_string;
	} else {
		command = "SELECT attrvalue FROM XAttr "
			"WHERE inumber = $1 AND attrname = $2";
		diag = "pgsql_xattr_load";
		set_fields = pgsql_xattr_set_attrvalue_binary;
	}

	e = gfarm_pgsql_generic_get_all(
		command,
		2, paramValues,
		&n, &vinfo,
		&gfarm_base_xattr_info_ops, set_fields,
		diag);
	if (e == GFARM_ERR_NO_ERROR) {
		*arg->sizep = vinfo->attrsize;
		*arg->valuep = vinfo->attrvalue;
		free(vinfo);
	}

	free(arg);
	return e;
}

static gfarm_error_t
pgsql_xattr_set_inum_and_attrname(PGresult *res, int row, void *vinfo)
{
	struct xattr_info *info = vinfo;

	info->inum = pgsql_get_int64(res, row, "inumber");
	info->attrname = pgsql_get_string(res, row, "attrname");
	info->namelen = strlen(info->attrname) + 1; // include '\0'
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_pgsql_xattr_load(void *closure,
		void (*callback)(void *, struct xattr_info *))
{
	gfarm_error_t e;
	int xmlMode = (closure != NULL) ? *(int*)closure : 0;
	char *command, *diag;
	struct xattr_info *vinfo;
	int i, n;

	if (xmlMode) {
		command = "SELECT inumber,attrname FROM XmlAttr";
		diag = "pgsql_xattr_load_xml";
	} else {
		command = "SELECT inumber,attrname FROM XAttr";
		diag = "pgsql_xattr_load_norm";
	}

	e = gfarm_pgsql_generic_get_all(
		command,
		0, NULL,
		&n, &vinfo,
		&gfarm_base_xattr_info_ops, pgsql_xattr_set_inum_and_attrname,
		diag);
	if (e == GFARM_ERR_NO_SUCH_OBJECT)
		return GFARM_ERR_NO_ERROR;
	if (e != GFARM_ERR_NO_ERROR)
		return e;

	for (i = 0; i < n; i++) {
		(*callback)(&xmlMode, &vinfo[i]);
	}
	gfarm_base_generic_info_free_all(n, vinfo,
			&gfarm_base_xattr_info_ops);
	return e;
}

static gfarm_error_t
pgsql_xattr_set_attrname(PGresult *res, int row, void *vinfo)
{
	struct xattr_info *info = vinfo;

	info->attrname = pgsql_get_string(res, row, "attrname");
	info->namelen = strlen(info->attrname) + 1; // include '\0'
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_pgsql_xmlattr_find(struct db_xmlattr_find_arg *arg)
{
	gfarm_error_t e;
	const char *paramValues[2];
	char inumber[GFARM_INT64STRLEN + 1];
	char *diag;
	char *command;
	int n = 0;
	struct xattr_info *vinfo = NULL;

	sprintf(inumber, "%" GFARM_PRId64, arg->inum);
	paramValues[0] = inumber;
	paramValues[1] = arg->expr;

	/*
	 * xpath($2, attrvalue) returns xml[].
	 * Array size > 0 if some attrvalue matched XPath expr, 0 if not.
	 */
	command = "SELECT attrname FROM XmlAttr "
		"WHERE inumber = $1 AND array_upper(xpath($2,attrvalue),1) > 0 "
		"ORDER BY attrname";
	diag = "pgsql_xmlattr_find";

	e = gfarm_pgsql_generic_get_all(
		command,
		2, paramValues,
		&n, &vinfo,
		&gfarm_base_xattr_info_ops, pgsql_xattr_set_attrname,
		diag);

	if (e == GFARM_ERR_NO_ERROR)
		e = (*(arg->foundcallback))(arg->foundcbdata, n, vinfo);
	else if (e == GFARM_ERR_NO_SUCH_OBJECT)
		e = GFARM_ERR_NO_ERROR;
	if (n > 0)
		gfarm_base_generic_info_free_all(n, vinfo,
			&gfarm_base_xattr_info_ops);
	free(arg);
	return e;
}

/**********************************************************************/

/* quota */
static gfarm_error_t
pgsql_quota_call(struct db_quota_arg *info, const char *sql,
	gfarm_error_t (*op)(const char *, int, const Oid *,
		const char *const *, const int *, const int *, int,
		const char *),
	const char *diag)
{
	gfarm_error_t e;
	const char *paramValues[18];
	struct quota *q = &info->quota;

	char grace_period[GFARM_INT64STRLEN + 1];
	char space[GFARM_INT64STRLEN + 1];
	char space_exceed[GFARM_INT64STRLEN + 1];
	char space_soft[GFARM_INT64STRLEN + 1];
	char space_hard[GFARM_INT64STRLEN + 1];
	char num[GFARM_INT64STRLEN + 1];
	char num_exceed[GFARM_INT64STRLEN + 1];
	char num_soft[GFARM_INT64STRLEN + 1];
	char num_hard[GFARM_INT64STRLEN + 1];
	char phy_space[GFARM_INT64STRLEN + 1];
	char phy_space_exceed[GFARM_INT64STRLEN + 1];
	char phy_space_soft[GFARM_INT64STRLEN + 1];
	char phy_space_hard[GFARM_INT64STRLEN + 1];
	char phy_num[GFARM_INT64STRLEN + 1];
	char phy_num_exceed[GFARM_INT64STRLEN + 1];
	char phy_num_soft[GFARM_INT64STRLEN + 1];
	char phy_num_hard[GFARM_INT64STRLEN + 1];

	sprintf(grace_period, "%" GFARM_PRId64, q->grace_period);
	sprintf(space, "%" GFARM_PRId64, q->space);
	sprintf(space_exceed, "%" GFARM_PRId64, q->space_exceed);
	sprintf(space_soft, "%" GFARM_PRId64, q->space_soft);
	sprintf(space_hard, "%" GFARM_PRId64, q->space_hard);
	sprintf(num, "%" GFARM_PRId64, q->num);
	sprintf(num_exceed, "%" GFARM_PRId64, q->num_exceed);
	sprintf(num_soft, "%" GFARM_PRId64, q->num_soft);
	sprintf(num_hard, "%" GFARM_PRId64, q->num_hard);
	sprintf(phy_space, "%" GFARM_PRId64, q->phy_space);
	sprintf(phy_space_exceed, "%" GFARM_PRId64, q->phy_space_exceed);
	sprintf(phy_space_soft, "%" GFARM_PRId64, q->phy_space_soft);
	sprintf(phy_space_hard, "%" GFARM_PRId64, q->phy_space_hard);
	sprintf(phy_num, "%" GFARM_PRId64, q->phy_num);
	sprintf(phy_num_exceed, "%" GFARM_PRId64, q->phy_num_exceed);
	sprintf(phy_num_soft, "%" GFARM_PRId64, q->phy_num_soft);
	sprintf(phy_num_hard, "%" GFARM_PRId64, q->phy_num_hard);

	paramValues[0] = info->name;
	paramValues[1] = grace_period;
	paramValues[2] = space;
	paramValues[3] = space_exceed;
	paramValues[4] = space_soft;
	paramValues[5] = space_hard;
	paramValues[6] = num;
	paramValues[7] = num_exceed;
	paramValues[8] = num_soft;
	paramValues[9] = num_hard;
	paramValues[10] = phy_space;
	paramValues[11] = phy_space_exceed;
	paramValues[12] = phy_space_soft;
	paramValues[13] = phy_space_hard;
	paramValues[14] = phy_num;
	paramValues[15] = phy_num_exceed;
	paramValues[16] = phy_num_soft;
	paramValues[17] = phy_num_hard;

	e = (*op)(
		sql,
		18, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0, /* ask for text results */
		diag);

	free(info);
	return (e);
}

static gfarm_error_t
gfarm_pgsql_quota_add(struct db_quota_arg *arg)
{
	static char *sql_user =
		"INSERT INTO QuotaUser"
		" (username, gracePeriod, "
		"fileSpace, fileSpaceExceed, fileSpaceSoft, fileSpaceHard, "
		"fileNum, fileNumExceed, fileNumSoft, fileNumHard, "
		"phySpace, phySpaceExceed, phySpaceSoft, phySpaceHard, "
		"phyNum, phyNumExceed, phyNumSoft, phyNumHard) "
		"VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9 ,$10, "
		"$11, $12, $13, $14, $15, $16, $17, $18)";
	static char *sql_group =
		"INSERT INTO QuotaGroup"
		" (groupname, gracePeriod, "
		"fileSpace, fileSpaceExceed, fileSpaceSoft, fileSpaceHard, "
		"fileNum, fileNumExceed, fileNumSoft, fileNumHard, "
		"phySpace, phySpaceExceed, phySpaceSoft, phySpaceHard, "
		"phyNum, phyNumExceed, phyNumSoft, phyNumHard) "
		"VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9 ,$10, "
		"$11, $12, $13, $14, $15, $16, $17, $18)";
	char *sql = arg->is_group ? sql_group : sql_user;

	return (pgsql_quota_call(arg, sql,
				 gfarm_pgsql_insert_with_retry,
				 "pgsql_quota_add"));
}

static gfarm_error_t
gfarm_pgsql_quota_modify(struct db_quota_arg *arg)
{
	static char *sql_user =
		"UPDATE QuotaUser SET gracePeriod = $2, "
		"fileSpace = $3, fileSpaceExceed = $4, "
		"fileSpaceSoft = $5, fileSpaceHard = $6, "
		"fileNum = $7, fileNumExceed = $8, "
		"fileNumSoft = $9, fileNumHard = $10, "
		"phySpace = $11, phySpaceExceed = $12, "
		"phySpaceSoft = $13, phySpaceHard = $14, "
		"phyNum = $15, phyNumExceed = $16, "
		"phyNumSoft = $17, phyNumHard = $18 WHERE username = $1";
	static char *sql_group =
		"UPDATE QuotaGroup SET gracePeriod = $2, "
		"fileSpace = $3, fileSpaceExceed = $4, "
		"fileSpaceSoft = $5, fileSpaceHard = $6, "
		"fileNum = $7, fileNumExceed = $8, "
		"fileNumSoft = $9, fileNumHard = $10, "
		"phySpace = $11, phySpaceExceed = $12, "
		"phySpaceSoft = $13, phySpaceHard = $14, "
		"phyNum = $15, phyNumExceed = $16, "
		"phyNumSoft = $17, phyNumHard = $18 WHERE groupname = $1";
	char *sql = arg->is_group ? sql_group : sql_user;

	return (pgsql_quota_call(arg, sql,
				 gfarm_pgsql_update_or_delete_with_retry,
				 "pgsql_quota_modify"));
}

static gfarm_error_t
gfarm_pgsql_quota_remove(struct db_quota_remove_arg *arg)
{
	gfarm_error_t e;
	const char *paramValues[1];
	static char *sql_user = "DELETE FROM QuotaUser WHERE username = $1";
	static char *sql_group = "DELETE FROM QuotaGroup WHERE groupname = $1";
	char *sql = arg->is_group ? sql_group : sql_user;

	paramValues[0] = arg->name;
	e = gfarm_pgsql_update_or_delete_with_retry(
		sql,
		1, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0, /* ask for text results */
		"pgsql_quota_remove");

	free(arg);
	return (e);
}

static void
quota_info_set_fields_from_copy_binary(
	const char *buf, int residual, void *vinfo)
{
	struct gfarm_quota_info *info = vinfo;
	uint16_t num_fields;

	COPY_BINARY(num_fields, buf, residual,
	    "pgsql_quota_load: field number");
	num_fields = ntohs(num_fields);
	if (num_fields < 18) /* allow fields addition in future */
		gflog_fatal(GFARM_MSG_1000450,
			    "pgsql_quota_load: fields = %d", num_fields);

	info->name = get_value_from_varchar_copy_binary(&buf, &residual);
	info->grace_period = get_value_from_int8_copy_binary(&buf, &residual);
	info->space = get_value_from_int8_copy_binary(&buf, &residual);
	info->space_exceed = get_value_from_int8_copy_binary(&buf, &residual);
	info->space_soft = get_value_from_int8_copy_binary(&buf, &residual);
	info->space_hard = get_value_from_int8_copy_binary(&buf, &residual);
	info->num = get_value_from_int8_copy_binary(&buf, &residual);
	info->num_exceed = get_value_from_int8_copy_binary(&buf, &residual);
	info->num_soft = get_value_from_int8_copy_binary(&buf, &residual);
	info->num_hard = get_value_from_int8_copy_binary(&buf, &residual);
	info->phy_space = get_value_from_int8_copy_binary(&buf, &residual);
	info->phy_space_exceed = get_value_from_int8_copy_binary(
		&buf, &residual);
	info->phy_space_soft = get_value_from_int8_copy_binary(
		&buf, &residual);
	info->phy_space_hard = get_value_from_int8_copy_binary(
		&buf, &residual);
	info->phy_num = get_value_from_int8_copy_binary(&buf, &residual);
	info->phy_num_exceed = get_value_from_int8_copy_binary(
		&buf, &residual);
	info->phy_num_soft = get_value_from_int8_copy_binary(&buf, &residual);
	info->phy_num_hard = get_value_from_int8_copy_binary(&buf, &residual);
}

static gfarm_error_t
gfarm_pgsql_quota_load(void *closure, int is_group,
	void (*callback)(void *, struct gfarm_quota_info *))
{
	struct gfarm_quota_info tmp_info;

	const char *command = (is_group ?
			       "COPY QuotaGroup TO STDOUT BINARY" :
			       "COPY QuotaUser TO STDOUT BINARY");

	return (gfarm_pgsql_generic_load(
		command,
		&tmp_info, (void (*)(void *, void *))callback, closure,
		&gfarm_base_quota_info_ops,
		quota_info_set_fields_from_copy_binary,
		"pgsql_quota_load"));
}

/* DO NOT REMOVE: this interfaces is provided for a private extension */
PGconn *
gfarm_pgsql_get_conn(void)
{
	return (conn);
}

/**********************************************************************/

const struct db_ops db_pgsql_ops = {
	gfarm_pgsql_initialize,
	gfarm_pgsql_terminate,

	gfarm_pgsql_begin,
	gfarm_pgsql_end,

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

	gfarm_pgsql_symlink_add,
	gfarm_pgsql_symlink_remove,
	gfarm_pgsql_symlink_load,

	gfarm_pgsql_xattr_add,
	gfarm_pgsql_xattr_modify,
	gfarm_pgsql_xattr_remove,
	gfarm_pgsql_xattr_removeall,
	gfarm_pgsql_xattr_get,
	gfarm_pgsql_xattr_load,
	gfarm_pgsql_xmlattr_find,

	gfarm_pgsql_quota_add,
	gfarm_pgsql_quota_modify,
	gfarm_pgsql_quota_remove,
	gfarm_pgsql_quota_load,
};
