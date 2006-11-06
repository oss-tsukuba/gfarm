#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>

#include <gfarm/gfarm.h>

#include "config.h"
#include "gfs_client.h"
#include "gfs_lock.h"
#include "gfs_misc.h"
#include "schedule.h"

static int
gfarm_file_missing_replica(char *gfarm_file, char *section,
	char *canonical_hostname)
{
	char *e, *e2, *path_section;
	struct gfs_connection *gfs_server;
	int peer_fd, missing = 0;

	e = gfarm_path_section(gfarm_file, section, &path_section);
	if (e != NULL)
		return (0);

	e = gfs_client_open_with_reconnection(canonical_hostname,
	    path_section, GFARM_FILE_RDONLY, 0, &gfs_server, &e2, &peer_fd);
	free(path_section);
	if (e != NULL)
		return (0);

	if (e2 == NULL)
		gfs_client_close(gfs_server, peer_fd);
	else if (e == GFARM_ERR_NO_SUCH_OBJECT)
		missing = 1;

	gfs_client_connection_free(gfs_server);
	return (missing);
}

static char *
gfarm_file_section_replicate_from_to_by_gfrepbe(
	char *gfarm_file, char *section, char *srchost, char *dsthost)
{
	char *e;
	char *e2;
	gfarm_stringlist gfarm_file_list;
	gfarm_stringlist section_list;

	e = gfarm_stringlist_init(&gfarm_file_list);
	if (e != NULL)
		return (e);
	e = gfarm_stringlist_init(&section_list);
	if (e != NULL) {
		gfarm_stringlist_free(&gfarm_file_list);
		return (e);
	}
	e = gfarm_stringlist_add(&gfarm_file_list, gfarm_file);
	e2 = gfarm_stringlist_add(&section_list, section);
	if (e == NULL && e2 == NULL) {
		e = gfarm_file_section_replicate_multiple(
		    &gfarm_file_list, &section_list, srchost, dsthost, &e2);
	}
	gfarm_stringlist_free(&gfarm_file_list);
	gfarm_stringlist_free(&section_list);
	return (e != NULL ? e : e2);
}

/*
 * 0: use gfrepbe_client/gfrepbe_server
 * 1: use gfs_client_bootstrap_replicate_file()
 */
#if 0 /* XXX for now */
static int gfarm_replication_method = GFARM_REPLICATION_NORMAL_METHOD;
#else
static int gfarm_replication_method = GFARM_REPLICATION_BOOTSTRAP_METHOD;
#endif

int
gfarm_replication_get_method(void)
{
	return (gfarm_replication_method);
}

void
gfarm_replication_set_method(int method)
{
	gfarm_replication_method = method;
}

/*
 * NOTE: gfarm_file_section_replicate_without_busy_check() assumes
 *	that the caller of this function already checked the section
 *	by gfs_file_section_check_busy(gfarm_file, section)
 */
