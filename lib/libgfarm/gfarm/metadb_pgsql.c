/*
 * $Id$
 */

#include <stdlib.h>
#include <string.h>
#include <libpq-fe.h>
#include <netinet/in.h>
#include <gfarm/gfarm.h>

#include "config.h"
#include "metadb_access.h"
#include "metadb_sw.h"

/* for test */
char *gfarm_postgresql_server_name = "srapc1367.sra.co.jp";
char *gfarm_postgresql_server_port = "5432";
char *gfarm_postgresql_dbname = "gfarm";
char *gfarm_postgresql_username = "gfarm";
char *gfarm_postgresql_passwd = "secret-postgresql-password";

/**********************************************************************/

static PGconn *conn = NULL;

static char *
gfarm_pgsql_initialize(void)
{
	int port;
	char *e;
	static char *e_save = NULL;

	if (gfarm_postgresql_server_name == NULL)
		return ("gfarm.conf: postgresql_serverhost is missing");
	if (gfarm_postgresql_server_port == NULL)
		return ("gfarm.conf: postgresql_serverport is missing");
	port = strtol(gfarm_postgresql_server_port, &e, 0);
	if (e == gfarm_postgresql_server_port || port <= 0 || port >= 65536)
		return ("gfarm.conf: postgresql_serverport: "
			"illegal value");
	if (gfarm_postgresql_dbname == NULL)
		return ("gfarm.conf: postgresql_dbname is missing");
	if (gfarm_postgresql_username == NULL)
		return ("gfarm.conf: postgresql_username is missing");
	if (gfarm_postgresql_passwd == NULL)
		return ("gfarm.conf: postgresql_passwd is missing");

	/*
	 * initialize PostgreSQL
	 */

	/* open a connection */
	conn = PQsetdbLogin(gfarm_postgresql_server_name,
			    gfarm_postgresql_server_port,
			    NULL, /* options */
			    NULL, /* tty */
			    gfarm_postgresql_dbname,
			    gfarm_postgresql_username,
			    gfarm_postgresql_passwd);
	if (PQstatus(conn) != CONNECTION_OK) {
		/* PQerrorMessage's return value will be freed in PQfinish() */
		if (e_save != NULL)
			free(e_save);
		e_save = strdup(PQerrorMessage(conn));
		(void)gfarm_metadb_terminate();
		return (e_save != NULL ? e_save : GFARM_ERR_NO_MEMORY);
	}
	return (NULL);
}

static char *
gfarm_pgsql_terminate(void)
{
	/* close and free connection resources */
	if (gfarm_does_own_metadb_connection()) {
		PQfinish(conn);
	}

	return (NULL);
}

/*
 * PostgreSQL connection cannot be used from forked children unless
 * the connection is ensured to be used exclusively.
 * This routine guarantees that never happens.
 * NOTE:
 * This is where gfarm_metadb_initialize() is called from.
 * Nearly every interface functions must call this function.
 */
