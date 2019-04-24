/*
 * $Id$
 */

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>
#include <syslog.h>
#include <sched.h>

#include <gfarm/error.h>
#include <gfarm/gflog.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

#include "gfutil.h"
#include "nanosec.h"
#include "thrsubr.h"
#include "gflog_reduced.h"

#include "config.h"
#include "auth.h"
#include "gfm_proto.h"
#include "timespec.h"

#include "db_access.h"
#include "rpcsubr.h"
#include "peer.h"
#include "fsngroup.h"
#include "gfmd.h"
#include "inode.h"
#include "dirset.h"
#include "process.h"
#include "dead_file_copy.h"
#include "dir.h"
#include "host.h"
#include "subr.h"
#include "user.h"
#include "back_channel.h"

/* for debug */
/* #define DEBUG_REPLICA_CHECK or CPPFLAGS='-DDEBUG_REPLICA_CHECK' */

#ifdef DEBUG_REPLICA_CHECK
#define RC_LOG_DEBUG gflog_warning
#define RC_LOG_INFO gflog_warning
#else
#define RC_LOG_DEBUG gflog_debug
#define RC_LOG_INFO gflog_info
#endif

#define SAME_WARNING_TRIGGER	10	/* check reduced mode */
#define SAME_WARNING_THRESHOLD	30	/* more than this -> reduced mode */
#define SAME_WARNING_DURATION	600	/* seconds to measure the limit */
#define SAME_WARNING_INTERVAL	60	/* seconds: interval of reduced log */

static struct gflog_reduced_state hosts_down_state =
	GFLOG_REDUCED_STATE_INITIALIZER(
		SAME_WARNING_TRIGGER,
		SAME_WARNING_THRESHOLD,
		SAME_WARNING_DURATION,
		SAME_WARNING_INTERVAL);

static struct gflog_reduced_state remove_ok_state =
	GFLOG_REDUCED_STATE_INITIALIZER(
		SAME_WARNING_TRIGGER,
		SAME_WARNING_THRESHOLD,
		SAME_WARNING_DURATION,
		SAME_WARNING_INTERVAL);

struct replication_info {
	gfarm_ino_t inum;
	gfarm_uint64_t gen;
	struct replica_spec replica_spec;
	struct dirset *tdirset;
};

static int replica_check_ctrl_enabled(void);
static int replica_check_remove_enabled(void);
static int replica_check_reduced_log_enabled(void);

static int
replica_check_remove_grace_used_space_ratio_locked(void)
{
	int rv;

	config_var_lock();
	rv = gfarm_replica_check_remove_grace_used_space_ratio;
	config_var_unlock();
	return (rv);
}

static int
replica_check_remove_grace_time_locked(void)
{
	int rv;

	config_var_lock();
	rv = gfarm_replica_check_remove_grace_time;
	config_var_unlock();
	return (rv);
}

static int
replica_check_host_down_thresh_locked(void)
{
	int rv;

	config_var_lock();
	rv = gfarm_replica_check_host_down_thresh;
	config_var_unlock();
	if (rv < 0)
		rv = 0;
	return (rv);
}

static int
replica_check_minimum_interval_locked(void)
{
	int rv;

	config_var_lock();
	rv = gfarm_replica_check_minimum_interval;
	config_var_unlock();
	if (rv < 0)
		rv = 0;
	return (rv);
}


#define REDUCED_WARN(msg_no, state, ...)				\
	{								\
		if (replica_check_reduced_log_enabled())		\
			gflog_reduced_warning(msg_no, state, __VA_ARGS__); \
		else							\
			gflog_warning(msg_no, __VA_ARGS__);		\
	}

#define REDUCED_INFO(msg_no, state, ...)				\
	{								\
		if (replica_check_reduced_log_enabled())		\
			gflog_reduced_info(msg_no, state, __VA_ARGS__); \
		else							\
			gflog_info(msg_no, __VA_ARGS__);		\
	}

static int sleep_time;  /* from gfarm_replica_check_sleep_time */
static double lock_sleep_time;  /* total */
static double retry_sleep_time; /* total */

static int yield_time;  /* from gfarm_replica_check_yield_time */
static double total_yield_time;

static gfarm_uint64_t req_ok_num_total, req_ok_size_total;
static gfarm_uint64_t remove_num_total, remove_size_total;

/*
 * 0: The replica should not be removed.
 * 1: The replica may be removed. (try inode_remove_replica_protected)
 */
static int
replica_check_remove_grace_is_over(struct inode *inode,
	struct host *replica_host)
{
	float used_space = host_status_get_disk_usage_percent(replica_host);
	struct gfarm_timespec *atime = inode_get_atime(inode);
	struct gfarm_timespec sub;
	struct gfarm_timespec grace_time;
	struct timespec now_timespec;

#ifdef DEBUG_REPLICA_CHECK
	RC_LOG_DEBUG(GFARM_MSG_1005013,
	    "replica_check_remove_grace_is_over: "
	    "inum=%lld, replica=%s, used_space=%f, grace_used_space_ratio=%d",
	    (long long)inode_get_number(inode),
	    host_name(replica_host), used_space,
	    gfarm_replica_check_remove_grace_used_space_ratio);
#endif
	/*
	 * gfarm_replica_check_remove_grace_used_space_ratio is
	 * giant_lock()ed.
	 */
	if (used_space <=
	    (float)gfarm_replica_check_remove_grace_used_space_ratio)
		return (0);

	/*
	 * gfarm_replica_check_remove_grace_time is giant_lock()ed.
	 */
	grace_time.tv_sec = gfarm_replica_check_remove_grace_time;
	grace_time.tv_nsec = 0;

	gfarm_gettime(&now_timespec);
	sub.tv_sec = now_timespec.tv_sec;
	sub.tv_nsec = now_timespec.tv_nsec;

	gfarm_timespec_sub(&sub, atime);
	if (gfarm_timespec_cmp(&sub, &grace_time) > 0) {
		/* (now - atime) > grace_time */
#ifdef DEBUG_REPLICA_CHECK
		RC_LOG_DEBUG(GFARM_MSG_1005014,
		    "replica_check_remove_grace_is_over: "
		    "inum=%lld, replica=%s: may be removed",
		    (long long)inode_get_number(inode),
		    host_name(replica_host));
#endif
		return (1); /* remove a surplus replica immediately */
	}

