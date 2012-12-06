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

#include <pthread.h>	/* db_access.h currently needs this */
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>

#include <libpq-fe.h>

#include <gfarm/gfarm.h>

#include "internal_host_info.h"

#include "gfutil.h"

#include "config.h"
#include "metadb_common.h"
#include "xattr_info.h"
#include "quota_info.h"
#include "metadb_server.h"
#include "quota.h"

#include "db_common.h"
#include "db_access.h"
#include "db_ops.h"


/* ERROR:  duplicate key violates unique constraint */
#define GFARM_PGSQL_ERRCODE_UNIQUE_VIOLATION	"23505"

/* ERROR:  invalid XML content */
#define GFARM_PGSQL_ERRCODE_INVALID_XML_CONTENT	"2200N"

/*
 * 57P01: FATAL:  terminating connection due to administrator command
 * 57P02: CRASH SHUTDOWN
 * 57P03: CANNOT CONNECT
 */
#define GFARM_PGSQL_ERRCODE_SHUTDOWN_PREFIX	"57P"

/*
 * 42***:	syntax error or access rule violation
 *	e.g.)
 *		42703:	undefined column
 */
#define GFARM_PGSQL_ERRCODE_SYNTAX_ERROR_PREFIX	"42"
#define GFARM_PGSQL_ERRCODE_UNDEFINED_COLUMN	"42703"

typedef enum {
	/*
	 * A value zero should represent "There is no clue that show
	 * us any error(s) have been occurred yet or not": since in
	 * many case we initialize any data by zero, and also the C
	 * language system and the runtime initialize any static data
	 * by zero implicitly.
	 */
	RDBMS_ERROR_UNKNOWN_STATE = 0,

	RDBMS_ERROR_NO_ERROR,

	/*
	 * SQL related:
	 */
	RDBMS_ERROR_INVALID_SCHEME,
	RDBMS_ERROR_SQL_SYNTAX_ERROR,

	/*
	 * Any others:
	 */
	RDBMS_ERROR_NO_MEMORY,

	RDBMS_ERROR_ANY_OTHERS,

	RDBMS_ERROR_MAX
} gfarm_rdbms_error_t;

typedef gfarm_error_t (*gfarm_pgsql_dml_sn_t)(gfarm_uint64_t,
	const char *, int, const Oid *, const char *const *,
	const int *, const int *, int, const char *);

typedef gfarm_error_t (*gfarm_pgsql_dml_t)(
	const char *, int, const Oid *, const char *const *,
	const int *, const int *, int, const char *);

static gfarm_error_t gfarm_pgsql_seqnum_modify(struct db_seqnum_arg *);

/**********************************************************************/

static void
free_arg(void *arg)
{
	/*
	 * - When metadb_replication_enabled() is false, we store objects to
	 *   PostgreSQL directly without journal file.
	 *   'arg' is allocated in db_*_dup() and freed here.
	 *
	 * - When metadb_replication_enabled() is true, we store objects to
	 *   the journal file via db_journal_ops before storing to PostgreSQL.
	 *   'arg' is allocated in db_*_dup() and freed in db_journal_enter().
	 *   Functions of db_pgsql_ops are called from
	 *   db_journal_store_thread().
	 *   In db_journal_store_thread(), each object to be passed to
	 *   functions of db_pgsql_ops is allocated in db_journal_read_ops().
	 *   db_journal_read_ops() allocates each object as multiple chunks
	 *   different from db_*_dup() functions which allocate each object
	 *   as single chunk.
	 *   The objects are possibly reused for retrying to call functions
	 *   of db_pgsql_ops and freed in db_journal_ops_free() called from
	 *   db_journal_free_rec_list().
	 *
	 */
	if (!gfarm_get_metadb_replication_enabled())
		free(arg);
}

static PGconn *conn = NULL;
static int transaction_nesting = 0;
static int transaction_ok;
static int connection_recovered = 0;

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
	if (conninfo == NULL) {
		gflog_debug(GFARM_MSG_1002144,
			"allocation of 'conninfo' failed");
		return (NULL);
	}

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
	if (conninfo == NULL) {
		gflog_debug(GFARM_MSG_1002145,
			"gfarm_pgsql_make_conninfo() failed");
		return (GFARM_ERR_NO_MEMORY);
	}

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
	char *v = PQgetvalue(res, row, PQfnumber(res, field_name));
	char *s = strdup(v);

	if (s == NULL) {
		gflog_error(GFARM_MSG_1002329,
		    "pgsql_get_string(%s): %s: no memory", field_name, v);
	}
	return (s);
}

char *
pgsql_get_string_ck(PGresult *res, int row, const char *field_name)
{
	char *v = PQgetvalue(res, row, PQfnumber(res, field_name));
	char *s = strdup(v);

	if (s == NULL) {
		gflog_fatal(GFARM_MSG_1002371,
		    "pgsql_get_string_ck(%s): %s: no memory", field_name, v);
	}
	return (s);
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
	else
		gflog_error(GFARM_MSG_1002330,
		    "pgsql_get_binary(%s): size=%d: no memory",
		    field_name, (int)*size);

	return (dst);
}

/**********************************************************************/

#define IS_POWER_OF_2(n)	(((n) & (n - 1)) == 0)
#define DAY_PER_SECOND		(24 * 3600)
#define RETRY_INTERVAL		10
#define MSG_THRESHOLD		4096

/*
 * Return Value:
 *	1: indicate that caller should retry.
 *	0: unrecoverable error, caller should end.
 * Note:
 *	If return value is 1, this function calls PQclear(),
 *	otherwise the caller of this function should call PQclear().
 */
static int
pgsql_should_retry(PGresult *res)
{
	int retry = 0;
	char *pge = PQresultErrorField(res, PG_DIAG_SQLSTATE);

	if (PQstatus(conn) == CONNECTION_BAD) {
		gflog_error(GFARM_MSG_1002331,
		    "PostgreSQL connection is down: %s: %s",
		    (pge != NULL) ? pge : "no SQL state",
		    PQresultErrorMessage(res));
		PQclear(res);
		for (;;) {
			++retry;
			if ((retry >= MSG_THRESHOLD &&
			     retry % (DAY_PER_SECOND/RETRY_INTERVAL) == 0) ||
			    IS_POWER_OF_2(retry)) {
				gflog_error(GFARM_MSG_1002332,
				    "PostgreSQL connection retrying");
			}
			PQreset(conn);
			if (PQstatus(conn) == CONNECTION_OK)
				break;
			sleep(RETRY_INTERVAL);
		}
		/* XXX FIXME: one transaction may be lost in this case */
		/*
		 * A connection to PostgreSQL is recovered here, but if
		 * metadata replication is disabled, upper layer module
		 * lacks of retry-transaction mechanism.  The upper layer
		 * module, in this case, retries to execute SQL commands,
		 * starting from the middle of the transaction.
		 *
		 * It may cause inconsistency of PostgreSQL database, but
		 * we execute SQL commands requested by the upper layer
		 * module anyway, since there is no way to keep consistency
		 * of the database in such a situation.
		 * 
		 * We enable 'connection_recovered' flag here.  While the
		 * flag is enabled, we check 'transaction_nesting' value
		 * lazily.  Once "START TRANSACTION", "COMMIT" or "ROLLBACK"
		 * SQL command is executed, the flag is disabled.
		 */
		gflog_info(GFARM_MSG_1002333,
		    "PostgreSQL connection recovered");
		transaction_nesting = 0;
		connection_recovered = 1;
		transaction_ok = 0;
		return (1);
	} else if (PQresultStatus(res) == PGRES_FATAL_ERROR &&
	    pge != NULL &&
	    strncmp(pge, GFARM_PGSQL_ERRCODE_SHUTDOWN_PREFIX,
	    strlen(GFARM_PGSQL_ERRCODE_SHUTDOWN_PREFIX)) == 0) {
		/* does this code path happen? */
		gflog_error(GFARM_MSG_1002334,
		    "PostgreSQL connection problem: %s: %s",
		    pge, PQresultErrorMessage(res));
		PQclear(res);
		return (1);
	}
	return (0); /* retry is not necessary */
}

static gfarm_error_t
gfarm_pgsql_check_misc(PGresult *res, const char *command, const char *diag)
{
	gfarm_error_t e;

	if (PQresultStatus(res) == PGRES_COMMAND_OK) {
		e = GFARM_ERR_NO_ERROR;
	} else if (pgsql_should_retry(res)) {
		e = GFARM_ERR_DB_ACCESS_SHOULD_BE_RETRIED;
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
	} else if (pgsql_should_retry(res)) {
		e = GFARM_ERR_DB_ACCESS_SHOULD_BE_RETRIED;
	} else {
		e = GFARM_ERR_UNKNOWN;
		gflog_error(GFARM_MSG_1000424, "%s: %s: %s", diag, command,
		    PQresultErrorMessage(res));
	}
	return (e);
}

