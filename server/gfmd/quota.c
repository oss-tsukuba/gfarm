/*
 * $Id$
 */

#include <pthread.h>	/* db_access.h currently needs this */
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/time.h>
#include <assert.h>

#include <gfarm/gfarm.h>

#include "gfp_xdr.h"
#include "auth.h"

#include "peer.h"
#include "subr.h"
#include "rpcsubr.h"
#include "user.h"
#include "group.h"
#include "inode.h"
#include "quota.h"
#include "quota_info.h"
#include "db_access.h"
#include "gfm_proto.h"
#include "relay.h"

#define QUOTA_NOT_CHECK_YET -1
#define is_checked(q) (q->space != QUOTA_NOT_CHECK_YET ? 1 : 0)

static gfarm_error_t db_state = GFARM_ERR_NO_ERROR;

/* private functions */
static void
update_softlimit(gfarm_time_t *exceedp, gfarm_time_t now, gfarm_time_t grace,
		gfarm_int64_t val, gfarm_int64_t soft)
{
	if (!quota_limit_is_valid(grace) /* disable all softlimit */ ||
	    !quota_limit_is_valid(soft) /* disable this softlimit */ ||
	    val <= soft /* not exceed */
		) {
		*exceedp = GFARM_QUOTA_INVALID;
		return;
	} else if (*exceedp >= 0)
		return; /* already exceeded */
	else if (val > soft)
		*exceedp = now; /* exceed now */
}

static void
quota_check_softlimit_exceed(struct quota *q)
{
	struct timeval now;

	if (!quota_limit_is_valid(q->grace_period)) {
		/* disable all softlimit */
		q->space_exceed = GFARM_QUOTA_INVALID;
		q->num_exceed = GFARM_QUOTA_INVALID;
		q->phy_space_exceed = GFARM_QUOTA_INVALID;
		q->phy_num_exceed = GFARM_QUOTA_INVALID;
		return;
	}

	/* update exceeded time of softlimit */
	gettimeofday(&now, NULL);
	update_softlimit(&q->space_exceed, now.tv_sec, q->grace_period,
			 q->space, q->space_soft);
	update_softlimit(&q->num_exceed, now.tv_sec, q->grace_period,
			 q->num, q->num_soft);
	update_softlimit(&q->phy_space_exceed, now.tv_sec, q->grace_period,
			 q->phy_space, q->phy_space_soft);
	update_softlimit(&q->phy_num_exceed, now.tv_sec, q->grace_period,
			 q->phy_num, q->phy_num_soft);
}

static void
quota_clear_value_user(void *closure, struct user *u)
{
	struct quota *q = user_quota(u);

	if (!q->on_db) /* disabled: not gfedquota yet */
		return;

	q->space = 0; /* is_checked is true */
	q->num = 0;
	q->phy_space = 0;
	q->phy_num = 0;
}

static void
quota_clear_value_group(void *closure, struct group *g)
{
	struct quota *q = group_quota(g);

	if (!q->on_db) /* disabled: not gfedquota yet */
		return;

	q->space = 0; /* is_checked is true */
	q->num = 0;
	q->phy_space = 0;
	q->phy_num = 0;
}

static void
quota_clear_value_all_user_and_group()
{
	user_all(NULL, quota_clear_value_user, 0);
	group_all(NULL, quota_clear_value_group, 0);
}

static void
quota_set_value_user(void *closure, struct user *u)
{
	struct quota *q = user_quota(u);

	if (!is_checked(q))
		return;

	quota_check_softlimit_exceed(q);
}

static void
quota_set_value_group(void *closure, struct group *g)
{
	struct quota *q = group_quota(g);

	if (!is_checked(q))
		return;

	quota_check_softlimit_exceed(q);
}

