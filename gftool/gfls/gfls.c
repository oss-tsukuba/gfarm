/*
 * $Id$
 */

#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <libgen.h>
#include <limits.h>
#include <unistd.h>
#include <sys/time.h>
#include <math.h>
#include <time.h>

#include <gfarm/gfarm.h>

#include "timespec.h" /* XXX should export this interface */
#include "gfarm_path.h"

char *program_name = "gfls";

#define INUM_LEN	11
#define INUM_PRINT(ino)	printf("%10lu ", (long)(ino))

enum output_format {
	OF_ONE_PER_LINE,
	OF_MULTI_COLUMN,
	OF_LONG
} option_output_format;			/* -1/-C/-l */
enum sort_order {
	SO_NAME,
	SO_SIZE,
	SO_MTIME
} option_sort_order = SO_NAME;		/* -S/-t */
enum option_all_kind {
	OA_NONE = 0,
	OA_ALL,
	OA_ALMOST_ALL
} option_all = OA_NONE;			/* -a/-A */
#define is_option_all		(option_all == OA_ALL)
#define is_option_almost_all	(option_all == OA_ALMOST_ALL)
int option_type_suffix = 0;		/* -F */
int option_recursive = 0;		/* -R */
int option_complete_time = 0;		/* -T */
int option_directory_itself = 0;	/* -d */
int option_inumber = 0;			/* -i */
int option_reverse_sort = 0;		/* -r */

#define CACHE_EXPIRATION_NOT_SPECIFIED	-1.0
double option_cache_expiration = CACHE_EXPIRATION_NOT_SPECIFIED; /* -E */

int screen_width = 80; /* default */

/*
 * gfls implementation
 */

struct ls_entry {
	char *path, *symlink;
	struct gfs_stat *st;
};

int
compare_name(const void *a, const void *b)
{
	const struct ls_entry *p = a, *q = b;

	return (strcmp(p->path, q->path));
}

int
compare_name_r(const void *a, const void *b)
{
	const struct ls_entry *p = a, *q = b;

	return (-strcmp(p->path, q->path));
}

int
compare_size(const void *a, const void *b)
{
	const struct ls_entry *p = a, *q = b;

	if (p->st->st_size > q->st->st_size)
		return (1);
	else if (p->st->st_size < q->st->st_size)
		return (-1);
	else
		return (0);
}

int
compare_size_r(const void *a, const void *b)
{
	return (-compare_size(a, b));
}

int
compare_mtime(const void *a, const void *b)
{
	const struct ls_entry *p = a, *q = b;

	return (gfarm_timespec_cmp(&q->st->st_mtimespec, &p->st->st_mtimespec));
}

int
compare_mtime_r(const void *a, const void *b)
{
	const struct ls_entry *p = a, *q = b;

	return (gfarm_timespec_cmp(&p->st->st_mtimespec, &q->st->st_mtimespec));
}

void
ls_sort(int n, struct ls_entry *ls)
{
	int (*compare)(const void *, const void *);

#ifdef __GNUC__ /* workaround gcc warning: unused variable */
	compare = NULL;
#endif
	if (option_reverse_sort) {
		switch (option_sort_order) {
		case SO_NAME: compare = compare_name_r; break;
		case SO_SIZE: compare = compare_size_r; break;
		case SO_MTIME: compare = compare_mtime_r; break;
		}
	} else {
		switch (option_sort_order) {
		case SO_NAME: compare = compare_name; break;
		case SO_SIZE: compare = compare_size; break;
		case SO_MTIME: compare = compare_mtime; break;
		}
	}
	qsort(ls, n, sizeof(*ls), compare);
}

gfarm_error_t
do_stats(char *prefix, int *np, char **files, struct gfs_stat *stats,
	struct ls_entry *ls)
{
	gfarm_error_t e, e_save = GFARM_ERR_NO_ERROR;
	int i, n = *np, m, prefix_len, space;
	char *namep, buffer[PATH_MAX * 2 + 1];

	prefix_len = strlen(prefix);
	if (prefix_len > sizeof(buffer) - 1)
		prefix_len = sizeof(buffer) - 1;
	memcpy(buffer, prefix, prefix_len);
	namep = &buffer[prefix_len];
	space = sizeof(buffer) - prefix_len - 1;

