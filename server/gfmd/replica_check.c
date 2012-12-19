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
#include <syslog.h>

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

struct suppress_log {
	char *type;
	int level, count, suppressed, max;
};

static struct suppress_log log_unavail
= { .type = "temporarily unavailable", .level = LOG_DEBUG, .max = 5 };
static struct suppress_log log_few
= { .type = "fewer replicas", .level = LOG_DEBUG, .max = 1 };
static struct suppress_log log_too_many
= { .type = "too many replicas", .level = LOG_DEBUG, .max = 1 };
static struct suppress_log log_hosts_down
= { .type = "hosts are down", .level = LOG_INFO, .max = 20 };

static int log_is_suppressed(struct suppress_log *log)
{
	if (log->count < log->max) {
		log->count++;
		return (0);
	}
	if (log->suppressed)
		return (1);

	log->suppressed = 1;
	switch (log->level) {
	case LOG_DEBUG:
		gflog_debug(GFARM_MSG_1003615,
		    "replica_check: suppress many `%s' debug messages",
		    log->type);
		break;
	case LOG_INFO:
		gflog_info(GFARM_MSG_1003616,
		    "replica_check: suppress many `%s' info messages",
		     log->type);
		break;
	default:
		break;
	}
	return (1);
}

static void suppress_log_reset(struct suppress_log *log)
{
	log->count = log->suppressed = 0;
}
static void suppress_log_reset_all()
{
	suppress_log_reset(&log_few);
	suppress_log_reset(&log_too_many);
	suppress_log_reset(&log_hosts_down);
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
		gflog_error(GFARM_MSG_1003617,
		    "host_schedule_except, n_srcs=%d: %s",
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
		e = file_replicating_new(inode, dst, 0, NULL, &fr);
		if (e == GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE) {
			busy = 1;
			if (!log_is_suppressed(&log_unavail))
				gflog_debug(GFARM_MSG_1003618,
				    "file_replicating_new, host=%s: %s",
				    host_name(dst), gfarm_error_string(e));
			/* next dst */
		} else if (e != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_1003619,
			    "file_replicating_new, host=%s: %s",
			    host_name(dst), gfarm_error_string(e));
			break;
		} else if ((e = async_back_channel_replication_request(
		    host_name(src), host_port(src), dst,
		    inode_get_number(inode), inode_get_gen(inode), fr))
			   != GFARM_ERR_NO_ERROR) {
			file_replicating_free_by_error_before_request(fr);
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
		if (!log_is_suppressed(&log_few))
			gflog_debug(GFARM_MSG_1003621,
			    "replica_check: %lld:%lld:%s: fewer replicas, "
			    "increase=%d/before=%d/desire=%d",
			    (long long)inode_get_number(inode),
			    (long long)inode_get_gen(inode),
			    user_name(inode_get_user(inode)),
			    n_success, ncopy, n_desire);
	} else
		gflog_notice(GFARM_MSG_1003622,
		    "replica_check: %lld:%lld:%s: will be fixed, desire=%d",
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
		gflog_debug(GFARM_MSG_1003623,
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
	if (ncopy == 0) {
		if (inode_get_size(inode) == 0)
			return (GFARM_ERR_NO_ERROR); /* normally */
		else if (inode_has_no_replica(inode)) {
			gflog_warning(GFARM_MSG_1003624,
			    "replica_check: %lld:%lld:%s: lost all replicas",
			    (long long)info->inum, (long long)info->gen,
			    user_name(inode_get_user(inode)));
			return (GFARM_ERR_NO_ERROR); /* not retry */
		} else if (!log_is_suppressed(&log_hosts_down)) {
			gflog_info(GFARM_MSG_1003625,
			    "replica_check: %lld:%lld:%s: hosts are down",
			    (long long)info->inum, (long long)info->gen,
			    user_name(inode_get_user(inode)));
			return (GFARM_ERR_NO_ERROR); /* not retry */
		}
	}
	if (ncopy > info->desired_number) {
		if (!log_is_suppressed(&log_too_many))
			gflog_debug(GFARM_MSG_1003626,
			   "replica_check: %lld:%lld:%s: "
			   "too many replicas, %d > %d",
			   (long long)info->inum, (long long)info->gen,
			   user_name(inode_get_user(inode)),
			   ncopy, info->desired_number);
		/* XXX remove too many replicas? */
		return (GFARM_ERR_NO_ERROR);
	}
	if (inode_is_opened_for_writing(inode)) {
		gflog_debug(GFARM_MSG_1003627,
		    "replica_check: %lld:%lld:%s: "
		    "opened in write mode, ignored",
		    (long long)info->inum, (long long)info->gen,
		    user_name(inode_get_user(inode)));
		return (GFARM_ERR_NO_ERROR); /* ignore */
	}
	/* available replicas for source */
	e = inode_replica_list(inode, &n_srcs, &srcs);
	if (e != GFARM_ERR_NO_ERROR) { /* no memory */
		gflog_error(GFARM_MSG_1003628,
		    "replica_check: %lld:%lld:%s: replica_list: %s",
		    (long long)info->inum, (long long)info->gen,
		    user_name(inode_get_user(inode)), gfarm_error_string(e));
		return (e); /* retry */
	}
	/* n_srcs may be 0, because host_is_up() may change */
	if (n_srcs <= 0) {
		free(srcs);
		if (!log_is_suppressed(&log_hosts_down))
			gflog_info(GFARM_MSG_1003629,
			    "replica_check: %lld:%lld:%s: hosts are down",
			    (long long)info->inum, (long long)info->gen,
			    user_name(inode_get_user(inode)));
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
	char *repattr;
	int desired_number;

	if (inode_get_replica_spec(file_ino, &repattr, &desired_number) ||
	    inode_search_replica_spec(dir_ino, &repattr, &desired_number)) {
		if (repattr != NULL) {
			free(repattr); /* XXX */
			return (0); /* XXX support repattr */
		} else {
			return (desired_number);
		}
	}
	return (0);
}

#define REPLICA_CHECK_DIRENTS_BUFCOUNT 512

static size_t replica_check_stack_size, replica_check_stack_index;
static struct replication_info *replica_check_stack;

static int
replica_check_stack_init()
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
replica_check_stack_push(struct inode *dir_ino, struct inode *file_ino)
{
	assert(replica_check_stack_index < replica_check_stack_size);

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
	while (!giant_trylock())
		gfarm_nanosleep(gfarm_replica_check_sleep_time);
}

static void (*replica_check_giant_lock)(void);
static void (*replica_check_giant_unlock)(void) = giant_unlock;

static int
replica_check_main_dir(gfarm_ino_t inum, gfarm_ino_t *countp)
{
	gfarm_error_t e;
	struct inode *dir_ino, *file_ino;
	Dir dir;
	DirCursor cursor;
	gfarm_off_t dir_offset = 0;
	DirEntry entry;
	struct replication_info rep_info;
	int need_to_retry = 0, eod = 0, i;

	while (!eod) {
		replica_check_giant_lock();
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
		if (!dir_cursor_set_pos(dir, dir_offset, &cursor)) {
			replica_check_giant_unlock();
			return (need_to_retry);
		}
		/* avoid long giant lock */
		for (i = 0; i < REPLICA_CHECK_DIRENTS_BUFCOUNT; i++) {
			entry = dir_cursor_get_entry(dir, &cursor);
			if (entry == NULL) {
				eod = 1; /* end of directory */
				break;
			}
			file_ino = dir_entry_get_inode(entry);
			if (inode_is_file(file_ino))
				replica_check_stack_push(dir_ino, file_ino);
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
				if (sl < GFARM_SECOND_BY_NANOSEC)
					sl *= 2; /* 2,4,8,...,512,1024,1024 */
			}
			if (e != GFARM_ERR_NO_ERROR) {
				need_to_retry = 1;
				gflog_debug(GFARM_MSG_1003631,
				    "replica_check_fix(): %s",
				    gfarm_error_string(e));
			}
			(*countp)++;
		}
	}
	return (need_to_retry);
}

static int
replica_check_main()
{
	gfarm_ino_t inum, table_size, count = 0;
	gfarm_ino_t root_inum = inode_root_number();
	int need_to_retry = 0;

	replica_check_giant_lock();
	table_size = inode_table_current_size();
	replica_check_giant_unlock();

	RC_LOG_INFO(GFARM_MSG_1003632, "replica_check: start");
	for (inum = root_inum;;) {
		need_to_retry = replica_check_main_dir(inum, &count);
		inum++; /* a next directory */
		if (inum >= table_size) {
			replica_check_giant_lock();
			table_size = inode_table_current_size();
			replica_check_giant_unlock();
			if (inum >= table_size)
				break;
		}
	}
	RC_LOG_INFO(GFARM_MSG_1003633,
	    "replica_check: finished, files=%llu", (unsigned long long)count);
	return (need_to_retry);
}

#define REPLICA_CHECK_DIAG "replica_check"

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
replica_check_targets_init()
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
	target_tv = *now;

	/* integrate near target times */
	target_tv.tv_sec += gfarm_replica_check_minimum_interval;

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
				future_tv.tv_sec +=
				    gfarm_replica_check_minimum_interval;
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
		 * integrate many replica_check_signal_*()
		 *
		 * waiting gfarm_replica_check_minimum_interval
		 * seconds here, and replica_check_targets_next() will
		 * gets the latest target time. (others are skipped)
		 */
		gfarm_mutex_unlock(
		    &replica_check_mutex, diag, REPLICA_CHECK_DIAG);
		gfarm_nanosleep(
		    (unsigned long long)gfarm_replica_check_minimum_interval *
		    GFARM_SECOND_BY_NANOSEC);
		gfarm_mutex_lock(
		    &replica_check_mutex, diag, REPLICA_CHECK_DIAG);
	}
	if (!replica_check_initialized)
		replica_check_initialized = 1;
	gfarm_mutex_unlock(&replica_check_mutex, diag, REPLICA_CHECK_DIAG);
}

