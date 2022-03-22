void group_init(void);
void group_initial_entry(void);

struct user;
struct group;
struct process;

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

struct group *group_tenant_lookup_including_invalid(const char *);
struct group *group_tenant_lookup(const char *);
struct group *group_tenant_lookup_or_enter_invalid(const char *);
struct group *group_lookup_in_tenant_including_invalid(
	const char *, struct tenant *);
struct group *group_lookup_in_tenant(const char *, struct tenant *);
gfarm_error_t grpassign_add(struct user *, struct group *);
void grpassign_remove(struct group_assignment *);
char *group_name(struct group *);
char *group_tenant_name_even_invalid(struct group *);
char *group_tenant_name(struct group *);
char *group_name_in_tenant_even_invalid(struct group *, struct process *);
char *group_name_in_tenant(struct group *, struct process *);
const char *group_get_tenant_name(struct group *);
int group_is_invalid(struct group *);
int group_is_valid(struct group *);

#define GROUP_FOREARCH_FLAG_INCLUDING_INVALID	0
#define GROUP_FOREARCH_FLAG_VALID_ONLY		1
void group_foreach_in_all_tenants(
	void *, void (*)(void *, struct group *), int);
void group_foreach_in_tenant(void *, void (*)(void *, struct group *),
	struct tenant *, int);

struct gfarm_group_info;
gfarm_error_t group_info_add(struct gfarm_group_info *);
gfarm_error_t group_user_tenant_check(struct gfarm_group_info *, const char *);
void group_modify(struct group *, struct gfarm_group_info *, const char *);
gfarm_error_t group_tenant_remove_in_cache(const char *);

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
