/*
 * $Id$
 */

#include <pthread.h>	/* db_access.h currently needs this */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <assert.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"
#include "thrsubr.h"

#include "auth.h"
#include "quota_info.h"
#include "config.h"

#include "uint64_map.h"
#include "peer.h"
#include "subr.h"
#include "rpcsubr.h"
#include "user.h"
#include "group.h"
#include "inode.h"
#include "dir.h"
#include "quota.h"
#include "dirset.h"
#include "quota_dir.h"
#include "process.h"
#include "db_access.h"

#define QUOTA_NOT_CHECK_YET -1
#define is_checked(q) ((q)->space != QUOTA_NOT_CHECK_YET)

static gfarm_error_t db_state = GFARM_ERR_NO_ERROR;

/* private functions */
static void
update_softlimit(gfarm_time_t *exceedp, gfarm_time_t now, gfarm_time_t grace,
	gfarm_int64_t val, gfarm_int64_t soft, int *need_db_update)
{
	if (!quota_limit_is_valid(grace) /* disable all softlimit */ ||
	    !quota_limit_is_valid(soft) /* disable this softlimit */ ||
	    val <= soft /* not exceed */
		) {
		if (*exceedp != GFARM_QUOTA_INVALID) {
			*exceedp = GFARM_QUOTA_INVALID;
			*need_db_update = 1;
		}
	} else if (*exceedp >= 0)
		return; /* already exceeded */
	else if (val > soft) {
		*exceedp = now; /* exceed now */
		*need_db_update = 1;
	}
}

static void
quota_softlimit_exceed(struct quota *q, int *need_db_update)
{
	struct timeval now;

	if (!quota_limit_is_valid(q->grace_period)) {
		if (q->space_exceed != GFARM_QUOTA_INVALID ||
		    q->num_exceed != GFARM_QUOTA_INVALID ||
		    q->phy_space_exceed != GFARM_QUOTA_INVALID ||
		    q->phy_num_exceed != GFARM_QUOTA_INVALID) {
			/* disable all softlimit */
			q->space_exceed = GFARM_QUOTA_INVALID;
			q->num_exceed = GFARM_QUOTA_INVALID;
			q->phy_space_exceed = GFARM_QUOTA_INVALID;
			q->phy_num_exceed = GFARM_QUOTA_INVALID;
			*need_db_update = 1;
		}
		return;
	}

	/* update exceeded time of softlimit */
	gettimeofday(&now, NULL);
	update_softlimit(&q->space_exceed, now.tv_sec, q->grace_period,
			 q->space, q->space_soft, need_db_update);
	update_softlimit(&q->num_exceed, now.tv_sec, q->grace_period,
			 q->num, q->num_soft, need_db_update);
	update_softlimit(&q->phy_space_exceed, now.tv_sec, q->grace_period,
			 q->phy_space, q->phy_space_soft, need_db_update);
	update_softlimit(&q->phy_num_exceed, now.tv_sec, q->grace_period,
			 q->phy_num, q->phy_num_soft, need_db_update);
}

static void
quota_metadata_softlimit_exceed(struct quota_metadata *q, int *need_db_update)
{
	struct timeval now;

	if (!quota_limit_is_valid(q->limit.grace_period)) {
		if (q->exceed.space_time != GFARM_QUOTA_INVALID ||
		    q->exceed.num_time != GFARM_QUOTA_INVALID ||
		    q->exceed.phy_space_time != GFARM_QUOTA_INVALID ||
		    q->exceed.phy_num_time != GFARM_QUOTA_INVALID) {
			/* disable all softlimit */
			q->exceed.space_time = GFARM_QUOTA_INVALID;
			q->exceed.num_time = GFARM_QUOTA_INVALID;
			q->exceed.phy_space_time = GFARM_QUOTA_INVALID;
			q->exceed.phy_num_time = GFARM_QUOTA_INVALID;
			*need_db_update = 1;
		}
		return;
	}

	/* update exceeded time of softlimit */
	gettimeofday(&now, NULL);
	update_softlimit(&q->exceed.space_time, now.tv_sec,
	    q->limit.grace_period, q->usage.space,
	    q->limit.soft.space, need_db_update);
	update_softlimit(&q->exceed.num_time, now.tv_sec,
	    q->limit.grace_period, q->usage.num,
	    q->limit.soft.num, need_db_update);
	update_softlimit(&q->exceed.phy_space_time, now.tv_sec,
	    q->limit.grace_period, q->usage.phy_space,
	    q->limit.soft.phy_space, need_db_update);
	update_softlimit(&q->exceed.phy_num_time, now.tv_sec,
	    q->limit.grace_period, q->usage.phy_num,
	    q->limit.soft.phy_num, need_db_update);
}

static void
quota_softlimit_exceed_user(struct quota *q, struct user *u)
{
	gfarm_error_t e;
	int need_db_update = 0;

	quota_softlimit_exceed(q, &need_db_update);
	if (need_db_update && user_is_valid(u)) {
		e = db_quota_user_set(q, user_tenant_name(u));
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1004505,
			    "db_quota_user_set(%s): %s",
			    user_tenant_name(u), gfarm_error_string(e));
	}
}

static void
quota_softlimit_exceed_group(struct quota *q, struct group *g)
{
	gfarm_error_t e;
	int need_db_update = 0;

	quota_softlimit_exceed(q, &need_db_update);
	if (need_db_update && group_is_valid(g)) {
		e = db_quota_group_set(q, group_tenant_name(g));
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1004506,
			    "db_quota_group_set(%s): %s",
			    group_tenant_name(g), gfarm_error_string(e));
	}
}

void
dirquota_softlimit_exceed(struct quota_metadata *q, struct dirset *ds)
{
	gfarm_error_t e;
	int need_db_update = 0;

	quota_metadata_softlimit_exceed(q, &need_db_update);
	if (need_db_update && dirset_is_valid(ds)) {
		e = db_quota_dirset_modify(
		    dirset_get_username(ds), dirset_get_dirsetname(ds), q);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1004634,
			    "db_quota_dirset_modify(%s:%s): %s",
			    dirset_get_username(ds), dirset_get_dirsetname(ds),
			    gfarm_error_string(e));
	}
}

static void
quota_info_usage_clear(struct gfarm_quota_info *usage)
{
	usage->space = 0;
	usage->num = 0;
	usage->phy_space = 0;
	usage->phy_num = 0;
}

static void
quota_usage_clear(struct gfarm_quota_subject_info *usage)
{
	usage->space = 0;
	usage->num = 0;
	usage->phy_space = 0;
	usage->phy_num = 0;
}

static void
usage_tmp_clear_user(void *closure, struct user *u)
{
	quota_usage_clear(user_usage_tmp(u));
}

static void
usage_tmp_clear_group(void *closure, struct group *g)
{
	quota_usage_clear(group_usage_tmp(g));
}

static void
usage_tmp_clear(void)
{
	user_foreach_in_all_tenants(NULL, usage_tmp_clear_user,
	    USER_FOREARCH_FLAG_INCLUDING_INVALID);
	group_foreach_in_all_tenants(NULL, usage_tmp_clear_group,
	    GROUP_FOREARCH_FLAG_INCLUDING_INVALID);
}

static void
usage_to_quota(struct gfarm_quota_subject_info *src_usage, struct quota *dst,
	const char *type, const char *name)
{
	if (dst->space != src_usage->space ||
	    dst->num != src_usage->num ||
	    dst->phy_space != src_usage->phy_space ||
	    dst->phy_num != src_usage->phy_num) {
		gflog_warning(GFARM_MSG_1005160,
		    "%s %s: unexpected quota update: "
		    "space:%llu->%llu, num:%llu->%llu, "
		    "phy_space:%llu->%llu, phy_num:%llu->%llu",
		    type, name,
		    (long long)dst->space, (long long)src_usage->space,
		    (long long)dst->num, (long long)src_usage->num,
		    (long long)dst->phy_space, (long long)src_usage->phy_space,
		    (long long)dst->phy_num, (long long)src_usage->phy_num);
		dst->space = src_usage->space;
		dst->num = src_usage->num;
		dst->phy_space = src_usage->phy_space;
		dst->phy_num = src_usage->phy_num;
	}
}