static char *
gfarm_file_section_replicate_without_busy_check(
	char *gfarm_file, char *section,
	gfarm_mode_t mode, file_offset_t file_size,
	char *src_canonical_hostname, char *src_if_hostname,
	char *dst_canonical_hostname)
{
	char *e, *e2, *path_section;
	struct gfarm_file_section_copy_info ci;
	struct gfs_connection *gfs_server;

	if (gfarm_replication_method != GFARM_REPLICATION_BOOTSTRAP_METHOD)
		return (gfarm_file_section_replicate_from_to_by_gfrepbe(
		    gfarm_file, section,
		    src_canonical_hostname, dst_canonical_hostname));

	e = gfarm_path_section(gfarm_file, section, &path_section);
	if (e != NULL)
		return (e);

	e = gfs_client_bootstrap_replicate_file_with_reconnection(
	    dst_canonical_hostname, path_section, mode, file_size,
	    src_canonical_hostname, src_if_hostname, &gfs_server, &e2);
	if (e == NULL && e2 == GFARM_ERR_NO_SUCH_OBJECT) {
		/* FT - the parent directory may be missing */
		if (gfarm_file_missing_replica(gfarm_file, section,
		    src_canonical_hostname)) {
			/* Delete the section copy info */
			(void)gfarm_file_section_copy_info_remove(gfarm_file,
			    section, src_canonical_hostname);
			e2 = GFARM_ERR_INCONSISTENT_RECOVERABLE;
		} else {
			/* FT - the parent directory of the destination may be missing */
			(void)gfs_client_mk_parent_dir(gfs_server, gfarm_file);
			e2 = gfs_client_bootstrap_replicate_file(
			    gfs_server, path_section, mode, file_size,
			    src_canonical_hostname, src_if_hostname);
		}
	}
	if (e == NULL && e2 == NULL)
		e2 = gfarm_file_section_copy_info_set(gfarm_file, section,
		    dst_canonical_hostname, &ci);

	if (e == NULL)
		gfs_client_connection_free(gfs_server);

	free(path_section);
	return (e != NULL ? e : e2);
}

static char *
gfarm_file_section_replicate(
	char *gfarm_file, char *section,
	gfarm_mode_t mode, file_offset_t file_size,
	char *src_canonical_hostname, char *src_if_hostname,
	char *dst_canonical_hostname)
{
	char *e = gfs_file_section_check_busy(gfarm_file, section);

	if (e != NULL)
		return (e);
	return (gfarm_file_section_replicate_without_busy_check(
	    gfarm_file, section, mode, file_size,
	    src_canonical_hostname, src_if_hostname,
	    dst_canonical_hostname));
}

static char *
gfarm_file_section_replicate_from_to_internal(
	char *gfarm_file, char *section,
	gfarm_mode_t mode, file_offset_t file_size,
	char *src_canonical_hostname, char *src_if_hostname,
	char *dsthost)
{
	char *e, *dst_canonical_hostname;

	e = gfarm_host_get_canonical_name(dsthost, &dst_canonical_hostname);
	if (e != NULL)
		return (e);
	/* already exists? don't have to replicate in that case */
	if (!gfarm_file_section_copy_info_does_exist(gfarm_file, section,
	    dst_canonical_hostname)) {
		e = gfarm_file_section_replicate(
		    gfarm_file, section, mode, file_size,
		    src_canonical_hostname, src_if_hostname,
		    dst_canonical_hostname);
	}
	free(dst_canonical_hostname);
	return (e);
}

static char *
gfarm_file_section_migrate_from_to_internal(
	char *gfarm_file, char *section,
	gfarm_mode_t mode, file_offset_t file_size,
	char *src_canonical_hostname, char *src_if_hostname,
	char *dsthost)
{
	char *e, *dst_canonical_hostname;

	e = gfarm_host_get_canonical_name(dsthost, &dst_canonical_hostname);
	if (e != NULL)
		return (e);
	/* already exists? don't have to replicate in that case */
	if (!gfarm_file_section_copy_info_does_exist(gfarm_file, section,
	    dst_canonical_hostname)) {
		e = gfarm_file_section_replicate(
		    gfarm_file, section, mode, file_size,
		    src_canonical_hostname, src_if_hostname,
		    dst_canonical_hostname);
		if (e == NULL) {
			e = gfs_unlink_replica_internal(
			    gfarm_file, section, src_canonical_hostname);
		}
	}
	free(dst_canonical_hostname);
	return (e);
}

