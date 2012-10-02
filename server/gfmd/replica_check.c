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
#include <string.h>

#include <gfarm/error.h>
#include <gfarm/gflog.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

#include "gfutil.h"
#include "nanosec.h"
#include "thrsubr.h"

#include "config.h"

#include "gfmd.h"
#include "inode.h"
#include "dir.h"
#include "host.h"
#include "subr.h"
#include "user.h"
#include "back_channel.h"

/* for debug */
/* #define DEBUG_REPLICA_CHECK or CFLAGS='-DDEBUG_REPLICA_CHECK' */

#ifdef DEBUG_REPLICA_CHECK
#define RC_LOG_DEBUG gflog_warning
#define RC_LOG_INFO gflog_warning
#else
#define RC_LOG_DEBUG gflog_debug
#define RC_LOG_INFO gflog_info
#endif

#define REPLICA_CHECK_SUPPRESS_LOG_MAX 200
static int log_count, log_suppressed;

static int suppress_log()
{
	if (log_count >= REPLICA_CHECK_SUPPRESS_LOG_MAX) {
		if (!log_suppressed) {
			gflog_warning(GFARM_MSG_UNFIXED,
			    "suppress many messages for replica_check");
			log_suppressed = 1;
		}
		return (1);
	}
	log_count++;
	return (0);
}

static void suppress_log_reset()
{
	log_count = log_suppressed = 0;
}

static gfarm_error_t
replica_check_replicate(
	struct inode *inode, int *n_srcsp, struct host **srcs,
	int n_desire, int ncopy)
{
	gfarm_error_t e;
	struct host **dsts, *src, *dst;
	int n_dsts, i, j, n_success = 0;
	struct file_replicating *fr;
	gfarm_off_t necessary_space;
	int n_shortage, busy;

	n_shortage = n_desire - ncopy;
	assert(n_shortage > 0);

	necessary_space = inode_get_size(inode);
	e = host_schedule_n_from_all_except(
	    n_srcsp, srcs, host_is_disk_available_filter,
	    &necessary_space, n_shortage, &n_dsts, &dsts);
	if (e != GFARM_ERR_NO_ERROR) {
		/* no memory ? */
		gflog_error(GFARM_MSG_UNFIXED,
		    "host_schedule_except: n_srcs=%d%s",
		    *n_srcsp, gfarm_error_string(e));
		goto end; /* retry in next interval */
	}
	/* dsts is scheduled */

	busy = 0;
	for (i = 0, j = 0; i < n_dsts; i++, j++) {
		if (j >= *n_srcsp)
			j = 0;
		src = srcs[j];
		dst = dsts[i];

		/* GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE may occurs */
		e = file_replicating_new(inode, dst, NULL, &fr);
		if (e == GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE) {
			busy = 1;
			gflog_debug(GFARM_MSG_UNFIXED,
			    "file_replicating_new: host %s: %s",
			    host_name(dst), gfarm_error_string(e));
			/* next dst */
		} else if (e != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_UNFIXED,
			    "file_replicating_new: host %s: %s",
			    host_name(dst), gfarm_error_string(e));
			break;
		} else if ((e = async_back_channel_replication_request(
		    host_name(src), host_port(src), dst,
		    inode_get_number(inode), inode_get_gen(inode), fr))
			   != GFARM_ERR_NO_ERROR) {
			/* no memory ? */
			file_replicating_free(fr);
			gflog_error(GFARM_MSG_UNFIXED,
			    "async_back_channel_replication_request: %s",
			    gfarm_error_string(e));
			break;
		} else
			n_success++;
	}
	free(dsts);
	if (busy) /* retry immediately */
		return (GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE);
end:
	if (n_success < n_shortage) {
		if (!suppress_log())
			gflog_warning(GFARM_MSG_UNFIXED,
			    "%lld:%lld:%s: fewer replicas, "
			    "increase=%d/before=%d/desire=%d",
			    (long long)inode_get_number(inode),
			    (long long)inode_get_gen(inode),
			    user_name(inode_get_user(inode)),
			    n_success, ncopy, n_desire);
	} else
		gflog_notice(GFARM_MSG_UNFIXED,
		    "%lld:%lld:%s: "
		    "replicas will be fixed (might be failed), ncopy=%d",
		    (long long)inode_get_number(inode),
		    (long long)inode_get_gen(inode),
		    user_name(inode_get_user(inode)), n_desire);

	return (e); /* error: retry in next interval */

}