static gfarm_error_t
gfarm_pgsql_check_insert(PGresult *res,
	const char *command, const char *diag)
{
	gfarm_error_t e;

	if (PQresultStatus(res) == PGRES_COMMAND_OK) {
		e = GFARM_ERR_NO_ERROR;
	} else if (pgsql_should_retry(res)) {
		e = GFARM_ERR_DB_ACCESS_SHOULD_BE_RETRIED;
	} else {
		char *err = PQresultErrorField(res, PG_DIAG_SQLSTATE);

		if (err == NULL)
			e = GFARM_ERR_UNKNOWN;
		else if (strcmp(err, GFARM_PGSQL_ERRCODE_UNIQUE_VIOLATION) == 0)
			e = GFARM_ERR_ALREADY_EXISTS;
		else if (strcmp(err, GFARM_PGSQL_ERRCODE_INVALID_XML_CONTENT)
		    == 0)
			e = GFARM_ERR_INVALID_ARGUMENT;
		else
			e = GFARM_ERR_UNKNOWN;

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
		if (pgsql_should_retry(res))
			return (GFARM_ERR_DB_ACCESS_SHOULD_BE_RETRIED);
		else if ((pge = PQresultErrorField(res, PG_DIAG_SQLSTATE))
		    != NULL &&
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

	if (e != GFARM_ERR_DB_ACCESS_SHOULD_BE_RETRIED)
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

	if (e != GFARM_ERR_DB_ACCESS_SHOULD_BE_RETRIED)
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
	PGresult *res = PQexecParams(conn, command, nParams,
	    paramTypes, paramValues, paramLengths, paramFormats, resultFormat);
	gfarm_error_t e = gfarm_pgsql_check_insert(res, command, diag);

	if (e != GFARM_ERR_DB_ACCESS_SHOULD_BE_RETRIED)
		PQclear(res);
	return (e);
}

static PGresult *
gfarm_pgsql_exec_params(const char *command,
	int nParams,
	const Oid *paramTypes,
	const char *const *paramValues,
	const int *paramLengths,
	const int *paramFormats,
	int resultFormat)
{
	return (PQexecParams(conn, command, nParams,
	    paramTypes, paramValues, paramLengths, paramFormats,
	    resultFormat));
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

	do {
		res = PQexecParams(conn, command, nParams,
		    paramTypes, paramValues, paramLengths, paramFormats,
		    resultFormat);
	} while (PQresultStatus(res) != PGRES_COMMAND_OK &&
	    pgsql_should_retry(res));
	return (res);
}

static gfarm_error_t
gfarm_pgsql_start(const char *diag)
{
	PGresult *res;

	if (connection_recovered)
		connection_recovered = 0;
	if (transaction_nesting++ > 0)
		return (GFARM_ERR_NO_ERROR);

	transaction_ok = 1;
	res = PQexec(conn, "START TRANSACTION");

	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		gflog_error(GFARM_MSG_1003244, "%s transaction BEGIN: %s",
		    diag, PQresultErrorMessage(res));
		return (pgsql_should_retry(res) ?
			GFARM_ERR_DB_ACCESS_SHOULD_BE_RETRIED :
			GFARM_ERR_UNKNOWN);
	}

	PQclear(res);
	return (GFARM_ERR_NO_ERROR);
}

/* this interface is exported for a use from a private extension */
gfarm_error_t
gfarm_pgsql_start_with_retry(const char *diag)
{
	PGresult *res;

	if (connection_recovered)
		connection_recovered = 0;
	if (transaction_nesting++ > 0)
		return (GFARM_ERR_NO_ERROR);

	transaction_ok = 1;
	do {
		res = PQexec(conn, "START TRANSACTION");
	} while (PQresultStatus(res) != PGRES_COMMAND_OK &&
	    pgsql_should_retry(res));
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		gflog_error(GFARM_MSG_1000428, "%s transaction BEGIN: %s",
		    diag, PQresultErrorMessage(res));
		PQclear(res);
		return (GFARM_ERR_UNKNOWN);
	}
	PQclear(res);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_pgsql_commit_sn(gfarm_uint64_t seqnum, const char *diag)
{
	if (connection_recovered)
		connection_recovered = 0;
	else if (--transaction_nesting > 0)
		return (GFARM_ERR_NO_ERROR);

	assert(transaction_nesting == 0);

	if (gfarm_get_metadb_replication_enabled() && seqnum > 0) {
		gfarm_error_t e;
		struct db_seqnum_arg a;

		a.name = "";
		a.value = seqnum;
		if ((e = gfarm_pgsql_seqnum_modify(&a))
		    != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1003245,
			    "gfarm_pgsql_seqnum_modify : %s",
			    gfarm_error_string(e));
			return (e);
		}
	}
	return (gfarm_pgsql_exec_and_log(transaction_ok ?
	    "COMMIT" : "ROLLBACK", diag));
}

/* this interface is exported for a use from a private extension */
gfarm_error_t
gfarm_pgsql_commit(const char *diag)
{
	return (gfarm_pgsql_commit_sn(0, diag));
}

/* this interface is exported for a use from a private extension */
static gfarm_error_t
gfarm_pgsql_rollback(const char *diag)
{
	transaction_ok = 0;

	return (gfarm_pgsql_commit_sn(0, diag));
}

static gfarm_error_t
gfarm_pgsql_insert0(gfarm_uint64_t seqnum,
	const char *command,
	int nParams,
	const Oid *paramTypes,
	const char *const *paramValues,
	const int *paramLengths,
	const int *paramFormats,
	int resultFormat,
	gfarm_error_t (*start_op)(const char *),
	PGresult *(*exec_params_op)(const char *, int, const Oid *,
	    const char *const *, const int *, const int *, int),
	const char *diag)
{
	PGresult *res;
	gfarm_error_t e;

	if (transaction_nesting == 0) {
		e = start_op(diag);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
		res = PQexecParams(conn, command, nParams, paramTypes,
		    paramValues, paramLengths, paramFormats, resultFormat);
		e = gfarm_pgsql_check_insert(res, command, diag);
		if (e == GFARM_ERR_DB_ACCESS_SHOULD_BE_RETRIED)
			return (e);
		if (e == GFARM_ERR_NO_ERROR)
			e = gfarm_pgsql_commit_sn(seqnum, diag);
		else
			gfarm_pgsql_rollback(diag);
	} else {
		res = exec_params_op(command, nParams, paramTypes,
		    paramValues, paramLengths, paramFormats, resultFormat);
		e = gfarm_pgsql_check_insert(res, command, diag);
		if (e == GFARM_ERR_DB_ACCESS_SHOULD_BE_RETRIED)
			return (e);
	}
	PQclear(res);
	return (e);
}

static gfarm_error_t
gfarm_pgsql_insert(gfarm_uint64_t seqnum,
	const char *command,
	int nParams,
	const Oid *paramTypes,
	const char *const *paramValues,
	const int *paramLengths,
	const int *paramFormats,
	int resultFormat,
	const char *diag)
{
	return (gfarm_pgsql_insert0(seqnum, command, nParams, paramTypes,
	    paramValues, paramLengths, paramFormats, resultFormat,
	    gfarm_pgsql_start, gfarm_pgsql_exec_params,
	    diag));
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
	return (gfarm_pgsql_insert0(0, command, nParams, paramTypes,
	    paramValues, paramLengths, paramFormats, resultFormat,
	    gfarm_pgsql_start_with_retry, gfarm_pgsql_exec_params_with_retry,
	    diag));
}

static gfarm_error_t
gfarm_pgsql_update_or_delete0(gfarm_uint64_t seqnum,
	const char *command,
	int nParams,
	const Oid *paramTypes,
	const char *const *paramValues,
	const int *paramLengths,
	const int *paramFormats,
	int resultFormat,
	gfarm_error_t (*start_op)(const char *),
	PGresult *(*exec_params_op)(const char *, int, const Oid *,
	    const char *const *, const int *, const int *, int),
	const char *diag)
{
	PGresult *res;
	gfarm_error_t e;

	if (transaction_nesting == 0) {
		e = start_op(diag);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
		res = PQexecParams(conn, command, nParams,
		    paramTypes, paramValues, paramLengths, paramFormats,
		    resultFormat);
		e = gfarm_pgsql_check_update_or_delete(res, command, diag);
		if (e == GFARM_ERR_DB_ACCESS_SHOULD_BE_RETRIED)
			return (e);
		if (e == GFARM_ERR_NO_ERROR)
			e = gfarm_pgsql_commit_sn(seqnum, diag);
		else
			gfarm_pgsql_rollback(diag);
	} else {
		res = exec_params_op(command, nParams,
		    paramTypes, paramValues, paramLengths, paramFormats,
		    resultFormat);
		e = gfarm_pgsql_check_update_or_delete(res, command, diag);
		if (e == GFARM_ERR_DB_ACCESS_SHOULD_BE_RETRIED)
			return (e);
	}

	PQclear(res);
	return (e);
}

static gfarm_error_t
gfarm_pgsql_update_or_delete(gfarm_uint64_t seqnum,
	const char *command,
	int nParams,
	const Oid *paramTypes,
	const char *const *paramValues,
	const int *paramLengths,
	const int *paramFormats,
	int resultFormat,
	const char *diag)
{
	return (gfarm_pgsql_update_or_delete0(seqnum, command, nParams,
	    paramTypes, paramValues, paramLengths, paramFormats, resultFormat,
	    gfarm_pgsql_start, gfarm_pgsql_exec_params, diag));
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
	return (gfarm_pgsql_update_or_delete0(0, command, nParams,
	    paramTypes, paramValues, paramLengths, paramFormats, resultFormat,
	    gfarm_pgsql_start_with_retry, gfarm_pgsql_exec_params_with_retry,
	    diag));
}

static gfarm_error_t
gfarm_pgsql_generic_get_all_no_retry(
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
	int n, i;
	char *results;

	res = PQexecParams(conn, sql,
		nparams,
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		1); /* ask for binary results */
	e = gfarm_pgsql_check_select(res, sql, diag);
	if (e == GFARM_ERR_DB_ACCESS_SHOULD_BE_RETRIED)
		return (e);
	if (e == GFARM_ERR_NO_ERROR) {
		n = PQntuples(res);
		if (n == 0) {
			gflog_debug(GFARM_MSG_1002147,
				"select results count : 0");
			e = GFARM_ERR_NO_SUCH_OBJECT;
		} else if ((results = malloc(ops->info_size * n)) == NULL) {
			gflog_debug(GFARM_MSG_1002148,
				"allocation of 'results' failed");
			e = GFARM_ERR_NO_MEMORY;
		} else {
			for (i = 0; i < n; i++) {
				(*ops->clear)(results + i * ops->info_size);
				e = (*set_fields)(res, i,
				    results + i * ops->info_size);
				if (e != GFARM_ERR_NO_ERROR) {
					gflog_debug(GFARM_MSG_1002372,
					    "gfarm_pgsql_generic_get_all"
					    "_no_retry: %s: %d/%d: %s",
					    sql, i, n, gfarm_error_string(e));
					while (--i >= 0) {
						(*ops->free)(results +
						    i * ops->info_size);
					}
					free(results);
					break;
				}
			}
			if (e == GFARM_ERR_NO_ERROR) {
				*np = n;
				*(char **)resultsp = results;
			}
		}
	}
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
	gfarm_error_t e;

	do {
		e = gfarm_pgsql_generic_get_all_no_retry(sql, nparams,
		    paramValues, np, resultsp, ops, set_fields, diag);
	} while (e == GFARM_ERR_DB_ACCESS_SHOULD_BE_RETRIED);
	return (e);
}

