gfarm_error_t gfarm_gsi_cred_config_convert_to_name(
	enum gfarm_auth_cred_type, char *, char *, char *, gss_name_t *);

void gfarm_gsi_client_cred_set(gss_cred_id_t);
gss_cred_id_t gfarm_gsi_client_cred_get(void);
