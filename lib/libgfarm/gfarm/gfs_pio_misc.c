/*
 * $Id$
 */

#include <sys/types.h>
#include <sys/socket.h> /* struct sockaddr */
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/time.h> /* for gfs_utime() */
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <openssl/evp.h>
#include <gfarm/gfarm.h>
#include "host.h"
#include "gfs_proto.h" /* for gfs_digest_calculate_local() */
#include "gfs_client.h"
#include "gfs_pio.h"
#include "schedule.h"
#include "timer.h"

char *
gfs_stat_canonical_path(char *gfarm_file, struct gfs_stat *s)
{
	char *e;
	int i, nsections;
	struct gfarm_file_section_info *sections;
	struct gfarm_path_info pi;
	long ino;

	e = gfs_get_ino(gfarm_file, &ino);
	if (e != NULL)
		return (e);

	e = gfarm_path_info_get(gfarm_file, &pi);
	if (e != NULL)
		return (e);

	*s = pi.status;
	s->st_ino = ino;
	s->st_user = strdup(s->st_user);
	s->st_group = strdup(s->st_group);
	gfarm_path_info_free(&pi);
	if (s->st_user == NULL || s->st_group == NULL) {
		gfs_stat_free(s);
		return (GFARM_ERR_NO_MEMORY);
	}

	if (!GFARM_S_ISREG(s->st_mode))
		return (NULL);

	/* regular file */
	e = gfarm_file_section_info_get_all_by_file(gfarm_file,
	    &nsections, &sections);
	if (e != NULL) {
		gfs_stat_free(s);
		/*
		 * If GFARM_ERR_NO_SUCH_OBJECT is returned here,
		 * gfs_stat() incorrectly assumes that this is a directory,
		 * and reports GFARM_ERR_NOT_A_DIRECTORY.
		 */
		return ("no fragment information");
	}

	s->st_size = 0;
	for (i = 0; i < nsections; i++)
		s->st_size += sections[i].filesize;
	s->st_nsections = nsections;

	gfarm_file_section_info_free_all(nsections, sections);

	return (NULL);
}

double gfs_stat_time;

char *
gfs_stat(const char *path, struct gfs_stat *s)
{
	char *e, *p;
	gfarm_timerval_t t1, t2;
	long ino;

	gfs_profile(gfarm_gettimerval(&t1));

	path = gfarm_url_prefix_skip(path);
	e = gfarm_canonical_path(path, &p);
	if (e != NULL)
		goto finish;
	e = gfs_stat_canonical_path(p, s);
	if (e == NULL)
		goto finish_free_p;
	if (e != GFARM_ERR_NO_SUCH_OBJECT)
		goto finish_free_p;

	/*
	 * XXX - assume that it's a directory that does not have the
	 * path info.
	 */
	e = gfs_get_ino(p, &ino);
	if (e != NULL)
		goto finish_free_p;
	s->st_ino = ino;
	s->st_mode = GFARM_S_IFDIR | 0777;
	s->st_user = strdup("root");
	s->st_group = strdup("gfarm");
	s->st_atimespec.tv_sec = 0;
	s->st_atimespec.tv_nsec = 0;
	s->st_mtimespec.tv_sec = 0;
	s->st_mtimespec.tv_nsec = 0;
	s->st_ctimespec.tv_sec = 0;
	s->st_ctimespec.tv_nsec = 0;
	s->st_size = 0;
	s->st_nsections = 0;

	e = NULL;
 finish_free_p:
	free(p);
 finish:
	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(gfs_stat_time += gfarm_timerval_sub(&t2, &t1));

	return (e);
}

void
gfs_stat_free(struct gfs_stat *s)
{
	if (s->st_user != NULL)
		free(s->st_user);
	if (s->st_group != NULL)
		free(s->st_group);
}