	return (0);
}

static int
replica_check_remove_grace_disabled(struct inode *inode,
	struct host *replica_host)
{
	return (1); /* remove all surplus replicas immediately */
}

static gfarm_error_t
replica_check_remove_replicas(struct inode *inode,
	int n_srcs, struct host **srcs, struct replication_info *info)
{
	gfarm_error_t e, e2;
	int i;
	int (*grace_is_over_func)(struct inode *, struct host *);

	if (n_srcs <= 1)
		return (GFARM_ERR_NO_ERROR); /* skip */
	if (n_srcs <= info->replica_spec.desired_number)
		return (GFARM_ERR_NO_ERROR); /* skip */
	if (gfarm_replica_check_remove_grace_used_space_ratio >= 100)
		return (GFARM_ERR_NO_ERROR); /* skip */

	if (inode_get_nlink(inode) >= 2) {
		char *repattr;
		int desired_number;

		/*
		 * Do not remove replicas, when this file is
		 * hard-linked and has no replica_spec.
		 */
		if (!inode_get_replica_spec(
		    inode, &repattr, &desired_number)) /* no replica_spec */
			return (GFARM_ERR_NO_ERROR); /* skip */
		/* replica_spec is set for this file */

		free(repattr);
		/* through */
	}

	/* prefer a host which has few disk_avail */
	host_sort_to_remove_replicas(n_srcs, srcs);

	/*
	 * gfarm_replica_check_remove_grace_used_space_ratio and
	 * gfarm_replica_check_remove_grace_time are giant_lock()ed.
	 */
	/* shortcut */
	if (gfarm_replica_check_remove_grace_used_space_ratio <= 0 &&
	    gfarm_replica_check_remove_grace_time <= 0)
		grace_is_over_func = replica_check_remove_grace_disabled;
	else
		grace_is_over_func = replica_check_remove_grace_is_over;

	e = GFARM_ERR_NO_ERROR;
	/* srcs are available replicas */
	for (i = 0; i < n_srcs; i++) {
#if 0
#ifdef DEBUG_REPLICA_CHECK
		gfarm_off_t used, avail;

		host_status_get_disk_usage(srcs[i], &used, &avail);
		RC_LOG_DEBUG(GFARM_MSG_1004272,
		    "replica_check: candidate to remove: "
		    "[%d]%s: used=%lld, avail=%lld",
		    i, host_name(srcs[i]), (long long)used, (long long)avail);
#endif
#endif
		if (grace_is_over_func(inode, srcs[i]) == 0)
			continue; /* skip */

		/* remove a replica only when it is excessive */
		e2 = inode_remove_replica_protected(inode, srcs[i],
		    &info->replica_spec, info->tdirset);
		if (e2 == GFARM_ERR_NO_ERROR) {
			REDUCED_INFO(GFARM_MSG_1004273, &remove_ok_state,
			    "replica_check: %lld:%lld:%s@%s: removed",
			    (long long)info->inum, (long long)info->gen,
			    user_name(inode_get_user(inode)),
			    host_name(srcs[i]));
			remove_num_total++;
			remove_size_total += inode_get_size(inode);
		}

		/* ignore error except GFARM_ERR_FILE_BUSY */
		/*
		 * When inode_schedule_replication() is creating
		 * replicas, inode_remove_replica_protected() may
		 * return GFARM_ERR_FILE_BUSY.
		 */
		if (e2 == GFARM_ERR_FILE_BUSY)
			e = e2; /* retry */

		/*
		 * Replicas cannot be moved between hostgroups
		 * (ex. group1:2,group2:0 to group1:0,group2:2) at a
		 * time of replica_check, because replicas cannot be
		 * removed when inode_remove_replica_protected()
		 * returns GFARM_ERR_FILE_BUSY or
		 * GFARM_ERR_INSUFFICIENT_NUMBER_OF_FILE_REPLICAS.
		 *
		 * When GFARM_ERR_FILE_BUSY is returned, replica_check
		 * will retry, and excessive replicas will be removed
		 * at next time.
		 *
		 * When GFARM_ERR_INSUFFICIENT_NUMBER_OF_FILE_REPLICAS
		 * is returned, replica_check does not retry, and
		 * excessive replicas cannot be removed immediately
		 * (may be removed later).
		 *
		 * If inode_remove_replica_protected() counts the
		 * total number of valid replicas and incomplete
		 * replicas after
		 * GFARM_ERR_INSUFFICIENT_NUMBER_OF_FILE_REPLICAS
		 * case, and GFARM_ERR_FILE_BUSY is returned,
		 * excessive replicas can be removed at next time.
		 *
		 * But, If incomplete replicas exists forever,
		 * replica_check retries forever.  Therefore
		 * inode_remove_replica_protected() does not use the
		 * number of incomplete replicas.
		 */
	}
	return (e); /* GFARM_ERR_FILE_BUSY: retry */
}