static gfarm_error_t
gfarm_pgsql_generic_grouping_get_all_no_retry(
	const char *count_sql,
	const char *results_sql,
	int nparams,
	const char **paramValues,
	int *np,
	void *resultsp,
	const struct gfarm_base_generic_info_ops *ops,
	gfarm_error_t (*set_fields_with_grouping)(PGresult *, int, int, void *),
	const char *diag)
{
	PGresult *cres, *rres;
	gfarm_error_t e;
	int ngroups;
	char *results;

	if ((e = gfarm_pgsql_start(diag)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002149, "pgsql restart failed");
		return (e);
	}
	cres = PQexecParams(conn,
		count_sql,
		nparams,
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		1); /* ask for binary results */
	e = gfarm_pgsql_check_select(cres, count_sql, diag);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002150,
			"pgsql select failed : count_sql");
		if (e == GFARM_ERR_DB_ACCESS_SHOULD_BE_RETRIED)
			return (e);
	} else if ((ngroups = PQntuples(cres)) == 0) {
		gflog_debug(GFARM_MSG_1002151,
			"pgsql select results count is 0 : count_sql");
		e = GFARM_ERR_NO_SUCH_OBJECT;
	} else if ((results = malloc(ops->info_size * ngroups)) == NULL) {
		gflog_debug(GFARM_MSG_1002152,
				"allocation of 'results' failed");
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
			gflog_debug(GFARM_MSG_1002153,
				"pgsql select failed :results_sql");
			free(results);
			if (e == GFARM_ERR_DB_ACCESS_SHOULD_BE_RETRIED)
				return (e);
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
					gflog_debug(GFARM_MSG_1002154,
						"set_fields_with_grouping "
						"failed");
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
		e = gfarm_pgsql_commit_sn(0, diag);
	else
		gfarm_pgsql_rollback(diag);

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
	gfarm_error_t (*set_fields_with_grouping)(PGresult *, int, int, void *),
	const char *diag)
{
	gfarm_error_t e;

	do {
		e = gfarm_pgsql_generic_grouping_get_all_no_retry(count_sql,
		    results_sql, nparams, paramValues, np, resultsp, ops,
		    set_fields_with_grouping, diag);
	} while (e == GFARM_ERR_DB_ACCESS_SHOULD_BE_RETRIED);
	return (e);
}

/**********************************************************************/

#define COPY_BINARY(data, buf, residual, msg) { \
	if (sizeof(data) > residual) \
		gflog_fatal(GFARM_MSG_1000429, \
		    "%s: %d bytes needed, but only %d bytes", \
		    msg, (int)sizeof(data), residual); \
	memcpy(&(data), buf, sizeof(data)); \
	buf += sizeof(data); \
	residual -= sizeof(data); \
}

#define COPY_INT32(int32, buf, residual, msg) { \
	assert(sizeof(int32) == sizeof(int32_t)); \
	COPY_BINARY(int32, buf, residual, msg); \
	int32 = ntohl(int32); \
}

/*
 * struct user::gsi_dn may be loaded by this function,
 * but currently "COPY User TO STDOUT BINARY" is NOT used for struct user.
 */
static char *
get_string_or_null_from_copy_binary(const char **bufp, int *residualp)
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
	if (p == NULL)
		gflog_fatal(GFARM_MSG_1002335,
		    "metadb_pgsql: copy varchar length=%d", len);
	memcpy(p, *bufp, len);
	p[len] = '\0';
	*bufp += len;
	*residualp -= len;
	return (p);
}

static char *
get_string_from_copy_binary(const char **bufp, int *residualp)
{
	char *p = get_string_or_null_from_copy_binary(bufp, residualp);

	if (p == NULL)
		gflog_fatal(GFARM_MSG_1002336,
		    "metadb_pgsql: copy varchar: got NULL");
	return (p);
}

static uint32_t
get_int32_from_copy_binary(const char **bufp, int *residualp)
{
	int32_t len;
	uint32_t val;

	COPY_INT32(len, *bufp, *residualp, "metadb_pgsql: copy int32 len");
	if (len == -1)
		gflog_fatal(GFARM_MSG_1002337,
		    "metadb_pgsql: copy int32: got NULL");
	if (len != sizeof(val))
		gflog_fatal(GFARM_MSG_1000432,
		    "metadb_pgsql: copy int32 length=%d", len);

	COPY_INT32(val, *bufp, *residualp, "metadb_pgsql: copy int32");
	return (val);
}

static uint64_t
get_int64_from_copy_binary(const char **bufp, int *residualp)
{
	int32_t len;
	uint64_t val;

	COPY_INT32(len, *bufp, *residualp, "metadb_pgsql: copy int64 len");
	if (len == -1)
		gflog_fatal(GFARM_MSG_1002338,
		    "metadb_pgsql: copy int64: got NULL");
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

	static const char binary_signature[COPY_BINARY_SIGNATURE_LEN] =
		"PGCOPY\n\377\r\n\0";

	do {
		res = PQexec(conn, command);
	} while (PQresultStatus(res) != PGRES_COPY_OUT &&
	    pgsql_should_retry(res));
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
gfarm_pgsql_begin(gfarm_uint64_t seqnum, void *arg)
{
	assert(transaction_nesting == 0);
	return (gfarm_pgsql_start("pgsql_begin"));
}

static gfarm_error_t
gfarm_pgsql_end(gfarm_uint64_t seqnum, void *arg)
{
	gfarm_error_t e = gfarm_pgsql_commit_sn(seqnum,
	    transaction_ok ? "gfarm_pgsql_end(OK)" : "gfarm_pgsql_end(NG)");

	assert(transaction_nesting == 0);
	return (e);
}

/**********************************************************************/

static char *
gen_scheme_check_query(const char *tablename,
	size_t ncolumns, const char * const columns[])
{
	int of = 0;
	size_t sz = gfarm_size_add(&of, strlen(tablename), 2 + 32 + 1);
		/* for "SELECT " + "FROM " + "LIMIT 1" + '\0' */
	int i;
	char *tmp = NULL;
	int offset = 0;

	assert((tablename != NULL && tablename[0] != '\0') &&
		(ncolumns > 0) && (of == 0));

	for (i = 0, of = 0; i < ncolumns && of == 0; i++) {
		/* two for ", " */
		sz = gfarm_size_add(&of, sz,
			gfarm_size_add(&of, strlen(columns[i]), 2));
	}
	if (of == 0 && sz > 0)
		tmp = (char *)malloc(sz);
	if (tmp == NULL) {
		gflog_error(GFARM_MSG_UNFIXED,
			"Can't allocate an SQL query string "
		"for table scheme check.");
		return (NULL);
	}

	offset += snprintf(tmp + offset, sz - offset, "SELECT ");
	for (i = 0; i < ncolumns; i++) {
		offset += snprintf(tmp + offset, sz - offset, "%s, ",
				columns[i]);
	}
	*(strrchr(tmp, ',')) = ' ';
	offset--;

	(void)snprintf(tmp + offset, sz - offset, "FROM %s LIMIT 1",
		tablename);

	return (tmp);
}

static gfarm_rdbms_error_t
gfarm_pgsql_sql_error_to_gfarm_rdbms_error(const PGresult *res)
{
	gfarm_rdbms_error_t dbe = RDBMS_ERROR_ANY_OTHERS;
	char *smsg = PQresultErrorField(res, PG_DIAG_SQLSTATE);

	if (strncasecmp(smsg,
		GFARM_PGSQL_ERRCODE_SYNTAX_ERROR_PREFIX,
		sizeof(GFARM_PGSQL_ERRCODE_SYNTAX_ERROR_PREFIX) - 1) == 0) {
		if (strncasecmp(
			smsg,
			GFARM_PGSQL_ERRCODE_UNDEFINED_COLUMN,
			sizeof(GFARM_PGSQL_ERRCODE_UNDEFINED_COLUMN) - 1) ==
			0) {
			dbe = RDBMS_ERROR_INVALID_SCHEME;
		} else {
			dbe = RDBMS_ERROR_SQL_SYNTAX_ERROR;
		}
	}

	return (dbe);
}

static gfarm_rdbms_error_t
gfarm_pgsql_check_scheme(const char *tablename,
	size_t ncolumns, const char * const columns[])
{
	static const char diag[] = "check_scheme";
	PGresult *res = NULL;
	ExecStatusType st;
	gfarm_rdbms_error_t dbe = RDBMS_ERROR_UNKNOWN_STATE;
	char *emsg = NULL;
	char *sql = gen_scheme_check_query(tablename, ncolumns, columns);

	if (sql == NULL) {
		dbe = RDBMS_ERROR_NO_MEMORY;
		goto bailout;
	}

	if (gfarm_pgsql_start(diag) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "pgsql restart failed");
		goto bailout;
	}

	res = PQexec(conn, sql);
	if (res == NULL) {
		gflog_error(GFARM_MSG_UNFIXED,
			"A PostgreSQL qurey result is NULL: %s",
			PQerrorMessage(conn));
		goto bailout;
	}
	/*
	 * Note:
	 *	This is what I intended to. For now we don't have to
	 *	be in an open transaction.
	 *
	 * XXX FIXME:
	 *	Do we have to close the transaction EVEN IF the result is
	 *	NULL ?
	 */
	(void)gfarm_pgsql_commit_sn(0, diag);

	st = PQresultStatus(res);
	switch (st) {
	case PGRES_TUPLES_OK:
		dbe = RDBMS_ERROR_NO_ERROR;
		break;
	case PGRES_BAD_RESPONSE:
	case PGRES_NONFATAL_ERROR:
	case PGRES_FATAL_ERROR:
		emsg = PQresultErrorMessage(res);
		dbe = gfarm_pgsql_sql_error_to_gfarm_rdbms_error(res);
		if (dbe != RDBMS_ERROR_ANY_OTHERS)
			gflog_error(GFARM_MSG_UNFIXED,
				"SQL realated error: %s", emsg);
		else
			gflog_error(GFARM_MSG_UNFIXED,
				"Unexpected PostgreSQL error: %s", emsg);
		break;
	default:
		/* PGRES_EMPTY_QUERY
		 * PGRES_COMMAND_OK
		 * PGRES_COPY_OUT
		 * PGRES_COPY_IN */
		emsg = PQresultErrorMessage(res);
		dbe = RDBMS_ERROR_ANY_OTHERS;
		gflog_error(GFARM_MSG_UNFIXED,
			"must not happen, unexpected query status: %s", emsg);
		assert(0);
		break;
	}

bailout:
	free(sql);
	if (res != NULL)
		PQclear(res);

	return (dbe);
}