static void
quota_set_value_all_user_and_group()
{
	user_all(NULL, quota_set_value_user, 0);
	group_all(NULL, quota_set_value_group, 0);
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
	u = user_lookup_including_invalid(qi->name);
	if (u == NULL) {
		quota_user_remove_db(qi->name);
	} else {
		struct quota *q = user_quota(u);
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
	g = group_lookup_including_invalid(qi->name);
	if (g == NULL) {
		quota_group_remove_db(qi->name);
	} else {
		struct quota *q = group_quota(g);
		quota_convert_2(qi, q);
		q->on_db = 1; /* load from db */
	}
	gfarm_quota_info_free(qi);
}

void
quota_init()
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
	q->space = QUOTA_NOT_CHECK_YET;
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
		q->space = int64_add(q->space, size);			\
		q->num = int64_add(q->num, 1);				\
		q->phy_space = int64_add(q->phy_space, size * ncopy);	\
		q->phy_num = int64_add(q->phy_num, ncopy);		\
	}

void
quota_update_file_add(struct inode *inode)
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
			quota_check_softlimit_exceed(uq);
		}
	}
	if (g) {
		struct quota *gq = group_quota(g);
		if (is_checked(gq)) {
			update_file_add(gq, size, ncopy);
			quota_check_softlimit_exceed(gq);
		}
	}

	if (debug_mode) {
		gfarm_ino_t inum;
		gfarm_int64_t gen;
		char *username, *groupname;
		inum = inode_get_number(inode);
		gen = inode_get_gen(inode);
		username = user_name(u);
		groupname = group_name(g);
		gflog_debug(GFARM_MSG_1000416,
			    "<quota_add> ino=%lld(gen=%lld): "
			    "size=%lld,ncopy=%lld,user=%s,group=%s",
			    (unsigned long long)inum, (unsigned long long)gen,
			    (unsigned long long)size, (unsigned long long)ncopy,
			    username, groupname);
	}
}

static void
quota_update_file_add_for_quotacheck(void *closure, struct inode *inode)
{
	quota_update_file_add(inode);
}

#define update_file_resize(q, old_size, new_size, ncopy)		\
	{								\
		gfarm_int64_t diff = new_size - old_size;		\
		q->space = int64_add(q->space, diff);			\
		q->phy_space = int64_add(q->phy_space, diff * ncopy);	\
	}

void
quota_update_file_resize(struct inode *inode, gfarm_off_t new_size)
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
			quota_check_softlimit_exceed(uq);
		}
	}
	if (g) {
		struct quota *gq = group_quota(g);
		if (is_checked(gq)) {
			update_file_resize(gq, old_size, new_size, ncopy);
			quota_check_softlimit_exceed(gq);
		}
	}
}

#define update_replica_num(q, size, n)					\
	{								\
		q->phy_space = int64_add(q->phy_space, size * n);	\
		q->phy_num = int64_add(q->phy_num, n);			\
	}

static void
quota_update_replica_num(struct inode *inode, gfarm_int64_t n)
{
	gfarm_off_t size = inode_get_size(inode);
	struct user *u = inode_get_user(inode);
	struct group *g = inode_get_group(inode);

	if (u) {
		struct quota *uq = user_quota(u);
		if (is_checked(uq)) {
			update_replica_num(uq, size, n);
			quota_check_softlimit_exceed(uq);
		}
	}
	if (g) {
		struct quota *gq = group_quota(g);
		if (is_checked(gq)) {
			update_replica_num(gq, size, n);
			quota_check_softlimit_exceed(gq);
		}
	}
}

void
quota_update_replica_add(struct inode *inode)
{
	quota_update_replica_num(inode, 1);
}

void
quota_update_replica_remove(struct inode *inode)
{
	quota_update_replica_num(inode, -1);
}

#define update_file_remove(q, size, ncopy)				\
	{								\
		q->space = int64_add(q->space, -size);			\
		q->num = int64_add(q->num, -1);				\
		q->phy_space = int64_add(q->phy_space, -(size * ncopy)); \
		q->phy_num = int64_add(q->phy_num, -ncopy);		\
	}