static void
quota_update_usage_user(void *closure, struct user *u)
{
	struct quota *q = user_quota(u);
	struct gfarm_quota_subject_info *usage_tmp = user_usage_tmp(u);

	usage_to_quota(usage_tmp, q, "user", user_tenant_name(u));
	quota_softlimit_exceed_user(q, u);

#if 0 /* this is OK since gfarm-2.7.17 */
	if (!user_is_valid(u))
		gflog_notice(GFARM_MSG_1004294,
		    "quota_check: removed user(%s), Usage: "
		    "space=%lld, inodes=%lld, phys_space=%lld, phys_num=%lld",
		    user_tenant_name_even_invalid(u),
		    (long long)q->space, (long long)q->num,
		    (long long)q->phy_space, (long long)q->phy_num);
#endif
}

static void
quota_update_usage_group(void *closure, struct group *g)
{
	struct quota *q = group_quota(g);
	struct gfarm_quota_subject_info *usage_tmp = group_usage_tmp(g);

	usage_to_quota(usage_tmp, q, "group", group_tenant_name(g));
	quota_softlimit_exceed_group(q, g);

#if 0 /* this is OK since gfarm-2.7.17 */
	if (!group_is_valid(g))
		gflog_notice(GFARM_MSG_1004295,
		    "quota_check: removed group(%s), Usage: "
		    "space=%lld, inodes=%lld, phys_space=%lld, phys_num=%lld",
		    group_tenant_name_even_invalid(g),
		    (long long)q->space, (long long)q->num,
		    (long long)q->phy_space, (long long)q->phy_num);
#endif
}

static void
quota_update_usage(void)
{
	user_foreach_in_all_tenants(NULL, quota_update_usage_user,
	    USER_FOREARCH_FLAG_INCLUDING_INVALID);
	group_foreach_in_all_tenants(NULL, quota_update_usage_group,
	    GROUP_FOREARCH_FLAG_INCLUDING_INVALID);
}

static gfarm_time_t
calculate_grace_period(gfarm_time_t exceeded_time,
		       gfarm_time_t grace_period,
		       gfarm_time_t now)
{
	gfarm_time_t val;

	if (!quota_limit_is_valid(grace_period) ||
	    !quota_limit_is_valid(exceeded_time))
		return (GFARM_QUOTA_INVALID); /* disable softlimit */

	val = grace_period - (now - exceeded_time);
	if (val < 0)
		return (0); /* expired */

	return (val); /* grace period until expiration */
}

void
quota_exceed_to_grace(gfarm_time_t grace_period,
	const struct gfarm_quota_subject_time *exceed,
	struct gfarm_quota_subject_time *grace)
{
	struct timeval now;

	gettimeofday(&now, NULL);

	grace->space_time = calculate_grace_period(
	    exceed->space_time, grace_period, now.tv_sec);
	grace->num_time = calculate_grace_period(
	    exceed->num_time, grace_period, now.tv_sec);
	grace->phy_space_time = calculate_grace_period(
	    exceed->phy_space_time, grace_period, now.tv_sec);
	grace->phy_num_time = calculate_grace_period(
	    exceed->phy_num_time, grace_period, now.tv_sec);
}

static void
quota_convert_1(const struct quota *q, const char *name,
		struct gfarm_quota_get_info *qi)
{
	struct timeval now;

	gettimeofday(&now, NULL);

	qi->name = (char *)name;
	qi->grace_period = q->grace_period;
	qi->space = q->space;
	qi->space_grace = calculate_grace_period(
		q->space_exceed, q->grace_period, now.tv_sec);
	qi->space_soft = q->space_soft;
	qi->space_hard = q->space_hard;
	qi->num = q->num;
	qi->num_grace = calculate_grace_period(
		q->num_exceed, q->grace_period, now.tv_sec);
	qi->num_soft = q->num_soft;
	qi->num_hard =  q->num_hard;
	qi->phy_space = q->phy_space;
	qi->phy_space_grace = calculate_grace_period(
		q->phy_space_exceed, q->grace_period, now.tv_sec);
	qi->phy_space_soft = q->phy_space_soft;
	qi->phy_space_hard = q->phy_space_hard;
	qi->phy_num = q->phy_num;
	qi->phy_num_grace = calculate_grace_period(
		q->phy_num_exceed, q->grace_period, now.tv_sec);
	qi->phy_num_soft = q->phy_num_soft;
	qi->phy_num_hard = q->phy_num_hard;
}

static void
quota_convert_2(const struct gfarm_quota_info *qi, struct quota *q)
{
	q->grace_period = qi->grace_period;
	q->space = qi->space;
	q->space_exceed = qi->space_exceed;
	q->space_soft = qi->space_soft;
	q->space_hard = qi->space_hard;
	q->num = qi->num;
	q->num_exceed = qi->num_exceed;
	q->num_soft = qi->num_soft;
	q->num_hard =  qi->num_hard;
	q->phy_space = qi->phy_space;
	q->phy_space_exceed = qi->phy_space_exceed;
	q->phy_space_soft = qi->phy_space_soft;
	q->phy_space_hard = qi->phy_space_hard;
	q->phy_num = qi->phy_num;
	q->phy_num_exceed = qi->phy_num_exceed;
	q->phy_num_soft = qi->phy_num_soft;
	q->phy_num_hard = qi->phy_num_hard;
}

static void
quota_user_remove_db(const char *name)
{
	gfarm_error_t e = db_quota_user_remove(name);

	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1000412,
			    "db_quota_user_remove(%s) %s",
			    name, gfarm_error_string(e));
}

static void
quota_group_remove_db(const char *name)
{
	gfarm_error_t e = db_quota_group_remove(name);

	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1000413,
			    "db_quota_group_remove(%s) %s",
			    name, gfarm_error_string(e));
}

/* public functions */
static void
quota_user_set_one_from_db(void *closure, struct gfarm_quota_info *qi)
{
	struct user *u;

	if (qi->name == NULL)
		return;
	u = user_tenant_lookup_including_invalid(qi->name);
	if (u == NULL) {
		quota_user_remove_db(qi->name);
	} else {
		struct quota *q = user_quota(u);

		quota_info_usage_clear(qi); /* usage in DB cannot be trusted */
		quota_convert_2(qi, q);
		q->on_db = 1; /* load from db */
	}
	gfarm_quota_info_free(qi);
}

static void
quota_group_set_one_from_db(void *closure, struct gfarm_quota_info *qi)
{
	struct group *g;

	if (qi->name == NULL)
		return;
	g = group_tenant_lookup_including_invalid(qi->name);
	if (g == NULL) {
		quota_group_remove_db(qi->name);
	} else {
		struct quota *q = group_quota(g);

		quota_info_usage_clear(qi); /* usage in DB cannot be trusted */
		quota_convert_2(qi, q);
		q->on_db = 1; /* load from db */
	}
	gfarm_quota_info_free(qi);
}

void
quota_init(void)
{
	gfarm_error_t e;

	e = db_quota_user_load(NULL, quota_user_set_one_from_db);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1000414,
			    "db_quota_user_load() %s", gfarm_error_string(e));
		db_state = e;
	}
	e = db_quota_group_load(NULL, quota_group_set_one_from_db);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1000415,
			    "db_quota_group_load() %s", gfarm_error_string(e));
		db_state = e;
	}
}

void
quota_data_init(struct quota *q)
{
	/* gfarmadm do not execute gfedquota yet */
	q->on_db = 0;
	/* disable all softlimit */
	q->grace_period = GFARM_QUOTA_INVALID;
	/* gfarmadm do not execute gfquotacheck yet */
	q->space = 0;
	q->space_exceed = GFARM_QUOTA_INVALID;
	q->space_soft = GFARM_QUOTA_INVALID;
	q->space_hard = GFARM_QUOTA_INVALID;
	q->num = 0;
	q->num_exceed = GFARM_QUOTA_INVALID;
	q->num_soft = GFARM_QUOTA_INVALID;
	q->num_hard = GFARM_QUOTA_INVALID;
	q->phy_space = 0;
	q->phy_space_exceed = GFARM_QUOTA_INVALID;
	q->phy_space_soft = GFARM_QUOTA_INVALID;
	q->phy_space_hard = GFARM_QUOTA_INVALID;
	q->phy_num = 0;
	q->phy_num_exceed = GFARM_QUOTA_INVALID;
	q->phy_num_soft = GFARM_QUOTA_INVALID;
	q->phy_num_hard = GFARM_QUOTA_INVALID;
}

/* for soft and hard */
static void
quota_limit_subject_init(struct gfarm_quota_subject_info *limit)
{
	limit->space = GFARM_QUOTA_INVALID;
	limit->num = GFARM_QUOTA_INVALID;
	limit->phy_space = GFARM_QUOTA_INVALID;
	limit->phy_num = GFARM_QUOTA_INVALID;
}