static char *
gfarm_file_section_transfer_to_internal(char *gfarm_file, char *section,
	gfarm_mode_t mode, file_offset_t file_size,
	char *dsthost,
	char *(*transfer_from_to_internal)(char *, char *,
	    gfarm_mode_t, file_offset_t, char *, char *, char *))
{
	char *e, *srchost, *if_hostname;
	struct sockaddr peer_addr;

	do {
		e = gfarm_file_section_host_schedule(gfarm_file, section,
		    &srchost);
		if (e != NULL)
			break;

		/* reflect "address_use" directive in the `srchost' */
		e = gfarm_host_address_get(srchost, gfarm_spool_server_port,
		    &peer_addr, &if_hostname);
		if (e != NULL) {
			free(srchost);
			break; /* XXX should try next candidate */
		}

		e = (*transfer_from_to_internal)(gfarm_file, section,
		    mode & GFARM_S_ALLPERM, file_size,
		    srchost, if_hostname, dsthost);

		free(if_hostname);
		free(srchost);

	} while (e == GFARM_ERR_INCONSISTENT_RECOVERABLE);
	return (e);
}

static char *
gfarm_file_section_replicate_to_internal(
	char *gfarm_file, char *section,
	gfarm_mode_t mode, file_offset_t file_size,
	char *dsthost)
{
	return (gfarm_file_section_transfer_to_internal(gfarm_file, section,
	    mode, file_size, dsthost,
	    gfarm_file_section_replicate_from_to_internal));
}

/*
 * XXX FIXME
 * if the owner of a file is not the same, permit a group/other write access.
 *	- This should be fixed in the release of gfarm version 2.
 */
char *
gfarm_fabricate_mode_for_replication(struct gfs_stat *gst, gfarm_mode_t *modep)
{
	char *e;

	if (strcmp(gst->st_user, gfarm_get_global_username()) == 0) {
		*modep = gst->st_mode & GFARM_S_ALLPERM;
	} else {
		e = gfs_stat_access(gst, R_OK);
		if (e != NULL)
			return (e);
		/* don't allow setuid/setgid */
		*modep = (gst->st_mode | 022) & 0777;
	}
	return (NULL);
}

static char *
gfarm_url_section_transfer_from_to(const char *gfarm_url, char *section,
	char *srchost, char *dsthost,
	char *(*transfer_from_to_internal)(char *, char *,
	    gfarm_mode_t, file_offset_t, char *, char *, char *))
{
	char *e, *gfarm_file, *canonical_hostname, *if_hostname;
	struct sockaddr peer_addr;
	struct gfarm_path_info pi;
	struct gfarm_file_section_info si;
	gfarm_mode_t mode;

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL)
		goto finish;
	e = gfarm_path_info_get(gfarm_file, &pi);
	if (e != NULL)
		goto finish_gfarm_file;
	e = gfarm_file_section_info_get(gfarm_file, section, &si);
	if (e != NULL)
		goto finish_path_info;

	e = gfarm_host_get_canonical_name(srchost, &canonical_hostname);
	if (e != NULL)
		goto finish_section_info;

	/* reflect "address_use" directive in the `srchost' */
	e = gfarm_host_address_get(srchost, gfarm_spool_server_port,
	    &peer_addr, &if_hostname);
	if (e != NULL)
		goto finish_canonical_hostname;
	e = gfarm_fabricate_mode_for_replication(&pi.status, &mode);
	if (e != NULL)
		goto finish_if_hostname;
	e = (*transfer_from_to_internal)(gfarm_file, section,
	    mode, si.filesize,
	    canonical_hostname, if_hostname, dsthost);
finish_if_hostname:
	free(if_hostname);
finish_canonical_hostname:
	free(canonical_hostname);
finish_section_info:
	gfarm_file_section_info_free(&si);
finish_path_info:
	gfarm_path_info_free(&pi);
finish_gfarm_file:
	free(gfarm_file);
finish:
	return (e);
}

char *
gfarm_url_section_replicate_from_to(const char *gfarm_url, char *section,
	char *srchost, char *dsthost)
{
	return (gfarm_url_section_transfer_from_to(gfarm_url, section,
	    srchost, dsthost, gfarm_file_section_replicate_from_to_internal));
}