static gfarm_error_t
replica_check_fix(struct replication_info *info)
{
	gfarm_error_t e;
	struct inode *inode = inode_lookup(info->inum);
	int n_srcs, n_existing, n_being_removed, transaction = 0;
	struct host **srcs;
	struct hostset *existing, *being_removed;
	int req_ok_num = 0;
	static const char diag[] = "replica_check_fix";

	if (inode == NULL || inode_get_gen(inode) != info->gen) {
		gflog_debug(GFARM_MSG_1003623,
		    "replica_check: %lld:%lld was changed, ignore",
		    (long long)info->inum, (long long)info->gen);
		return (GFARM_ERR_NO_ERROR); /* ignore */
	}

	e = inode_replica_fix_prepare(inode, diag,
	    &n_srcs, &srcs,
	    &n_existing, &existing,
	    &n_being_removed, &being_removed);
	if (e != GFARM_ERR_NO_ERROR) {
		if (e == GFARM_ERR_NO_SUCH_OBJECT) /* 0byte file */
			return (GFARM_ERR_NO_ERROR); /* already OK */
		if (e == GFARM_ERR_NOT_A_REGULAR_FILE /* inode changed */ ||
		    e == GFARM_ERR_FILE_BUSY /* currently writing */) {
			gflog_debug(GFARM_MSG_UNFIXED,
			    "replica_check: %lld:%lld:%s: ignored: %s",
			    (long long)info->inum, (long long)info->gen,
			    user_name(inode_get_user(inode)),
			    gfarm_error_string(e));
			return (GFARM_ERR_NO_ERROR); /* ignore */
		}
		if (e == GFARM_ERR_INPUT_OUTPUT) { /* lost all reaplica */
			gflog_error(GFARM_MSG_1003624,
			    "replica_check: %lld:%lld:%s: lost all replicas",
			    (long long)info->inum, (long long)info->gen,
			    user_name(inode_get_user(inode)));
			return (GFARM_ERR_NO_ERROR); /* error, ignore */
		}
		if (e == GFARM_ERR_NO_FILESYSTEM_NODE) {
			/* all of gfsd are down, no available replica */
			REDUCED_WARN(GFARM_MSG_1004274, &hosts_down_state,
			    "replica_check: %lld:%lld:%s: "
			    "no available replica",
			    (long long)info->inum, (long long)info->gen,
			    user_name(inode_get_user(inode)));
			return (GFARM_ERR_NO_ERROR); /* ignore */
		}
		gflog_error(GFARM_MSG_UNFIXED,
		    "replica_check: %lld:%lld:%s: %s",
		    (long long)info->inum, (long long)info->gen,
		    user_name(inode_get_user(inode)), gfarm_error_string(e));
		return (e); /* probably GFARM_ERR_NO_MEMORY: retry */
	}

	/*
	 * it's faster to check this before inode_replica_fix_prepare(),
	 * but we do this after the function,
	 * because "lost all replica" cannot be detected otherwise.
	 */
	if (info->replica_spec.repattr == NULL &&
	    info->replica_spec.desired_number <= 0) { /* disabled */
		hostset_free(existing);
		hostset_free(being_removed);
		free(srcs);
		return (GFARM_ERR_NO_ERROR); /* skip */
	}

	e = db_begin(diag);
	if (e == GFARM_ERR_NO_ERROR)
		transaction = 1;
	else
		gflog_warning(GFARM_MSG_1004275,
		    "replica_check: db_begin(): %s", gfarm_error_string(e));

	/* create replicas if the replicas are insufficient */
	e = inode_schedule_replication(
	    inode, info->tdirset, 1,
	    info->replica_spec.desired_number, info->replica_spec.repattr,
	    n_srcs, srcs, &n_existing, existing,
	    gfarm_replica_check_host_down_thresh, /* giant_lock()ed */
	    &n_being_removed, being_removed, diag, &req_ok_num);

	if (e == GFARM_ERR_NO_ERROR && replica_check_remove_enabled())
		/* remove excessive number of replicas */
		e = replica_check_remove_replicas(inode, n_srcs, srcs, info);

	req_ok_num_total += req_ok_num;
	req_ok_size_total += inode_get_size(inode) * req_ok_num;

	if (transaction)
		db_end(diag);
	hostset_free(existing);
	hostset_free(being_removed);
	free(srcs);
	return (e);
}

static void
replica_check_desired_set(
	struct inode *dir_ino, struct inode *file_ino,
	struct replication_info *infop)
{
	char *repattr;
	int desired_number;

	if (inode_get_replica_spec(file_ino, &repattr, &desired_number) ||
	    inode_search_replica_spec(dir_ino, &repattr, &desired_number)) {
		infop->replica_spec.desired_number = desired_number;
		infop->replica_spec.repattr = repattr;
	} else {
		infop->replica_spec.desired_number = 0;
		infop->replica_spec.repattr = NULL;
	}
}

#define REPLICA_CHECK_DIRENTS_BUFCOUNT 512

static size_t replica_check_stack_size, replica_check_stack_index;
static struct replication_info *replica_check_stack;

static int
replica_check_stack_init(void)
{
	replica_check_stack_index = 0;
	replica_check_stack_size = REPLICA_CHECK_DIRENTS_BUFCOUNT;
	GFARM_MALLOC_ARRAY(replica_check_stack, replica_check_stack_size);
	if (replica_check_stack == NULL) {
		gflog_error(GFARM_MSG_1003630, "replica_check: no memory");
		return (0);
	}
	return (1);
}

static void
replica_check_stack_push(struct inode *dir_ino, struct inode *file_ino,
	struct dirset *tdirset)
{
	struct replication_info *rep_info =
	    &replica_check_stack[replica_check_stack_index];

	assert(replica_check_stack_index < replica_check_stack_size);

	rep_info->inum = inode_get_number(file_ino);
	rep_info->gen = inode_get_gen(file_ino);
	replica_check_desired_set(dir_ino, file_ino, rep_info);
	rep_info->tdirset = tdirset;
	if (tdirset != TDIRSET_IS_UNKNOWN && tdirset != TDIRSET_IS_NOT_SET)
		dirset_inc_busy_count(tdirset);

	replica_check_stack_index++;
}

static int
replica_check_stack_pop(struct replication_info *infop)
{
	if (replica_check_stack_index == 0)
		return (0);
	replica_check_stack_index--;
	*infop = replica_check_stack[replica_check_stack_index];
	return (1);
}

static void
replication_info_free(struct replication_info *rep_info)
{
	if (rep_info->tdirset != TDIRSET_IS_UNKNOWN &&
	    rep_info->tdirset != TDIRSET_IS_NOT_SET)
		dirset_dec_busy_count(rep_info->tdirset);
	replica_spec_free(&rep_info->replica_spec);
}

static void
sleep_time_update(int *varp, int nanosec)
{
	if (nanosec > GFARM_SECOND_BY_NANOSEC)
		nanosec = GFARM_SECOND_BY_NANOSEC;
	else if (nanosec < 0)
		nanosec = 0;

	*varp = nanosec;
}