char *
gfs_stat_section(const char *gfarm_url, const char *section, struct gfs_stat *s)
{
	char *e, *gfarm_file;
	struct gfarm_file_section_info sinfo;
	struct gfarm_path_info pi;
	long ino;

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL)
		return (e);
	e = gfs_get_ino(gfarm_file, &ino);
	if (e != NULL) {
		free(gfarm_file);
		return (e);
	}
	e = gfarm_path_info_get(gfarm_file, &pi);
	if (e != NULL) {
		free(gfarm_file);
		return (e);
	}

	*s = pi.status;
	s->st_ino = ino;
	s->st_user = strdup(s->st_user);
	s->st_group = strdup(s->st_group);
	gfarm_path_info_free(&pi);

	if (!GFARM_S_ISREG(s->st_mode)) {
		free(gfarm_file);
		return (NULL);
	}

	e = gfarm_file_section_info_get(gfarm_file, section, &sinfo);
	free(gfarm_file);
	if (e != NULL)
		return (e);

	s->st_size = sinfo.filesize;
	s->st_nsections = 1;

	gfarm_file_section_info_free(&sinfo);

	return (NULL);
}

char *
gfs_stat_index(char *gfarm_url, int index, struct gfs_stat *s)
{
	char section[GFARM_INT32STRLEN + 1];

	sprintf(section, "%d", index);

	return (gfs_stat_section(gfarm_url, section, s));
}

char *
gfs_fstat(GFS_File gf, struct gfs_stat *status)
{
	return ((*gf->ops->view_stat)(gf, status));
}

char *
gfs_access(const char *gfarm_url, int mode)
{
	char *e, *gfarm_file;
	struct gfarm_path_info pi;
	int nsections;
	struct gfarm_file_section_info *sections;

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL)
		return (e);
	e = gfarm_path_info_get(gfarm_file, &pi);
	if (e != NULL) {
		free(gfarm_file);
		return (e);
	}
	/*
	 * Check all fragments are ready or not.
	 * XXX - is this check necessary?
	 */
	e = gfarm_file_section_info_get_all_by_file(
		gfarm_file, &nsections, &sections);
	free(gfarm_file);
	if (e != NULL)
		goto finish_free_path_info;
	gfarm_file_section_info_free_all(nsections, sections);
	if (!GFARM_S_IS_PROGRAM(pi.status.st_mode)
	    && nsections != pi.status.st_nsections)
		e = GFARM_ERR_FRAGMENT_NUMBER_DOES_NOT_MATCH;
	if (e != NULL)
		e = gfarm_path_info_access(&pi, mode);
 finish_free_path_info:
	gfarm_path_info_free(&pi);
	return (e);
}

char *
gfs_utimes(const char *gfarm_url, const struct gfarm_timespec *tsp)
{
	char *e, *gfarm_file;
	struct gfarm_path_info pi;
	struct timeval now;

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL)
		return (e);
	e = gfarm_path_info_get(gfarm_file, &pi);
	free(gfarm_file);
	if (e != NULL)
		return (e);
	e = gfarm_path_info_access(&pi, GFS_W_OK);
	if (e != NULL)
		goto finish_free_path_info;

	gettimeofday(&now, NULL);
	if (tsp == NULL) {
		pi.status.st_atimespec.tv_sec = 
		pi.status.st_mtimespec.tv_sec = now.tv_sec;
		pi.status.st_atimespec.tv_nsec = 
		pi.status.st_mtimespec.tv_nsec = now.tv_usec * 1000;
	} else {
		pi.status.st_atimespec = tsp[0];
		pi.status.st_mtimespec = tsp[1];
	}
	pi.status.st_ctimespec.tv_sec = now.tv_sec;
	pi.status.st_ctimespec.tv_nsec = now.tv_usec * 1000;
	e = gfarm_path_info_replace(pi.pathname, &pi);
 finish_free_path_info:
	gfarm_path_info_free(&pi);
	return (e);
}

/*
 *
 */