/* for limit */
static void
quota_limit_init(struct gfarm_quota_limit_info *limit)
{
	limit->grace_period = GFARM_QUOTA_INVALID; /* disable all softlimit */
	quota_limit_subject_init(&limit->soft);
	quota_limit_subject_init(&limit->hard);
}

/* for exceed, grace */
void
quota_subject_time_init(struct gfarm_quota_subject_time *time)
{
	time->space_time = GFARM_QUOTA_INVALID;
	time->num_time = GFARM_QUOTA_INVALID;
	time->phy_space_time = GFARM_QUOTA_INVALID;
	time->phy_num_time = GFARM_QUOTA_INVALID;
}

void
quota_metadata_init(struct quota_metadata *q)
{
	quota_limit_init(&q->limit);
	quota_usage_clear(&q->usage);
	quota_subject_time_init(&q->exceed);
}

static void
quota_metadata_memory_init(struct quota_metadata_memory *qmm)
{
	quota_metadata_init(&qmm->q);
	qmm->usage_is_valid = 0;
}

void
quota_metadata_memory_convert_to_db(
	const struct quota_metadata_memory *qmm, struct quota_metadata *q)
{
	*q = qmm->q;
	if (!qmm->usage_is_valid) {
		quota_usage_clear(&q->usage);
		q->usage.space = QUOTA_NOT_CHECK_YET;
	}
}

void
quota_metadata_memory_convert_from_db(
	struct quota_metadata_memory *qmm, const struct quota_metadata *q)
{
	qmm->q = *q;

#if 0
	qmm->usage_is_valid = is_checked(&q->usage);
	if (!qmm->usage_is_valid)
		quota_usage_clear(&qmm->q.usage);
#else /* usage in backend DB is garbage */
	quota_usage_clear(&qmm->q.usage);
	qmm->usage_is_valid = 0;
#endif
}

void
dirquota_init(struct dirquota *dq)
{
	quota_metadata_memory_init(&dq->qmm);

	/*
	 * at the first place, usage is zero inodes and zero bytes.
	 * and then, if quota_metadata_memory_convert_from_db() is called via
	 * dirset_set_quota_metadata_in_cache(), usage_is_valid becomes 0.
	 */
	dq->qmm.usage_is_valid = 1;

	dq->dirquota_checking = 0;
	dq->invalidate_requested = 0;
}

/* this is protected by giant_lock */
static int dirquota_invalidate_all_requested = 0;

int
dirquota_is_checked(const struct dirquota *dq)
{
	return (!dirquota_invalidate_all_requested &&
	    dq->qmm.usage_is_valid && !dq->invalidate_requested);
}

void
dirquota_check_retry_if_running(struct dirquota *dq)
{
	if (dq->dirquota_checking)
		dq->invalidate_requested = 1;
}

static inline gfarm_int64_t
int64_add(gfarm_int64_t orig, gfarm_int64_t diff)
{
	gfarm_int64_t val;

	if (diff == 0)
		return (orig);
	else if (diff > 0) {
		val = orig + diff;
		/*
		 * signed overflow causes undefined behavior in the C language
		 * standard, thus we have to use unsigned here.
		 */
		if ((gfarm_uint64_t)val < orig ||
		    (gfarm_uint64_t)val > GFARM_INT64_MAX) /* overflow */
			val = GFARM_INT64_MAX;
		return (val);
	} else { /* diff < 0 */
		val = orig + diff;
		if (val < 0)
			val = 0;
		return (val);
	}
}

#define update_file_add(q, size, ncopy)					\
	{								\
		(q)->space = int64_add((q)->space, size);		\
		(q)->num = int64_add((q)->num, 1);			\
		(q)->phy_space = int64_add((q)->phy_space, size * ncopy); \
		(q)->phy_num = int64_add((q)->phy_num, ncopy);		\
	}


static void
usage_tmp_update(struct inode *inode)
{
	gfarm_off_t size;
	gfarm_int64_t ncopy;
	struct user *u = inode_get_user(inode);
	struct group *g = inode_get_group(inode);

	if (inode_is_file(inode)) {
		size = inode_get_size(inode);
		ncopy = inode_get_ncopy_with_dead_host(inode);
	} else {
		size = 0;
		ncopy = 0;
	}

	if (u)
		update_file_add(user_usage_tmp(u), size, ncopy);
	if (g)
		update_file_add(group_usage_tmp(g), size, ncopy);
}

static void
dirquota_usage_tmp_update(struct dirset *ds, struct inode *inode)
{
	gfarm_off_t size;
	gfarm_int64_t ncopy;
	struct dirquota *dq = dirset_get_dirquota(ds);

	if (inode_is_file(inode)) {
		size = inode_get_size(inode);
		ncopy = inode_get_ncopy_with_dead_host(inode);
	} else {
		size = 0;
		ncopy = 0;
	}

	update_file_add(&dq->usage_tmp, size, ncopy);
}

static void quota_check_retry_if_running(void);

void
quota_update_file_add(struct inode *inode, struct dirset *tdirset)
{
	gfarm_off_t size;
	gfarm_int64_t ncopy;
	struct user *u = inode_get_user(inode);
	struct group *g = inode_get_group(inode);

	if (inode_is_file(inode)) {
		size = inode_get_size(inode);
		ncopy = inode_get_ncopy_with_dead_host(inode);
	} else {
		size = 0;
		ncopy = 0;
	}

	if (u) {
		struct quota *uq = user_quota(u);
		if (is_checked(uq)) {
			update_file_add(uq, size, ncopy);
			quota_softlimit_exceed_user(uq, u);
		}
	}
	if (g) {
		struct quota *gq = group_quota(g);
		if (is_checked(gq)) {
			update_file_add(gq, size, ncopy);
			quota_softlimit_exceed_group(gq, g);
		}
	}
	if (tdirset != TDIRSET_IS_UNKNOWN && tdirset != TDIRSET_IS_NOT_SET) {
		struct dirquota *dq = dirset_get_dirquota(tdirset);
		update_file_add(&dq->qmm.q.usage, size, ncopy);
		dirquota_softlimit_exceed(&dq->qmm.q, tdirset);
		if (!dirquota_is_checked(dq))
			dirquota_check_retry_if_running(dq);
	}

	if (debug_mode) {
		gfarm_ino_t inum;
		gfarm_int64_t gen;
		char *username, *groupname;
		inum = inode_get_number(inode);
		gen = inode_get_gen(inode);
		username = user_tenant_name(u);
		groupname = group_tenant_name(g);
		gflog_debug(GFARM_MSG_1000416,
			    "<quota_add> ino=%lld(gen=%lld): "
			    "size=%lld,ncopy=%lld,user=%s,group=%s",
			    (unsigned long long)inum, (unsigned long long)gen,
			    (unsigned long long)size, (unsigned long long)ncopy,
			    username, groupname);
	}

	quota_check_retry_if_running();
}

#define update_file_resize(q, old_size, new_size, ncopy)		\
	{								\
		gfarm_int64_t diff = new_size - old_size;		\
		(q)->space = int64_add((q)->space, diff);		\
		(q)->phy_space = int64_add((q)->phy_space, diff * ncopy); \
	}

void
quota_update_file_resize(struct inode *inode, struct dirset *tdirset,
	gfarm_off_t new_size)
{
	gfarm_off_t old_size = inode_get_size(inode);
	gfarm_int64_t ncopy = inode_get_ncopy_with_dead_host(inode);
	struct user *u = inode_get_user(inode);
	struct group *g = inode_get_group(inode);

	assert(inode_is_file(inode));

	if (u) {
		struct quota *uq = user_quota(u);
		if (is_checked(uq)) {
			update_file_resize(uq, old_size, new_size, ncopy);
			quota_softlimit_exceed_user(uq, u);
		}
	}
	if (g) {
		struct quota *gq = group_quota(g);
		if (is_checked(gq)) {
			update_file_resize(gq, old_size, new_size, ncopy);
			quota_softlimit_exceed_group(gq, g);
		}
	}
	if (tdirset != TDIRSET_IS_UNKNOWN && tdirset != TDIRSET_IS_NOT_SET) {
		struct dirquota *dq = dirset_get_dirquota(tdirset);
		update_file_resize(&dq->qmm.q.usage,
		    old_size, new_size, ncopy);
		dirquota_softlimit_exceed(&dq->qmm.q, tdirset);
		if (!dirquota_is_checked(dq))
			dirquota_check_retry_if_running(dq);
	}

	quota_check_retry_if_running();
}