/**********************************************************************/

static gfarm_error_t
host_info_set_fields_with_grouping(
	PGresult *res, int startrow, int nhostaliases, void *vinfo)
{
	int i;
	struct gfarm_internal_host_info *info = vinfo;

	info->hi.hostname = pgsql_get_string_ck(res, startrow, "hostname");
	info->hi.port = pgsql_get_int32(res, startrow, "port");
	info->hi.architecture =
		pgsql_get_string_ck(res, startrow, "architecture");
	info->hi.ncpu = pgsql_get_int32(res, startrow, "ncpu");
	info->hi.flags = pgsql_get_int32(res, startrow, "flags");
	info->fsngroupname =
		pgsql_get_string_ck(res, startrow, "fsngroupname");
	info->hi.nhostaliases = nhostaliases;
	GFARM_MALLOC_ARRAY(info->hi.hostaliases, nhostaliases + 1);
	if (info->hi.hostaliases == NULL) {
		gflog_fatal(GFARM_MSG_1002373,
		    "host_info_set_fields_with_grouping(%s, %d): no memory",
		    info->hi.hostname, nhostaliases + 1);
	}
	for (i = 0; i < nhostaliases; i++) {
		info->hi.hostaliases[i] =
			pgsql_get_string_ck(res, startrow + i, "hostalias");
	}
	info->hi.hostaliases[info->hi.nhostaliases] = NULL;
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
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1002155,
				"gfarm_pgsql_insert_and_log() failed");
			return (e);
		}
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
pgsql_host_update(gfarm_uint64_t seqnum, struct gfarm_host_info *info,
	const char *sql,
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

	if ((e = gfarm_pgsql_start(diag)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002156,
			"gfarm_pgsql_start() failed");
		return (e);
	}
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
	if ((e = (*check)(res, sql, diag))
	    == GFARM_ERR_DB_ACCESS_SHOULD_BE_RETRIED)
		return (e);
	PQclear(res);

	if (e == GFARM_ERR_NO_ERROR && remove_all_aliases_first)
		e = hostaliases_remove(info->hostname);

	if (e == GFARM_ERR_NO_ERROR)
		e = hostaliases_set(info);

	if (e == GFARM_ERR_NO_ERROR)
		e = gfarm_pgsql_commit_sn(seqnum, diag);
	else
		gfarm_pgsql_rollback(diag);
	return (e);
}

static gfarm_error_t
gfarm_pgsql_host_add(gfarm_uint64_t seqnum, struct gfarm_host_info *info)
{
	gfarm_error_t e;

	e = pgsql_host_update(seqnum, info,
		"INSERT INTO Host (hostname, port, architecture, ncpu, flags) "
		    "VALUES ($1, $2, $3, $4, $5)",
		gfarm_pgsql_check_insert, 0, "pgsql_host_add");

	free_arg(info);
	return (e);
}

static gfarm_error_t
gfarm_pgsql_host_modify(gfarm_uint64_t seqnum, struct db_host_modify_arg *arg)
{
	gfarm_error_t e;
	/* XXX FIXME: should use modflags, add_aliases and del_aliases */

	e = pgsql_host_update(seqnum, &arg->hi,
		"UPDATE Host "
		    "SET port = $2, architecture = $3, ncpu = $4, flags = $5 "
		    "WHERE hostname = $1",
		gfarm_pgsql_check_update_or_delete, 1, "pgsql_host_modify");

	free_arg(arg);
	return (e);
}

static gfarm_error_t
gfarm_pgsql_host_remove(gfarm_uint64_t seqnum, char *hostname)
{
	gfarm_error_t e;
	const char *paramValues[1];

	paramValues[0] = hostname;
	e = gfarm_pgsql_update_or_delete(seqnum,
		"DELETE FROM Host WHERE hostname = $1",
		1, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0, /* ask for text results */
		"pgsql_host_remove");

	free_arg(hostname);
	return (e);
}

static gfarm_error_t
gfarm_pgsql_host_check_scheme(void)
{
	const char *tablename = "Host";
	const char * const columns[] = {
		"hostname", "port", "architecture", "ncpu",
		"flags", "fsngroupname"
	};
	gfarm_rdbms_error_t dbe;

	if ((dbe = gfarm_pgsql_check_scheme(tablename,
			sizeof(columns) / sizeof(char *), columns)) !=
		RDBMS_ERROR_NO_ERROR) {
		/*
		 * The case like this, which got failed to check the
		 * Host table, we can't live any longer. Just quit.
		 * And before quit, we close the DB connection anyway.
		 */
		(void)gfarm_pgsql_terminate();

		switch (dbe) {
		case RDBMS_ERROR_INVALID_SCHEME:
			gflog_fatal(GFARM_MSG_UNFIXED,
				"The Host table scheme is not valid. "
				"Running the config-gfarm-update might "
				"solve this.");
			break;
		case RDBMS_ERROR_SQL_SYNTAX_ERROR:
			gflog_error(GFARM_MSG_UNFIXED,
				"Must not happen. "
				"SQL syntax error(s) in query??");
			assert(0);
			break;
		default:
			gflog_fatal(GFARM_MSG_UNFIXED,
				"Got an error while checking the Host "
				"table scheme. Exit anyway to prevent "
				"any filesystem metadata corruptions.");
		}

		/*
		 * For failsafe.
		 */
		exit(2);
	}

	return ((dbe == RDBMS_ERROR_NO_ERROR) ?
		GFARM_ERR_NO_ERROR : GFARM_ERR_UNKNOWN);
}

static gfarm_error_t
gfarm_pgsql_host_load(void *closure,
	void (*callback)(void *, struct gfarm_internal_host_info *))
{
	gfarm_error_t e;
	int i, n;
	struct gfarm_internal_host_info *infos;

	e = gfarm_pgsql_host_check_scheme();
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	e = gfarm_pgsql_generic_grouping_get_all_no_retry(
		"SELECT Host.hostname, count(hostalias) "
		    "FROM Host LEFT OUTER JOIN HostAliases "
			"ON Host.hostname = HostAliases.hostname "
		    "GROUP BY Host.hostname "
		    "ORDER BY Host.hostname",

		"SELECT Host.hostname, port, architecture, ncpu, flags, "
				"fsngroupname, hostalias "
		    "FROM Host LEFT OUTER JOIN HostAliases "
			"ON Host.hostname = HostAliases.hostname "
		    "ORDER BY Host.hostname, hostalias",

		0, NULL,
		&n, &infos,
		&gfarm_base_internal_host_info_ops,
		host_info_set_fields_with_grouping,
		"pgsql_host_load");
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002157,
		    "gfarm_pgsql_generic_grouping_get_all_no_retry() failed");
		return (e);
	}

	for (i = 0; i < n; i++)
		(*callback)(closure, &infos[i]);

	free(infos);
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

static gfarm_error_t
gfarm_pgsql_fsngroup_modify(gfarm_uint64_t seqnum,
	struct db_fsngroup_modify_arg *arg)
{
	gfarm_error_t e;
	const char *paramValues[2];

	paramValues[0] = arg->hostname;
	paramValues[1] = arg->fsngroupname;
	e = gfarm_pgsql_update_or_delete(seqnum,
		"UPDATE Host SET fsngroupname = $2 "
			 "WHERE hostname = $1",
		2, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0, /* ask for text results */
		"pgsql_fsngroup_modify");

	free_arg((void *)arg);
	return (e);
}

/**********************************************************************/