static void
replica_check_giant_lock(void)
{
	if (yield_time > 0) {
		gfarm_nanosleep(yield_time);
		total_yield_time += .000000001 * yield_time;
	}
	if (!giant_trylock()) {
#ifdef DEBUG_REPLICA_CHECK
		RC_LOG_DEBUG(GFARM_MSG_1004276,
		    "replica_check: cannot get lock, sleep");
#endif
		gfarm_nanosleep(sleep_time);
		lock_sleep_time += .000000001 * sleep_time;

		giant_lock();
		/* gfarm_replica_check_sleep_time is giant_lock()ed */
		sleep_time_update(&sleep_time, gfarm_replica_check_sleep_time);
		sleep_time_update(&yield_time, gfarm_replica_check_yield_time);
	}
}

#define YIELD_INTERVAL 0  /* 0: every time */
static int yield_count = 0;

static void
replica_check_giant_unlock_yield(void)
{
	giant_unlock();
	if (++yield_count > YIELD_INTERVAL) {
		sched_yield(); /* lower priority than other threads */
		yield_count = 0;
	}
}

static void (*replica_check_giant_unlock)(void) = giant_unlock;

static void
replica_check_giant_lock_init()
{
	gfarm_error_t e;
	int stime, ytime;

	e = gfarm_pthread_set_priority_minimum("replica_check");
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_info(GFARM_MSG_1004279,
		    "replica_check: use sched_yield()");
		replica_check_giant_unlock = replica_check_giant_unlock_yield;
	}

	config_var_lock();
	stime = gfarm_replica_check_sleep_time;
	ytime = gfarm_replica_check_yield_time;
	config_var_unlock();
	sleep_time_update(&sleep_time, stime);
	sleep_time_update(&yield_time, ytime);
}

/*
 * these variables are protected by giant lock since they are also
 * accessed by a signal handler
 */
static gfarm_ino_t info_inum, info_table_size;
static time_t info_time_start;

static int
replica_check_main_dir(gfarm_ino_t inum, gfarm_ino_t *countp, int *stopped)
{
	gfarm_error_t e;
	struct inode *dir_ino, *file_ino;
	Dir dir;
	DirCursor cursor;
	gfarm_off_t dir_offset = 0;
	DirEntry entry;
	struct dirset *tdirset;
	struct replication_info rep_info;
	int need_to_retry = 0, eod = 0, i;

	*stopped = 0;
	while (!eod) {
		replica_check_giant_lock();
		info_inum = inum;
		dir_ino = inode_lookup(inum);
		if (dir_ino == NULL) {
			replica_check_giant_unlock();
			return (need_to_retry);
		}
		dir = inode_get_dir(dir_ino); /* include inode_is_dir() */
		if (dir == NULL) {
			replica_check_giant_unlock();
			return (need_to_retry);
		}
		if (!replica_check_ctrl_enabled()) { /* gfrepcheck stop */
			*stopped = 1;
			replica_check_giant_unlock();
			return (need_to_retry);
		}
		if (!dir_cursor_set_pos(dir, dir_offset, &cursor)) {
			replica_check_giant_unlock();
			return (need_to_retry);
		}
		tdirset = inode_search_tdirset(dir_ino);
		/* avoid long giant lock */
		for (i = 0; i < REPLICA_CHECK_DIRENTS_BUFCOUNT; i++) {
			entry = dir_cursor_get_entry(dir, &cursor);
			if (entry == NULL) {
				eod = 1; /* end of directory */
				break;
			}
			file_ino = dir_entry_get_inode(entry);
			if (inode_is_file(file_ino))
				replica_check_stack_push(dir_ino, file_ino,
				    tdirset);
			if (!dir_cursor_next(dir, &cursor)) {
				eod = 1; /* end of directory */
				break;
			}
		}
		dir_offset = dir_cursor_get_pos(dir, &cursor);
		replica_check_giant_unlock();

		while (replica_check_stack_pop(&rep_info)) {
			/* 1 milisec. */
			unsigned long long sl = GFARM_MILLISEC_BY_NANOSEC;

			for (;;) {
				replica_check_giant_lock();
				e = replica_check_fix(&rep_info);
				replica_check_giant_unlock();
				if (e !=
				    GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE)
					break; /* success or error */
				/* retry */
				gfarm_nanosleep(sl);
				retry_sleep_time += .000000001 * sl;
				if (sl < GFARM_SECOND_BY_NANOSEC)
					sl *= 2; /* 2,4,8,...,512,1024,1024 */
			}
			if (e != GFARM_ERR_NO_ERROR &&
			    e != GFARM_ERR_DISK_QUOTA_EXCEEDED) {
				need_to_retry = 1;
				gflog_debug(GFARM_MSG_1003631,
				    "replica_check_fix(): %s",
				    gfarm_error_string(e));
			}
			(*countp)++;
			replication_info_free(&rep_info);
		}
	}
	return (need_to_retry);
}

#define REPLICA_CHECK_INTERRUPT_STEP 10000

#define ENABLE_STR(b) ((b) ? "enable" : "disable")

