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

static void
print_errmsg(char *path, char *msg)
{
	fprintf(stderr, "%s on %s: %s\n",
		path, gfarm_host_get_self_name(), msg);
}

static void
print_errmsg_with_section(char *path, char *section, char *msg)
{
	fprintf(stderr, "%s (%s) on %s: %s\n",
		path, section, gfarm_host_get_self_name(), msg);
}

static int
unlink_dir(const char *src)
{
	struct stat sb;

	if (lstat(src, &sb)) {
		perror(src);
		return (1);
	}
	if (S_ISDIR(sb.st_mode)) {
		DIR *dirp;
		struct dirent *dp;

		dirp = opendir(src);
		if (dirp == NULL) {
			perror(src);
			return (1);
		}
		while ((dp = readdir(dirp)) != NULL) {
			char *f;

			if (strcmp(dp->d_name, ".") == 0
			    || strcmp(dp->d_name, "..") == 0 || dp->d_ino == 0)
				continue;

			f = malloc(strlen(src) + 1 + strlen(dp->d_name) + 1);
			if (f == NULL) {
				fputs("not enough memory", stderr);
				return (1);
			}
			strcpy(f, src);
			strcat(f, "/");
			strcat(f, dp->d_name);

			(void)unlink_dir(f);

			free(f);
		}
		closedir(dirp);
		if (rmdir(src)) {
			perror(src);
			return (1);
		}
	}
	else if (unlink(src)) {
		/* if 'src' is not a directory, try to unlink. */
		perror(src);
		return (1);
	}
	return (0);
}

static void
delete_invalid_file_or_directory(char *pathname)
{
	if (delete_invalid_file) {
		if (unlink_dir(pathname) == 0)
			printf("%s on %s: deleted\n",
			       pathname, gfarm_host_get_self_name());
		else
			print_errmsg(pathname, "cannot delete");
	}
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

	/*
	 * If the corresponding metadata does not exist, register it.
	 * Otherwise, check file size and checksum.
	 */
	return (gfs_pio_set_fragment_info_local(pathname, gfarm_file, sec));
}

static int fixdir(char *dir, char *gfarm_prefix);

static int
fixurl(char *gfarm_url)
{
	char *gfarm_file, *local_path, *e;
	char sec[GFARM_INT32STRLEN];
	struct stat sb;
	int rank;
	int r = 1;

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL) {
		print_errmsg(gfarm_url, e);
		return (r);
	}

	/* check whether gfarm_url is directory or not. */
	e = gfarm_path_localize(gfarm_file, &local_path);
	if (e == NULL && stat(local_path, &sb) == 0 && S_ISDIR(sb.st_mode)) {
		if (chdir(local_path) == 0)
			r = fixdir(".", gfarm_url);
		goto error_local_path;
	}
	if (e != NULL) {
		print_errmsg(gfarm_url, e);
		goto error_gfarm_file;
	}
	free(local_path);

	/* XXX - assume gfarm_url is a fragmented file. */
	e = gfs_pio_get_node_rank(&rank);
	if (e != NULL) {
		print_errmsg(gfarm_url, e);
		goto error_gfarm_file;
	}

	e = gfarm_path_localize_file_fragment(gfarm_file, rank, &local_path);
	if (e != NULL) {
		print_errmsg(gfarm_url, e);
		goto error_gfarm_file;
	}

	sprintf(sec, "%d", rank);
	e = fixfrag_i(local_path, gfarm_file, sec);
	if (e != NULL && e != GFARM_ERR_ALREADY_EXISTS) {
		print_errmsg_with_section(gfarm_url, sec, e);
		delete_invalid_file_or_directory(local_path);
		goto error_local_path;
	}
	r = 0;

 error_local_path:
	free(local_path);
 error_gfarm_file:
	free(gfarm_file);
	return (r);
}

static int
fixfrag(char *pathname, char *gfarm_prefix)
{
	char *gfarm_url, *sec, *gfarm_file, *e;
	struct gfs_stat gst;
	int r = 1;

	gfarm_url = malloc(strlen(gfarm_prefix) + strlen(pathname) + 2);
	if (gfarm_url == NULL) {
		fputs("not enough memory", stderr);
		return (r);
	}
	strcpy(gfarm_url, gfarm_prefix);
	switch (gfarm_url[strlen(gfarm_url) - 1]) {
	case '/':
	case ':':
		break;
	default:
		strcat(gfarm_url, "/");
	}
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
		print_errmsg(pathname, "invalid filename");
		delete_invalid_file_or_directory(pathname);
		goto error_gfarm_url;
	}

	e = gfs_stat(gfarm_url, &gst);
	if (e == NULL) {
		if (!GFARM_S_ISREG(gst.st_mode)) {
			gfs_stat_free(&gst);
			print_errmsg(gfarm_url, "not a regular file");
			delete_invalid_file_or_directory(pathname);
			goto error_gfarm_url;
		}
		gfs_stat_free(&gst);
	}
	else
		/* permit no fragment case */;

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL) {
		print_errmsg_with_section(gfarm_url, sec, e);
		delete_invalid_file_or_directory(pathname);
		goto error_gfarm_url;
	}

	/* check whether the fragment is already registered. */
	e = fixfrag_i(pathname, gfarm_file, sec);
	if (e != NULL) {
		if (e != GFARM_ERR_ALREADY_EXISTS) {
			print_errmsg(pathname, e);
			delete_invalid_file_or_directory(pathname);
			goto error_gfarm_file;
		}
		else
			/* no message */;
	}
	else
		printf("%s (%s) on %s: fixed\n", gfarm_url, sec,
		       gfarm_host_get_self_name());
	r = 0;

error_gfarm_file:
	free(gfarm_file);
error_gfarm_url:	
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
	char *gfarm_url, *gfarm_file, *e;

	if (lstat(dir, &sb)) {
		perror(dir);
		return (1);
	}
	if (S_ISREG(sb.st_mode))
		return (fixfrag(dir, gfarm_prefix));

	if (!S_ISDIR(sb.st_mode)) {
		print_errmsg(dir, "neither a regular file nor a directory");
		delete_invalid_file_or_directory(dir);
		return (1);
	}

	/* 'dir' is a directory */
	gfarm_url = malloc(strlen(gfarm_prefix) + strlen(dir) + 2);
	if (gfarm_url == NULL) {
		fputs("not enough memory", stderr);
		return (1);
	}
	strcpy(gfarm_url, gfarm_prefix);
	switch (gfarm_url[strlen(gfarm_url) - 1]) {
	case '/':
	case ':':
		break;
	default:
		strcat(gfarm_url, "/");
	}
	strcat(gfarm_url, dir);

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL) {
		print_errmsg(gfarm_url, e);
		delete_invalid_file_or_directory(dir);
		free(gfarm_url);
		return (1);
	}
	free(gfarm_file);
	free(gfarm_url);

	dirp = opendir(dir);
	if (dirp == NULL) {
		perror(dir);
		return (1);
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
			return (1);
		}
		strcpy(dir1, dir);
		if (strcmp(dir, ""))
			strcat(dir1, "/");
		strcat(dir1, dp->d_name);

		fixdir(dir1, gfarm_prefix);

		free(dir1);
	}
	return (closedir(dirp));
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
		exit(1);
	}
	if (!gfarm_is_active_file_system_node) {
		fprintf(stderr, "%s: not a filesystem node\n", progname);
		exit(1);
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
		exit(1);
	}
	exit(0);
}