#define update_replica_num(q, size, n)					\
	{								\
		(q)->phy_space = int64_add((q)->phy_space, size * n);	\
		(q)->phy_num = int64_add((q)->phy_num, n);		\
	}

static void
quota_update_replica_num(struct inode *inode, struct dirset *tdirset,
	gfarm_int64_t n)
{
	gfarm_off_t size = inode_get_size(inode);
	struct user *u = inode_get_user(inode);
	struct group *g = inode_get_group(inode);

	if (u) {
		struct quota *uq = user_quota(u);
		if (is_checked(uq)) {
			update_replica_num(uq, size, n);
			quota_softlimit_exceed_user(uq, u);
		}
	}
	if (g) {
		struct quota *gq = group_quota(g);
		if (is_checked(gq)) {
			update_replica_num(gq, size, n);
			quota_softlimit_exceed_group(gq, g);
		}
	}
	if (tdirset != TDIRSET_IS_UNKNOWN && tdirset != TDIRSET_IS_NOT_SET) {
		struct dirquota *dq = dirset_get_dirquota(tdirset);
		update_replica_num(&dq->qmm.q.usage, size, n);
		dirquota_softlimit_exceed(&dq->qmm.q, tdirset);
		if (!dirquota_is_checked(dq))
			dirquota_check_retry_if_running(dq);
	}

	quota_check_retry_if_running();
}

void
quota_update_replica_add(struct inode *inode, struct dirset *tdirset)
{
	quota_update_replica_num(inode, tdirset, 1);
}

void
quota_update_replica_remove(struct inode *inode, struct dirset *tdirset)
{
	quota_update_replica_num(inode, tdirset, -1);
}

#define update_file_remove(q, size, ncopy)				\
	{								\
		(q)->space = int64_add((q)->space, -size);		\
		(q)->num = int64_add((q)->num, -1);			\
		(q)->phy_space = int64_add((q)->phy_space, -(size * ncopy)); \
		(q)->phy_num = int64_add((q)->phy_num, -ncopy);		\
	}

void
quota_update_file_remove(struct inode *inode, struct dirset *tdirset)
{
	gfarm_off_t size;
	gfarm_int64_t ncopy;
	struct user *u = inode_get_user(inode);
	struct group *g = inode_get_group(inode);

	if (inode_is_file(inode)) {
		size = inode_get_size(inode);
		ncopy = inode_get_ncopy_with_dead_host(inode);
	} else {
		size = 0;
		ncopy = 0;
	}

	if (u) {
		struct quota *uq = user_quota(u);
		if (is_checked(uq)) {
			update_file_remove(uq, size, ncopy);
			quota_softlimit_exceed_user(uq, u);
		}
	}
	if (g) {
		struct quota *gq = group_quota(g);
		if (is_checked(gq)) {
			update_file_remove(gq, size, ncopy);
			quota_softlimit_exceed_group(gq, g);
		}
	}
	if (tdirset != TDIRSET_IS_UNKNOWN && tdirset != TDIRSET_IS_NOT_SET) {
		struct dirquota *dq = dirset_get_dirquota(tdirset);
		update_file_remove(&dq->qmm.q.usage, size, ncopy);
		dirquota_softlimit_exceed(&dq->qmm.q, tdirset);
		if (!dirquota_is_checked(dq))
			dirquota_check_retry_if_running(dq);
	}

	quota_check_retry_if_running();
}

void
dirquota_update_file_add(struct inode *inode, struct dirset *tdirset)
{
	gfarm_off_t size;
	gfarm_int64_t ncopy;

	if (inode_is_file(inode)) {
		size = inode_get_size(inode);
		ncopy = inode_get_ncopy_with_dead_host(inode);
	} else {
		size = 0;
		ncopy = 0;
	}

	if (tdirset != TDIRSET_IS_UNKNOWN && tdirset != TDIRSET_IS_NOT_SET) {
		struct dirquota *dq = dirset_get_dirquota(tdirset);
		update_file_add(&dq->qmm.q.usage, size, ncopy);
		dirquota_softlimit_exceed(&dq->qmm.q, tdirset);
		if (!dirquota_is_checked(dq))
			dirquota_check_retry_if_running(dq);
	}

	/* quota_check_retry_if_running() is unnecessary here */
}

void
dirquota_update_file_remove(struct inode *inode, struct dirset *tdirset)
{
	gfarm_off_t size;
	gfarm_int64_t ncopy;

	if (inode_is_file(inode)) {
		size = inode_get_size(inode);
		ncopy = inode_get_ncopy_with_dead_host(inode);
	} else {
		size = 0;
		ncopy = 0;
	}

	if (tdirset != TDIRSET_IS_UNKNOWN && tdirset != TDIRSET_IS_NOT_SET) {
		struct dirquota *dq = dirset_get_dirquota(tdirset);
		update_file_remove(&dq->qmm.q.usage, size, ncopy);
		dirquota_softlimit_exceed(&dq->qmm.q, tdirset);
		if (!dirquota_is_checked(dq))
			dirquota_check_retry_if_running(dq);
	}

	/* quota_check_retry_if_running() is unnecessary here */
}

enum quota_exceeded_type {
	QUOTA_NOT_EXCEEDED = 0,
	QUOTA_EXCEEDED_SPACE_SOFT,
	QUOTA_EXCEEDED_SPACE_HARD,
	QUOTA_EXCEEDED_NUM_SOFT,
	QUOTA_EXCEEDED_NUM_HARD,
	QUOTA_EXCEEDED_PHY_SPACE_SOFT,
	QUOTA_EXCEEDED_PHY_SPACE_HARD,
	QUOTA_EXCEEDED_PHY_NUM_SOFT,
	QUOTA_EXCEEDED_PHY_NUM_HARD,
};

static int
is_exceeded(struct timeval *nowp, struct quota *q,
	int num_file_creating, int num_replica_adding, gfarm_off_t size)
{
	int check_logical = 0, check_physical = 0;

	if (!is_checked(q))  /* quota is disabled */
		return (QUOTA_NOT_EXCEEDED);

	/* softlimit */
	if (quota_limit_is_valid(q->grace_period)) {
		if (quota_limit_is_valid(q->space_soft) &&
		    quota_limit_is_valid(q->space_exceed) &&
		    (nowp->tv_sec - q->space_exceed) > q->grace_period)
			return (QUOTA_EXCEEDED_SPACE_SOFT);
		if (quota_limit_is_valid(q->num_soft) &&
		    quota_limit_is_valid(q->num_exceed) &&
		    (nowp->tv_sec - q->num_exceed) > q->grace_period)
			return (QUOTA_EXCEEDED_NUM_SOFT);
		if (quota_limit_is_valid(q->phy_space_soft) &&
		    quota_limit_is_valid(q->phy_space_exceed) &&
		    (nowp->tv_sec - q->phy_space_exceed) > q->grace_period)
			return (QUOTA_EXCEEDED_PHY_SPACE_SOFT);
		if (quota_limit_is_valid(q->phy_num_soft) &&
		    quota_limit_is_valid(q->phy_num_exceed) &&
		    (nowp->tv_sec - q->phy_num_exceed) > q->grace_period)
			return (QUOTA_EXCEEDED_PHY_NUM_SOFT);
	}

	/* hardlimit */
	if (num_file_creating >= 1 && num_replica_adding <= 0) {
		check_logical = 1;
	} else if (num_file_creating <= 0 && num_replica_adding >= 1) {
		check_physical = 1;
	} else {
		check_logical = 1;
		check_physical = 1;
	}

	if (check_logical) {
		if ((quota_limit_is_valid(q->space_hard) &&
		    q->space + size > q->space_hard))
			return (QUOTA_EXCEEDED_SPACE_HARD);
		if (quota_limit_is_valid(q->num_hard) &&
		    q->num + num_file_creating > q->num_hard)
			return (QUOTA_EXCEEDED_NUM_HARD);
	}
	if (check_physical) {
		if (quota_limit_is_valid(q->phy_space_hard) &&
		    q->phy_space + (size * num_replica_adding) >
		    q->phy_space_hard)
			return (QUOTA_EXCEEDED_PHY_SPACE_HARD);
		if (quota_limit_is_valid(q->phy_num_hard) &&
		    q->phy_num + num_replica_adding > q->phy_num_hard)
			return (QUOTA_EXCEEDED_PHY_NUM_HARD);
	}

	return (QUOTA_NOT_EXCEEDED);
}