static int
replica_check_main(void)
{
	gfarm_ino_t inum, table_size, count = 0;
	gfarm_ino_t root_inum = inode_root_number();
	int need_to_retry = 0, stopped;
	time_t time_total, time_start;

	replica_check_giant_lock();
	info_table_size = table_size = inode_table_current_size();
	info_time_start = time_start = time(NULL);
	replica_check_giant_unlock();

	lock_sleep_time = retry_sleep_time = 0;
	total_yield_time = 0;
	req_ok_num_total = req_ok_size_total = 0;
	remove_num_total = remove_size_total = 0;
	RC_LOG_INFO(GFARM_MSG_1003632, "replica_check: start");
	RC_LOG_INFO(GFARM_MSG_1004277,
	    "replica_check: remove=%s, reduced_log=%s",
	    ENABLE_STR(replica_check_remove_enabled()),
	    ENABLE_STR(replica_check_reduced_log_enabled()));
	RC_LOG_INFO(GFARM_MSG_1005015,
	    "replica_check: remove_grace_used_space_ratio=%d, "
	    "remove_grace_time=%d",
	    replica_check_remove_grace_used_space_ratio_locked(),
	    replica_check_remove_grace_time_locked());

	for (inum = root_inum;;) {
		if (inum % REPLICA_CHECK_INTERRUPT_STEP == 0 &&
		    !replica_check_ctrl_enabled()) { /* gfrepcheck stop */
			RC_LOG_INFO(GFARM_MSG_1005016,
			    "replica_check: stopped (interrupted)");
			break;
		}

		if (replica_check_main_dir(inum, &count, &stopped))
			need_to_retry = 1;
		if (stopped) { /* gfrepcheck stop */
			RC_LOG_INFO(GFARM_MSG_1005017,
			    "replica_check: stopped (interrupted)");
			break;
		}
		inum++; /* a next directory */
		if (inum >= table_size) {
			replica_check_giant_lock();
			info_table_size = table_size
				= inode_table_current_size();
			replica_check_giant_unlock();
			if (inum >= table_size)
				break;
		}
	}
	time_total = time(NULL) - time_start;
	RC_LOG_INFO(GFARM_MSG_1005040,
	    "replica_check: finished, table=%llu, inum=%llu, file=%llu, "
	    "rep num=%llu, rep size=%llu, remove num=%llu, remove size=%llu, "
	    "time=%lld (%.3fh), lock sleep=%g, retry sleep=%g",
	    (unsigned long long)table_size,
	    (unsigned long long)inum,
	    (unsigned long long)count,
	    (unsigned long long)req_ok_num_total,
	    (unsigned long long)req_ok_size_total,
	    (unsigned long long)remove_num_total,
	    (unsigned long long)remove_size_total,
	    (long long)time_total, (float)time_total / 60 / 60,
	    lock_sleep_time, retry_sleep_time);
	if (total_yield_time != 0) { /* hide undocumented unless it's set */
		RC_LOG_INFO(GFARM_MSG_1005049,
		    "replica_check: yield sleep=%g",
		    total_yield_time);
	}

	replica_check_giant_lock();
	info_time_start = 0;  /* stopped */
	replica_check_giant_unlock();

	return (need_to_retry);
}

/* this function is called from sigs_handler() in gfmd.c */
void
replica_check_info(void)
{
	gfarm_ino_t inum, table_size;
	time_t time_start, elapse;
	float progress;
	long long estimate;

	replica_check_giant_lock();
	table_size = info_table_size;
	inum = info_inum;
	time_start = info_time_start;
	replica_check_giant_unlock();

	if (time_start == 0 || table_size == 0) {
		RC_LOG_INFO(GFARM_MSG_1003756, "replica_check: standby");
		return;
	}

	elapse = time(NULL) - time_start;
	progress = (float)inum / (float)table_size;
	/* elapse / estimate_all = progress */
	/* estimate_all = elapse / progress */
	/* estimate = estimate_all - elapse */
	estimate = (long long)((float)elapse / progress - (float)elapse);

	RC_LOG_INFO(GFARM_MSG_1003757,
	    "replica_check: progress=%lld/%lld (%.2f%%),"
	    " elapse:estimate=%lld:%lld sec.",
	    (long long)inum, (long long)table_size, progress * 100,
	    (long long)elapse, estimate);
}


static const char REPLICA_CHECK_DIAG[] = "replica_check";
static pthread_mutex_t replica_check_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t replica_check_cond = PTHREAD_COND_INITIALIZER;
static int replica_check_initialized = 0; /* ignore cond_signal in startup */
static struct timeval *targets;
static size_t targets_num, targets_size;

#define MAX_TARGETS_SIZE 1024

static int
replica_check_timeval_cmp(const void *p1, const void *p2)
{
	const struct timeval *t1 = p1;
	const struct timeval *t2 = p2;

	return (-gfarm_timeval_cmp(t1, t2));
}

static int
replica_check_targets_init(void)
{
	targets_num = 0;
	targets_size = MAX_TARGETS_SIZE;
	GFARM_MALLOC_ARRAY(targets, targets_size);
	if (targets == NULL) {
		gflog_error(GFARM_MSG_1003634, "replica_check: no memory");
		return (0);
	}
	return (1);
}

/* do not call giant_lock() in this */
static void
replica_check_targets_add(time_t sec)
{
	size_t i;

	if (targets_num >= targets_size) {
		qsort(targets, targets_size, sizeof(struct timeval),
		    replica_check_timeval_cmp);
		i = targets_size / 2; /* replace center */
	} else
		i = targets_num++;

	gettimeofday(&targets[i], NULL);
	targets[i].tv_sec += sec;
#ifdef DEBUG_REPLICA_CHECK
	RC_LOG_DEBUG(GFARM_MSG_1003635,
	    "replica_check: add targets[%ld]=%ld.%06ld", (long)i,
	    (long)targets[i].tv_sec,
	    (long)targets[i].tv_usec);
#endif
}

/* do not call giant_lock() in this */
/* next target is not deleted */
static int
replica_check_targets_next(struct timeval *next, const struct timeval *now,
	int minimum_interval)
{
	size_t i;
	struct timeval now2;

	if (targets_num <= 0)
		return (0);
	if (targets_num == 1) {
		*next = targets[0];
#ifdef DEBUG_REPLICA_CHECK
		RC_LOG_DEBUG(GFARM_MSG_1003636,
		    "replica_check: targets[0]=%ld.%06ld",
		    (long)next->tv_sec, (long)next->tv_usec);
#endif
		return (1);
	}
	/* late to early */
	qsort(targets, targets_num, sizeof(struct timeval),
	    replica_check_timeval_cmp);

#ifdef DEBUG_REPLICA_CHECK
	for (i = 0; i < targets_num; i++)
		RC_LOG_DEBUG(GFARM_MSG_1003637,
		    "replica_check: targets[%ld]=%ld.%06ld", (long)i,
		    (long)targets[i].tv_sec, (long)targets[i].tv_usec);
#endif
	now2 = *now;

	/* integrate near target times */
	now2.tv_sec += minimum_interval;

	/* assert(targets_num >= 2); */
	for (i = targets_num - 1;; i--) {
		if (gfarm_timeval_cmp(&targets[i], &now2) > 0) {
			if (i == targets_num - 1) { /* future times only */
				*next = targets[i]; /* nearest future time */
				targets_num = i + 1;
				return (1);
			} else { /* latest past time */
				*next = targets[i + 1]; /* previous */
				targets_num = i + 2;
				return (1);
			}
		} /* else: skip past times (older than now2) */

		if (i == 0)
			break;
	}
	*next = targets[0];
	targets_num = 1;
	return (1);
}

