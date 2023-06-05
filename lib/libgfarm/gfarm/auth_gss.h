/*
 * interface that lib/gfarm/gfarm/auth_common_gss.c provides
 * to GSI and Kerberos
 */

struct gfarm_gss;
struct gfarm_auth_common_gss_static;

struct gfarm_auth_gss_ops {
	gss_cred_id_t (*client_cred_get)(void);
	void (*client_cred_set_failed)(void);
	gfarm_error_t (*client_cred_check_failed)(void);
};

gfarm_error_t gfarm_auth_common_gss_static_init(
	struct gfarm_auth_common_gss_static **, const char *);
void gfarm_auth_common_gss_static_term(
	struct gfarm_auth_common_gss_static *, const char *);

char *gfarm_gss_client_cred_name(struct gfarm_gss *,
	struct gfarm_auth_common_gss_static *);

void gfarm_auth_gss_client_cred_set(
	struct gfarm_auth_common_gss_static *, gss_cred_id_t cred);
gss_cred_id_t gfarm_auth_gss_client_cred_get(
	struct gfarm_auth_common_gss_static *);
void gfarm_auth_gss_client_cred_set_failed(struct gfarm_gss *,
	struct gfarm_auth_common_gss_static *);
gfarm_error_t gfarm_auth_gss_client_cred_check_failed(struct gfarm_gss *,
	struct gfarm_auth_common_gss_static *);

gfarm_error_t gfarm_auth_client_method_gss_protocol_available(
	struct gfarm_gss *, struct gfarm_auth_common_gss_static *);

gfarm_error_t gfarm_gss_cred_config_convert_to_name(struct gfarm_gss *,
	enum gfarm_auth_cred_type, char *, char *, char *, gss_name_t *);
gfarm_error_t gfarm_gss_cred_name_for_server(struct gfarm_gss *,
	const char *, const char *, gss_name_t *);
