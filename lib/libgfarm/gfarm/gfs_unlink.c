/*
 * $Id$
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <openssl/evp.h>
#include <gfarm/gfarm.h>
#include "config.h"
#include "gfs_client.h"
#include "gfs_pio.h"	/* gfs_profile */
#include "gfs_misc.h"	/* gfs_unlink_replica_internal() */
#include "timer.h"

/*  */

/* XXX - should provide parallel version. */
char *
gfarm_foreach_copy(char *(*op)(struct gfarm_file_section_copy_info *, void *),
	const char *gfarm_file, const char *section, void *arg, int *nsuccessp)
{
	char *e, *e_save = NULL;
	int j, ncopies, nsuccess = 0;
	struct gfarm_file_section_copy_info *copies;

	e = gfarm_file_section_copy_info_get_all_by_section(
		gfarm_file, section, &ncopies, &copies);
	if (e == NULL) {
		for (j = 0; j < ncopies; j++) {
			e = op(&copies[j], arg);
			if (e == NULL)
				++nsuccess;
			if (e_save == NULL)
				e_save = e;
		}
		gfarm_file_section_copy_info_free_all(ncopies, copies);
	}
	if (nsuccessp != NULL)
		*nsuccessp = nsuccess;

	return (e_save != NULL ? e_save : e);
}

char *
gfarm_foreach_section(char *(*op)(struct gfarm_file_section_info *, void *),
	const char *gfarm_file, void *arg,
	char *(*undo_op)(struct gfarm_file_section_info *, void *))
{
	char *e, *e_save = NULL;
	int i, nsections;
	struct gfarm_file_section_info *sections;

	e = gfarm_file_section_info_get_all_by_file(gfarm_file,
	    &nsections, &sections);
	if (e == NULL) {
		for (i = 0; i < nsections; i++) {
			e = op(&sections[i], arg);
			if (e != NULL && undo_op) {
				for (; i >= 0; --i)
					undo_op(&sections[i], arg);
				break;
			}
			if (e_save == NULL)
				e_save = e;
		}
		gfarm_file_section_info_free_all(nsections, sections);
	}
	return (e_save != NULL ? e_save : e);
}

/*  */

char *
gfs_unlink_replica_internal(const char *gfarm_file, const char *section,
	const char *hostname)
{
	char *path_section, *e, *e_int;
	struct gfs_connection *gfs_server;
	struct sockaddr peer_addr;

	/* metadata part */
	e = gfarm_file_section_copy_info_remove(gfarm_file, section, hostname);
	if (e != NULL)
		goto finish;

	/* physical file part */
	e_int = gfarm_host_address_get(hostname, gfarm_spool_server_port,
		&peer_addr, NULL);
	if (e_int != NULL)
		goto finish;

	e_int = gfs_client_connection(hostname, &peer_addr, &gfs_server);
	if (e_int != NULL)
		goto finish;

	e_int = gfarm_path_section(gfarm_file, section, &path_section);
	if (e_int == NULL) {
		e_int = gfs_client_unlink(gfs_server, path_section);
		free(path_section);
	}
finish:
	/*
	 * how to report e_int?  This usually results in a junk file
	 * unless it is GFARM_ERR_NO_SUCH_OBJECT.
	 */
	return (e);
}

static char *
unlink_copy_remove(struct gfarm_file_section_copy_info *info, void *arg)
{
	return (gfs_unlink_replica_internal(
			info->pathname, info->section, info->hostname));
}

static char *
unlink_section_remove(struct gfarm_file_section_info *info, void *arg)
{
	gfarm_foreach_copy(unlink_copy_remove,
		info->pathname, info->section, arg, NULL);
	return (gfarm_file_section_info_remove(info->pathname, info->section));
}

static char *
gfs_unlink_check_perm(char *gfarm_file)
{
	struct gfarm_path_info pi;
	char *e;

	e = gfarm_path_info_get(gfarm_file, &pi);
	if (e == NULL) {
		if (GFARM_S_ISDIR(pi.status.st_mode))
			e = GFARM_ERR_IS_A_DIRECTORY;
		else
			e = gfarm_path_info_access(&pi, W_OK);
		gfarm_path_info_free(&pi);
	}
	return (e);
}