struct replication_info {
	gfarm_ino_t inum;
	gfarm_uint64_t gen;
	int desired_number;
};

static gfarm_error_t
replica_check_fix(struct replication_info *info)
{
	struct inode *inode = inode_lookup(info->inum);
	int n_srcs, ncopy;
	struct host **srcs;
	gfarm_error_t e;

	if (inode == NULL || !inode_is_file(inode) ||
	    inode_get_gen(inode) != info->gen) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%lld:%lld was changed, ignore replica_check",
		    (long long)info->inum, (long long)info->gen);
		return (GFARM_ERR_NO_ERROR); /* ignore */
	}
	if (info->desired_number <= 0) /* normally */
		return (GFARM_ERR_NO_ERROR);

	/* is_valid==0: invalid replicas (==replicating now) are OK */
	ncopy = inode_get_ncopy_with_grace_of_dead(
	    inode, 0, gfarm_replica_check_host_down_thresh);
	if (ncopy == info->desired_number) /* normally */
		return (GFARM_ERR_NO_ERROR);
	if (ncopy == 0) { /* all gfsd are down or all replicas are lost */
		if (inode_get_size(inode) > 0) {
			if (!suppress_log())
				gflog_warning(GFARM_MSG_UNFIXED,
				    "%lld:%lld:%s: "
				    "no available replica for replica_check",
				    (long long)info->inum,
				    (long long)info->gen,
				    user_name(inode_get_user(inode)));
			return (GFARM_ERR_NO_ERROR); /* not retry */
		}
		/* else: normally */
		return (GFARM_ERR_NO_ERROR);
	}
	if (ncopy > info->desired_number) {
		if (!suppress_log())
			gflog_info(GFARM_MSG_UNFIXED,
			   "%lld:%lld:%s: "
			   "too many replicas for replica_check: %d > %d",
			   (long long)info->inum, (long long)info->gen,
			   user_name(inode_get_user(inode)),
			   ncopy, info->desired_number);
		/* XXX remove too many replicas? */
		return (GFARM_ERR_NO_ERROR);
	}
	if (inode_is_opened_for_writing(inode)) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%lld:%lld:%s: "
		    "opened in write mode, ignore replica_check",
		    (long long)info->inum, (long long)info->gen,
		    user_name(inode_get_user(inode)));
		return (GFARM_ERR_NO_ERROR); /* ignore */
	}
	/* available replicas for source */
	e = inode_replica_list(inode, &n_srcs, &srcs);
	if (e != GFARM_ERR_NO_ERROR) { /* no memory */
		gflog_error(GFARM_MSG_UNFIXED,
		    "%lld:%lld:%s: replica_list for replica_check: %s",
		    (long long)info->inum, (long long)info->gen,
		    user_name(inode_get_user(inode)), gfarm_error_string(e));
		return (e); /* retry */
	}
	/* n_srcs may be 0, because host_is_up() may change */
	if (n_srcs <= 0) {
		gflog_info(GFARM_MSG_UNFIXED,
		    "%lld:%lld:%s: "
		    "source hosts are down for replica_check",
		    (long long)info->inum, (long long)info->gen,
		    user_name(inode_get_user(inode)));
		free(srcs);
		return (GFARM_ERR_FILE_BUSY); /* retry afer a while */
	}
	/* ncopy is not necessarily the same as n_src */
	e = replica_check_replicate(inode, &n_srcs, srcs,
	    info->desired_number, ncopy);
	free(srcs);

	return (e);
}

static int
replica_check_desired_number(struct inode *dir_ino, struct inode *file_ino)
{
	int desired_number;

	if (inode_has_desired_number(file_ino, &desired_number) ||
	    inode_traverse_desired_replica_number(dir_ino, &desired_number))
		return (desired_number);
	return (0);
}

static size_t replica_check_stack_size, replica_check_stack_index;
static struct replication_info *replica_check_stack;