static gfarm_error_t
user_info_set_field(PGresult *res, int row, void *vinfo)
{
	struct gfarm_user_info *info = vinfo;

	info->username = pgsql_get_string_ck(res, row, "username");
	info->homedir = pgsql_get_string_ck(res, row, "homedir");
	info->realname = pgsql_get_string_ck(res, row, "realname");
	info->gsi_dn = pgsql_get_string_ck(res, row, "gsiDN");
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
pgsql_user_call(gfarm_uint64_t seqnum, struct gfarm_user_info *info,
	const char *sql, gfarm_pgsql_dml_sn_t op, const char *diag)
{
	const char *paramValues[4];

	paramValues[0] = info->username;
	paramValues[1] = info->homedir;
	paramValues[2] = info->realname;
	paramValues[3] = info->gsi_dn;
	return (*op)(
		seqnum,
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
gfarm_pgsql_user_add(gfarm_uint64_t seqnum, struct gfarm_user_info *info)
{
	gfarm_error_t e;
	e = pgsql_user_call(seqnum, info,
		"INSERT INTO GfarmUser (username, homedir, realname, gsiDN) "
		     "VALUES ($1, $2, $3, $4)",
		gfarm_pgsql_insert,
		"pgsql_user_add");

	free_arg(info);
	return (e);
}

static gfarm_error_t
gfarm_pgsql_user_modify(gfarm_uint64_t seqnum, struct db_user_modify_arg *arg)
{
	gfarm_error_t e;
	e = pgsql_user_call(seqnum, &arg->ui,
		"UPDATE GfarmUser "
		     "SET homedir = $2, realname = $3, gsiDN = $4 "
		     "WHERE username = $1",
		gfarm_pgsql_update_or_delete,
		"pgsql_user_modify");

	free_arg(arg);
	return (e);
}

static gfarm_error_t
gfarm_pgsql_user_remove(gfarm_uint64_t seqnum, char *username)
{
	gfarm_error_t e;
	const char *paramValues[1];

	paramValues[0] = username;
	e = gfarm_pgsql_update_or_delete(seqnum,
		"DELETE FROM GfarmUser WHERE username = $1",
		1, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0, /* ask for text results */
		"pgsql_user_remove");

	free_arg(username);
	return (e);
}

static gfarm_error_t
gfarm_pgsql_user_load(void *closure,
	void (*callback)(void *, struct gfarm_user_info *))
{
	gfarm_error_t e;
	int i, n;
	struct gfarm_user_info *infos;

	e = gfarm_pgsql_generic_get_all_no_retry(
		"SELECT * FROM GfarmUser",
		0, NULL,
		&n, &infos,
		&gfarm_base_user_info_ops, user_info_set_field,
		"pgsql_user_load");
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002158,
			"gfarm_pgsql_generic_get_all_no_retry()");
		return (e);
	}
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

	info->groupname = pgsql_get_string_ck(res, startrow, "groupname");
	info->nusers = nusers;
	GFARM_MALLOC_ARRAY(info->usernames, nusers + 1);
	if (info->usernames == NULL) {
		gflog_fatal(GFARM_MSG_1002374,
		    "group_info_set_fields_with_grouping(%s, %d): no memory",
		    info->groupname, nusers + 1);
	}
	for (i = 0; i < nusers; i++) {
		info->usernames[i] =
		    pgsql_get_string_ck(res, startrow + i, "username");
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
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1002159,
				"gfarm_pgsql_insert_and_log() failed");
			return (e);
		}
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_pgsql_group_add(gfarm_uint64_t seqnum, struct gfarm_group_info *info)
{
	const char *paramValues[1];
	gfarm_error_t e;
	static const char diag[] = "pgsql_group_add";

	if ((e = gfarm_pgsql_start(diag))
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
			diag);

		if (e == GFARM_ERR_NO_ERROR)
			e = grpassign_set(info);

		if (e == GFARM_ERR_NO_ERROR)
			e = gfarm_pgsql_commit_sn(seqnum, diag);
		else
			gfarm_pgsql_rollback(diag);
	}

	free_arg(info);
	return (e);
}

static gfarm_error_t
gfarm_pgsql_group_modify(gfarm_uint64_t seqnum,
	struct db_group_modify_arg *arg)
{
	struct gfarm_group_info *info = &arg->gi;
	gfarm_error_t e;
	static const char diag[] = "gfarm_pgsql_group_modify";

	if ((e = gfarm_pgsql_start("pgsql_group_modify"))
	    == GFARM_ERR_NO_ERROR) {

		e = grpassign_remove(info->groupname);

		if (e == GFARM_ERR_NO_ERROR)
			e = grpassign_set(info);

		if (e == GFARM_ERR_NO_ERROR)
			e = gfarm_pgsql_commit_sn(seqnum, diag);
		else
			gfarm_pgsql_rollback(diag);
	}
	free_arg(arg);
	return (e);
}

static gfarm_error_t
gfarm_pgsql_group_remove(gfarm_uint64_t seqnum, char *groupname)
{
	gfarm_error_t e;
	const char *paramValues[1];

	paramValues[0] = groupname;
	e = gfarm_pgsql_update_or_delete(seqnum,
		"DELETE FROM GfarmGroup WHERE groupname = $1",
		1, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0, /* ask for text results */
		"pgsql_group_remove");

	free_arg(groupname);
	return (e);
}

static gfarm_error_t
gfarm_pgsql_group_load(void *closure,
	void (*callback)(void *, struct gfarm_group_info *))
{
	gfarm_error_t e;
	int i, n;
	struct gfarm_group_info *infos;

	e = gfarm_pgsql_generic_grouping_get_all_no_retry(
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
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002160,
		    "gfarm_pgsql_generic_grouping_get_all_no_retry() failed");
		return (e);
	}
	for (i = 0; i < n; i++)
		(*callback)(closure, &infos[i]);

	free(infos);
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

static gfarm_error_t
pgsql_inode_call(gfarm_uint64_t seqnum, struct gfs_stat *info,
	const char *sql, gfarm_pgsql_dml_sn_t op, const char *diag)
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
		seqnum,
		sql,
		13, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0, /* ask for text results */
		diag);

	free_arg(info);
	return (e);
}

static gfarm_error_t
gfarm_pgsql_inode_add(gfarm_uint64_t seqnum, struct gfs_stat *info)
{
	return (pgsql_inode_call(seqnum, info,
		"INSERT INTO INode (inumber, igen, nlink, size, mode, "
			           "username, groupname, "
				   "atimesec, atimensec, "
				   "mtimesec, mtimensec, "
				   "ctimesec, ctimensec) "
		    "VALUES ($1, $2, $3, $4, $5, "
			    "$6, $7, $8, $9 ,$10, $11, $12, $13)",
		gfarm_pgsql_insert,
		"pgsql_inode_add"));
}

static gfarm_error_t
gfarm_pgsql_inode_modify(gfarm_uint64_t seqnum, struct gfs_stat *info)
{
	return (pgsql_inode_call(seqnum, info,
		"UPDATE INode SET igen = $2, nlink = $3, size = $4, "
				"mode = $5, username = $6, groupname = $7, "
				"atimesec = $8,  atimensec = $9, "
				"mtimesec = $10, mtimensec = $11, "
				"ctimesec = $12, ctimensec = $13 "
		    "WHERE inumber = $1",
		gfarm_pgsql_update_or_delete,
		"pgsql_inode_modify"));
}

static gfarm_error_t
pgsql_inode_uint64_call(gfarm_uint64_t seqnum,
	struct db_inode_uint64_modify_arg *arg, const char *sql,
	const char *diag)
{
	gfarm_error_t e;
	const char *paramValues[2];
	char inumber[GFARM_INT64STRLEN + 1];
	char uint64[GFARM_INT64STRLEN + 1];

	sprintf(inumber, "%" GFARM_PRId64, arg->inum);
	paramValues[0] = inumber;
	sprintf(uint64, "%" GFARM_PRId64, arg->uint64);
	paramValues[1] = uint64;

	e = gfarm_pgsql_update_or_delete(
		seqnum,
		sql,
		2, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0, /* ask for text results */
		diag);

	free_arg(arg);
	return (e);
}

static gfarm_error_t
pgsql_inode_uint32_call(gfarm_uint64_t seqnum,
	struct db_inode_uint32_modify_arg *arg,
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

	e = gfarm_pgsql_update_or_delete(
		seqnum,
		sql,
		2, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0, /* ask for text results */
		diag);

	free_arg(arg);
	return (e);
}

static gfarm_error_t
pgsql_inode_string_call(gfarm_uint64_t seqnum,
	struct db_inode_string_modify_arg *arg,
	const char *sql, const char *diag)
{
	gfarm_error_t e;
	const char *paramValues[2];
	char inumber[GFARM_INT64STRLEN + 1];

	sprintf(inumber, "%" GFARM_PRId64, arg->inum);
	paramValues[0] = inumber;
	paramValues[1] = arg->string;

	e = gfarm_pgsql_update_or_delete(
		seqnum,
		sql,
		2, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0, /* ask for text results */
		diag);

	free_arg(arg);
	return (e);
}

static gfarm_error_t
pgsql_inode_timespec_call(gfarm_uint64_t seqnum,
	struct db_inode_timespec_modify_arg *arg,
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

	e = gfarm_pgsql_update_or_delete(
		seqnum,
		sql,
		3, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0, /* ask for text results */
		diag);

	free_arg(arg);
	return (e);
}

static gfarm_error_t
gfarm_pgsql_inode_gen_modify(gfarm_uint64_t seqnum,
	struct db_inode_uint64_modify_arg *arg)
{
	return (pgsql_inode_uint64_call(seqnum, arg,
	    "UPDATE INode SET igen = $2 WHERE inumber = $1",
	    "pgsql_inode_gen_modify"));
}

static gfarm_error_t
gfarm_pgsql_inode_nlink_modify(gfarm_uint64_t seqnum,
	struct db_inode_uint64_modify_arg *arg)
{
	return (pgsql_inode_uint64_call(seqnum, arg,
	    "UPDATE INode SET nlink = $2 WHERE inumber = $1",
	    "pgsql_inode_nlink_modify"));
}

static gfarm_error_t
gfarm_pgsql_inode_size_modify(gfarm_uint64_t seqnum,
	struct db_inode_uint64_modify_arg *arg)
{
	return (pgsql_inode_uint64_call(seqnum, arg,
	    "UPDATE INode SET size = $2 WHERE inumber = $1",
	    "pgsql_inode_size_modify"));
}

static gfarm_error_t
gfarm_pgsql_inode_mode_modify(gfarm_uint64_t seqnum,
	struct db_inode_uint32_modify_arg *arg)
{
	return (pgsql_inode_uint32_call(seqnum, arg,
	    "UPDATE INode SET mode = $2 WHERE inumber = $1",
	    "pgsql_inode_mode_modify"));
}

