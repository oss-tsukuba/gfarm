#include <pthread.h>

#include <errno.h>
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#define GFARM_INTERNAL_USE
#include <gfarm/gflog.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

#include "gfutil.h"
#include "thrsubr.h"

#include "config.h"
#include "subr.h"

int debug_mode = 0;

static pthread_mutex_t giant_mutex;

static void
giant_mutex_init(void)
{
	gfarm_mutex_init(&giant_mutex, "giant_init", "giant");
}

static void
giant_mutex_lock(void)
{
	gfarm_mutex_lock(&giant_mutex, "giant_lock", "giant");
}

/* false: busy */
static int
giant_mutex_trylock(void)
{
	return (gfarm_mutex_trylock(&giant_mutex, "giant_trylock", "giant"));
}

static void
giant_mutex_unlock(void)
{
	gfarm_mutex_unlock(&giant_mutex, "giant_unlock", "giant");
}

/*
 * NUM_THREAD_POOLS: 7
 *	back_channel_recv_watcher
 *	back_channel_send_thread_pool
 *	proto_status_send_thread_pool
 *	sync_protocol_watcher
 *	authentication_thread_pool
 *	gfmdc_recv_watcher
 *	gfmdc_send_thread_pool
 * NUM_THREADS: 11
 * 	back_channel remover
 *	dead_file_copy removal_finalizer
 *	dead_file_copy_scanner
 *	file_copy_by_host_remover
 *	db_journal_apply_thread
 *	resumer
 *	peer_closer
 *	quota_check_thread for quota_check_ctl
 *	quota_check_thread for dirquota_check_ctl
 *	replica_check_thread
 *	sigs_handler
 *   but does not include the following threads:
 * 	callout_main
 *	db_journal_store_thread
 *	db_journal_recvq_thread
 *	gfmdc_journal_asyncsend_thread
 *	gfmdc_connect_thread
 *	db_thread
 */
#define NUM_THREAD_POOLS	7
#define NUM_THREADS		11

/*
 * unlike mutex, ticketlock can be unlocked by a thread other than the owner.
 * so, be careful of misuse.
 */

static struct gfarm_ticketlock giant_ticketlock;

static void
giant_ticketlock_init(void)
{
	gfarm_ticketlock_init(&giant_ticketlock,
	    gfarm_metadb_thread_pool_size * NUM_THREAD_POOLS + NUM_THREADS,
	    "giant_init", "giant");
}

static void
giant_ticketlock_lock(void)
{
	gfarm_ticketlock_lock(&giant_ticketlock, "giant_lock", "giant");
}

/* false: busy */
static int
giant_ticketlock_trylock(void)
{
	return (gfarm_ticketlock_trylock(
	    &giant_ticketlock, "giant_trylock", "giant"));
}

static void
giant_ticketlock_unlock(void)
{
	gfarm_ticketlock_unlock(&giant_ticketlock, "giant_unlock", "giant");
}

static struct gfarm_queuelock giant_queuelock;

static void
giant_queuelock_init(void)
{
	gfarm_queuelock_init(&giant_queuelock, "giant_init", "giant");
}

static void
giant_queuelock_lock(void)
{
	gfarm_queuelock_lock(&giant_queuelock, "giant_lock", "giant");
}

/* false: busy */
static int
giant_queuelock_trylock(void)
{
	return (gfarm_queuelock_trylock(
	    &giant_queuelock, "giant_trylock", "giant"));
}

static void
giant_queuelock_unlock(void)
{
	gfarm_queuelock_unlock(&giant_queuelock, "giant_unlock", "giant");
}

static struct giant_lock_sw_entry {
	const char *name;
	void (*init)(void);
	void (*lock)(void);
	int (*trylock)(void);
	void (*unlock)(void);
} giant_lock_sw_table[] = {
	/* GFARM_LOCK_TYPE_MUTEX */
	{
		"mutex",
		giant_mutex_init,
		giant_mutex_lock,
		giant_mutex_trylock,
		giant_mutex_unlock,
	},
	/* GFARM_LOCK_TYPE_TICKETLOCK */
	{
		"ticketlock",
		giant_ticketlock_init,
		giant_ticketlock_lock,
		giant_ticketlock_trylock,
		giant_ticketlock_unlock,
	},
	/* GFARM_LOCK_TYPE_QUEUELOCK */
	{
		"queuelock",
		giant_queuelock_init,
		giant_queuelock_lock,
		giant_queuelock_trylock,
		giant_queuelock_unlock,
	},
};