	m = 0;
	for (i = 0; i < n; i++) {
		if (strlen(files[i]) <= space) {
			strcpy(namep, files[i]);
		} else {
			memcpy(namep, files[i], space);
			namep[space] = '\0';
		}
		e = gfs_lstat_cached(buffer, &stats[i]);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "%s: %s\n", buffer,
			    gfarm_error_string(e));
			if (e_save != GFARM_ERR_NO_ERROR)
				e_save = e;

			/* mark this stats[i] isn't initialized */
			stats[i].st_user = stats[i].st_group = NULL;
			continue;
		}
		ls[m].path = files[i];
		ls[m].st = &stats[i];
		if (!GFARM_S_ISLNK(ls[m].st->st_mode) ||
		    gfs_readlink(buffer, &ls[m].symlink) != GFARM_ERR_NO_ERROR)
			ls[m].symlink = NULL;
			
		m++;
	}
	*np = m;
	return (e_save);
}

int
put_suffix(struct ls_entry *ls)
{
	struct gfs_stat *st = ls->st;

	if (GFARM_S_ISDIR(st->st_mode)) {
		putchar('/');
		return 1;
	} else if (GFARM_S_ISLNK(st->st_mode)) {
		putchar('@');
		return 1;
	} else if (GFARM_S_IS_PROGRAM(st->st_mode)) {
		putchar('*');
		return 1;
	}
	return 0;
}

void
put_perm(int mode, int highbit, int highchar)
{
	if (mode & 04)
		putchar('r');
	else
		putchar('-');
	if (mode & 02)
		putchar('w');
	else
		putchar('-');
	if (mode & highbit) {
		if (mode & 01)
			putchar(highchar);
		else
			putchar(toupper(highchar));
	} else if (mode & 01)
		putchar('x');
	else
		putchar('-');
}

#define HALFYEAR	((365 * 24 * 60 * 60) / 2)

void
put_time(struct gfarm_timespec *ts)
{
	static struct timeval now;
	static int initialized = 0;
	struct tm *tm;
	time_t sec;
	char buffer[100];

	if (!initialized) {
		gettimeofday(&now, NULL);
		initialized = 1;
	}
	sec = ts->tv_sec;
	tm = localtime(&sec);
	if (option_complete_time)
		strftime(buffer, sizeof(buffer) - 1, "%b %e %H:%M:%S %Y", tm);
	else if (ts->tv_sec >= now.tv_sec - HALFYEAR &&
	    ts->tv_sec <= now.tv_sec + HALFYEAR)
		strftime(buffer, sizeof(buffer) - 1, "%b %e %H:%M", tm);
	else
		strftime(buffer, sizeof(buffer) - 1, "%b %e  %Y", tm);
	buffer[sizeof(buffer) - 1] = '\0';
	fputs(buffer, stdout);
}

void
put_stat(struct gfs_stat *st)
{
	if (GFARM_S_ISDIR(st->st_mode))
		putchar('d');
	else if (GFARM_S_ISLNK(st->st_mode))
		putchar('l');
	else
		putchar('-');
	put_perm(st->st_mode >> 6, GFARM_S_ISUID >> 6, 's');
	put_perm(st->st_mode >> 3, GFARM_S_ISGID >> 3, 's');
	put_perm(st->st_mode, GFARM_S_ISTXT, 't');
	printf(" %d %-8s %-8s ", (int)st->st_nlink, st->st_user, st->st_group);
	printf("%10" GFARM_PRId64 " ", st->st_size);
	put_time(&st->st_mtimespec);
	putchar(' ');
}

