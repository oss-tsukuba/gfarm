/* interface that GSI and Kerberos provide to libgfarm/gfarm/ */

struct gfarm_gss;

struct gfarm_gss *gfarm_gss_gsi(void);
void gfarm_gsi_client_cred_set(gss_cred_id_t);
gss_cred_id_t gfarm_gsi_client_cred_get(void);

struct gfarm_gss *gfarm_gss_kerberos(void);
void gfarm_kerberos_client_cred_set(gss_cred_id_t);
gss_cred_id_t gfarm_kerberos_client_cred_get(void);
