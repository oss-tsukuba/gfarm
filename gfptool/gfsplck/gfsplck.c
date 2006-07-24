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
#include <glob.h>
#include <errno.h>

#include <gfarm/gfarm.h>

#include "config.h"
#include "gfs_misc.h"

char *progname = "gfsplck";

static int check_all = 0;
static int delete_invalid_file = 0;

static void
print_errmsg(const char *path, char *msg)
{
	fprintf(stderr, "%s on %s: %s\n",
		path, gfarm_host_get_self_name(), msg);
}

static void
print_errmsg_with_section(const char *path, char *section, char *msg)
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

		/* allow read and write access always */
		chmod(src, (sb.st_mode | S_IRUSR | S_IWUSR) & 07777);

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

			GFARM_MALLOC_ARRAY(f, 
				strlen(src) + 1 + strlen(dp->d_name) + 1);
			if (f == NULL) {
				print_errmsg(dp->d_name, "not enough memory");
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
append_prefix_pathname(const char *prefix, const char *path)
{
	char *url;

	GFARM_MALLOC_ARRAY(url, strlen(prefix) + strlen(path) + 2);
	if (url == NULL)
		return (url);

	strcpy(url, prefix);
	switch (url[strlen(url) - 1]) {
	case '/':
	case ':':
		break;
	default:
		strcat(url, "/");
	}
	strcat(url, path);

	return (url);
}

static char *
check_file_size(char *pathname, char *gfarm_file, char *section)
{
	struct stat st;
	struct gfarm_file_section_info fi;
	char *e;

	if (lstat(pathname, &st) == -1)
		return (gfarm_errno_to_error(errno));

	e = gfarm_file_section_info_get(gfarm_file, section, &fi);
	if (e == GFARM_ERR_NO_SUCH_OBJECT)
		return (GFARM_ERR_NO_FRAGMENT_INFORMATION);
	else if (e != NULL)
		return (e);
	if (fi.filesize != st.st_size)
		e = "file size mismatch";
	gfarm_file_section_info_free(&fi);

	return (e);
}

static char *
fixfrag_ii(char *pathname, char *gfarm_file, char *sec)
{
	char *hostname, *e;
	struct gfarm_file_section_copy_info sc_info;

	/*
	 * XXX - Gfarm v1 uses a special lock file to avoid race
	 * condition during on-demand replication.  This file should
	 * not be registered.
	 */
	if (strstr(sec, ":::lock"))
		return ("lock file");

	/* check section busy */
	e = gfs_file_section_check_busy(gfarm_file, sec);
	/* allow no fragment case */
	if (e != NULL && e != GFARM_ERR_NO_SUCH_OBJECT)
		return (e);

	if (check_all == 0) {
		/* check file size */
		e = check_file_size(pathname, gfarm_file, sec);
		if (e != GFARM_ERR_NO_FRAGMENT_INFORMATION && e != NULL)
			return (e);

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

static char *
fixfrag_i(const char *gfarm_url, char *pathname, char *gfarm_file, char *sec)
{
	char *e;

	e = fixfrag_ii(pathname, gfarm_file, sec);
	if (e != NULL) {
		if (e != GFARM_ERR_ALREADY_EXISTS) {
			print_errmsg_with_section(gfarm_url, sec, e);
			if (e != GFARM_ERR_TEXT_FILE_BUSY)
				delete_invalid_file_or_directory(pathname);
		}
	}
	else
		printf("%s (%s) on %s: fixed\n", gfarm_url, sec,
		       gfarm_host_get_self_name());

	return (e);
}

static int fixdir(char *dir, const char *gfarm_prefix);

static void
fixurl(const char *gfarm_url)
{
	char *gfarm_file, *local_path, *e;
	struct stat sb;
	int len_path, is_invalid = 0, is_directory = 0;
	glob_t pglob;
	char **pathp, *pat;
	struct gfs_stat gs;

	e = gfarm_canonical_path(gfarm_url_prefix_skip(gfarm_url), &gfarm_file);
	if (e != NULL) {
		/*
		 * no path info, try to delete invalid physical files
		 * or directories
		 */
		e = gfarm_canonical_path_for_creation(
			gfarm_url_prefix_skip(gfarm_url), &gfarm_file);
		if (e != NULL) {
			/* in this case, give up searching invalid files */
			print_errmsg(gfarm_url, e);
			return;
		}
		is_invalid = 1;
	}
	else {
		/* check it is a directory or not */
		e = gfs_stat(gfarm_url, &gs);
		if (e != NULL) {
			if (e != GFARM_ERR_NO_FRAGMENT_INFORMATION) {
				/* maybe permission denied */
				print_errmsg(gfarm_url, e);
				goto error_gfarm_file;
			}
			/* no fragment information case */
		}
		else {
			is_directory = GFARM_S_ISDIR(gs.st_mode);
			gfs_stat_free(&gs);
		}
	}
	/*
	 * Check local_path; if it is invalid or not a directory,
	 * delete it.  Otherwise, check it recursively.
	 */
	e = gfarm_path_localize(gfarm_file, &local_path);
	if (e == NULL && stat(local_path, &sb) == 0) {
		if (is_invalid || !is_directory || !S_ISDIR(sb.st_mode)) {
			print_errmsg(local_path, "invalid file or directory");
			delete_invalid_file_or_directory(local_path);
		}
		else if (chdir(local_path) == 0)
			(void)fixdir(".", gfarm_url);
		/* continue */
	}
	if (e != NULL) {
		print_errmsg(gfarm_url, e);
		goto error_gfarm_file;
	}

	/* investigate file sections */
	len_path = strlen(local_path);
	GFARM_MALLOC_ARRAY(pat, len_path + 3);
	if (pat == NULL) {
		print_errmsg(gfarm_url, "not enough memory");
		free(local_path);
		goto error_gfarm_file;
	}
	strcpy(pat, local_path);
	strcat(pat, ":*");
	free(local_path);

	pglob.gl_offs = 0;
	glob(pat, GLOB_DOOFFS, NULL, &pglob);
	free(pat);
	
	pathp = pglob.gl_pathv;
	while (*pathp) {
		char *sec = &((*pathp)[len_path + 1]);

		if (is_invalid || is_directory) {
			print_errmsg_with_section(
				gfarm_url, sec, "invalid file");
			delete_invalid_file_or_directory(*pathp);
			++pathp;
			continue;
		}
		(void)fixfrag_i(gfarm_url, *pathp, gfarm_file, sec);

		++pathp;
	}
	globfree(&pglob);

 error_gfarm_file:
	free(gfarm_file);
	return;
}

static int
fixfrag(char *pathname, const char *gfarm_prefix)
{
	char *gfarm_url, *sec, *pname, *gfarm_file, *e;
	struct gfs_stat gst;
	int r = 1;

	gfarm_url = append_prefix_pathname(gfarm_prefix, pathname);
	if (gfarm_url == NULL) {
		print_errmsg(pathname, "not enough memory");
		return (r);
	}

	/* divide into file and section parts. */
	sec = &gfarm_url[strlen(gfarm_url) - 1];
	pname = sec - strlen(pathname) + 1;
	while (sec > pname && *sec != '/') {
		if (*sec == ':') {
			*sec = '\0';
			++sec;
			break;
		}
		--sec;
	}
	if (sec == pname || *sec == '/') {
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
	e = fixfrag_i(gfarm_url, pathname, gfarm_file, sec);
	if (e != NULL && e != GFARM_ERR_ALREADY_EXISTS)
		goto error_gfarm_file;

	r = 0;

error_gfarm_file:
	free(gfarm_file);
error_gfarm_url:	
	free(gfarm_url);
	return (r);
}

static int
fixdir(char *dir, const char *gfarm_prefix)
{
	DIR* dirp;
	struct dirent *dp;
	struct stat sb;
	char *dir1;
	char *gfarm_url, *e;
	int is_directory;
	struct gfs_stat gs;

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
	gfarm_url = append_prefix_pathname(gfarm_prefix, dir);
	if (gfarm_url == NULL) {
		print_errmsg(dir, "not enough memory");
		return (1);
	}

	e = gfs_stat(gfarm_url, &gs);
	if (e != NULL) {
		print_errmsg(gfarm_url, e);
		delete_invalid_file_or_directory(dir);
		free(gfarm_url);
		return (1);
	}
	is_directory = GFARM_S_ISDIR(gs.st_mode);
	gfs_stat_free(&gs);
	if (!is_directory) {
		print_errmsg(gfarm_url, "invalid directory");
		delete_invalid_file_or_directory(dir);
		free(gfarm_url);
		return (1);
	}
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

		GFARM_MALLOC_ARRAY(dir1, strlen(dir) + strlen(dp->d_name) + 2);
		if (dir1 == NULL) {
			print_errmsg(dp->d_name, "not enough memory");
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
		print_errmsg(progname, e);
		exit(1);
	}
	if (!gfarm_is_active_file_system_node) {
		print_errmsg(progname, "not a filesystem node");
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

	if (*argv) {
		while (*argv)
			fixurl(*argv++);
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
		print_errmsg(progname, e);
		exit(1);
	}
	exit(0);
}