gfarm_error_t
list_files(char *prefix, int n, char **files, int *need_newline)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	int i;
	struct ls_entry *ls;
	struct gfs_stat *stats = NULL;

	GFARM_MALLOC_ARRAY(ls, n);
	if (ls == NULL)
		return (GFARM_ERR_NO_MEMORY);
	if (option_output_format == OF_LONG ||
	    option_sort_order != SO_NAME ||
	    option_type_suffix || option_inumber) {
		GFARM_MALLOC_ARRAY(stats, n);
		if (stats == NULL) {
			free(ls);
			return (GFARM_ERR_NO_MEMORY);
		}
		e = do_stats(prefix, &n, files, stats, ls);
	} else {
		for (i = 0; i < n; i++) {
			ls[i].path = files[i];
			ls[i].st = NULL;
			ls[i].symlink = NULL;
		}
	}
	ls_sort(n, ls);

	if (option_output_format == OF_MULTI_COLUMN) {
		int j, k, columns, lines, column_width, max_width = 0;

		for (i = 0; i < n; i++) {
			j = strlen(ls[i].path);
			if (max_width < j)
				max_width = j;
		}
		column_width = max_width +
		    (option_type_suffix ? 1 : 0) +
		    (option_inumber ? INUM_LEN : 0);
		columns = screen_width / (column_width + 1);
		if (columns <= 0) /* a pathname is wider than screen_width */
			columns = 1;
		lines = (n + columns - 1) / columns;
		for (i = 0; i < lines; i++) {
			for (j = 0; j < columns; j++) {
				int len_suffix = 0;
				k = i + j * lines;
				if (k >= n)
					break;
				if (option_inumber)
					INUM_PRINT(ls[k].st->st_ino);
				fputs(ls[k].path, stdout);
				if (option_type_suffix)
					len_suffix = put_suffix(&ls[k]);
				if (i + (j + 1) * lines < n)
					printf("%*s",
					    (int)(column_width
					    - (option_inumber ? INUM_LEN : 0)
					    - strlen(ls[k].path))
					    - len_suffix + 1, "");
			}
			putchar('\n');
		}				
	} else {
		for (i = 0; i < n; i++) {
			if (option_inumber)
				INUM_PRINT(ls[i].st->st_ino);
			if (option_output_format == OF_LONG)
				put_stat(ls[i].st);
			fputs(ls[i].path, stdout);
			if (option_type_suffix)
				(void)put_suffix(&ls[i]);
			if (option_output_format == OF_LONG &&
			    ls[i].symlink != NULL)
				printf(" -> %s", ls[i].symlink);
			putchar('\n');
		}
	}
	if (n > 0 || e != GFARM_ERR_NO_ERROR)
		*need_newline = 1;
	if (stats != NULL) {
		for (i = 0; i < n; i++) {
			if (stats[i].st_user != NULL ||
			    stats[i].st_group != NULL)
				gfs_stat_free(&stats[i]);
		}
		free(stats);
	}
	free(ls);
	return (e);
}

gfarm_error_t list_dirs(char *, int, char **, int *);

#define is_dot_or_dot_dot(s) \
	(s[0] == '.' && (s[1] == '\0' || (s[1] == '.' && s[2] == '\0')))

gfarm_error_t
list_dir(char *prefix, char *dirname, int *need_newline)
{
	gfarm_error_t e, e_save = GFARM_ERR_NO_ERROR;
	char *s, *path;
	const char *p;
	gfarm_stringlist names;
	gfs_glob_t types;
	GFS_Dir dir;
	struct gfs_dirent *entry;
	int len = strlen(prefix) + strlen(dirname);

	GFARM_MALLOC_ARRAY(path, len + 1 + 1);
	if (path == NULL)
		return (GFARM_ERR_NO_MEMORY);
	sprintf(path, "%s%s", prefix, dirname);
	e = gfarm_stringlist_init(&names);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		free(path);
		return (e);
	}
	e = gfs_glob_init(&types);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		gfarm_stringlist_free(&names);
		free(path);
		return (e);
	}
	e = gfs_opendir_caching(path, &dir);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", path, gfarm_error_string(e));
		gfs_glob_free(&types);
		gfarm_stringlist_free(&names);
		free(path);
		return (e);
	}
	while ((e = gfs_readdir(dir, &entry))
	    == GFARM_ERR_NO_ERROR && entry != NULL) {
		if (!option_all && entry->d_name[0] == '.')
			continue;
		if (is_option_almost_all && is_dot_or_dot_dot(entry->d_name))
			continue;
		s = strdup(entry->d_name);
		if (s == NULL) {
			e = GFARM_ERR_NO_MEMORY;
			break;
		}
		gfarm_stringlist_add(&names, s);
		gfs_glob_add(&types, entry->d_type);
	}
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s%s: %s\n", prefix, dirname,
		    gfarm_error_string(e));
		e_save = e;
	}
	gfs_closedir(dir);
	p = gfarm_url_prefix_hostname_port_skip(path);
	if (*p == '\0' || *gfarm_url_dir_skip(p) != '\0') {
		path[len] = '/';
		path[len + 1] = '\0';
	}
	e = list_files(path, gfarm_stringlist_length(&names),
	    GFARM_STRINGLIST_STRARRAY(names), need_newline);
	if (e_save == GFARM_ERR_NO_ERROR)
		e_save = e;
	if (option_recursive) {
		int i;

		for (i = 0; i < gfarm_stringlist_length(&names); i++) {
			s = GFARM_STRINGLIST_STRARRAY(names)[i];
			if (is_dot_or_dot_dot(s))
				continue; /* "." or ".." */
			if (!option_all && s[0] == '.')
				continue;
			if (gfs_glob_elem(&types, i) == GFS_DT_DIR) {
				e = list_dirs(path, 1, &s, need_newline);
				if (e_save == GFARM_ERR_NO_ERROR)
					e_save = e;
			}
		}
	}
	gfs_glob_free(&types);
	gfarm_stringlist_free_deeply(&names);
	free(path);
	return (e_save);
}

