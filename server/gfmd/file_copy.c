#include <pthread.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>

#include <gfarm/error.h>
#include <gfarm/gflog.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

#include "nanosec.h"
#include "thrsubr.h"

#include "config.h"

#include "subr.h"
#include "inum_string_list.h"
#include "db_access.h"
#include "host.h"
#include "inode.h"

/*
 * remove file_copy entries, when a filesystem node is removed
 */

static pthread_mutex_t file_copy_by_host_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t file_copy_by_host_wakeup = PTHREAD_COND_INITIALIZER;
static int file_copy_by_host_needed = 0;

static const char file_copy_by_host_mutex_diag[] = "file_copy_by_host_mutex";
static const char file_copy_by_host_wakeup_diag[] = "file_copy_by_host_wakeup";

static void
housekeep_giant_lock(void)
{
	int sleep_time = gfarm_metadb_replica_remover_by_host_sleep_time;

	if (sleep_time <= 0)
		sleep_time = 1;
	while (!giant_trylock())
		gfarm_nanosleep(sleep_time);
}

static void
housekeep_giant_unlock(void)
{
	giant_unlock();
}

static void
file_copy_by_host_remove(void)
{
	gfarm_ino_t inum, inum_limit, inum_target;

	gflog_info(GFARM_MSG_1004266, "file_copy remover start");
	housekeep_giant_lock();

	inum = inode_root_number();
	inum_limit = inode_table_current_size();
	inum_target = inum +
	    gfarm_metadb_replica_remover_by_host_inode_step;
	if (inum_target > inum_limit)
		inum_target = inum_limit;
	for (;; inum++) {
		if (inum >= inum_target) {
			housekeep_giant_unlock();
			/* make a chance for clients */
			housekeep_giant_lock();
			inum_limit = inode_table_current_size();
			if (inum >= inum_limit)
				break;
			inum_target = inum +
			    gfarm_metadb_replica_remover_by_host_inode_step;
			if (inum_target > inum_limit)
				inum_target = inum_limit;
		}
		inode_remove_replica_in_cache_for_invalid_host(inum);
	}

	housekeep_giant_unlock();
	gflog_info(GFARM_MSG_1004267, "file_copy remover completed");
}

static void *
file_copy_by_host_remover(void *arg)
{
	static const char diag[] = "file_copy_by_host_remover";

	(void)gfarm_pthread_set_priority_minimum(diag);

	for (;;) {
		gfarm_mutex_lock(&file_copy_by_host_mutex, diag,
		    file_copy_by_host_mutex_diag);
		while (!file_copy_by_host_needed)
			gfarm_cond_wait(&file_copy_by_host_wakeup,
			    &file_copy_by_host_mutex, diag,
			    file_copy_by_host_wakeup_diag);
		file_copy_by_host_needed = 0;
		gfarm_mutex_unlock(&file_copy_by_host_mutex, diag,
		    file_copy_by_host_mutex_diag);

		file_copy_by_host_remove();
	}

	return (NULL);
}

void
file_copy_removal_by_host_start(void)
{
	static const char diag[] = "file_copy_by_host_start";

	gfarm_mutex_lock(&file_copy_by_host_mutex, diag,
	    file_copy_by_host_mutex_diag);
	file_copy_by_host_needed = 1;
	gfarm_cond_signal(&file_copy_by_host_wakeup, diag,
	    file_copy_by_host_wakeup_diag);
	gfarm_mutex_unlock(&file_copy_by_host_mutex, diag,
	    file_copy_by_host_mutex_diag);
}

/*
 * initial loading
 */

static struct inum_string_list_entry *file_copy_removal_list = NULL;

static void
file_copy_defer_db_removal(gfarm_ino_t inum, char *hostname)
{
	if (!inum_string_list_add(&file_copy_removal_list, inum, hostname)) {
		gflog_error(GFARM_MSG_1002825,
		    "file_copy %llu host:%s: no memory to record for removal",
		    (unsigned long long)inum, hostname);
		free(hostname);
	} else {
		gflog_error(GFARM_MSG_1002826,
		    "file_copy %llu host:%s: removing orphan data",
		    (unsigned long long)inum, hostname);
	}
}

static void
file_copy_db_remove_one_orphan(void *closure,
	gfarm_ino_t inum, const char *hostname)
{
	gfarm_error_t e;
	struct inode *inode;
	gfarm_int64_t gen;
	struct host *spool_host;

	inode = inode_lookup(inum);
	assert(inode != NULL);
	gen = inode_get_gen(inode);
	spool_host = host_lookup_including_invalid(hostname);
	assert(spool_host != NULL);
	e = inode_remove_replica_orphan(inode, spool_host);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1003710,
		    "cannot remove a replica (%s, %lld:%lld): %s",
		    hostname, (long long)inum, (long long)gen,
		    gfarm_error_string(e));
}

void
file_copy_db_remove_orphan(void)
{
	inum_string_list_foreach(file_copy_removal_list,
	    file_copy_db_remove_one_orphan, NULL);
}

void
file_copy_free_orphan(void)
{
	inum_string_list_free(&file_copy_removal_list);
}

/* The memory owner of `hostname' is changed to inode.c */
static void
file_copy_add_one(void *closure, gfarm_ino_t inum, char *hostname)
{
	gfarm_error_t e;
	struct inode *inode = inode_lookup(inum);
	struct host *host = host_lookup_at_loading(hostname);

	if (inode == NULL) {
		gflog_error(GFARM_MSG_1000343,
		    "file_copy_add_one: no inode %lld",
		    (unsigned long long)inum);
		file_copy_defer_db_removal(inum, hostname);
		return;
	} else if (!inode_is_file(inode)) {
		gflog_error(GFARM_MSG_1000344,
		    "file_copy_add_one: not file %lld",
		    (unsigned long long)inum);
		file_copy_defer_db_removal(inum, hostname);
		return;
	} else if (host == NULL) {
		gflog_error(GFARM_MSG_1003711,
		    "file_copy_add_one(%s, %lld): no memory",
		    hostname, (long long)inum);
	} else if ((e = inode_add_file_copy_in_cache(inode, host))
	    != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1000346,
		    "file_copy_add_one: add_replica: %s",
		    gfarm_error_string(e));
	} else if (!host_is_valid(host)) {
		file_copy_defer_db_removal(inum, hostname);
		return;
	}
	free(hostname);
}

void
file_copy_init(void)
{
	gfarm_error_t e;

	e = db_filecopy_load(NULL, file_copy_add_one);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1000361,
		    "loading filecopy: %s", gfarm_error_string(e));

	if ((e = create_detached_thread(file_copy_by_host_remover, NULL))
	    != GFARM_ERR_NO_ERROR)
		gflog_fatal(GFARM_MSG_1004268,
		    "create_detached_thread(file_copy_by_host_remover): "
		    "%s", gfarm_error_string(e));
}