static void
replica_check_targets_del(void)
{
	if (targets_num == 0)
		return;
	targets_num--;
}

static int
replica_check_timedwait(
	const struct timeval *tv, const struct timeval *now, const char *diag)
{
	struct timespec ts;

	if (gfarm_timeval_cmp(tv, now) <= 0)
		return (0); /* past time */

	ts.tv_sec = tv->tv_sec;
	ts.tv_nsec = (long)tv->tv_usec * GFARM_MICROSEC_BY_NANOSEC;
	return (gfarm_cond_timedwait(
	    &replica_check_cond, &replica_check_mutex,
	    &ts, diag, REPLICA_CHECK_DIAG));
}

static void
replica_check_cond_wait(void)
{
	static const char diag[] = "replica_check_cond_wait";
	struct timeval next, now;
	int minimum_interval = replica_check_minimum_interval_locked();

	gfarm_mutex_lock(&replica_check_mutex, diag, REPLICA_CHECK_DIAG);
	/* do not call giant_lock() in replica_check_mutex */
	for (;;) {
		gettimeofday(&now, NULL);
		if (replica_check_targets_next(
		    &next, &now, minimum_interval)) {
#ifdef DEBUG_REPLICA_CHECK
			{
				struct timeval after;

				after = next;
				gfarm_timeval_sub(&after, &now);
				RC_LOG_DEBUG(GFARM_MSG_1003638,
				    "replica_check: next=%ld.%06ld"
				    "(after %ld.%06ld)",
				    (long)next.tv_sec, (long)next.tv_usec,
				    (long)after.tv_sec, (long)after.tv_usec);
			}
#endif
			if (!replica_check_timedwait(&next, &now, diag)) {
				/* reach the next target time */
				replica_check_targets_del();
				break; /* execute */
			}
		} else /* no target time */
			gfarm_cond_wait(
			    &replica_check_cond, &replica_check_mutex,
			    diag, REPLICA_CHECK_DIAG);
		/* caught cond_signal */

		/*
		 * integrate many replica_check_start_*()
		 *
		 * waiting gfarm_replica_check_minimum_interval
		 * seconds here, and replica_check_targets_next() will
		 * gets the latest target time. (others are skipped)
		 */
		gfarm_mutex_unlock(
		    &replica_check_mutex, diag, REPLICA_CHECK_DIAG);
		gfarm_nanosleep((unsigned long long)minimum_interval *
		    GFARM_SECOND_BY_NANOSEC);
		gfarm_mutex_lock(
		    &replica_check_mutex, diag, REPLICA_CHECK_DIAG);
	}
	if (!replica_check_initialized)
		replica_check_initialized = 1;
	gfarm_mutex_unlock(&replica_check_mutex, diag, REPLICA_CHECK_DIAG);
}

static void
replica_check_cond_signal(const char *diag, long sec)
{
	gfarm_mutex_lock(&replica_check_mutex, diag, REPLICA_CHECK_DIAG);
	/* do not call giant_lock() in replica_check_mutex */
	if (replica_check_initialized) {
#ifdef DEBUG_REPLICA_CHECK
		RC_LOG_DEBUG(GFARM_MSG_1003639,
		    "replica_check: %s is called", diag);
#endif
		replica_check_targets_add(sec);
		gfarm_cond_signal(
		    &replica_check_cond, diag, REPLICA_CHECK_DIAG);
	}
#ifdef DEBUG_REPLICA_CHECK
	else
		RC_LOG_DEBUG(GFARM_MSG_1003640,
		    "replica_check: %s is ignored", diag);
#endif
	gfarm_mutex_unlock(&replica_check_mutex, diag, REPLICA_CHECK_DIAG);
}

void
replica_check_start_host_up(void)
{
	static const char diag[] = "replica_check_start_host_up";

	replica_check_cond_signal(diag, 0);
}

void
replica_check_start_host_down(void)
{
	static const char diag[] = "replica_check_start_host_down";

	replica_check_cond_signal(diag,
	    replica_check_host_down_thresh_locked());
	/* NOTE: execute replica_check_main() twice after restarting gfsd */
}

void
replica_check_start_xattr_update(void)
{
	static const char diag[] = "replica_check_start_xattr_update";

	replica_check_cond_signal(diag, 0);
}

void
replica_check_start_move(void)
{
	static const char diag[] = "replica_check_start_move";

	replica_check_cond_signal(diag, 0);
}

void
replica_check_start_rep_request_failed(void)
{
	static const char diag[] = "replica_check_start_rep_request_failed";

	replica_check_cond_signal(diag, 0);
}

void
replica_check_start_rep_result_failed(void)
{
	static const char diag[] = "replica_check_start_rep_result_failed";

	replica_check_cond_signal(diag, 0);
}

void
replica_check_start_fsngroup_modify(void)
{
	static const char diag[] = "replica_check_start_fsngroup_modify";

	replica_check_cond_signal(diag, 60);  /* 60 sec. */
}

void
replica_check_start_host_is_not_busy(void)
{
	static const char diag[] = "replica_check_start_host_is_not_busy";

	replica_check_cond_signal(diag, 0);
}

#define ENABLE  1
#define DISABLE 0

static const char REPLICA_CHECK_CTRL_DIAG[] = "replica_check_ctrl";
static pthread_mutex_t replica_check_ctrl_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t replica_check_ctrl_cond = PTHREAD_COND_INITIALIZER;
static int replica_check_ctrl_waiting = 0;
static int replica_check_ctrl = DISABLE;
static int replica_check_remove = DISABLE;
static int replica_check_reduced_log = DISABLE;