int
string_compare(const void *a, const void *b)
{
	const char *const *p = a, *const *q = b;

	return (strcmp(*p, *q));
}

void
string_sort(int n, char **v)
{
	qsort(v, n, sizeof(*v), string_compare);
}

gfarm_error_t
list_dirs(char *prefix, int n, char **dirs, int *need_newline)
{
	gfarm_error_t e, e_save = GFARM_ERR_NO_ERROR;
	int i;

	string_sort(n, dirs);
	for (i = 0; i < n; i++) {
		if (*need_newline) {
			printf("\n");
			*need_newline = 0;
		}
		printf("%s%s:\n", prefix, dirs[i]);
		e = list_dir(prefix, dirs[i], need_newline);
		*need_newline = 1;
		if (e_save == GFARM_ERR_NO_ERROR)
			e_save = e;
	}
	return (e_save);
}

gfarm_error_t
list(gfarm_stringlist *paths, gfs_glob_t *types, int *need_newline)
{
	gfarm_error_t e, e_save = GFARM_ERR_NO_ERROR;
	gfarm_stringlist dirs, files;
	int i, nfiles, ndirs;

	if (option_directory_itself) {
		return (list_files("", gfarm_stringlist_length(paths),
		    GFARM_STRINGLIST_STRARRAY(*paths), need_newline));
	}

	e = gfarm_stringlist_init(&dirs);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		return (e);
	}
	e = gfarm_stringlist_init(&files);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		gfarm_stringlist_free(&dirs);
		return (e);
	}
	for (i = 0; i < gfarm_stringlist_length(paths); i++) {
		char *path = gfarm_stringlist_elem(paths, i);

		if (gfs_glob_elem(types, i) == GFS_DT_DIR)
			gfarm_stringlist_add(&dirs, path);
		else
			gfarm_stringlist_add(&files, path);
	}
	nfiles = gfarm_stringlist_length(&files);
	ndirs = gfarm_stringlist_length(&dirs);

	if (nfiles > 0) {
		e = list_files("",
		    nfiles, GFARM_STRINGLIST_STRARRAY(files), need_newline);
		/* warning is already printed in list_files() */
		if (e_save == GFARM_ERR_NO_ERROR)
			e_save = e;
	}
	gfarm_stringlist_free(&files);

	if (nfiles == 0 && ndirs == 1) {
		e = list_dir("", gfarm_stringlist_elem(&dirs, 0),
		    need_newline);
		/* warning is already printed in list_dir() */
	} else {
		e = list_dirs("", ndirs, GFARM_STRINGLIST_STRARRAY(dirs),
		    need_newline);
		/* warning is already printed in list_dirs() */
	}
	if (e_save == GFARM_ERR_NO_ERROR)
		e_save = e;
	gfarm_stringlist_free(&dirs);

	return (e_save);
}

void
usage(void)
{
	fprintf(stderr, "Usage: %s [-1ACFRSTadilrt] [-E <sec>] <path>...\n",
		program_name);
	exit(EXIT_FAILURE);
}

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	gfarm_stringlist paths;
	gfs_glob_t types;
	int i, c, exit_code = EXIT_SUCCESS;
	char *ep, *realpath = NULL;
	const char *cwd;
	static const char dot[] = ".";

	if (argc > 0)
		program_name = basename(argv[0]);
	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: gfarm_initialize: %s\n", program_name,
		    gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}

	if (isatty(STDOUT_FILENO)) {
		char *s = getenv("COLUMNS");
#ifdef TIOCGWINSZ
		struct winsize win;
#endif

		if (s != NULL)
			screen_width = strtol(s, NULL, 0);
#ifdef TIOCGWINSZ
		else if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &win) == 0 &&
		    win.ws_col > 0)
			screen_width = win.ws_col;