static int
dirquota_is_exceeded(struct timeval *nowp, struct dirquota *dq,
	int num_file_creating, int num_replica_adding, gfarm_off_t size)
{
	int check_logical = 0, check_physical = 0;
	struct quota_metadata *q;

#if 0 /* check dirquota, even if it is inaccurate */
	if (!dirquota_is_checked(dq))  /* quota is disabled */
		return (QUOTA_NOT_EXCEEDED);
#endif
	q = &dq->qmm.q;

	/* softlimit */
	if (quota_limit_is_valid(q->limit.grace_period)) {
		if (quota_limit_is_valid(q->limit.soft.space) &&
		    quota_limit_is_valid(q->exceed.space_time) &&
		    (nowp->tv_sec - q->exceed.space_time) >
		    q->limit.grace_period)
			return (QUOTA_EXCEEDED_SPACE_SOFT);
		if (quota_limit_is_valid(q->limit.soft.num) &&
		    quota_limit_is_valid(q->exceed.num_time) &&
		    (nowp->tv_sec - q->exceed.num_time) >
		    q->limit.grace_period)
			return (QUOTA_EXCEEDED_NUM_SOFT);
		if (quota_limit_is_valid(q->limit.soft.phy_space) &&
		    quota_limit_is_valid(q->exceed.phy_space_time) &&
		    (nowp->tv_sec - q->exceed.phy_space_time) >
		    q->limit.grace_period)
			return (QUOTA_EXCEEDED_PHY_SPACE_SOFT);
		if (quota_limit_is_valid(q->limit.soft.phy_num) &&
		    quota_limit_is_valid(q->exceed.phy_num_time) &&
		    (nowp->tv_sec - q->exceed.phy_num_time) >
		    q->limit.grace_period)
			return (QUOTA_EXCEEDED_PHY_NUM_SOFT);
	}

	/* hardlimit */
	if (num_file_creating >= 1 && num_replica_adding <= 0) {
		check_logical = 1;
	} else if (num_file_creating <= 0 && num_replica_adding >= 1) {
		check_physical = 1;
	} else {
		check_logical = 1;
		check_physical = 1;
	}

	if (check_logical) {
		if ((quota_limit_is_valid(q->limit.hard.space) &&
		    q->usage.space + size > q->limit.hard.space))
			return (QUOTA_EXCEEDED_SPACE_HARD);
		if (quota_limit_is_valid(q->limit.hard.num) &&
		    q->usage.num + num_file_creating > q->limit.hard.num)
			return (QUOTA_EXCEEDED_NUM_HARD);
	}
	if (check_physical) {
		if (quota_limit_is_valid(q->limit.hard.phy_space) &&
		    q->usage.phy_space + (size * num_replica_adding) >
		    q->limit.hard.phy_space)
			return (QUOTA_EXCEEDED_PHY_SPACE_HARD);
		if (quota_limit_is_valid(q->limit.hard.phy_num) &&
		    q->usage.phy_num + num_replica_adding >
		    q->limit.hard.phy_num)
			return (QUOTA_EXCEEDED_PHY_NUM_HARD);
	}

	return (QUOTA_NOT_EXCEEDED);
}

/*
 * num_file_creating >= 1 && num_replica_adding == 0 : check logical quota
 * num_file_creating == 0 && num_replica_adding >= 1 : check physical quota
 * num_file_creating == 0 && num_replica_adding == 0 : check both
 * num_file_creating >= 1 && num_replica_adding >= 1 : check both
 */