void
replica_check_signal_general(const char *diag, long sec)
{
	if (!gfarm_replica_check)
		return;

	gfarm_mutex_lock(&replica_check_mutex, diag, REPLICA_CHECK_DIAG);
	if (replica_check_initialized) {
#ifdef DEBUG_REPLICA_CHECK
		RC_LOG_DEBUG(GFARM_MSG_1003639, "%s is called", diag);
#endif
		replica_check_targets_add(sec);
		gfarm_cond_signal(
		    &replica_check_cond, diag, REPLICA_CHECK_DIAG);
	}
#ifdef DEBUG_REPLICA_CHECK
	else
		RC_LOG_DEBUG(GFARM_MSG_1003640, "%s is ignored", diag);
#endif
	gfarm_mutex_unlock(&replica_check_mutex, diag, REPLICA_CHECK_DIAG);
}

void
replica_check_signal_host_up()
{
	static const char diag[] = "replica_check_signal_host_up";

	replica_check_signal_general(diag, 0);
}

void
replica_check_signal_host_down()
{
	static const char diag[] = "replica_check_signal_host_down";

	replica_check_signal_general(
	    diag, gfarm_replica_check_host_down_thresh);
	/* NOTE: execute replica_check_main() twice after restarting gfsd */
}

void
replica_check_signal_update_xattr()
{
	static const char diag[] = "replica_check_signal_update_xattr";

	replica_check_signal_general(diag, 0);
}