void
quota_update_file_remove(struct inode *inode)
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
			quota_check_softlimit_exceed(uq);
		}
	}
	if (g) {
		struct quota *gq = group_quota(g);
		if (is_checked(gq)) {
			update_file_remove(gq, size, ncopy);
			quota_check_softlimit_exceed(gq);
		}
	}
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
	    int is_file_creating, int is_replica_adding)
{
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
	if ((quota_limit_is_valid(q->space_hard) &&
	     q->space > q->space_hard))
		return (QUOTA_EXCEEDED_SPACE_HARD);
	if (quota_limit_is_valid(q->num_hard) &&
	    q->num + (is_file_creating ? 1 : 0) > q->num_hard)
		return (QUOTA_EXCEEDED_NUM_HARD);
	if (quota_limit_is_valid(q->phy_space_hard) &&
	    q->phy_space > q->phy_space_hard)
		return (QUOTA_EXCEEDED_PHY_SPACE_HARD);
	if (quota_limit_is_valid(q->phy_num_hard) &&
	    q->phy_num + (is_replica_adding ? 1 : 0) > q->phy_num_hard)
		return (QUOTA_EXCEEDED_PHY_NUM_HARD);

	return (QUOTA_NOT_EXCEEDED);
}

gfarm_error_t
quota_check_limits(struct user *u, struct group *g,
		   int is_file_creating, int is_replica_adding)
{
	struct timeval now;

	gettimeofday(&now, NULL);
	if (u && is_exceeded(&now, user_quota(u),
			    is_file_creating, is_replica_adding)) {
		gflog_debug(GFARM_MSG_1002051,
			 "user_quota(%s) exceeded", user_name(u));
		return (GFARM_ERR_DISK_QUOTA_EXCEEDED);
	}
	if (g && is_exceeded(&now, group_quota(g),
			     is_file_creating, is_replica_adding)) {
		gflog_debug(GFARM_MSG_1002052,
			 "group_quota(%s) exceeded", group_name(g));
		return (GFARM_ERR_DISK_QUOTA_EXCEEDED);
	}

	return (GFARM_ERR_NO_ERROR);
}

void
quota_user_remove(struct user *u)
{
	struct quota *q = user_quota(u);

	if (q->on_db) {
		quota_user_remove_db(user_name(u));
		q->on_db = 0;
	}
	q->space = QUOTA_NOT_CHECK_YET;
}

void
quota_group_remove(struct group *g)
{
	struct quota *q = group_quota(g);

	if (q->on_db) {
		quota_group_remove_db(group_name(g));
		q->on_db = 0;
	}
	q->space = QUOTA_NOT_CHECK_YET;
}

void
quota_check(void)
{
	/* zero clear and set true in is_checked */
	quota_clear_value_all_user_and_group();
	/* load all inodes from memory and count usage values of files */
	/* XXX FIXME too long giant lock */
	inode_lookup_all(NULL, quota_update_file_add_for_quotacheck);
	/* update memory */
	quota_set_value_all_user_and_group();
}