static char *
gfarm_pgsql_check(void)
{
	/*
	 * if there is a valid PostgreSQL connection, return.  If not,
	 * create a new connection.
	 */
	if (conn != NULL && gfarm_does_own_metadb_connection())
		return (NULL);
	/* XXX close the file descriptor for conn, but how? */
	conn = NULL;
	return (gfarm_metadb_initialize());
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

static char *
host_info_get_one(
	PGresult *res,
	int startrow,
	int nhostaliases,
	struct gfarm_host_info *info)
{
	int i;

	info->hostname = strdup(
		PQgetvalue(res, startrow, PQfnumber(res, "hostname")));
	info->architecture = strdup(
		PQgetvalue(res, startrow, PQfnumber(res, "architecture")));
	info->ncpu = ntohl(
	    *((uint32_t *)PQgetvalue(res, startrow, PQfnumber(res, "ncpu"))));
	info->nhostaliases = nhostaliases;
	info->hostaliases = malloc(sizeof(*(info->hostaliases)) * 
						(nhostaliases + 1));
	for (i = 0; i < nhostaliases; i++) {
		info->hostaliases[i] = strdup(
			PQgetvalue(res, startrow + i,
				   PQfnumber(res, "hostalias")));
	}		
	info->hostaliases[info->nhostaliases] = NULL;
	return (NULL);
}

static char *
host_info_get(
	char *csql,
	char *isql,
	int nparams,
	const char **paramValues,
	struct gfarm_host_info *info)
{
	PGresult *res, *resi, *resc;
	char *e = NULL;
	static char *e_save = NULL;

	if (e_save != NULL) {
		free(e_save);
		e_save = NULL;
	}	

	res = PQexec(conn, "BEGIN");
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		PQclear(res);
		return(PQerrorMessage(conn));
	}	
	PQclear(res);

	resc = PQexecParams(conn,
	        csql,	    
	        nparams,
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		1); /* ask for binary results */
	if (PQresultStatus(resc) != PGRES_TUPLES_OK) {
		e_save = strdup(PQerrorMessage(conn));
		if (e_save == NULL) 
			e = GFARM_ERR_NO_MEMORY;
		goto clear_resc;
	}	

	resi = PQexecParams(conn,
		isql,    
		nparams, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		1); /* ask for binary results */
	if (PQresultStatus(resi) != PGRES_TUPLES_OK) {
		e_save = strdup(PQerrorMessage(conn));
		if (e_save == NULL) 
			e = GFARM_ERR_NO_MEMORY;
		goto clear_resi;
	}	
	if (PQntuples(resi) == 0) {
		e = GFARM_ERR_NO_SUCH_OBJECT;
		goto clear_resi;
	}

	e = host_info_get_one(resi,
		0,
		gfarm_ntoh64(
			*((uint64_t *)PQgetvalue(resc,
						 0,
						 PQfnumber(resc, "count")))),
		info);

 clear_resi:      
	PQclear(resi);
 clear_resc:	
	PQclear(resc);

	res = PQexec(conn, "END");
	PQclear(res);

	return (e_save != NULL ? e_save : e);
}

static char *
gfarm_pgsql_host_info_get(
	const char *hostname,
	struct gfarm_host_info *info)
{
	char *e;
	const char *paramValues[1];

	if ((e = gfarm_pgsql_check()) != NULL)
		return (e);

	paramValues[0] = hostname;
	return (host_info_get(
		"SELECT count(hostaliases) "
		    "FROM HostAliases WHERE hostname = $1",

		"SELECT Host.hostname, architecture, ncpu, hostalias "
		    "FROM Host LEFT OUTER JOIN HostAliases "
			"ON Host.hostname = HostAliases.hostname "
		"WHERE Host.hostname = $1 "
		"ORDER BY Host.hostname, hostalias",

		1,
		paramValues,
		info));
}

static char *
hostaliases_remove(const char *hostname)
{
	PGresult *res;
	const char *paramValues[1];
	char *e = NULL;

	paramValues[0] = hostname;
	res = PQexecParams(conn,
		"DELETE FROM HostAliases WHERE hostname = $1",
		1, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0); /* dummy parameter for result format */
	if (PQresultStatus(res) != PGRES_COMMAND_OK)
		e = PQerrorMessage(conn);
	PQclear(res);
	return (e);
}

static char *
gfarm_pgsql_host_info_remove_hostaliases(const char *hostname)
{
	char *e;
	static char *e_save = NULL;

	if ((e = gfarm_pgsql_check()) != NULL)
		return (e);

	if (e_save != NULL) {
		free(e_save);
		e_save = NULL;
	}	

	/*
	 * XXX - needs to check if hostname exists in Hosts.
	 *       this check and deletion should be done in a trunsuction
	 */

	return (e_save = hostaliases_remove(hostname));
}

static char *
hostaliases_set(struct gfarm_host_info *info)
{
	PGresult *res;
	const char *paramValues[2];
	int i;

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
				PQclear(res);
				return(PQerrorMessage(conn));
			}	
			PQclear(res);
		}
	}	
	return (NULL);
}

