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

#define quota_limit_is_valid(val)			\
	((val >= 0 && val <= GFARM_INT64_MAX) ? 1 : 0)