static char *
digest_calculate(char *filename,
		 char **digest_type, char *digest_string, unsigned *md_len_p,
		 file_offset_t *filesizep)
{
	int fd, i, rv;
	EVP_MD_CTX md_ctx;
	unsigned char md_value[EVP_MAX_MD_SIZE];
	char buffer[GFS_FILE_BUFSIZE];

	if ((fd = open(filename, O_RDONLY)) == -1)
		return (gfarm_errno_to_error(errno));
	EVP_DigestInit(&md_ctx, GFS_DEFAULT_DIGEST_MODE);
	rv = gfs_digest_calculate_local(
		fd, buffer, GFS_FILE_BUFSIZE,
		GFS_DEFAULT_DIGEST_MODE,
		&md_ctx, md_len_p, md_value, filesizep);
	close(fd);
	if (rv != 0)
		return (gfarm_errno_to_error(rv));

	for (i = 0; i < *md_len_p; i++)
		sprintf(&digest_string[i + i], "%02x",
			md_value[i]);

	*digest_type = GFS_DEFAULT_DIGEST_NAME;
	return (NULL);
}

/*
 * Register a gfarm fragment to a Meta DB.  This function is intended
 * to be used with legacy applications to register a new file.
 */

char *
gfs_pio_set_fragment_info_local(char *filename,
	char *gfarm_file, char *section)
{
	char *digest_type;
	char digest_value_string[EVP_MAX_MD_SIZE * 2 + 1];
	unsigned int digest_len;
	file_offset_t filesize;
	char *e = NULL;
	struct gfarm_file_section_info fi;
	struct gfarm_file_section_copy_info fci;

	/* Calculate checksum. */
	e = digest_calculate(filename, &digest_type, digest_value_string,
			     &digest_len, &filesize);
	if (e != NULL)
		return (e);

	/* Update the filesystem metadata. */
	e = gfarm_file_section_info_get(gfarm_file, section, &fi);
	if (e == GFARM_ERR_NO_SUCH_OBJECT) {
		fi.filesize = filesize;
		fi.checksum_type = digest_type;
		fi.checksum = digest_value_string;

		e = gfarm_file_section_info_set(gfarm_file, section, &fi);
		if (e != NULL)
			return (e);
	}
	else if (e == NULL) {
		if (fi.filesize != filesize)
			return "file size mismatch";
		if (strcasecmp(fi.checksum_type, digest_type) != 0)
			return "checksum type mismatch";
		if (strcasecmp(fi.checksum,digest_value_string) != 0)
			return "check sum mismatch";

		gfarm_file_section_info_free(&fi);
	}
	else
		return (e);

	e = gfarm_host_get_canonical_self_name(&fci.hostname);
	if (e == NULL) {
		e = gfarm_file_section_copy_info_set(
			gfarm_file, section, fci.hostname, &fci);
	}

	return (e);
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
 * `srchost' must already reflect "address_use" directive.
 */
static char *
gfarm_file_section_replicate_from_to_internal(
	char *gfarm_file, char *section,
	gfarm_mode_t mode, file_offset_t file_size,
	char *src_canonical_hostname, char *src_if_hostname,
	char *dsthost)
{
	char *e, *path_section;
	struct gfarm_file_section_copy_info ci;
	struct gfs_connection *gfs_server;
	struct sockaddr peer_addr;

	e = gfarm_host_get_canonical_name(dsthost, &ci.hostname);
	if (e != NULL)
		goto finish;
	if (gfarm_file_section_copy_info_does_exist(gfarm_file, section,
	    ci.hostname)) /* already exists, don't have to replicate */
		goto finish_hostname;

	if (gfarm_replication_method != GFARM_REPLICATION_BOOTSTRAP_METHOD) {
		e = gfarm_file_section_replicate_from_to_by_gfrepbe(
		    gfarm_file, section,
		    src_canonical_hostname, ci.hostname);
		goto finish_hostname;
	}

	e = gfarm_path_section(gfarm_file, section, &path_section);
	if (e != NULL)
		goto finish_hostname;

	e = gfarm_host_address_get(dsthost, gfarm_spool_server_port,
	    &peer_addr, NULL);
	if (e != NULL)
		goto finish_path_section;

	e = gfs_client_connect(ci.hostname, &peer_addr, &gfs_server);
	if (e != NULL)
		goto finish_path_section;
	e = gfs_client_bootstrap_replicate_file(gfs_server,
	    path_section, mode, file_size,
	    src_canonical_hostname, src_if_hostname);
	/* FT - the parent directory of the destination may be missing */
	if (e == GFARM_ERR_NO_SUCH_OBJECT) {
		if (gfs_pio_remote_mkdir_parent_canonical_path(
			    gfs_server, gfarm_file) == NULL)
			e = gfs_client_bootstrap_replicate_file(
				gfs_server, path_section, mode, file_size,
				src_canonical_hostname, src_if_hostname);
	}
#if 0 /* XXX - not implemented yet */
	/* FT - source file should be missing */
	if (e == GFARM_ERR_NO_SUCH_OBJECT) {
		/* XXX - need to check explicitly */
		if (gfs_client_exist() == GFARM_ERR_NO_SUCH_OBJECT)
			/* Delete the section copy info */
			if (gfarm_file_section_copy_info_remove(gfarm_file,
				section, src_canonical_hostname) == NULL)
				e = GFARM_ERR_INCONSISTENT_RECOVERABLE;
	}
#endif
	gfs_client_disconnect(gfs_server);
	if (e != NULL)
		goto finish_path_section;
	e = gfarm_file_section_copy_info_set(gfarm_file, section,
	    ci.hostname, &ci);

finish_path_section:
	free(path_section);
finish_hostname:
	free(ci.hostname);
finish:
	return (e);
}

static char *
gfarm_file_section_replicate_to_internal(char *gfarm_file, char *section,
	gfarm_mode_t mode, file_offset_t file_size,
	char *dsthost)
{
	char *e, *srchost, *if_hostname;
	struct sockaddr peer_addr;

	e = gfarm_file_section_host_schedule(gfarm_file, section, &srchost);
	if (e != NULL)
		goto finish;

	/* reflect "address_use" directive in the `srchost' */
	e = gfarm_host_address_get(srchost, gfarm_spool_server_port,
	    &peer_addr, &if_hostname);
	if (e != NULL)
		goto finish_srchost;

	e = gfarm_file_section_replicate_from_to_internal(
	    gfarm_file, section, mode & GFARM_S_ALLPERM, file_size,
	    srchost, if_hostname, dsthost);

	free(if_hostname);
finish_srchost:
	free(srchost);
finish:
	return (e);
}

