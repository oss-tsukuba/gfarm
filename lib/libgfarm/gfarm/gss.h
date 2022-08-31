struct gfarm_gss {
	const char *protocol;
	gss_cred_id_t (*client_cred_get)(void);

	void (*gfarmGssPrintMajorStatus)(gfarm_OM_uint32);
	void (*gfarmGssPrintMinorStatus)(gfarm_OM_uint32);


	int (*gfarmGssImportName)(gss_name_t *,
				  void *,
				  size_t,
				  gss_OID,
				  gfarm_OM_uint32 *,
				  gfarm_OM_uint32 *);
	int (*gfarmGssImportNameOfHostBasedService)(gss_name_t *,
						    char *,
						    char *,
						    gfarm_OM_uint32 *,
						    gfarm_OM_uint32 *);
	int (*gfarmGssImportNameOfHost)(gss_name_t *,
					char *,
					gfarm_OM_uint32 *,
					gfarm_OM_uint32 *);
	int (*gfarmGssDeleteName)(gss_name_t *,
				  gfarm_OM_uint32 *,
				  gfarm_OM_uint32 *);
	int (*gfarmGssNewCredentialName)(gss_name_t *,
					 gss_cred_id_t,
					 gfarm_OM_uint32 *,
					 gfarm_OM_uint32 *);
	char *(*gfarmGssNewDisplayName)(const gss_name_t,
				       gfarm_OM_uint32 *,
				       gfarm_OM_uint32 *,
				       gss_OID *);

	int (*gfarmGssAcquireCredential)(gss_cred_id_t *,
					 const gss_name_t,
					 gss_cred_usage_t,
					 gfarm_OM_uint32 *,
					 gfarm_OM_uint32 *,
					 gss_name_t *);
	int (*gfarmGssDeleteCredential)(gss_cred_id_t *,
					gfarm_OM_uint32 *,
					gfarm_OM_uint32 *);

	int (*gfarmSecSessionInitializeInitiator)(char *,
						  gfarm_OM_uint32 *,
						  gfarm_OM_uint32 *);
	int (*gfarmSecSessionInitializeBoth)(char *,
					     char *,
					     gfarm_OM_uint32 *,
					     gfarm_OM_uint32 *);
	void (*gfarmSecSessionFinalizeInitiator)(void);
	void (*gfarmSecSessionFinalizeBoth)(void);

	gfarmSecSession *(*gfarmSecSessionAccept)(
		int,
		gss_cred_id_t,
		gfarmSecSessionOption *,
		int *,
		gfarm_OM_uint32 *,
		gfarm_OM_uint32 *);
	gfarmSecSession *(*gfarmSecSessionInitiate)(int,
						    const gss_name_t,
						    gss_cred_id_t,
						    gfarm_OM_uint32,
						    gfarmSecSessionOption *,
						    int *,
						    gfarm_OM_uint32 *,
						    gfarm_OM_uint32 *);
	struct gfarmSecSessionInitiateState *(*gfarmSecSessionInitiateRequest)(
		struct gfarm_eventqueue *,
		int,
		const gss_name_t,
		gss_cred_id_t,
		gfarm_OM_uint32,
		gfarmSecSessionOption *,
		void (*)(void *),
		void *,
		gfarm_OM_uint32 *,
		gfarm_OM_uint32 *);
	gfarmSecSession *(*gfarmSecSessionInitiateResult)(
		struct gfarmSecSessionInitiateState *,
		gfarm_OM_uint32 *,
		gfarm_OM_uint32 *);
	void (*gfarmSecSessionTerminate)(gfarmSecSession *);

	int (*gfarmSecSessionReceiveInt8)(gfarmSecSession *,
					  gfarm_int8_t **,
					  int *,
					  int);
	int (*gfarmSecSessionSendInt8)(gfarmSecSession *,
				       gfarm_int8_t *,
				       int,
				       int);

	int (*gfarmSecSessionGetInitiatorDistName)(gfarmSecSession *, char **);
};