double gfs_unlink_time;

char *
gfs_unlink(const char *gfarm_url)
{
	char *gfarm_file, *e, *e1 = NULL;
	gfarm_timerval_t t1, t2;

	gfs_profile(gfarm_gettimerval(&t1));

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL)
		goto finish_unlink;

	e = gfs_unlink_check_perm(gfarm_file);
	if (e != NULL)
		goto finish_free_gfarm_file;

	e = gfarm_foreach_section(unlink_section_remove,
		gfarm_file, NULL, NULL);
	e1 = gfarm_path_info_remove(gfarm_file);

finish_free_gfarm_file:
	free(gfarm_file);

finish_unlink:
	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(gfs_unlink_time += gfarm_timerval_sub(&t2, &t1));

	return (e != NULL ? e : e1);
}

/* internal use in the gfarm library */
char *
gfs_unlink_section_internal(const char *gfarm_file, const char *section)
{
	char *e1, *e2;

	e1 = gfarm_foreach_copy(unlink_copy_remove,
		gfarm_file, section, NULL, NULL);
	e2 = gfarm_file_section_info_remove(gfarm_file, section);

	return (e1 != NULL ? e1 : e2);
}

char *
gfs_unlink_section(const char *gfarm_url, const char *section)
{
	char *e, *gfarm_file;
	int nsections;
	struct gfarm_file_section_info *sections;

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL)
		return (e);

	e = gfs_unlink_check_perm(gfarm_file);
	if (e != NULL)
		goto free_gfarm_file;

	e = gfs_unlink_section_internal(gfarm_file, section);
	if (e == NULL) {
		e = gfarm_file_section_info_get_all_by_file(
			gfarm_file, &nsections, &sections);
		if (e == NULL)
			gfarm_file_section_info_free_all(nsections, sections);
		else if (e == GFARM_ERR_NO_SUCH_OBJECT)
			e = gfarm_path_info_remove(gfarm_file);
	}

 free_gfarm_file:
	free(gfarm_file);
	return (e);
}

char *
gfs_unlink_section_replica(const char *gfarm_url, const char *section,
	int nreplicas, char **replica_hosts, int force)
{
	char *e, *e_save = NULL;
	char *gfarm_file, **replica_canonical_hostnames;
	int i, j, ncopies, ndeletes;
	struct gfarm_file_section_copy_info *copies;
	char *do_delete;
	int remove_section_info = 0;

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL)
		goto finish;

	e = gfs_unlink_check_perm(gfarm_file);
	if (e != NULL)
		goto finish_gfarm_file;

	e = gfarm_file_section_copy_info_get_all_by_section(
	    gfarm_file, section, &ncopies, &copies);
	if (e != NULL) {
		if (e == GFARM_ERR_NO_SUCH_OBJECT && force)
			/*
			 * filesystem metadata should be collapsed.
			 * Ignore the previous error and delete the
			 * section information.
			 */
			e = gfarm_file_section_info_remove(
				gfarm_file, section);
		goto finish_gfarm_file;
	}
	if (ncopies == 0) {
		/* assert(0); */
		e = "gfs_unlink_section_replica: no file replica";
		goto finish_copies;
	}

	do_delete = malloc(ncopies);
	if (do_delete == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto finish_copies;
	}
	memset(do_delete, 0, ncopies);

	e = gfarm_host_get_canonical_names(nreplicas, replica_hosts,
	    &replica_canonical_hostnames);
	if (e != NULL)
		goto finish_do_delete;

	ndeletes = 0;
	for (i = 0; i < nreplicas; i++) {
		for (j = 0; j < ncopies; j++) {
			if (strcasecmp(replica_canonical_hostnames[i],
			    copies[j].hostname) == 0) {
				if (do_delete[j]) {
					e = "gfs_unlink_section_replica: "
					    "duplicate hostname";
					if (e_save == NULL)
						e_save = e;
					/* do not finish, but continue */
					continue;
				}
				do_delete[j] = 1;
				ndeletes++;
				break;
			}
		}
		if (j >= ncopies && e_save == NULL) {
			e_save = GFARM_ERR_NO_REPLICA_ON_HOST;
			/* do not finish, but continue */
		}
	}
	if (ndeletes == ncopies) {
		if (force == 0) {
			e_save = "cannot remove all replicas";
			goto finish_replica_canonical_hostnames;
		}
		else
			remove_section_info = 1;
	}
	for (j = 0; j < ncopies; j++) {
		if (!do_delete[j])
			continue;
		e = gfs_unlink_replica_internal(gfarm_file, section,
			copies[j].hostname);
		if (e != NULL) {
			if (e_save == NULL)
				e_save = e;
			continue;
		}
	}
	if (remove_section_info) {
		int ncps;
		struct gfarm_file_section_copy_info *cps;
		e = gfarm_file_section_copy_info_get_all_by_section(
			gfarm_file, section, &ncps, &cps);
		if (e == GFARM_ERR_NO_SUCH_OBJECT)
			/* Oh, there is no section copy info. */
			/* Some filesystem nodes might be down. */
			e = gfarm_file_section_info_remove(
				gfarm_file, section);
		else if (e == NULL)
			gfarm_file_section_copy_info_free_all(ncps, cps);

		if (e != NULL) {
			if (e_save == NULL)
				e_save = e;
		}
	}