static char *
gfarm_pgsql_host_info_set(
	char *hostname,
	struct gfarm_host_info *info)
{
	PGresult *res;
	const char *paramValues[3];
	char *e;
	static char *e_save = NULL;
	char ncpu[sizeof(info->ncpu) + 1]; /* for \0 according to convention */

	if ((e = gfarm_pgsql_check()) != NULL)
		return (e);

	if (e_save != NULL) {
		free(e_save);
		e_save = NULL;
	}	

	res = PQexec(conn, "BEGIN");
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		PQclear(res);
		return(PQerrorMessage(conn));
	}	
	PQclear(res);

	paramValues[0] = hostname;
	paramValues[1] = info->architecture;
	sprintf(ncpu, "%d", info->ncpu);
	paramValues[2] = ncpu;
	res = PQexecParams(conn,
		"INSERT INTO Host (hostname, architecture, ncpu) "
		    "VALUES ($1, $2, $3)",
		3, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0); /* dummy parameter for result format */
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		PQclear(res);
		e_save = strdup(PQerrorMessage(conn));
		if (e_save == NULL) 
			e = GFARM_ERR_NO_MEMORY;
		goto end;
	}	
	PQclear(res);

	e_save = hostaliases_set(info);
	if (e_save != NULL) {
		e_save = strdup(e_save);
		if (e_save == NULL)
			e = GFARM_ERR_NO_MEMORY;
	}	
 end:
	res = PQexec(conn, "END");
	PQclear(res);

	return (e_save != NULL ? e_save : e);
}

static char *
gfarm_pgsql_host_info_replace(
	char *hostname,
	struct gfarm_host_info *info)
{
	PGresult *res;
	const char *paramValues[3];
	char *e;
	static char *e_save = NULL;
	char ncpu[sizeof(info->ncpu) + 1]; /* for \0 according to convention */

	if ((e = gfarm_pgsql_check()) != NULL)
		return (e);

	if (e_save != NULL) {
		free(e_save);
		e_save = NULL;
	}	

	res = PQexec(conn, "BEGIN");
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		PQclear(res);
		return(PQerrorMessage(conn));
	}	
	PQclear(res);

	paramValues[0] = hostname;
	paramValues[1] = info->architecture;
	sprintf(ncpu, "%d", info->ncpu);
	paramValues[2] = ncpu;
	res = PQexecParams(conn,
		"UPDATE Host SET architecture = $2, ncpu =$3 "
		    "WHERE hostname = $1",
		3, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0); /* dummy parameter for result format */
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		PQclear(res);
		e_save = strdup(PQerrorMessage(conn));
		if (e_save == NULL) 
			e = GFARM_ERR_NO_MEMORY;
		goto end;
	}	
	PQclear(res);

	e_save = hostaliases_remove(hostname);
	if (e_save != NULL) {
		e_save = strdup(e_save);
		if (e_save == NULL)
			e = GFARM_ERR_NO_MEMORY;
	}	
	e_save = hostaliases_set(info);
	if (e_save != NULL) {
		e_save = strdup(e_save);
		if (e_save == NULL)
			e = GFARM_ERR_NO_MEMORY;
	}	

 end:

	res = PQexec(conn, "END");
	PQclear(res);

	return (e_save != NULL ? e_save : e);
}

static char *
gfarm_pgsql_host_info_remove(const char *hostname)
{
	PGresult *res;
	const char *paramValues[1];
	char *e;

	if ((e = gfarm_pgsql_check()) != NULL)
		return (e);

	paramValues[0] = hostname;
	res = PQexecParams(conn,
		"DELETE FROM Host WHERE hostname = $1",
		1, /* number of params */
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		0);  /* dummy parameter for result format */
	if (PQresultStatus(res) != PGRES_COMMAND_OK)
		e = PQerrorMessage(conn);
	PQclear(res);
	return (e);
}

static char *
host_info_get_all(
	char *csql,
	char *isql,
	int nparams,
	const char **paramValues,
	int *np,
	struct gfarm_host_info **infosp)
{
	PGresult *res, *resi, *resc;
	char *e = NULL;
	static char *e_save = NULL;
	int i, startrow;
	struct gfarm_host_info *ip;

	if (e_save != NULL) {
		free(e_save);
		e_save = NULL;
	}	