/* MAINCTRL */
static void
replica_check_ctrl_set(int ctrl)
{
	static const char diag[] = "replica_check_ctrl_set";

	gfarm_mutex_lock(&replica_check_ctrl_mutex, diag,
	    REPLICA_CHECK_CTRL_DIAG);

	/* escape from replica_check_cond_wait() */
	if (replica_check_ctrl_waiting == 0)
		replica_check_cond_signal(diag, 0);

	replica_check_ctrl = ctrl;
	gfarm_cond_signal(&replica_check_ctrl_cond, diag,
	    REPLICA_CHECK_CTRL_DIAG);
	gfarm_mutex_unlock(&replica_check_ctrl_mutex, diag,
	    REPLICA_CHECK_CTRL_DIAG);
}

/* MAINCTRL */
static int
replica_check_ctrl_enabled(void)
{
	static const char diag[] = "replica_check_ctrl_enabled";
	int ctrl;

	gfarm_mutex_lock(&replica_check_ctrl_mutex, diag,
			 REPLICA_CHECK_CTRL_DIAG);
	ctrl = replica_check_ctrl;
	gfarm_mutex_unlock(&replica_check_ctrl_mutex, diag,
			   REPLICA_CHECK_CTRL_DIAG);

	return (ctrl == ENABLE);
}

/* MAINCTRL */
static void
replica_check_ctrl_wait(void)
{
	static const char diag[] = "replica_check_ctrl_wait";

	gfarm_mutex_lock(&replica_check_ctrl_mutex, diag,
	    REPLICA_CHECK_CTRL_DIAG);
	while (replica_check_ctrl == DISABLE) {
		gflog_info(GFARM_MSG_1003758, "replica_check: stopped");
		replica_check_ctrl_waiting = 1;
		gfarm_cond_wait(&replica_check_ctrl_cond,
		    &replica_check_ctrl_mutex, diag, REPLICA_CHECK_CTRL_DIAG);
	}
	replica_check_ctrl_waiting = 0;
	gfarm_mutex_unlock(&replica_check_ctrl_mutex, diag,
	    REPLICA_CHECK_CTRL_DIAG);
}

static void
replica_check_remove_set(int ctrl)
{
	static const char diag[] = "replica_check_remove_set";

	gfarm_mutex_lock(&replica_check_ctrl_mutex, diag,
	    REPLICA_CHECK_CTRL_DIAG);
	replica_check_remove = ctrl;
	gfarm_mutex_unlock(&replica_check_ctrl_mutex, diag,
	    REPLICA_CHECK_CTRL_DIAG);
}

static int
replica_check_remove_enabled(void)
{
	static const char diag[] = "replica_check_remove_enabled";
	int ctrl;

	gfarm_mutex_lock(&replica_check_ctrl_mutex, diag,
	    REPLICA_CHECK_CTRL_DIAG);
	ctrl = replica_check_remove;
	gfarm_mutex_unlock(&replica_check_ctrl_mutex, diag,
	    REPLICA_CHECK_CTRL_DIAG);

	return (ctrl == ENABLE);
}

static void
replica_check_reduced_log_set(int ctrl)
{
	static const char diag[] = "replica_check_reduced_log_set";

	gfarm_mutex_lock(&replica_check_ctrl_mutex, diag,
	    REPLICA_CHECK_CTRL_DIAG);
	replica_check_reduced_log = ctrl;
	gfarm_mutex_unlock(&replica_check_ctrl_mutex, diag,
	    REPLICA_CHECK_CTRL_DIAG);
}

static int
replica_check_reduced_log_enabled(void)
{
	static const char diag[] = "replica_check_reduced_log_enabled";
	int ctrl;

	gfarm_mutex_lock(&replica_check_ctrl_mutex, diag,
	    REPLICA_CHECK_CTRL_DIAG);
	ctrl = replica_check_reduced_log;
	gfarm_mutex_unlock(&replica_check_ctrl_mutex, diag,
	    REPLICA_CHECK_CTRL_DIAG);

	return (ctrl == ENABLE);
}

static const char *
replica_check_ctrl_string(gfarm_int32_t ctrl)
{
	switch (ctrl) {
	case GFM_PROTO_REPLICA_CHECK_CTRL_START:
		return ("START");
	case GFM_PROTO_REPLICA_CHECK_CTRL_STOP:
		return ("STOP");
	case GFM_PROTO_REPLICA_CHECK_CTRL_REMOVE_ENABLE:
		return ("REMOVE_ENABLE");
	case GFM_PROTO_REPLICA_CHECK_CTRL_REMOVE_DISABLE:
		return ("REMOVE_DISABLE");
	case GFM_PROTO_REPLICA_CHECK_CTRL_REDUCED_LOG_ENABLE:
		return ("REDUCED_LOG_ENABLE");
	case GFM_PROTO_REPLICA_CHECK_CTRL_REDUCED_LOG_DISABLE:
		return ("REDUCED_LOG_DISABLE");
	default:
		return ("<UNKNOWN>");
	}
}

gfarm_error_t
gfm_server_replica_check_ctrl(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_int32_t ctrl;
	struct user *user;
	static const char diag[] = "GFM_PROTO_REPLICA_CHECK_CTRL";

	e = gfm_server_get_request(peer, diag, "i", &ctrl);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	giant_lock();
	if (!from_client || (user = peer_get_user(peer)) == NULL ||
	    !user_is_admin(user)) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_1003759, "%s", gfarm_error_string(e));
	} else {
		gflog_info(GFARM_MSG_1004278,
		    "replica_check: ctrl=%s",
		    replica_check_ctrl_string(ctrl));
		switch (ctrl) {
		case GFM_PROTO_REPLICA_CHECK_CTRL_START: /* MAINCTRL */
			replica_check_ctrl_set(ENABLE);
			break;
		case GFM_PROTO_REPLICA_CHECK_CTRL_STOP: /* MAINCTRL */
			replica_check_ctrl_set(DISABLE);
			break;
		case GFM_PROTO_REPLICA_CHECK_CTRL_REMOVE_ENABLE:
			replica_check_remove_set(ENABLE);
			break;
		case GFM_PROTO_REPLICA_CHECK_CTRL_REMOVE_DISABLE:
			replica_check_remove_set(DISABLE);
			break;
		case GFM_PROTO_REPLICA_CHECK_CTRL_REDUCED_LOG_ENABLE:
			replica_check_reduced_log_set(ENABLE);
			break;
		case GFM_PROTO_REPLICA_CHECK_CTRL_REDUCED_LOG_DISABLE:
			replica_check_reduced_log_set(DISABLE);
			break;
		default:
			e = GFARM_ERR_INVALID_ARGUMENT;
			gflog_debug(GFARM_MSG_1003760, "%s: %d",
			    gfarm_error_string(e), ctrl);
		}
	}
	giant_unlock();
	return (gfm_server_put_reply(peer, diag, e, ""));
}

