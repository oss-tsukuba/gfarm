/*
 * $Id$
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <string.h>
#include <stdlib.h>
#include <openssl/evp.h>
#include <gfarm/gfarm.h>
#include "config.h"
#include "gfs_client.h"
#include "gfs_pio.h"	/* gfs_profile */
#include "gfs_misc.h"	/* gfs_unlink_replica_internal() */
#include "timer.h"

char *
gfs_unlink_replica_internal(const char *gfarm_file, const char *section,
	const char *hostname)
{
	char *path_section, *e;
	struct gfs_connection *gfs_server;
	struct sockaddr peer_addr;

	e = gfarm_path_section(gfarm_file, section, &path_section);
	if (e != NULL)
		goto finish;

	e = gfarm_file_section_copy_info_remove(gfarm_file, section, hostname);
	if (e != NULL)
		goto finish_path_section;

	e = gfarm_host_address_get(hostname, gfarm_spool_server_port,
		&peer_addr, NULL);
	if (e != NULL)
		goto finish_path_section;

	e = gfs_client_connection(hostname, &peer_addr, &gfs_server);
	if (e != NULL)
		goto finish_path_section;

	e = gfs_client_unlink(gfs_server, path_section);

finish_path_section:
	free(path_section);
finish:
	return (e);
}

double gfs_unlink_time;

char *
gfs_unlink(const char *gfarm_url)
{
	char *gfarm_file, *e, *e_save = NULL;
	int i, j, nsections;
	struct gfarm_path_info pi;
	struct gfarm_file_section_info *sections;
	gfarm_timerval_t t1, t2;

	gfs_profile(gfarm_gettimerval(&t1));

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL)
		goto finish_unlink;

	e = gfarm_path_info_get(gfarm_file, &pi);
	if (e != NULL)
		goto finish_free_gfarm_file;

	if (GFARM_S_ISDIR(pi.status.st_mode)) {
		gfarm_path_info_free(&pi);
		e = GFARM_ERR_IS_A_DIRECTORY;
		goto finish_free_gfarm_file;
	}
	gfarm_path_info_free(&pi);
	e = gfarm_file_section_info_get_all_by_file(gfarm_file,
	    &nsections, &sections);
	if (e != NULL) {
		if (e != GFARM_ERR_NO_SUCH_OBJECT)
			goto finish_free_gfarm_file;
		/* no fragment information */
		nsections = 0;
		sections = NULL;
	}
	/* XXX - should unlink in parallel. */
	for (i = 0; i < nsections; i++) {
		int ncopies;
		struct gfarm_file_section_copy_info *copies;

		e = gfarm_file_section_copy_info_get_all_by_section(
		    gfarm_file, sections[i].section, &ncopies, &copies);
		if (e != NULL) {
			if (e_save == NULL)
				e_save = e;
			continue;
		}
		for (j = 0; j < ncopies; j++) {
			e = gfs_unlink_replica_internal(gfarm_file,
				sections[i].section, copies[j].hostname);
			if (e != NULL && e_save == NULL)
				e_save = e;
		}
		gfarm_file_section_copy_info_free_all(ncopies, copies);
	}
	if (sections != NULL) {
		gfarm_file_section_info_free_all(nsections, sections);
		e = gfarm_file_section_info_remove_all_by_file(gfarm_file);
		if (e != NULL && e_save == NULL)
			e_save = e;
	}
	e = gfarm_path_info_remove(gfarm_file);
	if (e != NULL && e_save == NULL)
		e_save = e;

finish_free_gfarm_file:
	free(gfarm_file);

finish_unlink:
	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(gfs_unlink_time += gfarm_timerval_sub(&t2, &t1));

	return (e_save != NULL ? e_save : e);
}

/* internal use in the gfarm library */
char *
gfs_unlink_section_internal(const char *gfarm_file, const char *section)
{
	char *e, *e_save = NULL;
	int j, ncopies;
	struct gfarm_file_section_copy_info *copies;

	e = gfarm_file_section_copy_info_get_all_by_section(
		gfarm_file, section, &ncopies, &copies);
	if (e == GFARM_ERR_NO_SUCH_OBJECT) {
		/* if there is the section info, remove it */
		(void)gfarm_file_section_info_remove(gfarm_file, section);
		return (e);
	}
	if (e != NULL)
		return (e);

	for (j = 0; j < ncopies; j++) {
		e = gfs_unlink_replica_internal(gfarm_file, section,
			copies[j].hostname);
		if (e != NULL) {
			if (e_save == NULL)
				e_save = e;
			continue;
		}
	}
	gfarm_file_section_copy_info_free_all(ncopies, copies);

	(void)gfarm_file_section_copy_info_remove_all_by_section(
		gfarm_file, section);
	e = gfarm_file_section_info_remove(gfarm_file, section);

	if (e != NULL && e != GFARM_ERR_NO_SUCH_OBJECT) {
		if (e_save == NULL)
			e_save = e;
	}

	return (e_save != NULL ? e_save : e);
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
	e = gfarm_file_section_info_get_all_by_file(gfarm_file, &nsections,
						    &sections);
	if (e != NULL)
		goto free_gfarm_file;
	e = gfs_unlink_section_internal(gfarm_file, section);
	if (e == NULL && nsections <= 1)
		e = gfarm_path_info_remove(gfarm_file);

	gfarm_file_section_info_free_all(nsections, sections);
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
	int i, nsections;
	struct gfarm_file_section_info *sections;

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL)
		return (e);

	e_save = gfarm_file_section_info_get_all_by_file(gfarm_file,
	    &nsections, &sections);
	if (e_save != NULL) {
		nsections = 0;
		sections = NULL;
	}
	for (i = 0; i < nsections; i++) {
		e = gfs_unlink_section_replica(gfarm_url, sections[i].section,
			nreplicas, replica_hosts, force);
		if (e != NULL && e_save == NULL)
			e_save = e;
	}
	if (sections != NULL)
		gfarm_file_section_info_free_all(nsections, sections);
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