char *
gfarm_url_section_migrate_from_to(const char *gfarm_url, char *section,
	char *srchost, char *dsthost)
{
	return (gfarm_url_section_transfer_from_to(gfarm_url, section,
	    srchost, dsthost, gfarm_file_section_migrate_from_to_internal));
}

static char *
gfarm_url_section_transfer_to(
	const char *gfarm_url, char *section, char *dsthost,
	char *(*transfer_from_to_internal)(char *, char *,
	    gfarm_mode_t, file_offset_t, char *, char *, char *))
{
	char *e, *gfarm_file;
	struct gfarm_path_info pi;
	struct gfarm_file_section_info si;
	gfarm_mode_t mode;

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL)
		goto finish;
	e = gfarm_path_info_get(gfarm_file, &pi);
	if (e != NULL)
		goto finish_gfarm_file;
	e = gfarm_file_section_info_get(gfarm_file, section, &si);
	if (e != NULL)
		goto finish_path_info;
	e = gfarm_fabricate_mode_for_replication(&pi.status, &mode);
	if (e != NULL)
		goto finish_section_info;
	e = gfarm_file_section_transfer_to_internal(gfarm_file, section,
	    mode, si.filesize,
	    dsthost, transfer_from_to_internal);
finish_section_info:
	gfarm_file_section_info_free(&si);
finish_path_info:
	gfarm_path_info_free(&pi);
finish_gfarm_file:
	free(gfarm_file);
finish:
	return (e);
}

char *
gfarm_url_section_replicate_to(
	const char *gfarm_url, char *section, char *dsthost)
{
	return (gfarm_url_section_transfer_to(gfarm_url, section, dsthost,
	    gfarm_file_section_replicate_from_to_internal));
}

char *
gfarm_url_section_migrate_to(
	const char *gfarm_url, char *section, char *dsthost)
{
	return (gfarm_url_section_transfer_to(gfarm_url, section, dsthost,
	    gfarm_file_section_migrate_from_to_internal));
}

static char *
gfarm_url_fragments_transfer(
	const char *gfarm_url, int ndsthosts, char **dsthosts,
	char *(*transfer_from_to_internal)(char *, char *,
	    gfarm_mode_t, file_offset_t, char *, char *, char *))
{
	char *e, *gfarm_file, **srchosts, **edsthosts;
	int nsrchosts;
	gfarm_mode_t mode;
	int i, pid, *pids;
	struct gfarm_path_info pi;

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL)
		return (e);

	e = gfarm_path_info_get(gfarm_file, &pi);
	if (e != NULL)
		goto finish_gfarm_file;

	if (!GFARM_S_IS_FRAGMENTED_FILE(pi.status.st_mode)) {
		gfarm_path_info_free(&pi);
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		goto finish_gfarm_file;
	}
	e = gfarm_fabricate_mode_for_replication(&pi.status, &mode);
	gfarm_path_info_free(&pi);
	if (e != NULL)
		goto finish_gfarm_file;
	e = gfarm_url_hosts_schedule(gfarm_url, "", &nsrchosts, &srchosts);
	if (e != NULL)
		goto finish_gfarm_file;

	GFARM_MALLOC_ARRAY(edsthosts, nsrchosts);
	if (edsthosts == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto finish_srchosts;
	}
	gfarm_strings_expand_cyclic(ndsthosts, dsthosts, nsrchosts, edsthosts);

	GFARM_MALLOC_ARRAY(pids, nsrchosts);
	if (pids == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto finish_edsthosts;
	}

	for (i = 0; i < nsrchosts; i++) {
		struct sockaddr peer_addr;
		char *if_hostname;
		char section_string[GFARM_INT32STRLEN + 1];
		struct gfarm_file_section_info si;

		pid = fork();
		if (pid < 0)
			break;
		if (pid) {
			/* parent */
			pids[i] = pid;
			continue;
		}
		/* child */

		/* reflect "address_use" directive in the `srchosts[i]' */
		e = gfarm_host_address_get(srchosts[i],
		    gfarm_spool_server_port, &peer_addr, &if_hostname);
		if (e != NULL)
			_exit(2);

		sprintf(section_string, "%d", i);
		e = gfarm_file_section_info_get(gfarm_file, section_string,
		    &si);
		if (e != NULL)
			_exit(3);

		e = (*transfer_from_to_internal)(gfarm_file, section_string,
		    mode, si.filesize,
		    srchosts[i], if_hostname, edsthosts[i]);
		if (e != NULL)
			_exit(1);
		_exit(0);
	}
	while (--i >= 0) {
		int rv, s;

		while ((rv = waitpid(pids[i], &s, 0)) == -1 && errno == EINTR)
			;
		if (rv == -1) {
			if (e == NULL)
				e = gfarm_errno_to_error(errno);
		} else if (WIFEXITED(s) && WEXITSTATUS(s) != 0) {
			e = "error happens on replication";
		}
	}
	free(pids);
 finish_edsthosts:
	free(edsthosts);
 finish_srchosts:
	gfarm_strings_free_deeply(nsrchosts, srchosts);
 finish_gfarm_file:
	free(gfarm_file);
	return (e);
}

