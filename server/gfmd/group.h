void group_init(void);
void group_initial_entry(void);

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

extern char ADMIN_GROUP_NAME[]; /* can modify host/user/group info of gfarm */
extern char ROOT_GROUP_NAME[]; /* can modify any data/metadata in gfarmfs */

struct group *group_lookup_including_invalid(const char *);
struct group *group_lookup_or_enter_invalid(const char *);
struct group *group_lookup(const char *);
gfarm_error_t grpassign_add(struct user *, struct group *);
void grpassign_remove(struct group_assignment *);
char *group_name(struct group *);
char *group_name_with_invalid(struct group *);
int group_is_invalid(struct group *);
int group_is_valid(struct group *);

void group_foreach(void *, void (*)(void *, struct group *), int);

struct gfarm_group_info;
gfarm_error_t group_info_add(struct gfarm_group_info *);
gfarm_error_t group_user_check(struct gfarm_group_info *, const char *);
void group_modify(struct group *, struct gfarm_group_info *, const char *);
gfarm_error_t group_remove_in_cache(const char *);

struct quota;
struct quota *group_quota(struct group *);
struct gfarm_quota_subject_info;
struct gfarm_quota_subject_info *group_usage_tmp(struct group *);

struct peer;
gfarm_error_t gfm_server_group_info_get_all(struct peer *, int, int);
gfarm_error_t gfm_server_group_info_get_by_names(struct peer *, int, int);
gfarm_error_t gfm_server_group_info_set(struct peer *, int, int);
gfarm_error_t gfm_server_group_info_modify(struct peer *, int, int);
gfarm_error_t gfm_server_group_info_remove(struct peer *, int, int);
gfarm_error_t gfm_server_group_info_add_users(struct peer *, int, int);
gfarm_error_t gfm_server_group_info_remove_users(struct peer *, int, int);
gfarm_error_t gfm_server_group_names_get_by_users(struct peer *, int, int);


/* exported for a use from a private extension */
gfarm_error_t group_info_remove_default(const char *, const char *);
extern gfarm_error_t (*group_info_remove)(const char *, const char *);