gfarm_error_t
quota_limit_check(struct user *u, struct group *g, struct dirset *tdirset,
	int num_file_creating, int num_replica_adding, gfarm_off_t size)
{
	struct timeval now;

	gettimeofday(&now, NULL);
	if (u && is_exceeded(&now, user_quota(u),
	    num_file_creating, num_replica_adding, size)) {
		gflog_debug(GFARM_MSG_1002051,
			 "user_quota(%s) exceeded", user_tenant_name(u));
		return (GFARM_ERR_DISK_QUOTA_EXCEEDED);
	}
	if (g && is_exceeded(&now, group_quota(g),
	    num_file_creating, num_replica_adding, size)) {
		gflog_debug(GFARM_MSG_1002052,
			 "group_quota(%s) exceeded", group_tenant_name(g));
		return (GFARM_ERR_DISK_QUOTA_EXCEEDED);
	}
	if (tdirset != TDIRSET_IS_UNKNOWN && tdirset != TDIRSET_IS_NOT_SET &&
	    dirquota_is_exceeded(&now, dirset_get_dirquota(tdirset),
	    num_file_creating, num_replica_adding, size)) {
		gflog_debug(GFARM_MSG_1004635,
		    "dirset_quota(%s:%s) exceeded",
		    dirset_get_username(tdirset),
		    dirset_get_dirsetname(tdirset));
		return (GFARM_ERR_DISK_QUOTA_EXCEEDED);
	}

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
dirquota_limit_check(struct dirset *tdirset,
	int num_file_creating, int num_replica_adding, gfarm_off_t size)
{
	struct timeval now;

	gettimeofday(&now, NULL);
	if (tdirset != TDIRSET_IS_UNKNOWN && tdirset != TDIRSET_IS_NOT_SET &&
	    dirquota_is_exceeded(&now, dirset_get_dirquota(tdirset),
	    num_file_creating, num_replica_adding, size)) {
		gflog_debug(GFARM_MSG_1004636,
		    "dirset_quota(%s:%s) exceeded",
		    dirset_get_username(tdirset),
		    dirset_get_dirsetname(tdirset));
		return (GFARM_ERR_DISK_QUOTA_EXCEEDED);
	}

	return (GFARM_ERR_NO_ERROR);
}

void
quota_user_remove(struct user *u)
{
	struct quota *q = user_quota(u);

	if (q->on_db) {
		quota_user_remove_db(user_tenant_name(u));
		q->on_db = 0;
	}
}

void
quota_group_remove(struct group *g)
{
	struct quota *q = group_quota(g);

	if (q->on_db) {
		quota_group_remove_db(group_tenant_name(g));
		q->on_db = 0;
	}
}

/*
 * common part of quota_check and dirquota_check
 */

struct quota_check_control {
	void (*main_function)(struct quota_check_control *);

	pthread_mutex_t mutex;
	pthread_cond_t wakeup;
	pthread_cond_t end;
	time_t target_time;
	int needed;
	int running;

	const char *mutex_diag;
	const char *wakeup_diag;
	const char *end_diag;
};

static void
quota_check_schedule(struct quota_check_control *ctl, time_t target_time)
{
	static const char diag[] = "quota_check_schedule";

	gfarm_mutex_lock(&ctl->mutex, diag, ctl->mutex_diag);
	ctl->needed = 1;
	ctl->target_time = target_time;
	gfarm_cond_signal(&ctl->wakeup, diag, ctl->wakeup_diag);
	gfarm_mutex_unlock(&ctl->mutex, diag, ctl->mutex_diag);
}

static void
quota_check_start(struct quota_check_control *ctl)
{
	quota_check_schedule(ctl, time(NULL));
}

static void
quota_check_wait_for_end(struct quota_check_control *ctl)
{
	static const char diag[] = "quota_check_wait_for_end";

	gfarm_mutex_lock(&ctl->mutex, diag, ctl->mutex_diag);
	while (ctl->needed || ctl->running)
		gfarm_cond_wait(&ctl->end, &ctl->mutex, diag, ctl->end_diag);
	gfarm_mutex_unlock(&ctl->mutex, diag, ctl->mutex_diag);
}

static void *
quota_check_thread(void *arg)
{
	struct quota_check_control *ctl = arg;
	time_t t;
	static const char diag[] = "quota_check_thread";

	(void)gfarm_pthread_set_priority_minimum(diag);

	for (;;) {
		gfarm_mutex_lock(&ctl->mutex, diag, ctl->mutex_diag);
		ctl->running = 0;
		gfarm_cond_signal(&ctl->end, diag, ctl->end_diag);

		while (!ctl->needed)
			gfarm_cond_wait(&ctl->wakeup, &ctl->mutex,
			    diag, ctl->wakeup_diag);

		for (;;) {
			t = time(NULL);
			if (ctl->target_time <= t)
				break;
			t = ctl->target_time - t;
			gfarm_mutex_unlock(&ctl->mutex, diag, ctl->mutex_diag);
			gfarm_sleep(t);
			gfarm_mutex_lock(&ctl->mutex, diag, ctl->mutex_diag);
		}

		ctl->needed = 0;
		ctl->running = 1;
		gfarm_mutex_unlock(&ctl->mutex, diag, ctl->mutex_diag);

		(*ctl->main_function)(ctl);
	}

	return (NULL);
}


/*
 * quota_check
 */

static void quota_check_main_loop(struct quota_check_control *);

static struct quota_check_control quota_check_ctl = {
	quota_check_main_loop,
	PTHREAD_MUTEX_INITIALIZER,
	PTHREAD_COND_INITIALIZER,
	PTHREAD_COND_INITIALIZER,
	0,
	0,
	0,
	"quota_check_mutex",
	"quota_check_wakeup",
	"quota_check_end",
};

static int
quota_check_needed_locked(const char *diag)
{
	int needed;

	gfarm_mutex_lock(&quota_check_ctl.mutex,
	    diag, quota_check_ctl.mutex_diag);
	needed = quota_check_ctl.needed;
	gfarm_mutex_unlock(&quota_check_ctl.mutex,
	    diag, quota_check_ctl.mutex_diag);
	return (needed);
}

static void
quota_check_needed_clear(const char *diag)
{
	gfarm_mutex_lock(&quota_check_ctl.mutex,
	    diag, quota_check_ctl.mutex_diag);
	quota_check_ctl.needed = 0;
	gfarm_mutex_unlock(&quota_check_ctl.mutex,
	    diag, quota_check_ctl.mutex_diag);
}

static int
quota_check_main(void)
{
	static const char diag[] = "quota_check_main";
	time_t time_start, time_total;
	gfarm_ino_t inum, inum_limit, inum_target;
	struct inode *inode;
#define QUOTA_CHECK_INODE_STEP 10000

	time_start = time(NULL);

	giant_lock();
	usage_tmp_clear();
	giant_unlock();

	giant_lock();
	inum = inode_root_number();
	inum_limit = inode_table_current_size();
	inum_target = inum + QUOTA_CHECK_INODE_STEP;
	if (inum_target > inum_limit)
		inum_target = inum_limit;
	for (;; inum++) {
		if (inum >= inum_target) {
			giant_unlock();
			/* make a chance for clients */
			/* usleep(100000); */ /* for debug */
			giant_lock();
			if (inum >= inum_limit)
				break;
			inum_target = inum + QUOTA_CHECK_INODE_STEP;
			if (inum_target > inum_limit)
				inum_target = inum_limit;
		}
		if (quota_check_needed_locked(diag)) {
			giant_unlock();
			return (1); /* retry */
		}
		inode = inode_lookup(inum);
		if (inode != NULL)
			usage_tmp_update(inode);
	}
	giant_unlock();

	giant_lock();
	if (quota_check_needed_locked(diag)) {
		giant_unlock();
		return (1); /* retry */
	}
	quota_update_usage();
	giant_unlock();

	time_total = time(NULL) - time_start;
	gflog_info(GFARM_MSG_1004296,
	    "quota_check: finished, inodes=%lld, time=%lld",
	    (long long)inum_limit, (long long)time_total);

	return (0); /* finished */
}

static void
quota_check_main_loop(struct quota_check_control *ctl)
{
	static const char diag[] = "quota_check_main_loop";
	int interval;

	gflog_info(GFARM_MSG_1004297, "quota_check: start");
	while (quota_check_main()) {
		quota_check_needed_clear(diag);
		config_var_lock();
		interval = gfarm_quota_check_retry_interval;
		config_var_unlock();
		if (interval > 0) {
			gflog_info(GFARM_MSG_1005105,
			    "quota_check: delay retry for %d seconds",
			    interval);
			gfarm_sleep(interval);
		}
		gflog_info(GFARM_MSG_1004298, "quota_check: retry");
	}
}

static void
quota_check_retry_if_running(void)
{
	static const char diag[] = "quota_check_retry_if_running";

	gfarm_mutex_lock(&quota_check_ctl.mutex,
	    diag, quota_check_ctl.mutex_diag);
	if (quota_check_ctl.running)
		quota_check_ctl.needed = 1;
	/* else: cond_wait now */
	gfarm_mutex_unlock(&quota_check_ctl.mutex,
	    diag, quota_check_ctl.mutex_diag);
}


/*
 * dirquota_check
 */


/* this is protected by giant_lock */
static struct dirquota_check_state {
	struct uint64_to_uint64_map *hardlink_counters;

	int giant_lock_limit;
	int handled_inodes;
	int handled_quota_dirs;
	int handled_dirsets;
	int retried_dirsets;
	int skipped_dirsets;
} dirquota_check_state;

static int
dirquota_check_needed_per_dirset(void *closure, struct dirset *ds)
{
	int *neededp = closure;

	if (*neededp)
		return (1); /* interrupted */
	if (!dirquota_is_checked(dirset_get_dirquota(ds))) {
		*neededp = 1;
		return (1); /* interrupted */
	}
	return (0);
}

static int
dirquota_check_needed(void)
{
	int needed = 0;

	if (dirquota_invalidate_all_requested)
		return (1);
	dirset_foreach_interruptible(
	    &needed, dirquota_check_needed_per_dirset);
	return (needed);
}

static int
dirquota_invalidate_per_dirset(void *closure, struct dirset *ds)
{
	struct dirquota *dq = dirset_get_dirquota(ds);

	dq->invalidate_requested = 1;
	return (0); /* never interrupt */
}

static enum inode_scan_choice
dirquota_check_per_inode(void *closure, struct inode *inode)
{
	struct dirset *ds = closure;
	struct dirquota *dq = dirset_get_dirquota(ds);
	gfarm_uint64_t n;

	if (dirquota_invalidate_all_requested || dq->invalidate_requested)
		return (INODE_SCAN_INTERRUPT);

	if (!inode_is_file(inode) || inode_get_nlink(inode) <= 1) {
		n = 1;
	} else {
		if (!uint64_to_uint64_map_inc_value(
		    dirquota_check_state.hardlink_counters,
		    inode_get_number(inode), &n)) {
			gflog_error(GFARM_MSG_1004637,
			    "dirquota_check: no memory for %lld hardlinks",
			    (long long)uint64_to_uint64_map_size(
			    dirquota_check_state.hardlink_counters));
			return (INODE_SCAN_INTERRUPT);
		}
	}

	if (n == 1) { /* count hard-linked files only at once */
		dirquota_usage_tmp_update(ds, inode);
		++dirquota_check_state.handled_inodes;
	}

	if (++dirquota_check_state.giant_lock_limit
	    >= QUOTA_CHECK_INODE_STEP) {
		dirquota_check_state.giant_lock_limit = 0;
		return (INODE_SCAN_RELEASE_GIANT_LOCK);
	}
	return (INODE_SCAN_CONTINUE);
}

static int
dirquota_check_per_quota_dir(void *closure, struct quota_dir *qd)
{
	struct dirset *ds = closure;
	struct inode *inode = inode_lookup(quota_dir_get_inum(qd));
	int interrupted;

	interrupted = inode_foreach_in_subtree_interruptible(
	    inode, ds, dirquota_check_per_inode, NULL);
	++dirquota_check_state.handled_quota_dirs;
	return (interrupted);
}

static int
dirquota_check_per_dirset(void *closure, struct dirset *ds)
{
	struct dirquota *dq = dirset_get_dirquota(ds);
	int interrupted;

	if (dirquota_invalidate_all_requested)
		return (1); /* interrupted */

	if (dq->invalidate_requested) {
		dq->invalidate_requested = 0;
		dq->qmm.usage_is_valid = 0;
	}
	if (dq->qmm.usage_is_valid) {
		++dirquota_check_state.skipped_dirsets;
		return (0);
	}

	dq->dirquota_checking = 1;
	quota_usage_clear(&dq->usage_tmp);

	dirquota_check_state.hardlink_counters = uint64_to_uint64_map_new();
	if (dirquota_check_state.hardlink_counters == NULL) {
		gflog_error(GFARM_MSG_1004638,
		    "dirquota_check: no memory for hardlink counter");
		interrupted = 1;
	} else {
		interrupted = dirset_foreach_quota_dir_interruptible(
		    ds, ds, dirquota_check_per_quota_dir) ||
		    dq->invalidate_requested ||
		    dirquota_invalidate_all_requested;
	}
	dq->dirquota_checking = 0;

	if (interrupted) {
		++dirquota_check_state.retried_dirsets;
	} else {
		++dirquota_check_state.handled_dirsets;
		dq->qmm.q.usage = dq->usage_tmp;
		dq->qmm.usage_is_valid = 1;
		dirquota_softlimit_exceed(&dq->qmm.q, ds);
	}

	uint64_to_uint64_map_free(dirquota_check_state.hardlink_counters);
	dirquota_check_state.hardlink_counters = NULL;

	return (interrupted);
}

static void
dirquota_check_run(void)
{
	if (dirquota_invalidate_all_requested) {
		dirset_foreach_interruptible(
		    NULL, dirquota_invalidate_per_dirset);
		dirquota_invalidate_all_requested = 0;
	}
	dirset_foreach_interruptible(NULL, dirquota_check_per_dirset);
}

static void
dirquota_check_main(struct quota_check_control *ctl)
{
	time_t time_start, time_total;
	int interval;

	gflog_info(GFARM_MSG_1004639, "dirquota_check: start");
	time_start = time(NULL);

	giant_lock();

	dirquota_check_state.giant_lock_limit = 0;
	dirquota_check_state.handled_inodes = 0;
	dirquota_check_state.handled_quota_dirs = 0;
	dirquota_check_state.handled_dirsets = 0;
	dirquota_check_state.retried_dirsets = 0;
	dirquota_check_state.skipped_dirsets = 0;

	if (dirquota_check_needed()) {
		for (;;) {
			dirquota_check_run();
			if (!dirquota_check_needed())
				break;

			config_var_lock();
			interval = gfarm_directory_quota_check_retry_interval;
			config_var_unlock();
			if (interval > 0) {
				gflog_info(GFARM_MSG_1005106,
				    "dirquota_check: "
				    "delay retry for %d seconds", interval);
				gfarm_sleep(interval);
			}
			gflog_info(GFARM_MSG_1005107, "dirquota_check: retry");
		}
	}

	giant_unlock();

	time_total = time(NULL) - time_start;
	gflog_info(GFARM_MSG_1004640,
	    "dirquota_check: finished, inodes=%lld, quota_dirs=%lld, "
	    "dirsets=%lld, dirset_retries=%lld, dirsets_ok=%lld time=%lld",
	    (long long)dirquota_check_state.handled_inodes,
	    (long long)dirquota_check_state.handled_quota_dirs,
	    (long long)dirquota_check_state.handled_dirsets,
	    (long long)dirquota_check_state.retried_dirsets,
	    (long long)dirquota_check_state.skipped_dirsets,
	    (long long)time_total);
}

static struct quota_check_control dirquota_check_ctl = {
	dirquota_check_main,
	PTHREAD_MUTEX_INITIALIZER,
	PTHREAD_COND_INITIALIZER,
	PTHREAD_COND_INITIALIZER,
	0,
	0,
	0,
	"dirquota_check_mutex",
	"dirquota_check_wakeup",
	"dirquota_check_end",
};

/* PREREQUISITE: giant_lock */
void
dirquota_invalidate(struct dirset *tdirset)
{
	if (tdirset != TDIRSET_IS_UNKNOWN && tdirset != TDIRSET_IS_NOT_SET) {
		struct dirquota *dq = dirset_get_dirquota(tdirset);

		dq->invalidate_requested = 1;
	}
}

/* PREREQUISITE: giant_lock or config_var_lock*/
void
dirquota_fixup_schedule(void)
{
	quota_check_schedule(&dirquota_check_ctl,
	    time(NULL) +  gfarm_directory_quota_check_start_delay);
}

/* PREREQUISITE: giant_lock */
void
dirquota_check_schedule(void)
{
	dirquota_invalidate_all_requested = 1;
	dirquota_fixup_schedule();
}


/*
 * thread startup
 */

void
quota_check_init(void)
{
	gfarm_error_t e;

	e = create_detached_thread(quota_check_thread, &quota_check_ctl);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_fatal(GFARM_MSG_1004299,
		    "create_detached_thread(quota_check): %s",
		    gfarm_error_string(e));
	/* quota_check_start() at startup is unnecessary since gfarm-2.7.17 */

	e = create_detached_thread(quota_check_thread, &dirquota_check_ctl);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_fatal(GFARM_MSG_1004641,
		    "create_detached_thread(dirquota_check): %s",
		    gfarm_error_string(e));
	/* quota_check_start() for dirquota at startup is still necessary */
	quota_check_start(&dirquota_check_ctl);
	/* don't call quota_check_wait_for_end() here to make startup faster */
}

/*
 * server operations
 */

static gfarm_error_t
quota_get_common(struct peer *peer, int from_client, int skip, int is_group)
{
	const char *diag = is_group ?
	    "GFM_PROTO_QUOTA_GROUP_GET" : "GFM_PROTO_QUOTA_USER_GET";
	gfarm_error_t e;
	struct user *user = NULL, *peer_user = peer_get_user(peer);
	struct group *group = NULL;
	struct process *process;
	struct tenant *tenant;
	char *name;
	struct quota *q;
	struct gfarm_quota_get_info qi;
	int is_super_admin;

	e = gfm_server_get_request(peer, diag, "s", &name);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002053,
			"%s request failed: %s", diag, gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		free(name);
		return (GFARM_ERR_NO_ERROR);
	}

	giant_lock();

	if (!from_client || peer_user == NULL) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_1002054,
			    "%s: !from_client or invalid peer_user ", diag);
	} else if ((process = peer_get_process(peer)) == NULL) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_UNFIXED, "%s (%s@%s): no process",
		    diag, peer_get_username(peer), peer_get_hostname(peer));
	} else if ((tenant = process_get_tenant(process)) == NULL) {
		e = GFARM_ERR_INTERNAL_ERROR;
		gflog_error(GFARM_MSG_UNFIXED, "%s (%s@%s): no tenant: %s",
		    diag, peer_get_username(peer), peer_get_hostname(peer),
		    gfarm_error_string(e));
	} else if (db_state != GFARM_ERR_NO_ERROR) {
		e = db_state;
		gflog_debug(GFARM_MSG_1002055, "db_quota is invalid: %s",
		    gfarm_error_string(e));
	} else if (!(is_super_admin = user_is_super_admin(peer_user)) &&
	    strchr(name, GFARM_TENANT_DELIMITER) != NULL) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s (%s@%s) '%s': '+' is not allowed as user/group name",
		    diag, peer_get_username(peer), peer_get_hostname(peer),
		    name);
	} else
		e = GFARM_ERR_NO_ERROR;

	if (e != GFARM_ERR_NO_ERROR) {
		giant_unlock();
		free(name);
		return (gfm_server_put_reply(peer, diag, e, ""));
	}

	if (is_group) {
		if (strcmp(name, "") == 0)
			e = GFARM_ERR_NO_SUCH_GROUP;
		else if ((group = (is_super_admin ?
		    group_tenant_lookup_including_invalid(name) :
		    group_lookup_in_tenant_including_invalid(name, tenant)))
		    == NULL) {
			if (user_is_tenant_admin(peer_user, tenant))
				e = GFARM_ERR_NO_SUCH_GROUP;
			else  /* hidden groupnames */
				e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((!user_in_group(peer_user, group)) &&
			!user_is_tenant_admin(peer_user, tenant))
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		/* user_in_group() || user_is_admin() : permit */
	} else {
		if (strcmp(name, "") == 0) {
			user = peer_user; /* permit not-admin */
			free(name);
			name = strdup_log(user_tenant_name(peer_user), diag);
			if (name == NULL)
				e = GFARM_ERR_NO_MEMORY;
		} else if (strcmp(name, user_tenant_name(peer_user)) == 0)
			user = peer_user; /* permit not-admin */
		else if (!user_is_tenant_admin(peer_user, tenant))
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		else if ((user = (is_super_admin ?
		    user_tenant_lookup_including_invalid(name) :
		    user_lookup_in_tenant_including_invalid(name, tenant)))
		    == NULL)
			e = GFARM_ERR_NO_SUCH_USER;
	}
	if (e != GFARM_ERR_NO_ERROR) {
		giant_unlock();
		gflog_debug(GFARM_MSG_1002056,
			    "%s: name=%s: %s", diag, name,
			    gfarm_error_string(e));
		free(name);
		return (gfm_server_put_reply(peer, diag, e, ""));
	}
	if (is_group)
		q = group_quota(group);
	else
		q = user_quota(user);
	if (!is_checked(q)) { /* quota is not initialized */
		giant_unlock();
		gflog_debug(GFARM_MSG_1002057,
			    "%s: %s's quota is not enabled", diag, name);
		free(name);
		e = GFARM_ERR_NO_SUCH_OBJECT;
		return (gfm_server_put_reply(peer, diag, e, ""));
	}
	quota_convert_1(q, name, &qi);
	giant_unlock();

	e = gfm_server_put_reply(
			peer, diag, e, "slllllllllllllllll",
			qi.name,
			qi.grace_period,
			qi.space,
			qi.space_grace,
			qi.space_soft,
			qi.space_hard,
			qi.num,
			qi.num_grace,
			qi.num_soft,
			qi.num_hard,
			qi.phy_space,
			qi.phy_space_grace,
			qi.phy_space_soft,
			qi.phy_space_hard,
			qi.phy_num,
			qi.phy_num_grace,
			qi.phy_num_soft,
			qi.phy_num_hard);
	free(name);
	return (e);
}