char *
gfarm_url_fragments_replicate(
	const char *gfarm_url, int ndsthosts, char **dsthosts)
{
	return (gfarm_url_fragments_transfer(gfarm_url, ndsthosts, dsthosts,
	    gfarm_file_section_replicate_from_to_internal));
}

char *
gfarm_url_fragments_migrate(
	const char *gfarm_url, int ndsthosts, char **dsthosts)
{
	return (gfarm_url_fragments_transfer(gfarm_url, ndsthosts, dsthosts,
	    gfarm_file_section_migrate_from_to_internal));
}

char *
gfarm_url_fragments_transfer_to_domainname(
	const char *gfarm_url, const char *domainname,
	char *(*transfer_from_to_internal)(char *, char *,
	    gfarm_mode_t, file_offset_t, char *, char *, char *))
{
	char *e, **dsthosts;
	int nfrags;

	e = gfarm_url_fragment_number(gfarm_url, &nfrags);
	if (e != NULL)
		return (e);

	GFARM_MALLOC_ARRAY(dsthosts, nfrags);
	if (dsthosts == NULL)
		return (GFARM_ERR_NO_MEMORY);

	e = gfarm_schedule_search_idle_by_domainname_to_write(domainname,
		nfrags, dsthosts);
	if (e != NULL)
		goto free_dsthosts;

	e = gfarm_url_fragments_transfer(gfarm_url, nfrags, dsthosts,
	    transfer_from_to_internal);

	while (--nfrags >= 0)
		free(dsthosts[nfrags]);
 free_dsthosts:
	free(dsthosts);

	return (e);
}

char *
gfarm_url_fragments_replicate_to_domainname(
	const char *gfarm_url, const char *domainname)
{
	return (gfarm_url_fragments_transfer_to_domainname(
	    gfarm_url, domainname,
	    gfarm_file_section_replicate_from_to_internal));
}

char *
gfarm_url_fragments_migrate_to_domainname(
	const char *gfarm_url, const char *domainname)
{
	return (gfarm_url_fragments_transfer_to_domainname(
	    gfarm_url, domainname,
	    gfarm_file_section_migrate_from_to_internal));
}

/*
 * internal functions which are used from gfs_pio_section.c and gfs_exec.c
 */

