/*
 * $Id$
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>

#include <gfarm/gfarm.h>

char *progname = "addsec";

int
fixfrag(char *pathname, char *gfarm_prefix)
{
	char *gfarm_url, *sec, *gfarm_file, *e;
	char *hostname;
	struct gfarm_path_info p_info;
	struct gfarm_file_section_copy_info sc_info;

	gfarm_url = malloc(strlen(gfarm_prefix) + strlen(pathname) + 1);
	if (gfarm_url == NULL) {
		fputs("not enough memory", stderr);
		return 1;
	}
	strcpy(gfarm_url, gfarm_prefix);
	strcat(gfarm_url, pathname);

	/* divide into file and section parts. */

	sec = gfarm_url + strlen(gfarm_prefix);
	while (*sec) {
		if (*sec == ':') {
			*sec = '\0';
			++sec;
			break;
		}
		++sec;
	}
	if (*sec == '\0') {
		fprintf(stderr, "%s: invalid filename\n", pathname);
		free(gfarm_url);
		return 1;
	}

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", gfarm_url, e);
		free(gfarm_url);
		return 1;
	}

	/* check whether the path info is already registered. */

	e = gfarm_path_info_get(gfarm_file, &p_info);
	if (e != NULL) {
		fprintf(stderr, "%s (%s) on %s: %s\n", gfarm_url, sec,
			gfarm_host_get_self_name(), e);
		free(gfarm_file);
		free(gfarm_url);
		return 1;
	}

	/* check whether the fragment is already registered. */

	e = gfarm_host_get_canonical_self_name(&hostname);
	if (e == NULL) {
		e = gfarm_file_section_copy_info_get(
			gfarm_file, sec, hostname, &sc_info);
		if (e == NULL) {
			/* already exist */
			goto finish;
		}
		if (e != GFARM_ERR_NO_SUCH_OBJECT) {
			fprintf(stderr, "%s: %s\n", gfarm_url, e);
			free(gfarm_file);
			free(gfarm_url);
			return 1;
		}
	}

	/* register the section info */

	e = gfs_pio_set_fragment_info_local(pathname, gfarm_url, sec);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", pathname, e);
		free(gfarm_file);
		free(gfarm_url);
		return 1;
	}
	printf("%s (%s): fixed\n", gfarm_url, sec);

 finish:
	free(gfarm_file);
	free(gfarm_url);

	return 0;
}

int
fixdir(char *dir, char *gfarm_prefix)
{
	DIR* dirp;
	struct dirent *dp;
	struct stat sb;
	char *dir1;

	if (stat(dir, &sb)) {
		perror(dir);
		return 1;
	}
	if (S_ISREG(sb.st_mode))
		return fixfrag(dir, gfarm_prefix);

	dirp = opendir(dir);
	if (dirp == NULL) {
		perror(dir);
		return 1;
	}

	if (strcmp(dir, ".") == 0)
		dir = ""; /* just a trick */

	while ((dp = readdir(dirp)) != NULL) {
		if (strcmp(dp->d_name, ".") && strcmp(dp->d_name, "..")) {
			dir1 = malloc(strlen(dir) + strlen(dp->d_name) + 2);
			if (dir1 == NULL) {
				fputs("not enough memory", stderr);
				closedir(dirp);
				return 1;
			}
			strcpy(dir1, dir);
			if (strcmp(dir, ""))
				strcat(dir1, "/");
			strcat(dir1, dp->d_name);

			fixdir(dir1, gfarm_prefix);

			free(dir1);
		}
	}
	closedir(dirp);
	if (dirp != NULL) {
		return 1;
	}

	return 0;
}

int
main(int argc, char *argv[])
{
	char *e, *gfarm_prefix;

	e = gfarm_initialize(&argc, &argv);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", progname, e);
	}

	if (chdir(gfarm_spool_root) == 0)
		gfarm_prefix = "gfarm:/";
	else
		gfarm_prefix = "gfarm:";

	fixdir(".", gfarm_prefix);

	e = gfarm_terminate();
	if (e != NULL) {
	    fprintf(stderr, "%s: %s\n", progname, e);
	}

	exit(0);
}
