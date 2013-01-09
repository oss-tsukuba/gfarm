/*
 * $Id$
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <libgen.h>
#include <limits.h>
#include <gfarm/gfarm.h>

static int option_force;
static int option_verbose;
static char GFARM_MSG_DELETED[] = "deleted";

static char *
path_info_remove(char *url, char *canonical_path, char *errmsg)
{
	char *e, *c_path = NULL;
	FILE *out = stdout;

	if (canonical_path == NULL) {
		e = gfarm_url_make_path(url, &c_path);
		if (e != NULL)
			return (e);
		canonical_path = c_path;
	}

	e = gfarm_path_info_remove(canonical_path);
	if (e == NULL)
		e = GFARM_MSG_DELETED;
	else
		out = stderr;

	fprintf(out, "%s: %s: %s\n", url, errmsg, e);

	if (c_path != NULL)
		free(c_path);
	return (e == GFARM_MSG_DELETED ? NULL : e);
}

static char *
section_info_remove(char *url, char *gfarm_file, char *section, char *errmsg)
{
	char *e = gfarm_file_section_info_remove(gfarm_file, section);
	FILE *out = stdout;

	if (e == NULL)
		e = GFARM_MSG_DELETED;
	else
		out = stderr;

	fprintf(out, "%s (%s): %s: %s\n", url, section, errmsg, e);

	return (e == GFARM_MSG_DELETED ? NULL : e);
}

static char *
section_copy_info_remove(
	char *url, char *pathname, char *section, char *host, char *errmsg)
{
	char *e = gfarm_file_section_copy_info_remove(pathname, section, host);
	FILE *out = stdout;

	if (e == NULL)
		e = GFARM_MSG_DELETED;
	else
		out = stderr;

	fprintf(out, "%s (%s) on %s: %s: %s\n", url, section, host, errmsg, e);

	return (e == GFARM_MSG_DELETED ? NULL : e);
}

static char *
gfsck_file(char *gfarm_url)
{
	char *gfarm_file, *e, *e_save = NULL;
	int i, nsections, valid_nsections = 0;
	struct gfarm_file_section_info *sections;
	GFS_File gf;

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL)
		return (e);

	e = gfarm_file_section_info_get_all_by_file(
		gfarm_file, &nsections, &sections);
	if (e != NULL) {
		/* no section info, remove path info */
		e = path_info_remove(gfarm_url, gfarm_file, e);
		free(gfarm_file);
		return (e);
	}

	e = gfs_pio_open(gfarm_url, GFARM_FILE_RDONLY, &gf);
	if (e != NULL) {
		free(gfarm_file);
		return (e);
	}

	for (i = 0; i < nsections; i++) {
		int j, ncopies, valid_ncopies = 0;
		struct gfarm_file_section_copy_info *copies;
		char *section = sections[i].section;

		e = gfarm_file_section_copy_info_get_all_by_section(
			gfarm_file, section, &ncopies, &copies);
		if (e == GFARM_ERR_NO_SUCH_OBJECT) {
			/* no section copy info, remove section info */
			e = section_info_remove(
				gfarm_url, gfarm_file, section, e);
			if (e != NULL && e_save == NULL)
				e_save = e;
			continue;
		}
		else if (e != NULL) {
			fprintf(stderr, "%s (%s): %s\n",
				gfarm_url, section, e);
			if (e_save == NULL)
				e_save = e;
			continue;
		}
		for (j = 0; j < ncopies; ++j) {
			char *hostname = copies[j].hostname;

			if (option_verbose)
				printf("%s (%s) on %s\n", gfarm_url, section,
				       hostname);
			e = gfs_pio_set_view_section(gf, section, hostname,
			    GFARM_FILE_NOT_REPLICATE | GFARM_FILE_NOT_RETRY);
			if (e == GFARM_ERR_INCONSISTENT_RECOVERABLE) {
				/* invalid section copy info removed */
				printf("%s (%s) on %s: "
				       "invalid metadata deleted\n",
				       gfarm_url, section, hostname);
				e = NULL;
				continue;
			}
			else if (option_force
				 || e == GFARM_ERR_NO_ROUTE_TO_HOST
				 || e == GFARM_ERR_NO_SUCH_OBJECT) {
				e = section_copy_info_remove(gfarm_url,
					gfarm_file, section, hostname, e);
				if (e != NULL && e_save == NULL)
					e_save = e;
				continue;
			}
			else if (e != NULL) {
				fprintf(stderr, "%s (%s) on %s: %s\n",
					gfarm_url, section, hostname, e);
				if (e_save == NULL)
					e_save = e;
				/* keep metadata */
			}
			++valid_ncopies;
		}
		gfarm_file_section_copy_info_free_all(ncopies, copies);
		if (valid_ncopies == 0) {
			/* no section copy info, remove section info */
			e = section_info_remove(gfarm_url, gfarm_file, section,
				"no file replica");
			if (e != NULL && e_save == NULL)
				e_save = e;
		}
		else
			++valid_nsections;
	}
	if (valid_nsections == 0) {
		/* no section info, remove path info */
		e = path_info_remove(gfarm_url, gfarm_file,
			GFARM_ERR_NO_FRAGMENT_INFORMATION);
		if (e != NULL && e_save == NULL)
			e_save = e;
	}
	else if (valid_nsections < nsections) {
		printf("%s: warning: number of file sections reduced\n",
		       gfarm_url);
	}

	gfarm_file_section_info_free_all(nsections, sections);
	free(gfarm_file);

	e = gfs_pio_close(gf);
	if (e != NULL)
		return (e);

	return (e_save);
}