static char *
lock_local_file_section(struct gfarm_file_section_info *sinfo,
	char *canonical_self_hostname,
	char **localpathp, int *replication_neededp)
{
	char *e, *localpath;
	struct stat st;
	int metadata_exist, localfile_exist;

	e = gfs_file_section_info_check_busy(sinfo);
	if (e != NULL)
		return (e);

	e = gfarm_path_localize_file_section(sinfo->pathname, sinfo->section,
	    &localpath);
	if (e != NULL)
		return (e);

	/* critical section starts */
	gfs_lock_local_path_section(localpath);

	/* FT - check existence of the local file and its metadata */
	metadata_exist = gfarm_file_section_copy_info_does_exist(
	    sinfo->pathname, sinfo->section, canonical_self_hostname);
	localfile_exist = stat(localpath, &st) == 0 && S_ISREG(st.st_mode);

	if (metadata_exist &&
	    localfile_exist && st.st_size == sinfo->filesize) {
		/* already exist */
		/* XXX - need integrity check by checksum */
		*replication_neededp = 0;
	} else {
		if (localfile_exist) /* FT - unknown local file.  delete it */
			unlink(localpath);
		if (metadata_exist)  /* FT - delete dangling metadata */
			gfarm_file_section_copy_info_remove(
			    sinfo->pathname, sinfo->section,
			    canonical_self_hostname);
		*replication_neededp = 1;
	}
	*localpathp = localpath;
	return (NULL);
}

char *
gfarm_file_section_replicate_from_to_local_with_locking(
	struct gfarm_file_section_info *sinfo, gfarm_mode_t mode,
	char *src_canonical_hostname, char *src_if_hostname,
	char **localpathp)
{
	char *e, *canonical_self_hostname, *localpath;
	int replication_needed;

	e = gfarm_host_get_canonical_self_name(&canonical_self_hostname);
	if (e != NULL)
		return (e);

	/* critical section starts */
	e = lock_local_file_section(sinfo, canonical_self_hostname,
	    &localpath, &replication_needed);
	if (e != NULL)
		return (e);

	if (replication_needed) {
		if (!gfarm_is_active_fsnode())
			e = "gfsd is not running now";
		else if (!gfarm_is_active_fsnode_to_write(sinfo->filesize))
			e = "not enough free disk space";
		else
			e = gfarm_file_section_replicate_without_busy_check(
			    sinfo->pathname, sinfo->section,
			    mode, sinfo->filesize, src_canonical_hostname,
			    src_if_hostname, canonical_self_hostname);
	}

	gfs_unlock_local_path_section(localpath);
	/* critical section ends */

	if (e == NULL && localpathp != NULL)
		*localpathp = localpath;
	else
		free(localpath);
	return (e);
}

char *
gfarm_file_section_replicate_to_local_with_locking(
	struct gfarm_file_section_info *sinfo, gfarm_mode_t mode,
	char **localpathp)
{
	char *e, *canonical_self_hostname, *localpath;
	int replication_needed;

	e = gfarm_host_get_canonical_self_name(&canonical_self_hostname);
	if (e != NULL)
		return (e);

	/* critical section starts */
	e = lock_local_file_section(sinfo, canonical_self_hostname,
	    &localpath, &replication_needed);
	if (e != NULL)
		return (e);

	if (replication_needed) {
		if (!gfarm_is_active_fsnode())
			e = "gfsd is not running now";
		else if (!gfarm_is_active_fsnode_to_write(sinfo->filesize))
			e = "not enough free disk space";
		else
			e = gfarm_file_section_transfer_to_internal(
			    sinfo->pathname, sinfo->section,
			    mode, sinfo->filesize, canonical_self_hostname,
			    gfarm_file_section_replicate_without_busy_check);
	}

	gfs_unlock_local_path_section(localpath);
	/* critical section ends */

	if (e == NULL && localpathp != NULL)
		*localpathp = localpath;
	else
		free(localpath);
	return (e);
}

/*
 * gfarm_url_program_deliver()
 */

