#include <stdlib.h> /* off_t */
#include <sys/types.h> /* off_t */
#include <sys/socket.h> /* struct sockaddr */
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <gfarm/gfarm.h>
#include "host.h"
#include "gfs_client.h"

char *
gfarm_url_fragment_cleanup(char *gfarm_url, int nhosts, char **hosts)
{
	char *e, *gfarm_file, **canonical_hostnames;
	int i, j, nfrags, ncopies;
	struct gfarm_file_section_info *frags;
	struct gfarm_file_section_copy_info *copies;
	struct gfs_connection *gfs_server;

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL)
		return (e);
	e = gfarm_file_section_info_get_all_by_file(
		gfarm_file, &nfrags, &frags);
	if (e != NULL) {
		free(gfarm_file);
		return (NULL);
	}
	gfarm_file_section_info_free_all(nfrags, frags);
	if (nfrags == nhosts) {
		free(gfarm_file);
		return (NULL); /* complete */
	}

	e = gfarm_host_get_canonical_names(nhosts, hosts,
	    &canonical_hostnames);
	if (e != NULL) {
		free(gfarm_file);
		return (e);
	}

	/*
	 * do removal
	 */
	/* remove gfarm_file_fragment_copy_info and actual copy */
	for (i = 0; i < nhosts; i++) {
		char *path_section;
		char section_string[GFARM_INT32STRLEN + 1];
		struct sockaddr peer_addr;

		sprintf(section_string, "%d", i);
		e = gfarm_path_section(gfarm_file, section_string,
		    &path_section);
		if (e != NULL)
			continue;
		e = gfarm_host_address_get(hosts[i],
		    gfarm_spool_server_port, &peer_addr, NULL);
		if (e != NULL) {
			free(path_section);
			continue;
		}
		/* remove copy */
		if (gfs_client_connection(canonical_hostnames[i],
		    &peer_addr, &gfs_server) == NULL)
			gfs_client_unlink(gfs_server, path_section);
		e = gfarm_file_section_copy_info_get_all_by_section(
		    gfarm_file, section_string, &ncopies, &copies);
		if (e != NULL) {
			free(path_section);
			continue;
		}
		for (j = 0; j < ncopies; j++) {
			/*
			 * equivalent to
			 * gfarm_file_section_copy_info_remove_all_by_section()
			 */
			gfarm_file_section_copy_info_remove(gfarm_file,
			    section_string, copies[j].hostname);

			/* remove actual copies */
			if (strcasecmp(copies[j].hostname,
			    canonical_hostnames[i]) == 0)
				continue;
			if (gfarm_host_address_get(copies[j].hostname,
			    gfarm_spool_server_port, &peer_addr, NULL) != NULL)
				continue;
			if (gfs_client_connection(copies[j].hostname,
			    &peer_addr, &gfs_server) == NULL)
				gfs_client_unlink(gfs_server, path_section);
		}
		free(path_section);
		gfarm_file_section_copy_info_free_all(ncopies, copies);
	}
	gfarm_file_section_info_remove_all_by_file(gfarm_file);
	gfarm_path_info_remove(gfarm_file);
	gfarm_strings_free_deeply(nhosts, canonical_hostnames);
	free(gfarm_file);
	return (NULL);
}

char *
gfarm_url_fragment_number(const char *gfarm_url, int *np)
{
	char *e, *gfarm_file;
	struct gfarm_path_info pi;

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL)
		return (e);
	e = gfarm_path_info_get(gfarm_file, &pi);
	free(gfarm_file);
	if (e != NULL)
		return (e);
	if (!GFARM_S_IS_FRAGMENTED_FILE(pi.status.st_mode))
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	*np = pi.status.st_nsections;
	gfarm_path_info_free(&pi);
	return (NULL);
}