#endif
		option_output_format = OF_MULTI_COLUMN;
	} else {
		option_output_format = OF_ONE_PER_LINE;
	}
	while ((c = getopt(argc, argv, "1ACE:FRSTadilrt?")) != -1) {
		switch (c) {
		case '1': option_output_format = OF_ONE_PER_LINE; break;
		case 'A': option_all = OA_ALMOST_ALL; break;
		case 'C': option_output_format = OF_MULTI_COLUMN; break;
		case 'E':
			errno = 0;
			option_cache_expiration = strtod(optarg, &ep);
			if (ep == optarg || *ep != '\0') {
				fprintf(stderr, "%s: -E %s: invalid argument\n",
				    program_name, optarg);
				usage();
			} else if (errno != 0) {
				fprintf(stderr, "%s: -E %s: %s\n",
				    program_name, optarg, strerror(errno));
				exit(EXIT_FAILURE);
			}
			break;
		case 'F': option_type_suffix = 1; break;
		case 'R': option_recursive = 1; break;
		case 'S': option_sort_order = SO_SIZE; break;
		case 'T': option_complete_time = 1; break;
		case 'a': option_all = OA_ALL; break;
		case 'd': option_directory_itself = 1; break;
		case 'i': option_inumber = 1; break;
		case 'l': option_output_format = OF_LONG; break;
		case 'r': option_reverse_sort = 1; break;
		case 't': option_sort_order = SO_MTIME; break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (option_cache_expiration == 0.0)
		gfs_stat_cache_enable(0);
	else if (option_cache_expiration != CACHE_EXPIRATION_NOT_SPECIFIED)
		gfs_stat_cache_expiration_set(option_cache_expiration*1000.0);

	e = gfarm_stringlist_init(&paths);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}
	e = gfs_glob_init(&types);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}
	if (argc < 1) {
		if (gfarm_realpath_by_gfarm2fs(dot, &realpath)
		    == GFARM_ERR_NO_ERROR)
			cwd = realpath;
		else
			cwd = dot;
		gfarm_stringlist_add(&paths, strdup(cwd));
		free(realpath);
		gfs_glob_add(&types, GFS_DT_DIR);
	} else {
		for (i = 0; i < argc; i++) {
			int last;

			e = gfarm_realpath_by_gfarm2fs(argv[i], &realpath);
			if (e == GFARM_ERR_NO_ERROR)
				argv[i] = realpath;
			/* do not treat glob error as an error */
			gfs_glob(argv[i], &paths, &types);
			free(realpath);

			last = gfs_glob_length(&types) - 1;
			if (last >= 0 && gfs_glob_elem(&types, last) ==
			    GFS_DT_UNKNOWN) {
				/*
				 * Currently, this only happens if there is
				 * no file which matches with argv[i].
				 * In such case, the number of entries which
				 * were added by gfs_glob() is 1.
				 * But also please note that GFS_DT_UNKNOWN
				 * may happen on other case in future.
				 */
				struct gfs_stat s;
				char *path = gfarm_stringlist_elem(&paths,
				    last);

				e = gfs_lstat_cached(path, &s);
				if (e != GFARM_ERR_NO_ERROR) {
					fprintf(stderr, "%s: %s\n", path,
					    gfarm_error_string(e));
					exit_code = EXIT_FAILURE;
					/* remove last entry */
					/* XXX: FIXME layering violation */
					free(paths.array[last]);
					paths.length--;
					types.length--;
				} else {
					GFS_GLOB_ELEM(types, last) =
					    gfs_mode_to_type(s.st_mode);
					gfs_stat_free(&s);
				}
			}
		}
	}
	if (gfarm_stringlist_length(&paths) > 0) {
		int need_newline = 0;

#if 1
		if (list(&paths, &types, &need_newline) != GFARM_ERR_NO_ERROR){
			/* warning is already printed in list() */
			exit_code = EXIT_FAILURE;
		}
#else
		for (i = 0; i < gfarm_stringlist_length(&paths); i++)
			printf("<%s>\n", gfarm_stringlist_elem(&paths, i));
#endif
	}
	gfarm_stringlist_free_deeply(&paths);
	gfs_glob_free(&types);

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: gfarm_terminate: %s\n", program_name,
		    gfarm_error_string(e));
		exit_code = EXIT_FAILURE;
	}
	return (exit_code);
}
