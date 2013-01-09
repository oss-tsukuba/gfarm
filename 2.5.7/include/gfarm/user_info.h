struct gfarm_user_info {
	char *username;

	char *realname;
	char *homedir;
	char *gsi_dn;
};

void gfarm_user_info_free(struct gfarm_user_info *);
void gfarm_user_info_free_all(int, struct gfarm_user_info *);