#define set_limit(target, received)					\
	{								\
		if (quota_limit_is_valid(received))			\
			target = received;				\
		else if (received == GFARM_QUOTA_INVALID)		\
			target = GFARM_QUOTA_INVALID;			\
	}

static gfarm_error_t
quota_lookup_internal(const char *name, int is_group,
	struct tenant *tenant, int is_super_admin,
	char **namep, struct quota **qp,
	const char *diag)
{
	gfarm_error_t e;

	if (is_group) {
		struct group *group = is_super_admin ?
		    group_tenant_lookup(name) :
		    group_lookup_in_tenant(name, tenant);
		if (group == NULL) {
			e = GFARM_ERR_NO_SUCH_GROUP;
			gflog_debug(GFARM_MSG_1002061,
				    "%s: name=%s: %s",
				    diag, name, gfarm_error_string(e));
		} else {
			if (namep != NULL)
				*namep = group_tenant_name(group);
			*qp = group_quota(group);
			e = GFARM_ERR_NO_ERROR;
		}
	} else {
		struct user *user = is_super_admin ?
		    user_tenant_lookup(name) :
		    user_lookup_in_tenant(name, tenant);
		if (user == NULL) {
			e = GFARM_ERR_NO_SUCH_USER;
			gflog_debug(GFARM_MSG_1002062,
				    "%s: name=%s: %s",
				    diag, name, gfarm_error_string(e));
		} else {
			if (namep != NULL)
				*namep = user_tenant_name(user);
			*qp = user_quota(user);
			e = GFARM_ERR_NO_ERROR;
		}
	}
	return (e);
}