static gfarm_error_t
gfarm_pgsql_inode_user_modify(gfarm_uint64_t seqnum,
	struct db_inode_string_modify_arg *arg)
{
	return (pgsql_inode_string_call(seqnum, arg,
	    "UPDATE INode SET username = $2 WHERE inumber = $1",
	    "pgsql_inode_user_modify"));
}

static gfarm_error_t
gfarm_pgsql_inode_group_modify(gfarm_uint64_t seqnum,
	struct db_inode_string_modify_arg *arg)
{
	return (pgsql_inode_string_call(seqnum, arg,
	    "UPDATE INode SET groupname = $2 WHERE inumber = $1",
	    "pgsql_inode_group_modify"));
}

static gfarm_error_t
gfarm_pgsql_inode_atime_modify(gfarm_uint64_t seqnum,
	struct db_inode_timespec_modify_arg *arg)
{
	return (pgsql_inode_timespec_call(seqnum, arg,
	  "UPDATE INode SET atimesec = $2, atimensec = $3 WHERE inumber = $1",
	    "pgsql_inode_atime_modify"));
}

static gfarm_error_t
gfarm_pgsql_inode_mtime_modify(gfarm_uint64_t seqnum,
	struct db_inode_timespec_modify_arg *arg)
{
	return (pgsql_inode_timespec_call(seqnum, arg,
	  "UPDATE INode SET mtimesec = $2, mtimensec = $3 WHERE inumber = $1",
	  "pgsql_inode_mtime_modify"));
}

static gfarm_error_t
gfarm_pgsql_inode_ctime_modify(gfarm_uint64_t seqnum,
	struct db_inode_timespec_modify_arg *arg)
{
	return (pgsql_inode_timespec_call(seqnum, arg,
	  "UPDATE INode SET ctimesec = $2, ctimensec = $3 WHERE inumber = $1",
	  "pgsql_inode_ctime_modify"));
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

	info->st_ino = get_int64_from_copy_binary(&buf, &residual);
	info->st_gen = get_int64_from_copy_binary(&buf, &residual);
	info->st_nlink = get_int64_from_copy_binary(&buf, &residual);
	info->st_size = get_int64_from_copy_binary(&buf, &residual);
	info->st_ncopy = 0;
	info->st_mode = get_int32_from_copy_binary(&buf, &residual);
	info->st_user = get_string_from_copy_binary(&buf, &residual);
	info->st_group = get_string_from_copy_binary(&buf, &residual);
	info->st_atimespec.tv_sec =
		get_int64_from_copy_binary(&buf, &residual);
	info->st_atimespec.tv_nsec =
		get_int32_from_copy_binary(&buf, &residual);
	info->st_mtimespec.tv_sec =
		get_int64_from_copy_binary(&buf, &residual);
	info->st_mtimespec.tv_nsec =
		get_int32_from_copy_binary(&buf, &residual);
	info->st_ctimespec.tv_sec =
		get_int64_from_copy_binary(&buf, &residual);
	info->st_ctimespec.tv_nsec =
		get_int32_from_copy_binary(&buf, &residual);
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
pgsql_inode_cksum_call(gfarm_uint64_t seqnum, struct db_inode_cksum_arg *arg,
	const char *sql, gfarm_pgsql_dml_sn_t op, const char *diag)
{
	gfarm_error_t e;
	const char *paramValues[3];
	char inumber[GFARM_INT64STRLEN + 1];

	sprintf(inumber, "%" GFARM_PRId64, arg->inum);
	paramValues[0] = inumber;
	paramValues[1] = arg->type;
	paramValues[2] = arg->sum;
	e = (*op)(
		seqnum,
		sql,
		3, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0, /* ask for text results */
		diag);

	free_arg(arg);
	return (e);
}

static gfarm_error_t
pgsql_inode_inum_call(gfarm_uint64_t seqnum, struct db_inode_inum_arg *arg,
	const char *sql, gfarm_pgsql_dml_sn_t op, const char *diag)
{
	gfarm_error_t e;
	const char *paramValues[1];
	char inumber[GFARM_INT64STRLEN + 1];

	sprintf(inumber, "%" GFARM_PRId64, arg->inum);
	paramValues[0] = inumber;
	e = (*op)(
		seqnum,
		sql,
		1, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0, /* ask for text results */
		diag);

	free_arg(arg);
	return (e);
}

static gfarm_error_t
gfarm_pgsql_file_info_add(gfarm_uint64_t seqnum,
	struct db_inode_cksum_arg *arg)
{
	return (pgsql_inode_cksum_call(seqnum, arg,
		"INSERT INTO FileInfo (inumber, checksumType, checksum) "
		     "VALUES ($1, $2, $3)",
		gfarm_pgsql_insert, "pgsql_cksum_add"));
}

static gfarm_error_t
gfarm_pgsql_file_info_modify(gfarm_uint64_t seqnum,
	struct db_inode_cksum_arg *arg)
{
	return (pgsql_inode_cksum_call(seqnum, arg,
		"UPDATE FileInfo SET checksumType = $2, checksum = $3 "
		    "WHERE inumber = $1",
		gfarm_pgsql_update_or_delete,
		"pgsql_cksum_modify"));
}

static gfarm_error_t
gfarm_pgsql_file_info_remove(gfarm_uint64_t seqnum,
	struct db_inode_inum_arg *arg)
{
	return (pgsql_inode_inum_call(seqnum, arg,
		"DELETE FROM FileInfo WHERE inumber = $1",
		gfarm_pgsql_update_or_delete,
		"pgsql_cksum_remove"));
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

	info->inum = get_int64_from_copy_binary(&buf, &residual);
	info->type = get_string_from_copy_binary(&buf, &residual);
	info->sum = get_string_from_copy_binary(&buf, &residual);
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
pgsql_filecopy_call(gfarm_uint64_t seqnum, struct db_filecopy_arg *arg,
	const char *sql, gfarm_pgsql_dml_sn_t op, const char *diag)
{
	gfarm_error_t e;
	const char *paramValues[2];
	char inumber[GFARM_INT64STRLEN + 1];

	sprintf(inumber, "%" GFARM_PRId64, arg->inum);
	paramValues[0] = inumber;
	paramValues[1] = arg->hostname;
	e = (*op)(
		seqnum,
		sql,
		2, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0, /* ask for text results */
		diag);

	free_arg(arg);
	return (e);
}

static gfarm_error_t
gfarm_pgsql_filecopy_add(gfarm_uint64_t seqnum,
	struct db_filecopy_arg *arg)
{
	return (pgsql_filecopy_call(seqnum, arg,
		"INSERT INTO FileCopy (inumber, hostname) "
#if 0
		"VALUES ($1, $2)",
#else /* XXX FIXME: workaround for SourceForge #431 */
		/*
		 * without "::varchar" just after $2,
		 * the following error occurrs with PostgreSQL-8.4.4:
		 * ERROR:  inconsistent types deduced for parameter $2
		 * DETAIL:  text versus character varying
		 *
		 * "::int8" was not necessary, but added for
		 * style consistency.
		 */
		"SELECT $1::int8, $2::varchar WHERE NOT EXISTS "
		"(SELECT * FROM FileCopy WHERE inumber=$1 AND hostname=$2)",
#endif
		gfarm_pgsql_insert,
		"pgsql_filecopy_add"));
}

static gfarm_error_t
gfarm_pgsql_filecopy_remove(gfarm_uint64_t seqnum,
	struct db_filecopy_arg *arg)
{
	return (pgsql_filecopy_call(seqnum, arg,
		"DELETE FROM FileCopy WHERE inumber = $1 AND hostname = $2",
		gfarm_pgsql_update_or_delete,
		"pgsql_filecopy_remove"));
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

	info->inum = get_int64_from_copy_binary(&buf, &residual);
	info->hostname = get_string_from_copy_binary(&buf, &residual);
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
pgsql_deadfilecopy_call(gfarm_uint64_t seqnum, struct db_deadfilecopy_arg *arg,
	const char *sql, gfarm_pgsql_dml_sn_t op, const char *diag)
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
		seqnum,
		sql,
		3, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0, /* ask for text results */
		diag);

	free_arg(arg);
	return (e);
}

gfarm_error_t
gfarm_pgsql_deadfilecopy_add(gfarm_uint64_t seqnum,
	struct db_deadfilecopy_arg *arg)
{
	return (pgsql_deadfilecopy_call(seqnum, arg,
		"INSERT INTO DeadFileCopy (inumber, igen, hostname) "
#if 0
			"VALUES ($1, $2, $3)",
#else /* XXX FIXME: workaround for SourceForge #407 */
			/*
			 * without "::varchar" just after $3,
			 * the following error occurrs with PostgreSQL-8.4.4:
			 * ERROR:  inconsistent types deduced for parameter $3
			 * DETAIL:  text versus character varying
			 *
			 * "::int8" was not necessary, but added for
			 * style consistency.
			 */
			"SELECT $1::int8, $2::int8, $3::varchar "
			"WHERE NOT EXISTS (SELECT * FROM DeadFileCopy "
			"WHERE inumber=$1 AND igen=$2 "
			"AND hostname=$3)",
#endif
		gfarm_pgsql_insert,
		"pgsql_deadfilecopy_add"));
}

static gfarm_error_t
gfarm_pgsql_deadfilecopy_remove(gfarm_uint64_t seqnum,
	struct db_deadfilecopy_arg *arg)
{
	return (pgsql_deadfilecopy_call(seqnum, arg,
		"DELETE FROM DeadFileCopy "
			"WHERE inumber = $1 AND igen = $2 AND hostname = $3",
		gfarm_pgsql_update_or_delete,
		"pgsql_deadfilecopy_remove"));
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