static char *
gfsck_dir(char *gfarm_dir, char *file)
{
	char *e, *gfarm_url;
	struct gfs_stat gsb;
	GFS_Dir gdir;
	struct gfs_dirent *gdent;

	GFARM_MALLOC_ARRAY(gfarm_url, strlen(gfarm_dir) + strlen(file) + 2);
	if (gfarm_url == NULL)
		return (GFARM_ERR_NO_MEMORY);
	if (gfarm_dir[0] == '\0')
		sprintf(gfarm_url, "%s", file);
	else if (strcmp(gfarm_dir, GFARM_URL_PREFIX) == 0)
		sprintf(gfarm_url, "%s%s", gfarm_dir, file);
	else
		sprintf(gfarm_url, "%s/%s", gfarm_dir, file);

	e = gfs_stat(gfarm_url, &gsb);
	if (e != NULL) {
		if (e == GFARM_ERR_NO_FRAGMENT_INFORMATION) {
			/* no fragment information, remove path info */
			e = path_info_remove(gfarm_url, NULL, e);
		}
		free(gfarm_url);
		return (e);
	}
	if (GFARM_S_ISREG(gsb.st_mode)) {
		gfs_stat_free(&gsb);
		e = gfsck_file(gfarm_url);
		free(gfarm_url);
		return (e);
	}
	if (!GFARM_S_ISDIR(gsb.st_mode)) {
		gfs_stat_free(&gsb);
		free(gfarm_url);
		return ("unknown file type");
	}
	gfs_stat_free(&gsb);

	e = gfs_opendir(gfarm_url, &gdir);
	if (e != NULL) {
		free(gfarm_url);
		return (e);
	}
	while ((e = gfs_readdir(gdir, &gdent)) == NULL && gdent != NULL) {
		if (gdent->d_name[0] == '.' && (gdent->d_name[1] == '\0' ||
		    (gdent->d_name[1] == '.' && gdent->d_name[2] == '\0')))
			continue; /* "." or ".." */
		e = gfsck_dir(gfarm_url, gdent->d_name);
		if (e != NULL) {
			fprintf(stderr, "%s%s%s: %s\n",
				gfarm_url, 
				strcmp(gfarm_url, GFARM_URL_PREFIX) == 0
				? "" : "/", gdent->d_name, e);
			/* it is not necessary to save error */
		}
	}
	(void)gfs_closedir(gdir);
	free(gfarm_url);

	return (NULL);
}

char *program_name = "gfsck";

static void
usage()
{
	fprintf(stderr, "Usage: %s [-fhv] path ...\n", program_name);
	exit(1);
}

int
main(int argc, char *argv[])
{
	extern int optind;
	int c, i, error = 0;
	gfarm_stringlist paths;
	gfs_glob_t types;
	char *e;

	if (argc <= 1)
		usage();
	program_name = basename(argv[0]);

	while ((c = getopt(argc, argv, "fhv?")) != EOF) {
		switch (c) {
		case 'f':
			option_force = 1;
			break;
		case 'v':
			option_verbose = 1;
			break;
		case 'h':
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	e = gfarm_initialize(&argc, &argv);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}

	e = gfarm_stringlist_init(&paths);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(EXIT_FAILURE);
	}
	e = gfs_glob_init(&types);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(EXIT_FAILURE);
	}
	for (i = 0; i < argc; i++)
		gfs_glob(argv[i], &paths, &types);

	for (i = 0; i < gfarm_stringlist_length(&paths); i++) {
		char *url = gfarm_stringlist_elem(&paths, i);

		e = gfsck_dir("", url);
		if (e != NULL) {
			fprintf(stderr, "%s: %s\n", url, e);
			error = 1;
		}
	}
	gfs_glob_free(&types);
	gfarm_stringlist_free_deeply(&paths);

	e = gfarm_terminate();
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}
	exit(error);
}