gfarm_error_t
quota_lookup(const char *name, int is_group,
	struct quota **qp,
	const char *diag)
{
	return (quota_lookup_internal(name, is_group, NULL, 1,
	    NULL, qp, diag));
}

static gfarm_error_t
quota_set_common(struct peer *peer, int from_client, int skip, int is_group)
{
	const char *diag = is_group ?
	    "GFM_PROTO_QUOTA_GROUP_SET" : "GFM_PROTO_QUOTA_USER_SET";
	gfarm_error_t e;
	struct gfarm_quota_set_info qi;
	char *n;
	struct quota *q;
	struct user *peer_user = peer_get_user(peer);
	struct process *process;
	struct tenant *tenant;
	int is_super_admin, need_db_update = 0;

	e = gfm_server_get_request(peer, diag, "slllllllll",
				   &qi.name,
				   &qi.grace_period,
				   &qi.space_soft,
				   &qi.space_hard,
				   &qi.num_soft,
				   &qi.num_hard,
				   &qi.phy_space_soft,
				   &qi.phy_space_hard,
				   &qi.phy_num_soft,
				   &qi.phy_num_hard);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002058, "%s request failed: %s",
			diag, gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		free(qi.name);
		return (GFARM_ERR_NO_ERROR);
	}

	if (db_state != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002059, "db_quota is invalid: %s",
			gfarm_error_string(db_state));
		free(qi.name);
		return (gfm_server_put_reply(peer, diag, db_state, ""));
	}

	giant_lock();
	if (!from_client || peer_user == NULL) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_1002060,
			    "%s: !from_client or invalid peer_user"
			    " or !user_is_admin", diag);
	} else if ((process = peer_get_process(peer)) == NULL) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_UNFIXED, "%s (%s@%s): no process",
		    diag, peer_get_username(peer), peer_get_hostname(peer));
	} else if ((tenant = process_get_tenant(process)) == NULL) {
		e = GFARM_ERR_INTERNAL_ERROR;
		gflog_error(GFARM_MSG_UNFIXED, "%s (%s@%s): no tenant: %s",
		    diag, peer_get_username(peer), peer_get_hostname(peer),
		    gfarm_error_string(e));
	} else if (!(is_super_admin = user_is_super_admin(peer_user)) &&
	    strchr(qi.name, GFARM_TENANT_DELIMITER) != NULL) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s (%s@%s) '%s': '+' is not allowed as user/group name",
		    diag, peer_get_username(peer), peer_get_hostname(peer),
		    qi.name);
	} else if ((e = quota_lookup_internal(qi.name, is_group,
	    tenant, is_super_admin, &n, &q, diag)) != GFARM_ERR_NO_ERROR) {
		/* do nothing */
	} else if (gfarm_read_only_mode()) {
		e = GFARM_ERR_READ_ONLY_FILE_SYSTEM;
		gflog_debug(GFARM_MSG_1005161, "%s (%s@%s) for "
		    "name %s during read_only",
		    diag, peer_get_username(peer), peer_get_hostname(peer),
		    qi.name);
	} else {
		/* set limits */
		set_limit(q->grace_period, qi.grace_period);
		set_limit(q->space_soft, qi.space_soft);
		set_limit(q->space_hard, qi.space_hard);
		set_limit(q->num_soft, qi.num_soft);
		set_limit(q->num_hard, qi.num_hard);
		set_limit(q->phy_space_soft, qi.phy_space_soft);
		set_limit(q->phy_space_hard, qi.phy_space_hard);
		set_limit(q->phy_num_soft, qi.phy_num_soft);
		set_limit(q->phy_num_hard, qi.phy_num_hard);

		/* check softlimit and update exceeded time */
		quota_softlimit_exceed(q, &need_db_update);

		/* update regardless of need_db_update */
		if (is_group)
			e = db_quota_group_set(q, n);
		else
			e = db_quota_user_set(q, n);
		if (e == GFARM_ERR_NO_ERROR)
			q->on_db = 1;
	}
	giant_unlock();
	free(qi.name);
	return (gfm_server_put_reply(peer, diag, e, ""));
}

gfarm_error_t
gfm_server_quota_user_get(struct peer *peer, int from_client, int skip)
{
	return (quota_get_common(peer, from_client, skip, 0));
}

gfarm_error_t
gfm_server_quota_user_set(struct peer *peer, int from_client, int skip)
{
	return (quota_set_common(peer, from_client, skip, 0));
}

gfarm_error_t
gfm_server_quota_group_get(struct peer *peer, int from_client, int skip)
{
	return (quota_get_common(peer, from_client, skip, 1));
}

gfarm_error_t
gfm_server_quota_group_set(struct peer *peer, int from_client, int skip)
{
	return (quota_set_common(peer, from_client, skip, 1));
}

gfarm_error_t
gfm_server_quota_check(struct peer *peer, int from_client, int skip)
{
	static const char diag[] = "GFM_PROTO_QUOTA_CHECK";
	gfarm_error_t e;
	struct user *peer_user = peer_get_user(peer);

	e = gfm_server_get_request(peer, diag, "");
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002063,
			"%s request failed: %s", diag, gfarm_error_string(e));
		return (e);
	}
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	if (db_state != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002064, "db_quota is invalid: %s",
			gfarm_error_string(db_state));
		return (gfm_server_put_reply(peer, diag, db_state, ""));
	}

	giant_lock();
	if (!from_client || peer_user == NULL ||
	    !user_is_super_admin(peer_user)) {
		giant_unlock();
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_1002065,
			    "%s: !from_client or invalid peer_user"
			    " or !user_is_admin", diag);
		return (gfm_server_put_reply(peer, diag, e, ""));
	}
	giant_unlock();

	quota_check_start(&quota_check_ctl);
	quota_check_wait_for_end(&quota_check_ctl);

	quota_check_start(&dirquota_check_ctl);
	quota_check_wait_for_end(&dirquota_check_ctl);

	return (gfm_server_put_reply(peer, diag, e, ""));
}
