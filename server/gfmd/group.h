void group_init(void);

struct user;
struct group;

struct group_assignment {
	/* end marker: {ga->user_prev, ga->user_next} == &ga->g->users */
	struct group_assignment *user_prev, *user_next;
	
	/* end marker: {ga->group_prev, ga->group_next} == &ga->u->groups */
	struct group_assignment *group_prev, *group_next;

	struct user *u;
	struct group *g;
};

extern char ADMIN_GROUP_NAME[];

struct group *group_lookup(const char *);
gfarm_error_t grpassign_add(struct user *, struct group *);
void grpassign_remove(struct group_assignment *);
char *group_name(struct group *);


struct peer;
gfarm_error_t gfm_server_group_info_get_all(struct peer *, int, int);
gfarm_error_t gfm_server_group_info_get_by_names(struct peer *, int, int);
gfarm_error_t gfm_server_group_info_set(struct peer *, int, int);
gfarm_error_t gfm_server_group_info_modify(struct peer *, int, int);
gfarm_error_t gfm_server_group_info_remove(struct peer *, int, int);
gfarm_error_t gfm_server_group_info_add_users(struct peer *, int, int);
gfarm_error_t gfm_server_group_info_remove_users(struct peer *, int, int);
gfarm_error_t gfm_server_group_names_get_by_users(struct peer *, int, int);