char *
gfarm_url_program_deliver(const char *gfarm_url, int nhosts, char **hosts,
			  char ***delivered_paths)
{
	char *e, *e2, **dp, *gfarm_file, *root, *arch, **canonical_hostnames;
	gfarm_mode_t mode;
	int i;
	struct gfarm_path_info pi;

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL)
		return (e);

	e = gfarm_path_info_get(gfarm_file, &pi);
	if (e != NULL) {
		free(gfarm_file);
		return (e);
	}
	if (!GFARM_S_IS_PROGRAM(pi.status.st_mode)) {
		gfarm_path_info_free(&pi);
		free(gfarm_file);
		return ("gfarm_url_program_deliver(): not a program");
	}
	/*
	 * XXX FIXME
	 * This may be called with GFARM_REPLICATION_BOOTSTRAP_METHOD
	 * to deliver gfrepbe_client/gfrepbe_server.
	 */
	e = gfarm_fabricate_mode_for_replication(&pi.status, &mode);
	gfarm_path_info_free(&pi);
	if (e != NULL) {
		free(gfarm_file);
		return (e);
	}
	GFARM_MALLOC_ARRAY(dp, nhosts + 1);
	if (dp == NULL) {
		free(gfarm_file);
		return (GFARM_ERR_NO_MEMORY);
	}
	dp[nhosts] = NULL;

	e = gfarm_host_get_canonical_names(nhosts, hosts,
	    &canonical_hostnames);
	if (e != NULL) {
		free(dp);
		free(gfarm_file);
		return (e);
	}

	/* XXX - this is too slow */
	for (i = 0; i < nhosts; i++) {
		struct gfarm_file_section_info si;

		dp[i] = NULL; /* for error handling */
		arch = gfarm_host_info_get_architecture_by_host(
		    canonical_hostnames[i]);
		if (arch == NULL) {
			/* architecture of the hosts[i] is not registered */
			e = "cannot deliver program to an unregistered host";
			goto error;
		}

		/* XXX - which to use? `hosts[i]' vs `copies[j].hostname' */
		e = gfs_client_get_spool_root_with_reconnection(hosts[i],
		    NULL, &e2, &root);
		if (e != NULL || e2 != NULL) {
			if (e == NULL)
				e = e2;
			free(arch);
			goto error;
		}
		e = gfarm_full_path_file_section(root, gfarm_file,
		    arch, &dp[i]);
		free(root);
		if (e != NULL) {
			free(arch);
			goto error;
		}
		e = gfarm_file_section_info_get(gfarm_file, arch, &si);
		if (e != NULL) {
			free(arch);
			goto error;
		}

		/*
		 * replicate the program
		 */
		e = gfarm_file_section_replicate_to_internal(gfarm_file, arch,
		    mode, si.filesize, hosts[i]);
		gfarm_file_section_info_free(&si);
		free(arch);
		if (e != NULL)
			goto error;
	}

	gfarm_strings_free_deeply(nhosts, canonical_hostnames);
	free(gfarm_file);
	*delivered_paths = dp;
	return (NULL);

error:
	gfarm_strings_free_deeply(nhosts, canonical_hostnames);
	free(gfarm_file);
	gfarm_strings_free_deeply(i + 1, dp);
	*delivered_paths = NULL;
	return (e);
}

/*
o * XXX This function isn't actually used.
 *
 * The reason why this function remains is just because it's exported by
 * public header <gfarm/gfs.h>.
 */

