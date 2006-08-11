struct gfarm_group_info {
	char *groupname;

	int nusers;
	char **usernames;
};

struct gfarm_group_names {
	int ngroups;
	char **groupnames;
};

void gfarm_group_info_free(struct gfarm_group_info *);
void gfarm_group_info_free_all(int, struct gfarm_group_info *);
void gfarm_group_names_free(struct gfarm_group_names *);