	res = PQexec(conn, "BEGIN");
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		PQclear(res);
		e_save = strdup(PQerrorMessage(conn));
		return (e_save != NULL ? e_save : GFARM_ERR_NO_MEMORY);
	}	
	PQclear(res);

	resc = PQexecParams(conn,
		csql,
		nparams,
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		1); /* ask for binary results */
	if (PQresultStatus(resc) != PGRES_TUPLES_OK) {
		e_save = strdup(PQerrorMessage(conn));
		if (e_save == NULL) 
			e = GFARM_ERR_NO_MEMORY;
		goto clear_resc;
	}	

	*np = PQntuples(resc);
	ip = malloc(sizeof(*ip) * *np);
	if (ip == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto clear_resc;
	}

	resi = PQexecParams(conn,
		isql,	    
		nparams,
		NULL, /* param types */
		paramValues,
		NULL, /* param lengths */
		NULL, /* param formats */
		1); /* ask for binary results */
	if (PQresultStatus(resi) != PGRES_TUPLES_OK) {
		e_save = strdup(PQerrorMessage(conn));
		if (e_save == NULL) 
			e = GFARM_ERR_NO_MEMORY;
		goto clear_resi;
	}

	startrow = 0;
	for (i = 0; i < PQntuples(resc); i++) {
		int nhostaliases;

	        nhostaliases = gfarm_ntoh64(*((uint64_t *)PQgetvalue(resc,
						i,
					        PQfnumber(resc, "count"))));
		e = host_info_get_one(resi, startrow, nhostaliases, &ip[i]);
		startrow += (nhostaliases == 0 ? 1 : nhostaliases);
	}
	*infosp = ip;

 clear_resi:      
	PQclear(resi);
 clear_resc:	
	PQclear(resc);

	res = PQexec(conn, "END");
	PQclear(res);

	return (e_save != NULL ? e_save : e);
}

static char *
gfarm_pgsql_host_info_get_all(
	int *np,
	struct gfarm_host_info **infosp)
{
	char *e;

	if ((e = gfarm_pgsql_check()) != NULL)
		return (e);

	return (host_info_get_all( 
		"SELECT Host.hostname, count(hostalias) "
		    "FROM Host LEFT OUTER JOIN HostAliases "
		        "ON Host.hostname = HostAliases.hostname "
		    "GROUP BY Host.hostname "
		    "ORDER BY Host.hostname",

		"SELECT Host.hostname, architecture, ncpu, hostalias "
		    "FROM Host LEFT OUTER JOIN HostAliases "
		        "ON Host.hostname = HostAliases.hostname "
		    "ORDER BY Host.hostname, hostalias",

		0,
		NULL,
		np,
		infosp));
}

static char *
gfarm_pgsql_host_info_get_by_name_alias(
	const char *name_alias,
	struct gfarm_host_info *info)
{
	const char *paramValues[1];
	char *e;
	int n;
	struct gfarm_host_info *infos;

	if ((e = gfarm_pgsql_check()) != NULL)
		return (e);

	paramValues[0] = name_alias;
	e = host_info_get_all(
		"SELECT Host.hostname, count(hostalias) "
		    "FROM Host LEFT OUTER JOIN HostAliases "
		        "ON Host.hostname = HostAliases.hostname "
		    "WHERE Host.hostname = $1 OR hostalias = $1 "
		    "GROUP BY Host.hostname "
		    "ORDER BY Host.hostname",

		"SELECT Host.hostname, architecture, ncpu, hostalias "
		    "FROM Host LEFT OUTER JOIN HostAliases "
		        "ON Host.hostname = HostAliases.hostname "
		    "WHERE Host.hostname = $1 OR hostalias = $1 "
		    "ORDER BY Host.hostname, hostalias",

		1,
		paramValues,
		&n,
		&infos);
	if (e != NULL)	
		return (e);
	if (n == 0)
		return (GFARM_ERR_UNKNOWN_HOST);
	if (n != 1 || infos[0].nhostaliases > 1)
		return (GFARM_ERR_AMBIGUOUS_RESULT);
	*info = infos[0];
	free(infos);
	return (NULL);
}