	info->inum = get_int64_from_copy_binary(&buf, &residual);
	info->igen = get_int64_from_copy_binary(&buf, &residual);
	info->hostname = get_string_from_copy_binary(&buf, &residual);
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
gfarm_pgsql_direntry_add(gfarm_uint64_t seqnum,
	struct db_direntry_arg *arg)
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
	e = gfarm_pgsql_insert(seqnum,
		"INSERT INTO DirEntry (dirINumber, entryName, entryINumber) "
		"VALUES ($1, $2, $3)",
		3, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0, /* ask for text results */
		"direntry_add");

	free_arg(arg);
	return (e);
}

static gfarm_error_t
gfarm_pgsql_direntry_remove(gfarm_uint64_t seqnum,
	struct db_direntry_arg *arg)
{
	gfarm_error_t e;
	const char *paramValues[2];
	char dir_inumber[GFARM_INT64STRLEN + 1];

	sprintf(dir_inumber, "%" GFARM_PRId64, arg->dir_inum);
	paramValues[0] = dir_inumber;
	paramValues[1] = arg->entry_name;
	e = gfarm_pgsql_update_or_delete(seqnum,
		"DELETE FROM DirEntry "
			"WHERE dirINumber = $1 AND entryName = $2",
		2, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0, /* ask for text results */
		"direntry_remove");

	free_arg(arg);
	return (e);
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

	info->dir_inum = get_int64_from_copy_binary(&buf, &residual);
	info->entry_name = get_string_from_copy_binary(&buf, &residual);
	info->entry_len = strlen(info->entry_name);
	info->entry_inum = get_int64_from_copy_binary(&buf, &residual);
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
pgsql_symlink_call(gfarm_uint64_t seqnum, struct db_symlink_arg *arg,
	const char *sql, gfarm_pgsql_dml_sn_t op, const char *diag)
{
	gfarm_error_t e;
	const char *paramValues[2];
	char inumber[GFARM_INT64STRLEN + 1];

	sprintf(inumber, "%" GFARM_PRId64, arg->inum);
	paramValues[0] = inumber;
	paramValues[1] = arg->source_path;
	e = (*op)(
		seqnum,
		sql,
		2, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0, /* ask for text results */
		diag);

	free_arg(arg);
	return (e);
}

static gfarm_error_t
gfarm_pgsql_symlink_add(gfarm_uint64_t seqnum,
	struct db_symlink_arg *arg)
{
	return (pgsql_symlink_call(seqnum, arg,
		"INSERT INTO Symlink (inumber, sourcePath) VALUES ($1, $2)",
		gfarm_pgsql_insert,
		"pgsql_symlink_add"));
}

static gfarm_error_t
gfarm_pgsql_symlink_remove(gfarm_uint64_t seqnum,
	struct db_inode_inum_arg *arg)
{
	return (pgsql_inode_inum_call(seqnum, arg,
		"DELETE FROM Symlink WHERE inumber = $1",
		gfarm_pgsql_update_or_delete,
		"pgsql_symlink_remove"));
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

	info->inum = get_int64_from_copy_binary(&buf, &residual);
	info->source_path = get_string_from_copy_binary(&buf, &residual);
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
gfarm_pgsql_xattr_add(gfarm_uint64_t seqnum, struct db_xattr_arg *arg)
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
	e = gfarm_pgsql_insert(
		seqnum,
		command,
		3, /* number of params */
		NULL, /* param types */
		paramValues,
		paramLengths,
		paramFormats,
		0, /* ask for text results */
		diag);

	free_arg(arg);
	return (e);
}

static gfarm_error_t
gfarm_pgsql_xattr_modify(gfarm_uint64_t seqnum, struct db_xattr_arg *arg)
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
	e = gfarm_pgsql_update_or_delete(
		seqnum,
		command,
		3, /* number of params */
		NULL, /* param types */
		paramValues,
		paramLengths,
		paramFormats,
		0, /* ask for text results */
		diag);

	free_arg(arg);
	return (e);
}

static gfarm_error_t
gfarm_pgsql_xattr_remove(gfarm_uint64_t seqnum, struct db_xattr_arg *arg)
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
	e = gfarm_pgsql_update_or_delete(
		seqnum,
		command,
		paramNum,
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0, /* ask for text results */
		diag);

	free_arg(arg);
	return (e);
}

static gfarm_error_t
gfarm_pgsql_xattr_removeall(gfarm_uint64_t seqnum, struct db_xattr_arg *arg)
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
	e = gfarm_pgsql_update_or_delete(
		seqnum,
		command,
		1,
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0, /* ask for text results */
		diag);

	free_arg(arg);
	return (e);
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
		gflog_debug(GFARM_MSG_1002161,
			"pgsql_get_string() failed");
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
	if ((info->attrsize > 0) && (info->attrvalue == NULL)) {
		gflog_debug(GFARM_MSG_1002162,
			"pgsql_get_binary() failed");
		return (GFARM_ERR_NO_MEMORY);
	} else {
		return (GFARM_ERR_NO_ERROR);
	}
}

static gfarm_error_t
gfarm_pgsql_xattr_get(gfarm_uint64_t seqnum, struct db_xattr_arg *arg)
{
	gfarm_error_t e;
	const char *paramValues[2];
	char inumber[GFARM_INT64STRLEN + 1];
	const char *diag;
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

	e = gfarm_pgsql_generic_get_all_no_retry(
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
	return (e);
}

static gfarm_error_t
pgsql_xattr_set_inum_and_attrname(PGresult *res, int row, void *vinfo)
{
	struct xattr_info *info = vinfo;

	info->inum = pgsql_get_int64(res, row, "inumber");
	info->attrname = pgsql_get_string_ck(res, row, "attrname");
	info->namelen = strlen(info->attrname) + 1; // include '\0'
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
pgsql_xattr_info_set_fields(PGresult *res, int row, void *vinfo)
{
	pgsql_xattr_set_inum_and_attrname(res, row, vinfo);
	return (pgsql_xattr_set_attrvalue_binary(res, row, vinfo));
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
		/* NOTE: if xmlMode, attrvalue is unnecessary to load. */
		command = "SELECT inumber,attrname FROM XmlAttr";
		diag = "pgsql_xattr_load_xml";
	} else {
		command = "SELECT inumber,attrname,attrvalue FROM XAttr";
		diag = "pgsql_xattr_load_norm";
	}

	/* XXX FIXME: should use gfarm_pgsql_generic_load() instead */
	e = gfarm_pgsql_generic_get_all_no_retry(
		command,
		0, NULL,
		&n, &vinfo,
		&gfarm_base_xattr_info_ops,
		xmlMode ?
		pgsql_xattr_set_inum_and_attrname :
		pgsql_xattr_info_set_fields,
		diag);
	if (e == GFARM_ERR_NO_SUCH_OBJECT)
		return GFARM_ERR_NO_ERROR;
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002163,
			"gfarm_pgsql_generic_get_all_no_retry() failed");
		return (e);
	}
	for (i = 0; i < n; i++) {
		(*callback)(&xmlMode, &vinfo[i]);
	}
	gfarm_base_generic_info_free_all(n, vinfo,
			&gfarm_base_xattr_info_ops);
	return (e);
}

static gfarm_error_t
pgsql_xattr_set_attrname(PGresult *res, int row, void *vinfo)
{
	struct xattr_info *info = vinfo;

	info->attrname = pgsql_get_string(res, row, "attrname");
	if (info->attrname == NULL) {
		/* error is logged in pgsql_get_string() */
		info->namelen = 0;
		return (GFARM_ERR_NO_MEMORY);
	} else {
		info->namelen = strlen(info->attrname) + 1; /* include '\0' */
		return (GFARM_ERR_NO_ERROR);
	}
}

static gfarm_error_t
gfarm_pgsql_xmlattr_find(gfarm_uint64_t seqnum,
	struct db_xmlattr_find_arg *arg)
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

	e = gfarm_pgsql_generic_get_all_no_retry(
		command,
		2, paramValues,
		&n, &vinfo,
		&gfarm_base_xattr_info_ops, pgsql_xattr_set_attrname,
		diag);

	if (e == GFARM_ERR_NO_ERROR) {
		e = (*(arg->foundcallback))(arg->foundcbdata, n, vinfo);
		if (n > 0)
			gfarm_base_generic_info_free_all(n, vinfo,
			    &gfarm_base_xattr_info_ops);
	} else if (e == GFARM_ERR_NO_SUCH_OBJECT)
		e = GFARM_ERR_NO_ERROR;
	free(arg);
	return (e);
}

/**********************************************************************/

/* quota */
static gfarm_error_t
pgsql_quota_call(gfarm_uint64_t seqnum, struct db_quota_arg *info,
	const char *sql, gfarm_pgsql_dml_sn_t op, const char *diag)
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
		seqnum,
		sql,
		18, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0, /* ask for text results */
		diag);

	free_arg(info);
	return (e);
}

static gfarm_error_t
gfarm_pgsql_quota_add(gfarm_uint64_t seqnum, struct db_quota_arg *arg)
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

	return (pgsql_quota_call(seqnum, arg, sql,
				 gfarm_pgsql_insert,
				 "pgsql_quota_add"));
}

static gfarm_error_t
gfarm_pgsql_quota_modify(gfarm_uint64_t seqnum, struct db_quota_arg *arg)
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

	return (pgsql_quota_call(seqnum, arg, sql,
				 gfarm_pgsql_update_or_delete,
				 "pgsql_quota_modify"));
}

static gfarm_error_t
gfarm_pgsql_quota_remove(gfarm_uint64_t seqnum,
	struct db_quota_remove_arg *arg)
{
	gfarm_error_t e;
	const char *paramValues[1];
	static char *sql_user = "DELETE FROM QuotaUser WHERE username = $1";
	static char *sql_group = "DELETE FROM QuotaGroup WHERE groupname = $1";
	char *sql = arg->is_group ? sql_group : sql_user;