char *
gfarm_url_section_replicate_from_to(const char *gfarm_url, char *section,
	char *srchost, char *dsthost)
{
	char *e, *gfarm_file, *canonical_hostname, *if_hostname;
	struct sockaddr peer_addr;
	struct gfarm_path_info pi;
	struct gfarm_file_section_info si;
	gfarm_mode_t mode_allowed = 0, mode_mask = GFARM_S_ALLPERM;

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
		goto finish_canonical_hostname;

	/* reflect "address_use" directive in the `srchost' */
	e = gfarm_host_address_get(srchost, gfarm_spool_server_port,
	    &peer_addr, &if_hostname);
	if (e != NULL)
		goto finish_section_info;
	/*
	 * XXX - if the owner of a file is not the same, permit a
	 * group/other write access - This should be fixed in the next
	 * major release.
	 */
	if (strcmp(pi.status.st_user, gfarm_get_global_username()) != 0) {
		e = gfarm_path_info_access(&pi, GFS_R_OK);
		if (e != NULL)
			goto finish_path_info;
		mode_allowed = 022;
		mode_mask = 0777; /* don't allow setuid/setgid */
	}
	e = gfarm_file_section_replicate_from_to_internal(
	    gfarm_file, section,
	    (pi.status.st_mode | mode_allowed) & mode_mask, si.filesize,
	    canonical_hostname, if_hostname, dsthost);

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
gfarm_url_section_replicate_to(
	const char *gfarm_url, char *section, char *dsthost)
{
	char *e, *gfarm_file;
	struct gfarm_path_info pi;
	struct gfarm_file_section_info si;
	gfarm_mode_t mode_allowed = 0, mode_mask = GFARM_S_ALLPERM;

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL)
		goto finish;
	e = gfarm_path_info_get(gfarm_file, &pi);
	if (e != NULL)
		goto finish_gfarm_file;
	e = gfarm_file_section_info_get(gfarm_file, section, &si);
	if (e != NULL)
		goto finish_path_info;
	/*
	 * XXX - if the owner of a file is not the same, permit a
	 * group/other write access - This should be fixed in the next
	 * major release.
	 */
	if (strcmp(pi.status.st_user, gfarm_get_global_username()) != 0) {
		e = gfarm_path_info_access(&pi, GFS_R_OK);
		if (e != NULL)
			goto finish_path_info;
		mode_allowed = 022;
		mode_mask = 0777; /* don't allow setuid/setgid */
	}
	e = gfarm_file_section_replicate_to_internal(
	    gfarm_file, section,
	    (pi.status.st_mode | mode_allowed) & mode_mask, si.filesize,
	    dsthost);

	gfarm_file_section_info_free(&si);
finish_path_info:
	gfarm_path_info_free(&pi);
finish_gfarm_file:
	free(gfarm_file);
finish:
	return (e);
}

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
	char buffer[GFS_FILE_BUFSIZE];
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


	hostnames = malloc(sizeof(char *) * nhosts);
	if (hostnames == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto finish_host_info;
	}
	for (i = 0; i < nhosts; i++)
		hostnames[i] = hosts[i].hostname;
	if (nhosts < nreplicas)
		nreplicas = nhosts;

	if (gfarm_host_get_canonical_self_name(&self_name) != NULL) {
		e = gfarm_schedule_search_idle_hosts(nhosts, hostnames,
						     nreplicas, hostnames);
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
			e = gfarm_schedule_search_idle_hosts(nhosts - 1,
							     hostnames + 1,
							     nreplicas - 1,
							     hostnames + 1);
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
	e = gfs_pio_create(gfarm_url, GFARM_FILE_WRONLY,
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

char *
gfarm_url_program_deliver(const char *gfarm_url, int nhosts, char **hosts,
			  char ***delivered_paths)
{
	char *e, **dp, *gfarm_file, *root, *arch, **canonical_hostnames;
	gfarm_mode_t mode, mode_mask = GFARM_S_ALLPERM;
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
	mode = pi.status.st_mode;
	if (!GFARM_S_IS_PROGRAM(mode)) {
		gfarm_path_info_free(&pi);
		free(gfarm_file);
		return ("gfarm_url_program_deliver(): not a program");
	}
	/*
	 * XXX - if the owner of a file is not the same, permit a
	 * group/other write access - This should be fixed in the next
	 * major release.
	 */
	/*
	 * XXX FIXME
	 * This may be called with GFARM_REPLICATION_BOOTSTRAP_METHOD
	 * to deliver gfrepbe_client/gfrepbe_server.
	 */
	if (strcmp(pi.status.st_user, gfarm_get_global_username()) != 0) {
		e = gfarm_path_info_access(&pi, GFS_X_OK);
		if (e != NULL) {
			gfarm_path_info_free(&pi);
			free(gfarm_file);
			return (e);
		}
		mode |= 022;
		mode_mask = 0777; /* don't allow setuid/setgid */
	}
	gfarm_path_info_free(&pi);
	dp = malloc(sizeof(char *) * (nhosts + 1));
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
		struct sockaddr peer_addr;
		struct gfs_connection *gfs_server;
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
		e = gfarm_host_address_get(hosts[i],
		    gfarm_spool_server_port, &peer_addr, NULL);
		if (e != NULL) {
			free(arch);
			goto error;
		}

		e = gfs_client_connection(canonical_hostnames[i], &peer_addr,
		    &gfs_server);
		if (e != NULL) {
			free(arch);
			goto error;
		}
		e = gfs_client_get_spool_root(gfs_server, &root);
		if (e != NULL) {
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
		    mode & mode_mask, si.filesize, hosts[i]);
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

char *
gfarm_url_fragments_replicate(
	const char *gfarm_url, int ndsthosts, char **dsthosts)
{
	char *e, *gfarm_file, **srchosts, **edsthosts;
	int nsrchosts;
	gfarm_mode_t mode, mode_mask = GFARM_S_ALLPERM;
	int i, pid, *pids;
	struct gfarm_path_info pi;

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL)
		return (e);

	e = gfarm_path_info_get(gfarm_file, &pi);
	if (e != NULL)
		goto finish_gfarm_file;

	mode = pi.status.st_mode;
	if (!GFARM_S_IS_FRAGMENTED_FILE(mode)) {
		gfarm_path_info_free(&pi);
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		goto finish_gfarm_file;
	}
	/*
	 * XXX - if the owner of a file is not the same, permit a
	 * group/other write access - This should be fixed in the next
	 * major release.
	 */
	if (strcmp(pi.status.st_user, gfarm_get_global_username()) != 0) {
		e = gfarm_path_info_access(&pi, GFS_R_OK);
		if (e != NULL) {
			gfarm_path_info_free(&pi);
			free(gfarm_file);
			return (e);
		}
		mode |= 022;
		mode_mask = 0777; /* don't allow setuid/setgid */
	}
	gfarm_path_info_free(&pi);
	e = gfarm_url_hosts_schedule(gfarm_url, "", &nsrchosts, &srchosts);
	if (e != NULL)
		goto finish_gfarm_file;

	edsthosts = malloc(sizeof(*edsthosts) * nsrchosts);
	if (edsthosts == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto finish_srchosts;
	}
	gfarm_strings_expand_cyclic(ndsthosts, dsthosts, nsrchosts, edsthosts);

	pids = malloc(sizeof(int) * nsrchosts);
	if (pids == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto finish_edsthosts;
	}
	/*
	 * To use different connection for each metadb access.
	 *
	 * XXX: FIXME layering violation
	 */
	e = gfarm_metadb_terminate();
	if (e != NULL)
		goto finish_pids;

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

		/*
		 * use different connection for each metadb access.
		 *
		 * XXX: FIXME layering violation
		 */
		e = gfarm_metadb_initialize();
		if (e != NULL)
			_exit(1);

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

		e = gfarm_file_section_replicate_from_to_internal(
		    gfarm_file, section_string,
		    mode & mode_mask, si.filesize,
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
	/*
	 * recover temporary closed metadb connection
	 *
	 * XXX: FIXME layering violation
	 */
	if (e != NULL) {
		gfarm_metadb_initialize();
	} else {
		e = gfarm_metadb_initialize();
	}
 finish_pids:
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
gfarm_url_fragments_replicate_to_domainname(
	const char *gfarm_url, const char *domainname)
{
	char *e, **dsthosts;
	int nfrags;

	e = gfarm_url_fragment_number(gfarm_url, &nfrags);
	if (e != NULL)
		return (e);

	dsthosts = malloc(nfrags * sizeof(char *));
	if (dsthosts == NULL)
		return (GFARM_ERR_NO_MEMORY);

	e = gfarm_schedule_search_idle_by_domainname(domainname,
		nfrags, dsthosts);
	if (e != NULL)
		goto free_dsthosts;

	e = gfarm_url_fragments_replicate(gfarm_url, nfrags, dsthosts);

	while (--nfrags >= 0)
		free(dsthosts[nfrags]);
 free_dsthosts:
	free(dsthosts);

	return (e);
}