static int
replica_check_stack_init()
{
	replica_check_stack_index = 0;
	replica_check_stack_size = 32;
	GFARM_MALLOC_ARRAY(replica_check_stack, replica_check_stack_size);
	if (replica_check_stack == NULL) {
		gflog_error(GFARM_MSG_UNFIXED, "replica_check: no memory");
		return (0);
	}
	return (1);
}

static void
replica_check_stack_push(struct inode *dir_ino, struct inode *file_ino)
{
	static int failed = 0;
	struct replication_info *tmp;
	int new_size;

	if (replica_check_stack_size <= replica_check_stack_index) {
		new_size = replica_check_stack_size * 2;
		GFARM_REALLOC_ARRAY(tmp, replica_check_stack, new_size);
		if (tmp == NULL) {
			if (failed == 0)
				gflog_error(GFARM_MSG_UNFIXED,
				    "replica_check: realloc: no memory");
			failed = 1;
			return;
		}
		replica_check_stack_size = new_size;
		replica_check_stack = tmp;
	}
	failed = 0;
	replica_check_stack[replica_check_stack_index].inum
		= inode_get_number(file_ino);
	replica_check_stack[replica_check_stack_index].gen
		= inode_get_gen(file_ino);
	replica_check_stack[replica_check_stack_index].desired_number
		= replica_check_desired_number(dir_ino, file_ino);
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
replica_check_giant_lock_default()
{
	if (giant_trylock()) {
		gfarm_nanosleep(gfarm_replica_check_sleep_time);
		giant_lock();
	}
}

static void (*replica_check_giant_lock)(void);
static void (*replica_check_giant_unlock)(void) = giant_unlock;

static int
replica_check_main()
{
	gfarm_ino_t i, root_inum, table_size, count = 0;
	struct inode *dir_ino, *file_ino;
	Dir dir;
	DirCursor cursor;
	DirEntry entry;
	struct replication_info rep_info;
	gfarm_error_t e;
	int need_to_retry = 0;

	root_inum = inode_root_number();

	replica_check_giant_lock();
	table_size = inode_table_current_size();
	replica_check_giant_unlock();

	RC_LOG_INFO(GFARM_MSG_UNFIXED, "replica_check: start");
	for (i = root_inum;;) {
		replica_check_giant_lock();
		dir_ino = inode_lookup(i);
		if (dir_ino && inode_is_dir(dir_ino)) {
			dir = inode_get_dir(dir_ino);
			assert(dir_cursor_set_pos(dir, 0, &cursor));
			do {
				entry = dir_cursor_get_entry(dir, &cursor);
				if (entry == NULL)
					break;
				file_ino = dir_entry_get_inode(entry);
				if (inode_is_file(file_ino))
					replica_check_stack_push(
					    dir_ino, file_ino);
			} while (dir_cursor_next(dir, &cursor) != 0);
		}
		replica_check_giant_unlock();

		while (replica_check_stack_pop(&rep_info)) {
			unsigned long long sl = GFARM_MILLISEC_BY_NANOSEC;
			/* 1 milisec. */

			for (;;) {
				replica_check_giant_lock();
				e = replica_check_fix(&rep_info);
				replica_check_giant_unlock();
				if (e !=
				    GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE)
					break; /* success */
				gfarm_nanosleep(sl);
				if (sl < GFARM_SECOND_BY_NANOSEC)
					sl *= 2; /* 2,4,8,...,512,1024,1024 */
				/* retry */
			}
			if (e != GFARM_ERR_NO_ERROR) {
				need_to_retry = 1;
				gflog_debug(GFARM_MSG_UNFIXED,
				    "replica_check_fix(): %s",
				    gfarm_error_string(e));
			}
			count++;
		}

		i++; /* a next directory */
		if (i >= table_size) {
			replica_check_giant_lock();
			table_size = inode_table_current_size();
			replica_check_giant_unlock();
			if (i >= table_size)
				break;
		}
	}
	RC_LOG_INFO(GFARM_MSG_UNFIXED,
	    "replica_check: finished, files=%llu", (unsigned long long)count);
	return (need_to_retry);
}

#define REPLICA_CHECK_DIAG "replica_check"

static pthread_mutex_t replica_check_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t replica_check_cond = PTHREAD_COND_INITIALIZER;
static int replica_check_initialized = 0; /* ignore cond_signal in startup */
static struct timeval *targets;
static size_t targets_num, targets_size;

static int
replica_check_targets_init()
{
	targets_num = 0;
	targets_size = 32;
	GFARM_MALLOC_ARRAY(targets, targets_size);
	if (targets == NULL) {
		gflog_error(GFARM_MSG_UNFIXED, "replica_check: no memory");
		return (0);
	}
	return (1);
}

static void
replica_check_targets_add(time_t sec)
{
	static int failed = 0;
	struct timeval *tmp;
	int new_size;

	if (targets_size <= targets_num) {
		new_size = targets_size * 2;
		GFARM_REALLOC_ARRAY(tmp, targets, new_size);
		if (tmp == NULL) {
			if (failed == 0)
				gflog_error(GFARM_MSG_UNFIXED,
				    "replica_check: realloc: no memory");
			failed = 1;
			return;
		}
		targets_size = new_size;
		targets = tmp;
	}
	failed = 0;
	gettimeofday(&targets[targets_num], NULL);
	targets[targets_num].tv_sec += sec;
#ifdef DEBUG_REPLICA_CHECK
	RC_LOG_DEBUG(GFARM_MSG_UNFIXED,
	    "replica_check: add targets[%ld]=%ld.%06ld", (long)targets_num,
	    (long)targets[targets_num].tv_sec,
	    (long)targets[targets_num].tv_usec);
#endif
	targets_num++;
}

static int
replica_check_timeval_cmp(const void *p1, const void *p2)
{
	const struct timeval *t1 = p1;
	const struct timeval *t2 = p2;

	return (-gfarm_timeval_cmp(t1, t2));
}

/* ignore targets within 10 sec. future. */
#define INTEGRATE_NEAR_TARGETS 10 /* sec. */

/* not delete */
static int
replica_check_targets_next(struct timeval *next, const struct timeval *now)
{
	size_t i;
	int is_future;
	struct timeval target_tv, future_tv;
	static struct timeval saved_tv = {.tv_sec = 0, .tv_usec = 0};

	if (targets_num <= 0)
		return (0);
	if (targets_num == 1) {
		*next = targets[0];
		saved_tv = *next;
#ifdef DEBUG_REPLICA_CHECK
		RC_LOG_DEBUG(GFARM_MSG_UNFIXED,
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
		RC_LOG_DEBUG(GFARM_MSG_UNFIXED,
		    "replica_check: targets[%ld]=%ld.%06ld", (long)i,
		    (long)targets[i].tv_sec, (long)targets[i].tv_usec);
#endif
	target_tv = *now;
	target_tv.tv_sec += INTEGRATE_NEAR_TARGETS;
	is_future = 0;
	/* assert(targets_num >= 2); */
	for (i = targets_num - 1;; i--) {
		if (saved_tv.tv_usec == targets[i].tv_usec && /* optimized */
		    saved_tv.tv_sec == targets[i].tv_sec) {
			/* do not select newer targets than this. */
			*next = targets[i];
			targets_num = i + 1;
			return (1);
		} else if (is_future) {
			if (gfarm_timeval_cmp(&targets[i], &future_tv) > 0) {
				*next = targets[i + 1]; /* previous */
				targets_num = i + 2;
				saved_tv = *next;
				return (1);
			} else if (i == 0) {
				*next = targets[i];
				targets_num = i + 1;
				saved_tv = *next;
				return (1);
			} /* else: skip near future time */
		} else if (gfarm_timeval_cmp(&targets[i], &target_tv) > 0) {
			if (i == targets_num - 1) { /* future times only */
				future_tv = targets[i];
				future_tv.tv_sec += INTEGRATE_NEAR_TARGETS;
				is_future = 1;
				/* continue */
			} else { /* nearest past time */
				*next = targets[i + 1]; /* previous */
				targets_num = i + 2;
				saved_tv = *next;
				return (1);
			}
		} else {
			/* skip past times (older than target_tv) */
			if (gfarm_timeval_cmp(&saved_tv, &target_tv) > 0)
				/* to select past times only */
				saved_tv.tv_sec = saved_tv.tv_usec = 0;
		}

		if (i == 0)
			break;
	}
	*next = targets[0];
	targets_num = 1;
	saved_tv = *next;
	return (1);
}

static void
replica_check_targets_del()
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
replica_check_wait()
{
	static const char diag[] = "replica_check_wait";
	struct timeval next, now;

	gfarm_mutex_lock(&replica_check_mutex, diag, REPLICA_CHECK_DIAG);
	for (;;) {
		gettimeofday(&now, NULL);
		if (replica_check_targets_next(&next, &now)) {
#ifdef DEBUG_REPLICA_CHECK
			{
				struct timeval after;

				after = next;
				gfarm_timeval_sub(&after, &now);
				RC_LOG_DEBUG(GFARM_MSG_UNFIXED,
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
		 * integrate many replica_check_signal_*()
		 *
		 * waiting INTEGRATE_NEAR_TARGETS sec. here, and
		 * replica_check_targets_next() will gets the latest
		 * target time. (others are skipped)
		 */
		gfarm_mutex_unlock(
		    &replica_check_mutex, diag, REPLICA_CHECK_DIAG);
		gfarm_nanosleep(
		    (unsigned long long)INTEGRATE_NEAR_TARGETS *
		    GFARM_SECOND_BY_NANOSEC);
		gfarm_mutex_lock(
		    &replica_check_mutex, diag, REPLICA_CHECK_DIAG);
	}
	if (!replica_check_initialized)
		replica_check_initialized = 1;
	gfarm_mutex_unlock(&replica_check_mutex, diag, REPLICA_CHECK_DIAG);
}

static void
replica_check_signal_common(const char *diag, time_t sec)
{
	if (!gfarm_replica_check)
		return;

	gfarm_mutex_lock(&replica_check_mutex, diag, REPLICA_CHECK_DIAG);
	if (replica_check_initialized) {
#ifdef DEBUG_REPLICA_CHECK
		RC_LOG_DEBUG(GFARM_MSG_UNFIXED, "%s is called", diag);
#endif
		replica_check_targets_add(sec);
		gfarm_cond_signal(
		    &replica_check_cond, diag, REPLICA_CHECK_DIAG);
	}
#ifdef DEBUG_REPLICA_CHECK
	else
		RC_LOG_DEBUG(GFARM_MSG_UNFIXED, "%s is ignored", diag);
#endif
	gfarm_mutex_unlock(&replica_check_mutex, diag, REPLICA_CHECK_DIAG);
}

void
replica_check_signal_host_up()
{
	static const char diag[] = "replica_check_signal_host_up";

	replica_check_signal_common(diag, 0);
}

void
replica_check_signal_host_down()
{
	static const char diag[] = "replica_check_signal_host_down";

	replica_check_signal_common(
	    diag, gfarm_replica_check_host_down_thresh);
	/* NOTE: execute replica_check_main() twice after restarting gfsd */
}

void
replica_check_signal_update_xattr()
{
	static const char diag[] = "replica_check_signal_update_xattr";

	replica_check_signal_common(diag, 0);
}

static void *
replica_check_thread(void *arg)
{
	int wait_time;

	if (!replica_check_stack_init())
		return (NULL);
	if (!replica_check_targets_init())
		return (NULL);

	if (gfarm_replica_check_sleep_time > 0)
		replica_check_giant_lock = replica_check_giant_lock_default;
	else
		replica_check_giant_lock = giant_lock;
	if (gfarm_replica_check_sleep_time > GFARM_SECOND_BY_NANOSEC)
		gfarm_replica_check_sleep_time = GFARM_SECOND_BY_NANOSEC;

	/* wait startup of gfsd hosts */
	wait_time = gfarm_metadb_heartbeat_interval;
	replica_check_targets_add(wait_time);

	for (;;) {
		replica_check_wait();
		suppress_log_reset();
		if (replica_check_main()) /* error occured, retry */
			replica_check_targets_add(wait_time);
	}
	/*NOTREACHED*/
}

void
replica_check_start()
{
	static int started = 0;

	if (!gfarm_replica_check) {
		gflog_notice(GFARM_MSG_UNFIXED, "replica_check is disabled");
		return;
	}
	if (started)
		return;
	started = 1;
	create_detached_thread(replica_check_thread, NULL);
}
