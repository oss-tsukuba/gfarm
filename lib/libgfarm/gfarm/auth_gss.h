struct gfarm_gss;

gfarm_error_t gfarm_gss_cred_config_convert_to_name(struct gfarm_gss *,
	enum gfarm_auth_cred_type, char *, char *, char *, gss_name_t *);
gfarm_error_t gfarm_gss_cred_name_for_server(struct gfarm_gss *,
	const char *, const char *, gss_name_t *);

void gfarm_gsi_client_cred_set(gss_cred_id_t);
gss_cred_id_t gfarm_gsi_client_cred_get(void);

void gfarm_kerberos_client_cred_set(gss_cred_id_t);
gss_cred_id_t gfarm_kerberos_client_cred_get(void);