/* server operations */
static gfarm_error_t
quota_get_common(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip, int is_group)
{
	const char *diag = is_group ?
	    "GFM_PROTO_QUOTA_GROUP_GET" : "GFM_PROTO_QUOTA_USER_GET";
	gfarm_error_t e;
	struct user *user = NULL, *peer_user = peer_get_user(peer);
	struct group *group = NULL;
	char *name;
	struct quota *q;
	struct gfarm_quota_get_info qi;
	struct relayed_request *relay;

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    is_group ? GFM_PROTO_QUOTA_GROUP_GET : GFM_PROTO_QUOTA_USER_GET,
	    "s", &name);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(name);
		return (GFARM_ERR_NO_ERROR);
	}
	if (!from_client || peer_user == NULL) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_1002054,
			    "%s: !from_client or invalid peer_user ", diag);
		free(name);
		return (gfm_server_relay_put_reply(peer, xid, sizep,
		    relay, diag, &e, ""));
	}

	if (db_state != GFARM_ERR_NO_ERROR) {
		free(name);
		gflog_debug(GFARM_MSG_1002055, "db_quota is invalid: %s",
			gfarm_error_string(db_state));
		return (gfm_server_relay_put_reply(peer, xid, sizep,
						relay, diag, &db_state, ""));
	}
	if (relay == NULL) {
		/* do not relay RPC to master gfmd */
		giant_lock();
		if (is_group) {
			if (strcmp(name, "") == 0)
				e = GFARM_ERR_NO_SUCH_GROUP;
			else if ((group = group_lookup(name)) == NULL) {
				if (user_is_admin(peer_user))
					e = GFARM_ERR_NO_SUCH_GROUP;
				else  /* hidden groupnames */
					e = GFARM_ERR_OPERATION_NOT_PERMITTED;
			} else if ((!user_in_group(peer_user, group)) &&
				   !user_is_admin(peer_user))
				e = GFARM_ERR_OPERATION_NOT_PERMITTED;
			/* user_in_group() || user_is_admin() : permit */
		} else {
			if (strcmp(name, "") == 0) {
				user = peer_user; /* permit not-admin */
				free(name);
				name = strdup_log(user_name(peer_user), diag);
				if (name == NULL)
					e = GFARM_ERR_NO_MEMORY;
			} else if (strcmp(name, user_name(peer_user)) == 0)
				user = peer_user; /* permit not-admin */
			else if (!user_is_admin(peer_user))
				e = GFARM_ERR_OPERATION_NOT_PERMITTED;
			else if ((user = user_lookup(name)) == NULL)
				e = GFARM_ERR_NO_SUCH_USER;
		}
		if (e != GFARM_ERR_NO_ERROR) {
			giant_unlock();
			gflog_debug(GFARM_MSG_1002056,
			    "%s: name=%s: %s", diag, name,
			    gfarm_error_string(e));
			free(name);
			return (gfm_server_relay_put_reply(peer, xid,
			    sizep, relay, diag, &e, ""));
		}
		if (is_group)
			q = group_quota(group);
		else
			q = user_quota(user);
		if (!is_checked(q)) { /* quota is not enabled */
			giant_unlock();
			gflog_debug(GFARM_MSG_1002057,
			    "%s: %s's quota is not enabled", diag, name);
			free(name);
			e = GFARM_ERR_NO_SUCH_OBJECT;
			return (gfm_server_relay_put_reply(peer, xid,
			    sizep, relay, diag, &e, ""));
		}
		quota_convert_1(q, name, &qi);
		giant_unlock();
	}

	e = gfm_server_relay_put_reply(
			peer, xid, sizep, relay, diag, &e,
			"slllllllllllllllll",
			&qi.name,
			&qi.grace_period,
			&qi.space,
			&qi.space_grace,
			&qi.space_soft,
			&qi.space_hard,
			&qi.num,
			&qi.num_grace,
			&qi.num_soft,
			&qi.num_hard,
			&qi.phy_space,
			&qi.phy_space_grace,
			&qi.phy_space_soft,
			&qi.phy_space_hard,
			&qi.phy_num,
			&qi.phy_num_grace,
			&qi.phy_num_soft,
			&qi.phy_num_hard);
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

gfarm_error_t
quota_lookup(const char *name, int is_group, struct quota **qp,
	const char *diag)
{
	gfarm_error_t e;

	if (is_group) {
		struct group *group = group_lookup(name);
		if (group == NULL) {
			e = GFARM_ERR_NO_SUCH_GROUP;
			gflog_debug(GFARM_MSG_1002061,
				    "%s: name=%s: %s",
				    diag, name, gfarm_error_string(e));
		} else {
			*qp = group_quota(group);
			e = GFARM_ERR_NO_ERROR;
		}
	} else {
		struct user *user = user_lookup(name);
		if (user == NULL) {
			e = GFARM_ERR_NO_SUCH_USER;
			gflog_debug(GFARM_MSG_1002062,
				    "%s: name=%s: %s",
				    diag, name, gfarm_error_string(e));
		} else {
			*qp = user_quota(user);
			e = GFARM_ERR_NO_ERROR;
		}
	}
	return (e);
}

