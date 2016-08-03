/*
 * $Id$
 */

/* related to struct user and struct group */
struct quota {
	int on_db;  /* 0: not exist on database and disable quota */
	gfarm_time_t    grace_period;
	gfarm_off_t     space; /* -1: disable, only memory */
	gfarm_time_t    space_exceed;
	gfarm_off_t     space_soft;
	gfarm_off_t     space_hard;
	gfarm_uint64_t  num; /* only memory */
	gfarm_time_t    num_exceed;
	gfarm_uint64_t  num_soft;
	gfarm_uint64_t  num_hard;
	gfarm_off_t     phy_space; /* only memory */
	gfarm_time_t    phy_space_exceed;
	gfarm_off_t     phy_space_soft;
	gfarm_off_t     phy_space_hard;
	gfarm_uint64_t  phy_num; /* only memory */
	gfarm_time_t    phy_num_exceed;
	gfarm_uint64_t  phy_num_soft;
	gfarm_uint64_t  phy_num_hard;
};

void quota_init(void);
void quota_dir_init(void);

void quota_data_init(struct quota *);

/* on gfmd memory, and on backend database (only used for dirquota for now) */
struct quota_metadata {
	struct gfarm_quota_limit_info limit;
	struct gfarm_quota_subject_info usage; /* backend is not up-to-date */
	struct gfarm_quota_subject_time exceed;
};

void quota_usage_init(struct gfarm_quota_subject_info *);
void quota_subject_time_init(struct gfarm_quota_subject_time *);
void quota_metadata_init(struct quota_metadata *);
void quota_usage_clear(struct gfarm_quota_subject_info *);
int quota_is_checked(const struct gfarm_quota_subject_info *);
void quota_exceed_to_grace(gfarm_time_t,
	const struct gfarm_quota_subject_time *,
	struct gfarm_quota_subject_time *);


struct user;
struct group;
struct dirset;
gfarm_error_t quota_limit_check(struct user *, struct group *, struct dirset *,
	int, int, gfarm_off_t);
gfarm_error_t dirquota_limit_check(struct dirset *, int, int, gfarm_off_t);
void quota_user_remove(struct user *);
void quota_group_remove(struct group *);

struct inode;
void quota_update_file_add(struct inode *, struct dirset *);
void quota_update_file_resize(struct inode *, struct dirset *, gfarm_off_t);
void quota_update_replica_add(struct inode *, struct dirset *);
void quota_update_replica_remove(struct inode *, struct dirset *);
void quota_update_file_remove(struct inode *, struct dirset *);
void dirquota_update_file_add(struct inode *, struct dirset *);
void dirquota_update_file_remove(struct inode *, struct dirset *);
gfarm_error_t quota_lookup(const char *, int, struct quota **, const char *);

void quota_check_init(void);
void quota_check_start(void);
void quota_dir_check_schedule(void);
void quota_dir_check_start(void);

struct peer;
gfarm_error_t gfm_server_quota_user_get(struct peer *, int, int);
gfarm_error_t gfm_server_quota_user_set(struct peer *, int, int);
gfarm_error_t gfm_server_quota_group_get(struct peer *, int, int);
gfarm_error_t gfm_server_quota_group_set(struct peer *, int, int);
gfarm_error_t gfm_server_quota_check(struct peer *, int, int);