static char *
gfarm_pgsql_host_info_get_allhost_by_architecture(const char *architecture,
	int *np, struct gfarm_host_info **infosp)
{
	char *e;
	const char *paramValues[1];

	if ((e = gfarm_pgsql_check()) != NULL)
		return (e);

	paramValues[0] = architecture;
	return (host_info_get_all( 
		"SELECT Host.hostname, count(hostalias) "
		    "FROM Host LEFT OUTER JOIN HostAliases "
		        "ON Host.hostname = HostAliases.hostname "
		    "WHERE architecture = $1 "
		    "GROUP BY Host.hostname "
		    "ORDER BY Host.hostname",

		"SELECT Host.hostname, architecture, ncpu, hostalias "
		    "FROM Host LEFT OUTER JOIN HostAliases "
		        "ON Host.hostname = HostAliases.hostname "
		    "WHERE architecture = $1 "
		    "ORDER BY Host.hostname, hostalias",

		1,
		paramValues,
		np,
		infosp));
}

/**********************************************************************/

static char *
gfarm_pgsql_path_info_get(
	const char *pathname,
	struct gfarm_path_info *info)
{
	char *e;

	if ((e = gfarm_pgsql_check()) != NULL)
		return (e);
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

static char *
gfarm_pgsql_path_info_set(
	char *pathname,
	struct gfarm_path_info *info)
{
	char *e;

	if ((e = gfarm_pgsql_check()) != NULL)
		return (e);
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

static char *
gfarm_pgsql_path_info_replace(
	char *pathname,
	struct gfarm_path_info *info)
{
	char *e;

