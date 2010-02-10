 /*
  * This header exports internal interface of db_pgsql module
  * for a use from a private extension.
  *
  * The official gfmd source code shouldn't include this header.
  */

int32_t pgsql_get_int32(PGresult *, int, const char *);
char *pgsql_get_string(PGresult *, int, const char *);
gfarm_error_t gfarm_pgsql_check_update_or_delete(PGresult *,
	const char *, const char *);

gfarm_error_t gfarm_pgsql_start_with_retry(const char *);
gfarm_error_t gfarm_pgsql_commit(const char *);
gfarm_error_t gfarm_pgsql_rollback(const char *);

gfarm_error_t gfarm_pgsql_insert_with_retry(const char *,
	int, const Oid *, const char *const *, const int *, const int *,
	int, const char *);
gfarm_error_t gfarm_pgsql_update_or_delete_with_retry(const char *,
	int, const Oid *, const char *const *, const int *, const int *,
	int, const char *);

gfarm_error_t gfarm_pgsql_generic_get_all(const char *,
	int, const char **, int *, void *,
	const struct gfarm_base_generic_info_ops *,
	gfarm_error_t (*)(PGresult *, int, void *),
	const char *);
gfarm_error_t gfarm_pgsql_generic_grouping_get_all(const char *, const char *,
	int, const char **, int *, void *,
	const struct gfarm_base_generic_info_ops *,
	gfarm_error_t (*)(PGresult *, int, int, void *),
	const char *);

PGconn *gfarm_pgsql_get_conn(void);