	paramValues[0] = arg->name;
	e = gfarm_pgsql_update_or_delete(
		seqnum,
		sql,
		1, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0, /* ask for text results */
		"pgsql_quota_remove");

	free_arg(arg);
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

	info->name = get_string_from_copy_binary(&buf, &residual);
	info->grace_period = get_int64_from_copy_binary(&buf, &residual);
	info->space = get_int64_from_copy_binary(&buf, &residual);
	info->space_exceed = get_int64_from_copy_binary(&buf, &residual);
	info->space_soft = get_int64_from_copy_binary(&buf, &residual);
	info->space_hard = get_int64_from_copy_binary(&buf, &residual);
	info->num = get_int64_from_copy_binary(&buf, &residual);
	info->num_exceed = get_int64_from_copy_binary(&buf, &residual);
	info->num_soft = get_int64_from_copy_binary(&buf, &residual);
	info->num_hard = get_int64_from_copy_binary(&buf, &residual);
	info->phy_space = get_int64_from_copy_binary(&buf, &residual);
	info->phy_space_exceed = get_int64_from_copy_binary(
		&buf, &residual);
	info->phy_space_soft = get_int64_from_copy_binary(
		&buf, &residual);
	info->phy_space_hard = get_int64_from_copy_binary(
		&buf, &residual);
	info->phy_num = get_int64_from_copy_binary(&buf, &residual);
	info->phy_num_exceed = get_int64_from_copy_binary(
		&buf, &residual);
	info->phy_num_soft = get_int64_from_copy_binary(&buf, &residual);
	info->phy_num_hard = get_int64_from_copy_binary(&buf, &residual);
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

/**********************************************************************/

static gfarm_error_t
pgsql_seqnum_call(struct db_seqnum_arg *arg,
	const char *sql, gfarm_pgsql_dml_t op, const char *diag)
{
	gfarm_error_t e;
	const char *paramValues[2];
	char value[GFARM_INT32STRLEN + 1];

	paramValues[0] = arg->name;
	snprintf(value, sizeof(value), "%" GFARM_PRId64, arg->value);
	paramValues[1] = value;
	e = (*op)(
		sql,
		2, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0, /* ask for text results */
		diag);
	return (e);
}

static gfarm_error_t
gfarm_pgsql_seqnum_add(struct db_seqnum_arg *arg)
{
	gfarm_error_t e;

	e = pgsql_seqnum_call(arg,
		"INSERT INTO SeqNum (name, value) "
		     "VALUES ($1, $2)",
		gfarm_pgsql_insert_and_log,
		"pgsql_seqnum_add");
	return (e);
}

static gfarm_error_t
gfarm_pgsql_seqnum_modify(struct db_seqnum_arg *arg)
{
	gfarm_error_t e;

	e = pgsql_seqnum_call(arg,
		"UPDATE SeqNum "
		     "SET value = $2 "
		     "WHERE name = $1",
		gfarm_pgsql_exec_params_and_log,
		"pgsql_seqnum_modify");
	return (e);
}

static gfarm_error_t
gfarm_pgsql_seqnum_remove(char *name)
{
	gfarm_error_t e;
	const char *paramValues[1];

	paramValues[0] = name;
	e = gfarm_pgsql_exec_params_and_log(
		"DELETE FROM SeqNum WHERE name = $1",
		1, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0, /* ask for text results */
		"pgsql_seqnum_remove");
	return (e);
}

static gfarm_error_t
seqnum_info_set_field(PGresult *res, int row, void *varg)
{
	struct db_seqnum_arg *arg = varg;

	arg->name = pgsql_get_string_ck(res, row, "name");
	arg->value = pgsql_get_int64(res, row, "value");
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_pgsql_seqnum_get(const char *name, gfarm_uint64_t *seqnump)
{
	gfarm_error_t e;
	const char *paramValues[1];
	struct db_seqnum_arg *args;
	int n;

	paramValues[0] = name;
	e = gfarm_pgsql_generic_get_all_no_retry(
		"SELECT * FROM SeqNum WHERE name = $1",
		1, paramValues,
		&n, &args,
		&gfarm_base_user_info_ops, seqnum_info_set_field,
		"pgsql_seqnum_get");
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003329,
		    "gfarm_pgsql_generic_get_all_no_retry()");
		return (e);
	}
	if (n > 0)
		*seqnump = args[0].value;
	else
		e = GFARM_ERR_INTERNAL_ERROR;
	free(args);
	return (e);
}

static gfarm_error_t
gfarm_pgsql_seqnum_load(void *closure,
	void (*callback)(void *, struct db_seqnum_arg *))
{
	gfarm_error_t e;
	int i, n;
	struct db_seqnum_arg *args;

	e = gfarm_pgsql_generic_get_all_no_retry(
		"SELECT * FROM SeqNum",
		0, NULL,
		&n, &args,
		&gfarm_base_user_info_ops, seqnum_info_set_field,
		"pgsql_seqnum_load");
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003246,
		    "gfarm_pgsql_generic_get_all_no_retry()");
		return (e);
	}
	for (i = 0; i < n; i++)
		(*callback)(closure, &args[i]);

	free(args);
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

static gfarm_error_t
mdhost_set_field(PGresult *res, int startrow, void *vinfo)
{
	struct gfarm_metadb_server *info = vinfo;

	info->name = pgsql_get_string_ck(res, startrow, "hostname");
	info->port = pgsql_get_int32(res, startrow, "port");
	info->clustername = pgsql_get_string_ck(res, startrow, "clustername");
	info->flags = pgsql_get_int32(res, startrow, "flags");
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
pgsql_mdhost_update(gfarm_uint64_t seqnum, struct gfarm_metadb_server *info,
	const char *sql,
	gfarm_error_t (*check)(PGresult *, const char *, const char *),
	const char *diag)
{
	PGresult *res;
	const char *paramValues[4];
	gfarm_error_t e;
	char port[GFARM_INT32STRLEN + 1];
	char flags[GFARM_INT32STRLEN + 1];

	if ((e = gfarm_pgsql_start(diag)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003247,
			"gfarm_pgsql_start() failed");
		return (e);
	}
	paramValues[0] = info->name;
	sprintf(port, "%d", info->port);
	paramValues[1] = port;
	paramValues[2] = info->clustername;
	sprintf(flags, "%d", info->flags);
	paramValues[3] = flags;
	res = PQexecParams(conn,
	    sql,
	    4, /* number of params */
	    NULL, /* param types */
	    paramValues,
	    NULL, /* param lengths */
	    NULL, /* param formats */
	    0); /* ask for text results */
	if ((e = (*check)(res, sql, diag))
	    == GFARM_ERR_DB_ACCESS_SHOULD_BE_RETRIED)
		return (e);
	PQclear(res);

	if (e == GFARM_ERR_NO_ERROR)
		e = gfarm_pgsql_commit_sn(seqnum, diag);
	else
		gfarm_pgsql_rollback(diag);
	return (e);
}

static gfarm_error_t
gfarm_pgsql_mdhost_add(gfarm_uint64_t seqnum, struct gfarm_metadb_server *info)
{
	gfarm_error_t e;

	e = pgsql_mdhost_update(seqnum, info,
	    "INSERT INTO MDHost (hostname, port, clustername, flags) "
		"VALUES ($1, $2, $3, $4)",
	    gfarm_pgsql_check_insert, "pgsql_mdhost_add");

	free_arg(info);
	return (e);
}

static gfarm_error_t
gfarm_pgsql_mdhost_modify(gfarm_uint64_t seqnum,
	struct db_mdhost_modify_arg *arg)
{
	gfarm_error_t e;

	e = pgsql_mdhost_update(seqnum, &arg->ms,
	    "UPDATE MDHost "
		"SET port = $2, clustername = $3, flags = $4 "
		"WHERE hostname = $1",
	    gfarm_pgsql_check_update_or_delete, "pgsql_mdhost_modify");

	free_arg(arg);
	return (e);
}

static gfarm_error_t
gfarm_pgsql_mdhost_remove(gfarm_uint64_t seqnum, char *hostname)
{
	gfarm_error_t e;
	const char *paramValues[1];

	paramValues[0] = hostname;
	e = gfarm_pgsql_update_or_delete(seqnum,
	    "DELETE FROM MDHost WHERE hostname = $1",
	    1, /* number of params */
	    NULL, /* param types */
	    paramValues,
	    NULL, /* param lengths */
	    NULL, /* param formats */
	    0, /* ask for text results */
	    "pgsql_mdhost_remove");

	free_arg(hostname);
	return (e);
}

static gfarm_error_t
gfarm_pgsql_mdhost_load(void *closure,
	void (*callback)(void *, struct gfarm_metadb_server *))
{
	gfarm_error_t e;
	int i, n;
	struct gfarm_metadb_server *infos;

	e = gfarm_pgsql_generic_get_all_no_retry(
	    "SELECT * FROM MDHost",
	    0, NULL,
	    &n, &infos,
	    &gfarm_base_metadb_server_ops, mdhost_set_field,
	    "pgsql_mdhost_load");
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003248,
			"gfarm_pgsql_generic_get_all_no_retry()");
		return (e);
	}
	for (i = 0; i < n; i++)
		(*callback)(closure, &infos[i]);

	free(infos);
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

/* DO NOT REMOVE: this interfaces is provided for a private extension */
/* The official gfmd source code shouldn't use these interface */
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
	gfarm_pgsql_inode_gen_modify,
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

	gfarm_pgsql_seqnum_get,
	gfarm_pgsql_seqnum_add,
	gfarm_pgsql_seqnum_modify,
	gfarm_pgsql_seqnum_remove,
	gfarm_pgsql_seqnum_load,

	gfarm_pgsql_mdhost_add,
	gfarm_pgsql_mdhost_modify,
	gfarm_pgsql_mdhost_remove,
	gfarm_pgsql_mdhost_load,

	gfarm_pgsql_fsngroup_modify,
};