static const char *
replica_check_status_target_string(gfarm_int32_t status_target)
{
	switch (status_target) {
	case GFM_PROTO_REPLICA_CHECK_STATUS_MAINCTRL:
		return ("MAINCTRL");
	case GFM_PROTO_REPLICA_CHECK_STATUS_REMOVE:
		return ("REMOVE");
	case GFM_PROTO_REPLICA_CHECK_STATUS_REDUCED_LOG:
		return ("REDUCED_LOG");
	default:
		return ("<UNKNOWN>");
	}
}

static gfarm_int32_t
replica_check_status_mainctrl(void) {
	gfarm_int32_t rv;

	if (replica_check_ctrl_enabled()) {
		/* info_time_start needs giant_lock() */
		if (info_time_start != 0)
			rv = GFM_PROTO_REPLICA_CHECK_STATUS_ENABLE_RUNNING;
		else
			rv = GFM_PROTO_REPLICA_CHECK_STATUS_ENABLE_STOPPED;
	} else {
		/* info_time_start needs giant_lock() */
		if (info_time_start != 0)
			rv = GFM_PROTO_REPLICA_CHECK_STATUS_DISABLE_RUNNING;
		else
			rv = GFM_PROTO_REPLICA_CHECK_STATUS_DISABLE_STOPPED;
	}
	return (rv);
}

gfarm_error_t
gfm_server_replica_check_status(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_int32_t status_target;
	gfarm_int32_t status = -1;
	struct user *user;
	static const char diag[] = "GFM_PROTO_REPLICA_CHECK_STATUS";

	e = gfm_server_get_request(peer, diag, "i", &status_target);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	giant_lock();
	if (!from_client || (user = peer_get_user(peer)) == NULL ||
	    !user_is_admin(user)) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_1005041, "%s", gfarm_error_string(e));
	} else {
		gflog_debug(GFARM_MSG_1005042,
		    "replica_check_status: target=%s",
		    replica_check_status_target_string(status_target));
		switch (status_target) {
		case GFM_PROTO_REPLICA_CHECK_STATUS_MAINCTRL:
			status = replica_check_status_mainctrl();
			break;
		case GFM_PROTO_REPLICA_CHECK_STATUS_REMOVE:
			status = replica_check_remove_enabled() ?
			    GFM_PROTO_REPLICA_CHECK_STATUS_ENABLE :
			    GFM_PROTO_REPLICA_CHECK_STATUS_DISABLE;
			break;
		case GFM_PROTO_REPLICA_CHECK_STATUS_REDUCED_LOG:
			status = replica_check_reduced_log_enabled() ?
			    GFM_PROTO_REPLICA_CHECK_STATUS_ENABLE :
			    GFM_PROTO_REPLICA_CHECK_STATUS_DISABLE;
			break;
		default:
			e = GFARM_ERR_INVALID_ARGUMENT;
			gflog_debug(GFARM_MSG_1005043,
			    "replica_check_status: %s: target=%d",
			    gfarm_error_string(e), status_target);
		}
	}
	giant_unlock();
	if (e != GFARM_ERR_NO_ERROR)
		e = gfm_server_put_reply(peer, diag, e, "");
	else
		e = gfm_server_put_reply(peer, diag, e, "i", status);
	return (e);
}

/* workaround for #406 - obsolete replicas remain existing */
#define DFC_SCAN_INTERVAL 21600 /* 6 hours */

static void *
replica_check_thread(void *arg)
{
	time_t dfc_scan_time = 0;

	replica_check_giant_lock_init();

	for (;;) {
		time_t t = time(NULL) +
		    replica_check_minimum_interval_locked();

		replica_check_cond_wait();
		replica_check_ctrl_wait(); /* if the ctrl is stop, wait here */
		if (replica_check_main()) /* error occured, retry */
			replica_check_cond_signal("retry_by_error",
			    gfarm_metadb_heartbeat_interval);

		/* call dead_file_copy_scan_deferred_all */
		if (time(NULL) >= dfc_scan_time) {
			replica_check_giant_lock();
			dead_file_copy_scan_deferred_all();
			replica_check_giant_unlock();

			dfc_scan_time = time(NULL) + DFC_SCAN_INTERVAL;
			replica_check_cond_signal("dead_file_copy_scan",
			    DFC_SCAN_INTERVAL);
			RC_LOG_DEBUG(GFARM_MSG_1003641,
			    "replica_check: dead_file_copy_scan_deferred_all,"
			    " next=%ld", (long)dfc_scan_time);
		}
		t = t - time(NULL);
		if (t > 0)
			gfarm_sleep(t);
	}
	/*NOTREACHED*/
	return (NULL);
}

void
replica_check_init(void)
{
	gfarm_error_t e;

	if (gfarm_replica_check)
		replica_check_ctrl_set(ENABLE);
	else
		replica_check_ctrl_set(DISABLE);

	if (gfarm_replica_check_remove)
		replica_check_remove_set(ENABLE);
	else
		replica_check_remove_set(DISABLE);

	if (gfarm_replica_check_reduced_log)
		replica_check_reduced_log_set(ENABLE);
	else
		replica_check_reduced_log_set(DISABLE);

	if (!replica_check_stack_init() || !replica_check_targets_init())
		return;
	/* initial target: wait for startup of gfsd hosts */
	replica_check_targets_add(gfarm_metadb_heartbeat_interval);

	if ((e = create_detached_thread(replica_check_thread, NULL))
	    != GFARM_ERR_NO_ERROR)
		gflog_fatal(GFARM_MSG_1004293,
		    "create_detached_thread(replica_check_thread): %s",
		    gfarm_error_string(e));
}
