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

char *progname = "gfsplck";

static int check_all = 0;
static int delete_invalid_file = 0;

static char *
check_path_info(char *gfarm_file)
{
	struct gfarm_path_info p_info;
	char *e;

	/* check whether the path info is already registered. */
	e = gfarm_path_info_get(gfarm_file, &p_info);
	if (e != NULL)
		return (e);

	gfarm_path_info_free(&p_info);
	return (NULL);
}

static char *
fixfrag_i(char *pathname, char *gfarm_file, char *sec)
{
	char *hostname, *e;
	struct gfarm_file_section_copy_info sc_info;

	if (check_all == 0) {
		/* check whether the fragment is already registered. */
		e = gfarm_host_get_canonical_self_name(&hostname);
		if (e == NULL) {
			e = gfarm_file_section_copy_info_get(
				gfarm_file, sec, hostname, &sc_info);
			if (e == NULL) {
				/* already exist */
				gfarm_file_section_copy_info_free(&sc_info);
				return (GFARM_ERR_ALREADY_EXISTS);
			}
			if (e != GFARM_ERR_NO_SUCH_OBJECT)
				return (e);
		}
	}

	/* register the section info */
	e = gfs_pio_set_fragment_info_local(pathname, gfarm_file, sec);
	if (e != NULL)
		return (e);

	return (NULL);
}

static int fixdir(char *dir, char *gfarm_prefix);

static int
fixurl(char *gfarm_url)
{
	char *gfarm_file, *local_path, *e;
	char sec[GFARM_INT32STRLEN];
	struct stat sb;
	int rank;

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL) {
		fprintf(stderr, "%s on %s: %s\n", gfarm_url,
			gfarm_host_get_self_name(), e);
		return 1;
	}

	/* check whether gfarm_url is directory or not. */
	e = gfarm_path_localize(gfarm_file, &local_path);
	if (e == NULL && stat(local_path, &sb) == 0 && S_ISDIR(sb.st_mode)) {
		int r = 1;
		if (chdir(local_path) == 0)
			r = fixdir(".", gfarm_url);
		free(gfarm_file);
		free(local_path);
		return (r);
	}
	if (e != NULL) {
		fprintf(stderr, "%s on %s: %s\n", gfarm_url,
			gfarm_host_get_self_name(), e);
		free(gfarm_file);
		return (1);
	}
	free(local_path);

	/* XXX - assume gfarm_url is a fragmented file. */
	e = gfs_pio_get_node_rank(&rank);
	if (e != NULL) {
		fprintf(stderr, "%s on %s: %s\n", gfarm_url,
			gfarm_host_get_self_name(), e);
		goto error_gfarm_file;
	}

	e = gfarm_path_localize_file_fragment(gfarm_file, rank, &local_path);
	if (e != NULL) {
		fprintf(stderr, "%s on %s: %s\n", gfarm_url,
			gfarm_host_get_self_name(), e);
		goto error_gfarm_file;
	}

	e = check_path_info(gfarm_file);
	if (e != NULL) {
		fprintf(stderr, "%s on %s: %s\n", gfarm_url,
			gfarm_host_get_self_name(), e);
		if (delete_invalid_file) {
			if (unlink(local_path) == 0)
				printf("%s on %s: deleted\n",
				       local_path, gfarm_host_get_self_name());
			else
				perror(local_path);
		}
		goto error_local_path;
	}

	sprintf(sec, "%d", rank);
	e = fixfrag_i(local_path, gfarm_file, sec);
	if (e != NULL && e != GFARM_ERR_ALREADY_EXISTS) {
		fprintf(stderr, "%s (%s) on %s: %s\n", gfarm_url, sec,
			gfarm_host_get_self_name(), e);
		if (delete_invalid_file) {
			if (unlink(local_path) == 0)
				printf("%s on %s: deleted\n",
				       local_path, gfarm_host_get_self_name());
			else
				perror(local_path);
		}
		goto error_local_path;
	}

	/* printf("%s (%s): fixed\n", gfarm_url, sec); */

	return (0);

 error_local_path:
	free(local_path);
 error_gfarm_file:
	free(gfarm_file);
	return (1);
}