void
replica_check_signal_rename()
{
	static const char diag[] = "replica_check_signal_rename";

	replica_check_signal_general(diag, 0);
}

/* workaround for #406 - obsolete replicas remain existing */
#define DFC_SCAN_INTERVAL 21600 /* 6 hours */

static void *
replica_check_thread(void *arg)
{
	int wait_time;
	time_t dfc_scan_time;

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

	dfc_scan_time = time(NULL) + DFC_SCAN_INTERVAL;
	replica_check_targets_add(DFC_SCAN_INTERVAL);

	for (;;) {
		time_t t = time(NULL) + gfarm_replica_check_minimum_interval;

		replica_check_wait();
		suppress_log_reset_all();

		if (replica_check_main()) /* error occured, retry */
			replica_check_targets_add(wait_time);

		if (time(NULL) >= dfc_scan_time) {
			replica_check_giant_lock();
			dead_file_copy_scan_deferred_all();
			replica_check_giant_unlock();

			dfc_scan_time = time(NULL) + DFC_SCAN_INTERVAL;
			replica_check_targets_add(DFC_SCAN_INTERVAL);
			RC_LOG_DEBUG(GFARM_MSG_UNFIXED,
			    "replica_check: dead_file_copy_scan_deferred_all,"
			    " next=%ld", (long)dfc_scan_time);
		}

		t = t - time(NULL);
		if (t > 0)
			gfarm_sleep(t);
	}
	/*NOTREACHED*/
}

void
replica_check_start()
{
	static int started = 0;

	if (!gfarm_replica_check) {
		gflog_notice(GFARM_MSG_1003642, "replica_check is disabled");
		return;
	}
	if (started)
		return;
	started = 1;
	create_detached_thread(replica_check_thread, NULL);
}