static gfarm_error_t
quota_set_common(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip, int is_group)
{
	const char *diag = is_group ?
	    "GFM_PROTO_QUOTA_GROUP_SET" : "GFM_PROTO_QUOTA_USER_SET";
	gfarm_error_t e;
	struct gfarm_quota_set_info qi;
	struct quota *q;
	struct user *peer_user = peer_get_user(peer);
	struct relayed_request *relay;

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    is_group ? GFM_PROTO_QUOTA_GROUP_SET : GFM_PROTO_QUOTA_USER_SET,
				   "slllllllll",
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
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(qi.name);
		return (GFARM_ERR_NO_ERROR);
	}

	if (db_state != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002059, "db_quota is invalid: %s",
			gfarm_error_string(db_state));
		free(qi.name);
		return (gfm_server_relay_put_reply(peer, xid, sizep,
						relay, diag, &db_state, ""));
	}

	if (relay == NULL) {
		/* do not relay RPC to master gfmd */
		giant_lock();
		if (!from_client || peer_user == NULL
				 || !user_is_admin(peer_user)) {
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
			gflog_debug(GFARM_MSG_1002060,
			    "%s: !from_client or invalid peer_user"
			    " or !user_is_admin", diag);
			goto end;
		} else if ((e = quota_lookup(qi.name, is_group, &q, diag))
			   != GFARM_ERR_NO_ERROR) {
			goto end;
		}

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
		quota_check_softlimit_exceed(q);

		if (is_group)
			e = db_quota_group_set(q, qi.name);
		else
			e = db_quota_user_set(q, qi.name);
		if (e == GFARM_ERR_NO_ERROR)
			q->on_db = 1;
end:
		giant_unlock();
	}
	free(qi.name);
	return (gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, ""));
}

gfarm_error_t
gfm_server_quota_user_get(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	return (quota_get_common(peer, xid, sizep, from_client, skip, 0));
}

gfarm_error_t
gfm_server_quota_user_set(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	return (quota_set_common(peer, xid, sizep, from_client, skip, 0));
}

gfarm_error_t
gfm_server_quota_group_get(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	return (quota_get_common(peer, xid, sizep, from_client, skip, 1));
}

gfarm_error_t
gfm_server_quota_group_set(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	return (quota_set_common(peer, xid, sizep, from_client, skip, 1));
}

gfarm_error_t
gfm_server_quota_check(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	static const char diag[] = "GFM_PROTO_QUOTA_CHECK";
	gfarm_error_t e;
	struct user *peer_user = peer_get_user(peer);
	struct relayed_request *relay;

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_QUOTA_CHECK, "");
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	if (db_state != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002064, "db_quota is invalid: %s",
			gfarm_error_string(db_state));
		return (gfm_server_relay_put_reply(peer, xid, sizep,
						relay, diag, &db_state, ""));
	}

	if (relay == NULL) {
		/* do not relay RPC to master gfmd */
		giant_lock();
		if (!from_client || peer_user == NULL
				 || !user_is_admin(peer_user)) {
			giant_unlock();
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
			gflog_debug(GFARM_MSG_1002065,
			    "%s: !from_client or invalid peer_user"
			    " or !user_is_admin", diag);
			return (gfm_server_relay_put_reply(peer, xid,
			    sizep, relay, diag, &e, ""));
		}
		/* XXX FIXME too long giant lock */
		quota_check();
		giant_unlock();
	}

	return (gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, ""));
}