static int
fixfrag(char *pathname, char *gfarm_prefix)
{
	char *gfarm_url, *sec, *gfarm_file, *e;
	int r = 0;

	gfarm_url = malloc(strlen(gfarm_prefix) + strlen(pathname) + 2);
	if (gfarm_url == NULL) {
		fputs("not enough memory", stderr);
		return 1;
	}
	strcpy(gfarm_url, gfarm_prefix);
	if (gfarm_url[strlen(gfarm_url) - 1] != '/'
	    && gfarm_url[strlen(gfarm_url) - 1] != ':')
		strcat(gfarm_url, "/");
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
		fprintf(stderr, "%s on %s: invalid filename\n", pathname,
			gfarm_host_get_self_name());
		free(gfarm_url);
		return 1;
	}

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL) {
		fprintf(stderr, "%s on %s: %s\n", gfarm_url,
			gfarm_host_get_self_name(), e);
		free(gfarm_url);
		return 1;
	}

	/* check whether the path info is already registered. */
	e = check_path_info(gfarm_file);
	if (e != NULL) {
		fprintf(stderr, "%s (%s) on %s: %s\n", gfarm_url, sec,
			gfarm_host_get_self_name(), e);
		if (delete_invalid_file) {
			if (unlink(pathname) == 0)
				printf("%s on %s: deleted\n",
				       pathname, gfarm_host_get_self_name());
			else
				perror(pathname);
		}
		r = 1;
		goto finish;
	}

	/* check whether the fragment is already registered. */
	e = fixfrag_i(pathname, gfarm_file, sec);
	if (e != NULL) {
		if (e != GFARM_ERR_ALREADY_EXISTS) {
			fprintf(stderr, "%s on %s: %s\n", pathname,
				gfarm_host_get_self_name(), e);
			if (delete_invalid_file) {
				if (unlink(pathname) == 0)
					printf("%s on %s: deleted\n",
					       pathname,
					       gfarm_host_get_self_name());
				else
					perror(pathname);
			}
			r = 1;
		}
		else
			/* no message */;
	}
	else
		printf("%s (%s) on %s: fixed\n", gfarm_url, sec,
		       gfarm_host_get_self_name());

 finish:
	free(gfarm_file);
	free(gfarm_url);

	return (r);
}

static int
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
		if (strcmp(dp->d_name, ".") == 0
		    || strcmp(dp->d_name, "..") == 0)
			continue;

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
	closedir(dirp);
	if (dirp != NULL)
		return 1;

	return 0;
}

/*
 *
 */

void
usage()
{
	fprintf(stderr, "usage: %s [ -a ] [ -d ] [ Gfarm directory . . . ]\n",
		progname);
	fprintf(stderr, "options:\n");
	fprintf(stderr, "\t-a\tcheck all files\n");
	fprintf(stderr, "\t-d\tdelete invalid files\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	char *e, *gfarm_prefix;
	extern int optind;
	int c;

	e = gfarm_initialize(&argc, &argv);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", progname, e);
	}

	while ((c = getopt(argc, argv, "ad")) != EOF) {
		switch (c) {
		case 'a':
			check_all = 1;
			break;
		case 'd':
			delete_invalid_file = 1;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc > 0) {
		while (argc-- > 0 && fixurl(*argv++) == 0);
		goto finish;
	}

	/* fix a whole spool directory. */
	if (chdir(gfarm_spool_root) == 0)
		gfarm_prefix = "gfarm:/";
	else
		gfarm_prefix = "gfarm:";

	fixdir(".", gfarm_prefix);

 finish:
	e = gfarm_terminate();
	if (e != NULL) {
	    fprintf(stderr, "%s: %s\n", progname, e);
	}

	exit(0);
}