finish_replica_canonical_hostnames:
	gfarm_strings_free_deeply(nreplicas, replica_canonical_hostnames);
finish_do_delete:
	free(do_delete);
finish_copies:
	gfarm_file_section_copy_info_free_all(ncopies, copies);
finish_gfarm_file:
	free(gfarm_file);
finish:
	return (e_save != NULL ? e_save : e);
}

/*
 * Eliminate file replicas of a gfarm_url on a specified node list.
 */
char *
gfs_unlink_replica(const char *gfarm_url,
	int nreplicas, char **replica_hosts, int force)
{
	char *gfarm_file, *e, *e_save;
	int i, nsections, nerr;
	struct gfarm_file_section_info *sections;

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL)
		return (e);

	e_save = gfs_unlink_check_perm(gfarm_file);
	if (e_save != NULL)
		goto free_gfarm_file;

	e_save = gfarm_file_section_info_get_all_by_file(gfarm_file,
	    &nsections, &sections);
	if (e_save != NULL)
		goto free_gfarm_file;

	nerr = 0;
	for (i = 0; i < nsections; i++) {
		e = gfs_unlink_section_replica(gfarm_url, sections[i].section,
			nreplicas, replica_hosts, force);
		if (e != NULL) {
			++nerr;
			if (e_save == NULL)
				e_save = e;
		}
	}
	/* reset error when at least one file replica is unlinked */
	if (nerr < nsections)
		e_save = NULL;
	gfarm_file_section_info_free_all(nsections, sections);

free_gfarm_file:
	free(gfarm_file);
	return (e_save);
}

/*
 * internal use - eliminate all file replicas that are not stored on
 * the specified host.
 */
char *
gfs_unlink_every_other_replicas(const char *gfarm_file, const char *section,
	const char *canonical_hostname)
{
	char *e, *e_save = NULL;
	int j, ncopies;
	struct gfarm_file_section_copy_info *copies;

	e = gfarm_file_section_copy_info_get_all_by_section(
		gfarm_file, section, &ncopies, &copies);
	if (e != NULL)
		return (e);

	for (j = 0; j < ncopies; j++) {
		/* skip when the specified node. */
		if (strcmp(canonical_hostname, copies[j].hostname) == 0)
			continue;

		e = gfs_unlink_replica_internal(gfarm_file,
			section, copies[j].hostname);
		if (e_save == NULL)
			e_save = e;
	}
	gfarm_file_section_copy_info_free_all(ncopies, copies);
	return (e_save);
}