char *
gfarm_url_program_register(
	const char *gfarm_url, char *architecture,
	char *filename, int nreplicas)
{
	char *e, *e_save = NULL, *if_hostname, *gfarm_file;
	int nhosts, fd, i;
	struct gfarm_host_info *hosts;
	struct sockaddr peer_addr;
	size_t rv;
	int length; /* XXX - should be size_t */
	GFS_File gf;
	struct stat s;
	char buffer[GFS_LOCAL_FILE_BUFSIZE];
	char *self_name;
	char **hostnames;

	if (stat(filename, &s) == -1)
		return (gfarm_errno_to_error(errno));
	if (!GFARM_S_IS_PROGRAM(s.st_mode))
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);

	/* XXX - use better strategy for the replication */

	e = gfarm_host_info_get_allhost_by_architecture(architecture,
	    &nhosts, &hosts);
	if (e == GFARM_ERR_NO_SUCH_OBJECT)
		return ("gfarm_url_program_register(): no such architecture");
	if (e != NULL)
		goto finish;

	GFARM_MALLOC_ARRAY(hostnames, nhosts);
	if (hostnames == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto finish_host_info;
	}
	for (i = 0; i < nhosts; i++)
		hostnames[i] = hosts[i].hostname;
	if (nhosts < nreplicas)
		nreplicas = nhosts;

	if (!gfarm_schedule_write_local_priority() ||
	    !gfarm_is_active_fsnode_to_write(0) ||
	    gfarm_host_get_canonical_self_name(&self_name) != NULL) {
		e = gfarm_schedule_search_idle_hosts_to_write(
		    nhosts, hostnames, nreplicas, hostnames);
	} else {
		/* give the top priority to self host */
		char * tmp;
		for (i = 0; i < nhosts; i++)
			if (strcmp(hostnames[i], self_name) == 0)
				break;
		if (i != 0 && i < nhosts) {
			tmp = hostnames[0];
			hostnames[0] = hostnames[i];
			hostnames[i] = tmp;
		}
		if (nreplicas > 1)
			e = gfarm_schedule_search_idle_hosts_to_write(
			    nhosts - 1, hostnames + 1,
			    nreplicas - 1, hostnames + 1);
	}
	if (e != NULL)
		goto finish_hostnames;

	/* reflect "address_use" directive in the `hostnames[0]' */
	e = gfarm_host_address_get(hostnames[0], gfarm_spool_server_port,
	    &peer_addr, &if_hostname);
	if (e != NULL)
		goto finish_hostnames;

	/*
	 * register the program
	 */
	fd = open(filename, O_RDONLY);
	if (fd == -1) {
		e = "gfarm_url_program_register(): can't open program";
		goto finish_if_hostname;
	}
	/* XXX - overwrite case */
	e = gfs_pio_create(gfarm_url, GFARM_FILE_WRONLY|GFARM_FILE_TRUNC,
	    s.st_mode & GFARM_S_ALLPERM, &gf);
	if (e != NULL) {
		close(fd);
		goto finish_if_hostname;
	}
	/* XXX - better strategy to select replica */
	e = gfs_pio_set_view_section(gf, architecture, if_hostname, 0);
	if (e != NULL) {
		/* XXX - take care of the case where node is down */
		gfs_pio_close(gf);
		close(fd);
		goto finish_if_hostname;
	}
	for (;;) {
		rv = read(fd, buffer, sizeof(buffer));
		if (rv <= 0)
			break;
		/* XXX - partial write case ? */
		e = gfs_pio_write(gf, buffer, rv, &length);
		if (e != NULL)
			break;
	}
	e_save = e;
	e = gfs_pio_close(gf);
	close(fd);
	if (e_save != NULL)
		e = e_save;
	if (e != NULL)
		goto finish_if_hostname;

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL)
		goto finish_if_hostname;

	/*
	 * replicate the program
	 */
	for (i = 1; i < nreplicas; i++) {
		/* XXX - better strategy to select replica */
		e = gfarm_file_section_replicate_from_to_internal(
		    gfarm_file, architecture,
		    s.st_mode & GFARM_S_ALLPERM, s.st_size,
		    hostnames[0], if_hostname, hostnames[i]);
		if (e != NULL)
			e_save = e;
	}
	e = NULL; /* there is at least on copy available. */
	/* XXX - partial error case? */

	free(gfarm_file);
finish_if_hostname:
	free(if_hostname);
finish_hostnames:
	free(hostnames);
finish_host_info:
	gfarm_host_info_free_all(nhosts, hosts);
finish:
	return (e);
}