static struct giant_lock_sw_entry *giant_lock_sw;

void
giant_init(void)
{
	giant_lock_sw =
	    &giant_lock_sw_table[gfarm_metadb_server_long_term_lock_type];
	giant_lock_sw->init();
}

void
giant_type_log(void)
{
	gflog_info(GFARM_MSG_1005048,
	    "giant lock type = %s", giant_lock_sw->name);
}

void
giant_lock(void)
{
	giant_lock_sw->lock();
}

/* false: busy */
int
giant_trylock(void)
{
	return (giant_lock_sw->trylock());
}

void
giant_unlock(void)
{
	giant_lock_sw->unlock();
}


static pthread_mutex_t config_var_mutex;

void
config_var_init(void)
{
	gfarm_mutex_init(&config_var_mutex, "config_var_init", "config_var");
}

void
config_var_lock(void)
{
	gfarm_mutex_lock(&config_var_mutex, "config_var_lock", "config_var");
}

void
config_var_unlock(void)
{
	gfarm_mutex_unlock(&config_var_mutex, "config_var_unlock",
	    "config_var");
}

static void
gfarm_pthread_attr_setstacksize(pthread_attr_t *attr)
{
	int err;

	if (gfarm_metadb_stack_size != GFARM_METADB_STACK_SIZE_DEFAULT){
#ifdef HAVE_PTHREAD_ATTR_SETSTACKSIZE
		err = pthread_attr_setstacksize(attr,
		    gfarm_metadb_stack_size);
		if (err != 0)
			gflog_warning(GFARM_MSG_1000218, "gfmd.conf: "
			    "metadb_server_stack_size %d: %s",
			    gfarm_metadb_stack_size, strerror(err));
#else
		gflog_warning(GFARM_MSG_1000219, "gfmd.conf: "
		    "metadb_server_stack_size %d: "
		    "configuration ignored due to lack of "
		    "pthread_attr_setstacksize()",
		    gfarm_metadb_stack_size);
#endif
	}
}

pthread_attr_t gfarm_pthread_attr;

void
gfarm_pthread_attr_init(void)
{
	int err;

	err = pthread_attr_init(&gfarm_pthread_attr);
	if (err != 0)
		gflog_fatal(GFARM_MSG_1000223,
		    "pthread_attr_init(): %s", strerror(err));
	err = pthread_attr_setdetachstate(&gfarm_pthread_attr,
	    PTHREAD_CREATE_DETACHED);
	if (err != 0)
		gflog_fatal(GFARM_MSG_1000224,
		    "PTHREAD_CREATE_DETACHED: %s", strerror(err));
	gfarm_pthread_attr_setstacksize(&gfarm_pthread_attr);
}


pthread_attr_t *
gfarm_pthread_attr_get(void)
{
	static pthread_once_t gfarm_pthread_attr_initialized =
	    PTHREAD_ONCE_INIT;

	pthread_once(&gfarm_pthread_attr_initialized,
	    gfarm_pthread_attr_init);

	return (&gfarm_pthread_attr);
}

gfarm_error_t
create_detached_thread(void *(*thread_main)(void *), void *arg)
{
	int err;
	pthread_t thread_id;

	err = pthread_create(&thread_id, gfarm_pthread_attr_get(),
	    thread_main, arg);
	return (err == 0 ? GFARM_ERR_NO_ERROR : gfarm_errno_to_error(err));
}

static const char *
gfarm_sched_policy_name(int policy)
{
	return ((policy == SCHED_FIFO)  ? "SCHED_FIFO" :
		(policy == SCHED_RR)    ? "SCHED_RR" :
		(policy == SCHED_OTHER) ? "SCHED_OTHER" :
#ifdef SCHED_BATCH
		(policy == SCHED_BATCH) ? "SCHED_BATCH" :
#endif
#ifdef SCHED_IDLE
		(policy == SCHED_IDLE) ? "SCHED_IDLE" :
#endif
		"SCHED_<unknown>");
}