	if ((e = gfarm_pgsql_check()) != NULL)
		return (e);
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

static char *
gfarm_pgsql_path_info_remove(const char *pathname)
{
	char *e;

	if ((e = gfarm_pgsql_check()) != NULL)
		return (e);
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

/* XXX - this is for a stopgap implementation of gfs_opendir() */
static char *
gfarm_pgsql_path_info_get_all_foreach(
	void (*callback)(void *, struct gfarm_path_info *),
	void *closure)
{
	char *e;

	if ((e = gfarm_pgsql_check()) != NULL)
		return (e);
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

#if 0 /* GFarmFile history isn't actually used yet */

/* get GFarmFiles which were created by the program */
static char *
gfarm_pgsql_file_history_get_allfile_by_program(
	char *program,
	int *np,
	char ***gfarm_files_p)
{
	char *e;

	if ((e = gfarm_pgsql_check()) != NULL)
		return (e);
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

/* get GFarmFiles which were created from the file as a input */
static char *
gfarm_pgsql_file_history_get_allfile_by_file(
	char *input_gfarm_file,
	int *np,
	char ***gfarm_files_p)
{
	char *e;

	if ((e = gfarm_pgsql_check()) != NULL)
		return (e);
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

#endif /* GFarmFile history isn't actually used yet */

/**********************************************************************/


static char *
gfarm_pgsql_file_section_info_get(
	const char *pathname,
	const char *section,
	struct gfarm_file_section_info *info)
{
	char *e;

	if ((e = gfarm_pgsql_check()) != NULL)
		return (e);
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

static char *
gfarm_pgsql_file_section_info_set(
	char *pathname,
	char *section,
	struct gfarm_file_section_info *info)
{
	char *e;

	if ((e = gfarm_pgsql_check()) != NULL)
		return (e);
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

static char *
gfarm_pgsql_file_section_info_replace(
	char *pathname,
	char *section,
	struct gfarm_file_section_info *info)
{
	char *e;

	if ((e = gfarm_pgsql_check()) != NULL)
		return (e);
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

static char *
gfarm_pgsql_file_section_info_remove(
	const char *pathname,
	const char *section)
{
	char *e;

	if ((e = gfarm_pgsql_check()) != NULL)
		return (e);
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

static char *
gfarm_pgsql_file_section_info_get_all_by_file(
	const char *pathname,
	int *np,
	struct gfarm_file_section_info **infosp)
{
	char *e;

	if ((e = gfarm_pgsql_check()) != NULL)
		return (e);
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

/**********************************************************************/

static char *
gfarm_pgsql_file_section_copy_info_get(
	const char *pathname,
	const char *section,
	const char *hostname,
	struct gfarm_file_section_copy_info *info)
{
	char *e;

	if ((e = gfarm_pgsql_check()) != NULL)
		return (e);
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

static char *
gfarm_pgsql_file_section_copy_info_set(
	char *pathname,
	char *section,
	char *hostname,
	struct gfarm_file_section_copy_info *info)
{
	char *e;

	if ((e = gfarm_pgsql_check()) != NULL)
		return (e);
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

static char *
gfarm_pgsql_file_section_copy_info_remove(
	const char *pathname,
	const char *section,
	const char *hostname)
{
	char *e;

	if ((e = gfarm_pgsql_check()) != NULL)
		return (e);
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

static char *
gfarm_pgsql_file_section_copy_info_get_all_by_file(
	const char *pathname,
	int *np,
	struct gfarm_file_section_copy_info **infosp)
{
	char *e;

	if ((e = gfarm_pgsql_check()) != NULL)
		return (e);
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

static char *
gfarm_pgsql_file_section_copy_info_get_all_by_section(
	const char *pathname,
	const char *section,
	int *np,
	struct gfarm_file_section_copy_info **infosp)
{
	char *e;

	if ((e = gfarm_pgsql_check()) != NULL)
		return (e);
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

static char *
gfarm_pgsql_file_section_copy_info_get_all_by_host(
	const char *hostname,
	int *np,
	struct gfarm_file_section_copy_info **infosp)
{
	char *e;

	if ((e = gfarm_pgsql_check()) != NULL)
		return (e);
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

/**********************************************************************/

#if 0 /* GFarmFile history isn't actually used yet */

static char *
gfarm_pgsql_file_history_get(
	char *gfarm_file,
	struct gfarm_file_history *info)
{
	char *e;

	if ((e = gfarm_pgsql_check()) != NULL)
		return (e);
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

static char *
gfarm_pgsql_file_history_set(
	char *gfarm_file,
	struct gfarm_file_history *info)
{
	char *e;

	if ((e = gfarm_pgsql_check()) != NULL)
		return (e);
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

static char *
gfarm_pgsql_file_history_remove(char *gfarm_file)
{
	char *e;

	if ((e = gfarm_pgsql_check()) != NULL)
		return (e);
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

#endif /* GFarmFile history isn't actually used yet */

/**********************************************************************/

const struct gfarm_metadb_internal_ops gfarm_pgsql_metadb_ops = {
	gfarm_pgsql_initialize,
	gfarm_pgsql_terminate,

	gfarm_pgsql_host_info_get,
	gfarm_pgsql_host_info_remove_hostaliases,
	gfarm_pgsql_host_info_set,
	gfarm_pgsql_host_info_replace,
	gfarm_pgsql_host_info_remove,
	gfarm_pgsql_host_info_get_all,
	gfarm_pgsql_host_info_get_by_name_alias,
	gfarm_pgsql_host_info_get_allhost_by_architecture,

	gfarm_pgsql_path_info_get,
	gfarm_pgsql_path_info_set,
	gfarm_pgsql_path_info_replace,
	gfarm_pgsql_path_info_remove,
	gfarm_pgsql_path_info_get_all_foreach,

	gfarm_pgsql_file_section_info_get,
	gfarm_pgsql_file_section_info_set,
	gfarm_pgsql_file_section_info_replace,
	gfarm_pgsql_file_section_info_remove,
	gfarm_pgsql_file_section_info_get_all_by_file,

	gfarm_pgsql_file_section_copy_info_get,
	gfarm_pgsql_file_section_copy_info_set,
	gfarm_pgsql_file_section_copy_info_remove,
	gfarm_pgsql_file_section_copy_info_get_all_by_file,
	gfarm_pgsql_file_section_copy_info_get_all_by_section,
	gfarm_pgsql_file_section_copy_info_get_all_by_host,
};
