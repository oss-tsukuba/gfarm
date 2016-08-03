/*
 * $Id$
 */

#define GFARM_QUOTA_INVALID -1
#define GFARM_QUOTA_NOT_UPDATE -2

/* for db */
struct gfarm_quota_info {
	char *name;
	gfarm_time_t    grace_period;
	gfarm_off_t     space;
	gfarm_time_t    space_exceed;
	gfarm_off_t     space_soft;
	gfarm_off_t     space_hard;
	gfarm_uint64_t  num;
	gfarm_time_t    num_exceed;
	gfarm_uint64_t  num_soft;
	gfarm_uint64_t  num_hard;
	gfarm_off_t     phy_space;
	gfarm_time_t    phy_space_exceed;
	gfarm_off_t     phy_space_soft;
	gfarm_off_t     phy_space_hard;
	gfarm_uint64_t  phy_num;
	gfarm_time_t    phy_num_exceed;
	gfarm_uint64_t  phy_num_soft;
	gfarm_uint64_t  phy_num_hard;
};

/* for getting quota */
struct gfarm_quota_get_info {
	char *name;
	gfarm_time_t    grace_period;
	gfarm_off_t     space;
	gfarm_time_t    space_grace;
	gfarm_off_t     space_soft;
	gfarm_off_t     space_hard;
	gfarm_uint64_t  num;
	gfarm_time_t    num_grace;
	gfarm_uint64_t  num_soft;
	gfarm_uint64_t  num_hard;
	gfarm_off_t     phy_space;
	gfarm_time_t    phy_space_grace;
	gfarm_off_t     phy_space_soft;
	gfarm_off_t     phy_space_hard;
	gfarm_uint64_t  phy_num;
	gfarm_time_t    phy_num_grace;
	gfarm_uint64_t  phy_num_soft;
	gfarm_uint64_t  phy_num_hard;
};

/* for setting quota */
struct gfarm_quota_set_info {
	char *name;
	gfarm_time_t    grace_period;
	gfarm_off_t     space_soft;
	gfarm_off_t     space_hard;
	gfarm_uint64_t  num_soft;
	gfarm_uint64_t  num_hard;
	gfarm_off_t     phy_space_soft;
	gfarm_off_t     phy_space_hard;
	gfarm_uint64_t  phy_num_soft;
	gfarm_uint64_t  phy_num_hard;
};

void gfarm_quota_info_free(struct gfarm_quota_info *);
void gfarm_quota_get_info_free(struct gfarm_quota_get_info *);
void gfarm_quota_set_info_free(struct gfarm_quota_set_info *);

/*
 * for dirquota (should be used for user/group quota in future)
 */

/* for soft, hard and usage */
struct gfarm_quota_subject_info {
	gfarm_off_t	space;
	gfarm_uint64_t	num;
	gfarm_off_t	phy_space;
	gfarm_uint64_t	phy_num;
};

/* for grace and exceed */
struct gfarm_quota_subject_time {
	gfarm_time_t	space_time;
	gfarm_time_t	num_time;
	gfarm_time_t	phy_space_time;
	gfarm_time_t	phy_num_time;
};

struct gfarm_quota_limit_info {
	gfarm_time_t grace_period;
	struct gfarm_quota_subject_info soft;
	struct gfarm_quota_subject_info hard;
};

struct gfarm_dirset_info {
	char *username;
	char *dirsetname;
};

void gfarm_dirset_info_free(struct gfarm_dirset_info *);

struct gfarm_dirset_dir_info {
	struct gfarm_dirset_info dirset;
	char *pathname;
};

void gfarm_dirset_dir_info_free(struct gfarm_dirset_dir_info *);


#define quota_limit_is_valid(val)			\
	((val >= 0 && val <= GFARM_INT64_MAX) ? 1 : 0)