static gfarm_error_t
gfarm_pthread_set_priority_idle(const char *thread_name, pthread_t thread)
{
#ifdef SCHED_IDLE /* Since Linux 2.6.23 */
	int save_errno, policy = SCHED_IDLE;
	struct sched_param param;

	param.sched_priority = 0;
	save_errno = pthread_setschedparam(thread, policy, &param);
	if (save_errno == 0) {
		gflog_info(GFARM_MSG_1004250,
		    "%s: scheduling policy=SCHED_IDLE", thread_name);
		return (GFARM_ERR_NO_ERROR); /* use SCHED_IDLE */
	}
	gflog_warning(GFARM_MSG_1004251,
	    "%s: pthread_set_schedparam(%d, %d): %s",
	    thread_name, policy, param.sched_priority, strerror(save_errno));
	return (gfarm_errno_to_error(save_errno));
#else
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
#endif
}

gfarm_error_t
gfarm_pthread_set_priority_minimum(const char *thread_name)
{
	pthread_t self = pthread_self();
	int policy, min_prio;
	struct sched_param param;
	int save_errno;

	if (gfarm_pthread_set_priority_idle(thread_name, self)
	    == GFARM_ERR_NO_ERROR)
		return (GFARM_ERR_NO_ERROR); /* use SCHED_IDLE */

	save_errno = pthread_getschedparam(self, &policy, &param);
	if (save_errno != 0) {
		gflog_warning(GFARM_MSG_1004252,
		    "%s: pthread_get_schedparam(): %s",
		    thread_name, strerror(save_errno));
		return (gfarm_errno_to_error(save_errno));
	}

	min_prio = sched_get_priority_min(policy);
	if (min_prio == -1) {
		save_errno = errno;
		gflog_warning(GFARM_MSG_1004253,
		    "%s: sched_get_priority_min(%s): %s",
		    thread_name, gfarm_sched_policy_name(policy),
		    strerror(errno));
		return (gfarm_errno_to_error(save_errno));
	}

	if (param.sched_priority == min_prio) {
		gflog_warning(GFARM_MSG_1004254,
		    "%s: cannot change the scheduling priority of %s",
		    thread_name, gfarm_sched_policy_name(policy));
		return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
	}

	param.sched_priority = min_prio;
	save_errno = pthread_setschedparam(self, policy, &param);
	if (save_errno != 0) {
		gflog_warning(GFARM_MSG_1004255,
		    "%s: pthread_set_schedparam(%d, %d): %s",
		    thread_name, policy, min_prio, strerror(save_errno));
		return (gfarm_errno_to_error(save_errno));
	}

	gflog_info(GFARM_MSG_1004256,
	    "%s: scheduling policy=%s, priority=%d",
	    thread_name, gfarm_sched_policy_name(policy), min_prio);

	return (GFARM_ERR_NO_ERROR);
}

/* only initialization routines are allowed to call this function */
char *
strdup_ck(const char *s, const char *diag)
{
	char *d = strdup(s);

	if (d == NULL)
		gflog_fatal(GFARM_MSG_1002313,
		    "%s: strdup(%s): no memory", diag, s);
	return (d);
}

char *
strdup_log(const char *s, const char *diag)
{
	char *d = strdup(s);

	if (d == NULL)
		gflog_error(GFARM_MSG_1002358,
		    "%s: strdup(%s): no memory", diag, s);
	return (d);
}

int
accmode_to_op(gfarm_uint32_t flag)
{
	int op;

	switch (flag & GFARM_FILE_ACCMODE) {
	case GFARM_FILE_RDONLY:
		op = (flag & GFARM_FILE_TRUNC) ? (GFS_R_OK|GFS_W_OK) :
		    GFS_R_OK;
		break;
	case GFARM_FILE_WRONLY:	op = GFS_W_OK; break;
	case GFARM_FILE_RDWR:	op = GFS_R_OK|GFS_W_OK; break;
	case GFARM_FILE_LOOKUP:	op = 0; break;
	default:
		assert(0);
		op = 0;
	}
	return (op);
}

const char *
accmode_to_string(gfarm_uint32_t flag)
{
	switch (flag & GFARM_FILE_ACCMODE) {
	case GFARM_FILE_RDONLY:	return ("RDONLY");
	case GFARM_FILE_WRONLY:	return ("WRONLY");
	case GFARM_FILE_RDWR:	return ("RDWR");
	case GFARM_FILE_LOOKUP:	return ("LOOKUP");
	}
	assert(0);
	return ("shouldn't happen");
}

/* giant_lock should be held before calling this */
gfarm_uint64_t
trace_log_get_sequence_number(void)
{
	static gfarm_uint64_t trace_log_seq_num;

	return (trace_log_seq_num++);
}
