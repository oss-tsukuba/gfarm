#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <libgen.h>
#include <limits.h>
#include <unistd.h>
#include <sys/stat.h> /* XXX */
#include <errno.h> /* XXX */
#include <sys/ioctl.h> /* TIOCGWISZ, struct winsize */
#include <sys/time.h>
#include <time.h>
#include <gfarm/gfarm.h>
#include "hash.h"

char *program_name = "gfls";

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
int option_type_suffix = 0;		/* -F */
int option_recursive = 0;		/* -R */
int option_complete_time = 0;		/* -T */
int option_directory_itself = 0;	/* -d */
int option_reverse_sort = 0;		/* -r */

int screen_width = 80; /* default */

char *
gfs_getcwd(char *cwd, int cwdsize)
{
	char *user = gfarm_get_global_username();
	int len = strlen(user);

	if (len < cwdsize) {
		strcpy(cwd, user);
	} else {
		memcpy(cwd, user, cwdsize - 1);
		cwd[cwdsize - 1] = '\0';
	}
	return (NULL);
}

static char gfarm_prefix[] = "gfarm:";

void
skip_gfarm_prefix(char **path)
{
	char *p = *path;

	if (memcmp(p, gfarm_prefix, GFARM_ARRAY_LENGTH(gfarm_prefix) - 1) == 0)
		p += GFARM_ARRAY_LENGTH(gfarm_prefix) - 1;
	*path = p;
}

/*
 * directory tree, opendir/readdir/closedir
 */
struct node {
	struct node *parent;
	char *name;
	int is_dir;
	union node_u {
		struct dir {
			struct gfarm_hash_table *children;
		} d;
	} u;
};

#define NODE_HASH_SIZE 53 /* prime */

struct node *root;

struct node *
init_node_name(struct node *n, char *name, int len)
{
	n->name = malloc(len + 1);
	if (n->name == NULL)
		return (NULL);
	memcpy(n->name, name, len);
	n->name[len] = '\0';
	return (n);
}

#define DIR_NODE_SIZE \
	(sizeof(struct node) - sizeof(union node_u) + sizeof(struct dir))

struct node *
init_dir_node(struct node *n, char *name, int len)
{
	if (init_node_name(n, name, len) == NULL)
		return (NULL);
	n->is_dir = 1;
	n->u.d.children = gfarm_hash_table_alloc(NODE_HASH_SIZE,
	    gfarm_hash_default, gfarm_hash_key_equal_default);
	return (n);
}

#define FILE_NODE_SIZE (sizeof(struct node) - sizeof(union node_u))

struct node *
init_file_node(struct node *n, char *name, int len)
{
	if (init_node_name(n, name, len) == NULL)
		return (NULL);
	n->is_dir = 0;
	return (n);
}

struct node *
lookup_node(struct node *parent, char *name, int len, int is_dir, int create)
{
	struct gfarm_hash_entry *entry;
	int created;
	struct node *n;

	if (!parent->is_dir)
		return (NULL); /* not a directory */
	if (len == 0) {
		return (parent);
	} else if (len == 1 && name[0] == '.') {
		return (parent);
	} else if (len == 2 && name[0] == '.' && name[1] == '.') {
		return (parent->parent);
	}
	if (len > GFS_MAXNAMLEN)
		len = GFS_MAXNAMLEN;
	if (!create) {
		entry = gfarm_hash_lookup(parent->u.d.children, name, len);
		return (entry == NULL ? NULL : gfarm_hash_entry_data(entry));
	}

	entry = gfarm_hash_enter(parent->u.d.children, name, len,
	    is_dir ? DIR_NODE_SIZE : FILE_NODE_SIZE, &created);
	n = gfarm_hash_entry_data(entry);
	if (!created)
		return (n);
	if (is_dir)
		init_dir_node(n, name, len);
	else
		init_file_node(n, name, len);
	if (n == NULL) {
		gfarm_hash_purge(parent->u.d.children, name, len);
		return (NULL);
	}
	n->parent = parent;
	return (n);
}

/* if (!create), (is_dir) may be -1, and that means "don't care". */
char *
lookup_relative(struct node *n, char *path, int is_dir, int create,
	struct node **np)
{
	int len;

	if (!n->is_dir)
		return (GFARM_ERR_NOT_A_DIRECTORY);
	for (;;) {
		while (*path == '/')
			path++;
		for (len = 0; path[len] != '/'; len++) {
			if (path[len] == '\0') {
				n = lookup_node(n, path, len, is_dir, create);
				if (n == NULL)
					return (create ? GFARM_ERR_NO_MEMORY :
					    GFARM_ERR_NO_SUCH_OBJECT);
				if (is_dir != -1 && n->is_dir != is_dir)
					return (n->is_dir ?
					    GFARM_ERR_IS_A_DIRECTORY :
					    GFARM_ERR_NOT_A_DIRECTORY);
				if (np != NULL)
					*np = n;
				return (NULL);
			}
		}
		n = lookup_node(n, path, len, 1, /* XXX */ create);
		if (n == NULL)
			return (create ? GFARM_ERR_NO_MEMORY :
			    GFARM_ERR_NO_SUCH_OBJECT);
		if (!n->is_dir)
			return (GFARM_ERR_NOT_A_DIRECTORY);
		path += len;
	}
}

/* if (!create), (is_dir) may be -1, and that means "don't care". */
char *
lookup_path(char *path, int is_dir, int create,	struct node **np)
{
	struct node *n;

	if (path[0] == '/') {
		n = root;
	} else {
		char *e;
		char cwd[PATH_MAX + 1];

		e = gfs_getcwd(cwd, sizeof(cwd));
		if (e != NULL)
			return (e);
		e = lookup_relative(root, cwd, 1, /* XXX */ 1, &n);
		if (e != NULL)
			return (e);
	}
	return (lookup_relative(n, path, is_dir, create, np));
}

char *
root_node(void)
{
	root = malloc(DIR_NODE_SIZE);
	if (root == NULL)
		return (GFARM_ERR_NO_MEMORY);
	init_dir_node(root, "", 0);
	root->parent = root;
	return (NULL);
}

void
remember_path(void *closure, struct gfarm_path_info *info)
{
	lookup_relative(root, info->pathname,
	    GFARM_S_ISDIR(info->status.st_mode), 1, NULL);
}

void
remember_dirtree(void)
{
	char *e = root_node();

	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(EXIT_FAILURE);
	}
	gfarm_path_info_get_all_foreach(remember_path, NULL);
}

char *
get_abspath(char *path, char **abspathp)
{
	struct node *n, *p;
	char *e, *abspath;
	int l, len;

	e = lookup_path(path, -1, 0, &n);
	if (e != NULL)
		return (e);
	len = 0;
	for (p = n; p != root; p = p->parent)
		len += strlen(p->name) + 1;
	len += GFARM_ARRAY_LENGTH(gfarm_prefix) - 1;
	abspath = malloc(len + 1);
	if (abspath == NULL)
		return (GFARM_ERR_NO_MEMORY);
	abspath[len] = '\0';
	for (p = n; p != root; p = p->parent) {
		l = strlen(p->name);
		len -= l;
		memcpy(abspath + len, p->name, l);
		abspath[--len] = '/';
	}
	memcpy(abspath, gfarm_prefix, GFARM_ARRAY_LENGTH(gfarm_prefix) - 1);
	*abspathp = abspath;
	return (NULL);
}

/*
 * gfs_opendir()/readdir()/closedir()
 */

struct gfs_dir {
	struct node *dir;
	struct gfarm_hash_iterator iterator;
	struct gfs_dirent buffer;
};

char *
gfs_opendir(char *path, GFS_Dir *dirp)
{
	char *e;
	struct node *n;
	struct gfs_dir *dir;

	skip_gfarm_prefix(&path);
	e = lookup_path(path, 1, 0, &n);
	if (e != NULL)
		return (e);
	dir = malloc(sizeof(struct gfs_dir));
	if (dir == NULL)
		return (GFARM_ERR_NO_MEMORY);
	dir->dir = n;
	gfarm_hash_iterator_begin(n->u.d.children, &dir->iterator);
	*dirp = dir;
	return (NULL);
}

char *
gfs_readdir(GFS_Dir dir, struct gfs_dirent **entry)
{
	struct gfarm_hash_entry *he;
	struct node *n;

	he = gfarm_hash_iterator_access(&dir->iterator);
	if (he == NULL) {
		*entry = NULL;
		return (NULL);
	}
	n = gfarm_hash_entry_data(he);
	gfarm_hash_iterator_next(&dir->iterator);
	dir->buffer.d_fileno = 1; /* XXX */
	dir->buffer.d_type = n->is_dir ? GFS_DT_DIR : GFS_DT_REG;
	dir->buffer.d_namlen = gfarm_hash_entry_key_length(he);
	memcpy(dir->buffer.d_name, gfarm_hash_entry_key(he),
	    dir->buffer.d_namlen);
	dir->buffer.d_name[dir->buffer.d_namlen] = '\0';
	*entry = &dir->buffer;
	return (NULL);
}

char *
gfs_closedir(GFS_Dir dir)
{
	free(dir);
	return (NULL);
}

char *
gfs_stat_fake(char *path, struct gfs_stat *s)
{
	char *e, *p;
	struct node *n;

	skip_gfarm_prefix(&path);
	e = get_abspath(path, &p);
	if (e != NULL)
		return (e);
	e = gfs_stat(p, s);
	free(p);
	if (e == NULL || e != GFARM_ERR_NO_SUCH_OBJECT)
		return (e);
	/* XXX */
	e = lookup_path(path, 1, 0, &n);
	if (e != NULL)
		return (e);
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
	return (NULL);
}

/*
 * gfarm_dtypelist
 */

#define GFARM_DTYPELIST_INITIAL	200
#define GFARM_DTYPELIST_DELTA	200
typedef struct {
	unsigned char *array;
	int length, size;
} gfarm_dtypelist;

#define GFARM_DTYPELIST_ARRAY(dtypelist)	(dtypelist).array
#define GFARM_DTYPELIST_ELEM(dtypelist, i)	(dtypelist).array[i]
#define gfarm_dtypelist_length(dtypelist)	(dtypelist)->length
#define gfarm_dtypelist_elem(dtypelist, i) \
	GFARM_DTYPELIST_ELEM(*(dtypelist), i)

char *
gfarm_dtypelist_init(gfarm_dtypelist *listp)
{
	unsigned char *v;

	v = malloc(sizeof(unsigned char) * GFARM_DTYPELIST_INITIAL);
	if (v == NULL)
		return (GFARM_ERR_NO_MEMORY);
	listp->size = GFARM_DTYPELIST_INITIAL;
	listp->length = 0;
	listp->array = v;
	return (NULL);
}

void
gfarm_dtypelist_free(gfarm_dtypelist *listp)
{
	free(listp->array);

	/* the following is not needed, but to make erroneous program abort */
	listp->size = 0;
	listp->length = 0;
	listp->array = NULL;
}

char *
gfarm_dtypelist_add(gfarm_dtypelist *listp, int dtype)
{
	int length = gfarm_dtypelist_length(listp);

	if (length >= listp->size) {
		int n = listp->size + GFARM_DTYPELIST_DELTA;
		unsigned char *t = realloc(listp->array,
		    sizeof(unsigned char) * n);

		if (t == NULL)
			return (GFARM_ERR_NO_MEMORY);
		listp->size = n;
		listp->array = t;
	}
	listp->array[length] = dtype;
	listp->length++;
	return (NULL);
}

/*
 * gfls implementation
 */

struct ls_entry {
	char *path;
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

	if (p->st->st_mtimespec.tv_sec > q->st->st_mtimespec.tv_sec)
		return (1);
	else if (p->st->st_mtimespec.tv_sec < q->st->st_mtimespec.tv_sec)
		return (-1);
	else if (p->st->st_mtimespec.tv_nsec > q->st->st_mtimespec.tv_nsec)
		return (1);
	else if (p->st->st_mtimespec.tv_nsec < q->st->st_mtimespec.tv_nsec)
		return (-1);
	else
		return (0);
}

int
compare_mtime_r(const void *a, const void *b)
{
	return (-compare_mtime(a, b));
}

void
ls_sort(int n, struct ls_entry *ls)
{
	int (*compare)(const void *, const void *);

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

char *
do_stats(char *prefix, int *np, char **files, struct gfs_stat *stats,
	struct ls_entry *ls)
{
	int i, n = *np, m, prefix_len, space;
	char *namep, buffer[PATH_MAX * 2 + 1], *e, *e_save = NULL;

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
		e = gfs_stat_fake(buffer, &stats[i]);
		if (e != NULL) {
fprintf(stderr, "XXX<%s>", prefix);
			fprintf(stderr, "%s: %s\n", buffer, e);
			if (e_save != NULL)
				e_save = e;
			continue;
		}
		ls[m].path = files[i];
		ls[m].st = &stats[i];
		m++;
	}
	*np = m;
	return (e_save);
}

void
put_suffix(struct ls_entry *ls)
{
	struct gfs_stat *st = ls->st;

	if (GFARM_S_ISDIR(st->st_mode))
		putchar('/');
	else if (GFARM_S_IS_PROGRAM(st->st_mode))
		putchar('*');
}

void
put_perm(int mode)
{
	if (mode & 04)
		putchar('r');
	else
		putchar('-');
	if (mode & 02)
		putchar('w');
	else
		putchar('-');
	if (mode & 01)
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
	else
		putchar('-');
	put_perm(st->st_mode >> 6);
	put_perm(st->st_mode >> 3);
	put_perm(st->st_mode);
	putchar(' ');
	printf("%-8s %-8s ", st->st_user, st->st_group);
	printf("%10" PR_FILE_OFFSET " ", st->st_size);
	put_time(&st->st_mtimespec);
	putchar(' ');
}

char *
list_files(char *prefix, int n, char **files, int *need_newline)
{
	int i;
	struct ls_entry *ls = malloc(sizeof(struct ls_entry) * n);
	struct gfs_stat *stats = NULL;
	char *e = NULL;

	if (ls == NULL)
		return (GFARM_ERR_NO_MEMORY);
	if (option_output_format == OF_LONG ||
	    option_sort_order != SO_NAME ||
	    option_type_suffix) {
		stats = malloc(sizeof(struct gfs_stat) * n);
		if (stats == NULL) {
			free(ls);
			return (GFARM_ERR_NO_MEMORY);
		}
		e = do_stats(prefix, &n, files, stats, ls);
	} else {
		for (i = 0; i < n; i++) {
			ls[i].path = files[i];
			ls[i].st = NULL;
		}
	}
	ls_sort(n, ls);

	if (option_output_format == OF_MULTI_COLUMN) {
		int j, k, columns, lines, column_width, max_width = 0;

		for (i = 0; i < n; i++)
			if (max_width < strlen(ls[i].path))
				max_width = strlen(ls[i].path);
		column_width = option_type_suffix ? max_width + 1 : max_width;
		columns = screen_width / (column_width + 1);
		lines = n / columns;
		if (lines * columns < n)
			lines++;
		for (i = 0; i < lines; i++) {
			for (j = 0; j < columns; j++) {
				k = i + j * lines;
				if (k >= n)
					break;
				fputs(ls[k].path, stdout);
				if (option_type_suffix)
					put_suffix(&ls[k]);
				printf("%*s",
				    max_width - strlen(ls[k].path) + 1, "");
			}
			putchar('\n');
		}				
	} else {
		for (i = 0; i < n; i++) {
			if (option_output_format == OF_LONG)
				put_stat(ls[i].st);
			fputs(ls[i].path, stdout);
			if (option_type_suffix)
				put_suffix(&ls[i]);
			putchar('\n');
		}
	}
	if (n > 0 || e != NULL)
		*need_newline = 1;
	if (stats != NULL)
		free(stats);
	free(ls);
	return (e);
}

char *list_dirs(char *, int, char **, int *);

char *
list_dir(char *prefix, char *dirname, int *need_newline)
{
	char *e, *s, *path, *e_save = NULL;
	gfarm_stringlist names;
	gfarm_dtypelist types;
	GFS_Dir dir;
	struct gfs_dirent *entry;
	int len = strlen(prefix) + strlen(dirname);

	path = malloc(len + 1 + 1);
	if (path == NULL)
		return (GFARM_ERR_NO_MEMORY);
	sprintf(path, "%s%s", prefix, dirname);
	e = gfarm_stringlist_init(&names);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		free(path);
		return (e);
	}
	e = gfarm_dtypelist_init(&types);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		gfarm_stringlist_free(&names);
		free(path);
		return (e);
	}
	e = gfs_opendir(path, &dir);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", path, e);
		gfarm_dtypelist_free(&types);
		gfarm_stringlist_free(&names);
		free(path);
		return (e);
	}
	while ((e = gfs_readdir(dir, &entry)) == NULL && entry != NULL) {
		s = strdup(entry->d_name);
		if (s == NULL) {
			e = GFARM_ERR_NO_MEMORY;
			break;
		}
		gfarm_stringlist_add(&names, s);
		gfarm_dtypelist_add(&types, entry->d_type);
	}
	if (e != NULL) {
		fprintf(stderr, "%s%s: %s\n", prefix, dirname, e);
		e_save = e;
	}
	gfs_closedir(dir);
	if (len > 0 && path[len - 1] != '/') {
		path[len] = '/';
		path[len + 1] = '\0';
	}
	e = list_files(path, gfarm_stringlist_length(&names),
	    GFARM_STRINGLIST_STRARRAY(names), need_newline);
	if (e_save == NULL)
		e_save = e;
	if (option_recursive) {
		int i;

		for (i = 0; i < gfarm_stringlist_length(&names); i++) {
			s = GFARM_STRINGLIST_STRARRAY(names)[i];
			if (s[0] == '.' && (s[1] == '\0' ||
			    (s[1] == '.' && s[2] == '\0')))
				continue;
			if (gfarm_dtypelist_elem(&types, i) == GFS_DT_DIR) {
				e = list_dirs(path, 1, &s, need_newline);
				if (e_save == NULL)
					e_save = e;
			}
		}
	}
	gfarm_dtypelist_free(&types);
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

char *
list_dirs(char *prefix, int n, char **dirs, int *need_newline)
{
	char *e, *e_save = NULL;
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
		if (e_save == NULL)
			e_save = e;
	}
	return (e_save);
}

char *
list(gfarm_stringlist *paths, gfarm_dtypelist *types, int *need_newline)
{
	char *e, *e_save = NULL;
	gfarm_stringlist dirs, files;
	gfarm_dtypelist filetypes;
	int i, nfiles, ndirs;

	if (option_directory_itself) {
		return (list_files("", gfarm_stringlist_length(paths),
		    GFARM_STRINGLIST_STRARRAY(*paths), need_newline));
	}

	e = gfarm_stringlist_init(&dirs);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		return (e);
	}
	e = gfarm_stringlist_init(&files);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		gfarm_stringlist_free(&dirs);
		return (e);
	}
	e = gfarm_dtypelist_init(&filetypes);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		gfarm_stringlist_free(&files);
		gfarm_stringlist_free(&dirs);
		return (e);
	}
	for (i = 0; i < gfarm_stringlist_length(paths); i++) {
		char *path = gfarm_stringlist_elem(paths, i);

		if (gfarm_dtypelist_elem(types, i) == GFS_DT_DIR)
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
		if (e_save == NULL)
			e_save = e;
	}
	gfarm_dtypelist_free(&filetypes);
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
	if (e_save == NULL)
		e_save = e;
	gfarm_stringlist_free(&dirs);

	return (e_save);
}

/*
 * gfs_glob
 */
void
glob_pattern_to_name(char *name, char *pattern, int length)
{
	int i, j;

	for (i = j = 0; j < length; i++, j++) {
		if (pattern[j] == '\\') {
			if (pattern[j + 1] != '\0' &&
			    pattern[j + 1] != '/')
				j++;
		}
		name[i] = pattern[j];
	}
	name[i] = '\0';
}

int
glob_charset_parse(char *pattern, int index, int *ip)
{
	int i = index;

	if (pattern[i] == '!')
		i++;
	if (pattern[i] != '\0') {
		if (pattern[i + 1] == '-' && pattern[i + 2] != '\0')
			i += 3;
		else
			i++;
	}
	while (pattern[i] != ']') {
		if (pattern[i] == '\0') {
			/* end of charset isn't found */
			if (ip != NULL)
				*ip = index;
			return (0);
		}
		if (pattern[i + 1] == '-' && pattern[i + 2] != '\0')
			i += 3;
		else
			i++;
	}
	if (ip != NULL)
		*ip = i;
	return (1);
}

int
glob_charset_match(int ch, char *pattern, int pattern_length)
{
	int i = 0, negate = 0;
	unsigned char c = ch, *p = (unsigned char *)pattern;

	if (p[i] == '!') {
		negate = 1;
		i++;
	}
	while (i < pattern_length) {
		if (p[i + 1] == '-' && p[i + 2] != '\0') {
			if (p[i] <= c && c <= p[i + 2])
				return (!negate);
			i += 3;
		} else {
			if (c == p[i])
				return (!negate);
			i++;
		}
	}
	return (negate);
}

int
glob_name_submatch(char *name, char *pattern, int namelen)
{
	int w;

	for (; --namelen >= 0; name++, pattern++){
		if (*pattern == '?')
			continue;
		if (*pattern == '[' &&
		    glob_charset_parse(pattern, 1, &w)) {
			if (glob_charset_match(*(unsigned char *)name,
			    pattern + 1, w - 1)) {
				pattern += w;
				continue;
			}
			return (0);
		}
		if (*pattern == '\\' &&
		    pattern[1] != '\0' && pattern[1] != '/') {
			if (*name == pattern[1]) {
				pattern++;
				continue;
			}
		}
		if (*name != *pattern)
			return (0);
	}
	return (1);
}

int
glob_prefix_length_to_asterisk(char *pattern, int pattern_length,
	char **asterisk)
{
	int i, length = 0;

	for (i = 0; i < pattern_length; length++, i++) {
		if (pattern[i] == '\\') {
			if (i + 1 < pattern_length  &&
			    pattern[i + 1] != '/')
				i++;
		} else if (pattern[i] == '*') {
			*asterisk = &pattern[i];
			return (length);
		} else if (pattern[i] == '[') {
			glob_charset_parse(pattern, i + 1, &i);
		}
	}
	*asterisk = &pattern[i];
	return (length);
}

int
glob_name_match(char *name, char *pattern, int pattern_length)
{
	char *asterisk;
	int residual = strlen(name);
	int sublen = glob_prefix_length_to_asterisk(pattern, pattern_length,
	    &asterisk);

	if (residual < sublen || !glob_name_submatch(name, pattern, sublen))
		return (0);
	if (*asterisk == '\0')
		return (residual == sublen);
	for (;;) {
		name += sublen; residual -= sublen;
		pattern_length -= asterisk + 1 - pattern;
		pattern = asterisk + 1;
		sublen = glob_prefix_length_to_asterisk(pattern,
		    pattern_length, &asterisk);
		if (*asterisk == '\0')
			break;
		for (;; name++, --residual){
			if (residual < sublen)
				return (0);
			if (glob_name_submatch(name, pattern, sublen))
				break;
		}
	}
	return (residual >= sublen &&
	    glob_name_submatch(name + residual - sublen, pattern, sublen));
}

char *
strdup_gfarm_prefix(char *s)
{
	char *p = malloc(strlen(s) + GFARM_ARRAY_LENGTH(gfarm_prefix));

	if (p == NULL)
		return (NULL);
	memcpy(p, gfarm_prefix, GFARM_ARRAY_LENGTH(gfarm_prefix) - 1);
	strcpy(p + GFARM_ARRAY_LENGTH(gfarm_prefix) - 1, s);
	return (p);
}

char GFARM_ERR_PATHNAME_TOO_LONG[] = "pathname too long";

#define GLOB_PATH_BUFFER_SIZE	(PATH_MAX * 2)

char *
gfs_glob_sub(char *path_buffer, char *path_tail, char *pattern,
	gfarm_stringlist *paths, gfarm_dtypelist *types)
{
	char *s, *e, *e_save = NULL;
	int i, nomagic, dirpos = -1;
	GFS_Dir dir;
	struct gfs_dirent *entry;
	struct gfs_stat st;

	for (i = 0; pattern[i] != '\0'; i++) {
		if (pattern[i] == '\\') {
			if (pattern[i + 1] != '\0' &&
			    pattern[i + 1] != '/')
				i++;
		} else if (pattern[i] == '/') {
			dirpos = i;
		} else if (pattern[i] == '?' || pattern[i] == '*') {
			break;
		} else if (pattern[i] == '[') {
			if (glob_charset_parse(pattern, i + 1, NULL))
				break;
		}
	}
	if (pattern[i] == '\0') { /* no magic */
		if (path_tail - path_buffer + strlen(pattern) >
		    GLOB_PATH_BUFFER_SIZE)
			return (GFARM_ERR_PATHNAME_TOO_LONG);
		glob_pattern_to_name(path_tail, pattern, strlen(pattern));
		e = gfs_stat_fake(path_buffer, &st);
		if (e != NULL)
			return (e);
		s = strdup_gfarm_prefix(path_buffer);
		if (s == NULL)
			return (GFARM_ERR_NO_MEMORY);
		gfarm_stringlist_add(paths, s);
		if (GFARM_S_ISDIR(st.st_mode))
			gfarm_dtypelist_add(types, GFS_DT_DIR);
		else
			gfarm_dtypelist_add(types, GFS_DT_REG);
		return (NULL);
	}
	nomagic = i;
	if (dirpos >= 0) {
		int dirlen = dirpos == 0 ? 1 : dirpos;

		if (path_tail - path_buffer + dirlen > GLOB_PATH_BUFFER_SIZE)
			return (GFARM_ERR_PATHNAME_TOO_LONG);
		glob_pattern_to_name(path_tail, pattern, dirlen);
		path_tail += strlen(path_tail);
	}
	dirpos++;
	for (i = nomagic; pattern[i] != '\0'; i++) {
		if (pattern[i] == '\\') {
			if (pattern[i + 1] != '\0' &&
			    pattern[i + 1] != '/')
				i++;
		} else if (pattern[i] == '/') {
			break;
		} else if (pattern[i] == '?' || pattern[i] == '*') {
		} else if (pattern[i] == '[') {
			glob_charset_parse(pattern, i + 1, &i);
		}
	}
	e = gfs_opendir(path_buffer, &dir);
	if (e != NULL)
		return (e);
	if (path_tail > path_buffer && path_tail[-1] != '/') {
		if (path_tail - path_buffer + 1 > GLOB_PATH_BUFFER_SIZE)
			return (GFARM_ERR_PATHNAME_TOO_LONG);
		*path_tail++ = '/';
	}
	while ((e = gfs_readdir(dir, &entry)) == NULL && entry != NULL) {
		if (!glob_name_match(entry->d_name, &pattern[dirpos],
		    i - dirpos))
			continue;
		if (path_tail - path_buffer + strlen(entry->d_name) >
		    GLOB_PATH_BUFFER_SIZE) {
			if (e_save == NULL)
				e_save = GFARM_ERR_PATHNAME_TOO_LONG;
			continue;
		}
		strcpy(path_tail, entry->d_name);
		if (pattern[i] == '\0') {
			s = strdup_gfarm_prefix(path_buffer);
			if (s == NULL)
				return (GFARM_ERR_NO_MEMORY);
			gfarm_stringlist_add(paths, s);
			gfarm_dtypelist_add(types, entry->d_type);
			continue;
		}
		e = gfs_glob_sub(path_buffer, path_tail + strlen(path_tail),
		    pattern + i, paths, types);
		if (e_save == NULL)
			e_save = e;
	}
	gfs_closedir(dir);
	return (e_save);
}

char *
gfs_glob(char *pattern,	gfarm_stringlist *paths, gfarm_dtypelist *types)
{
	char *s, *p = NULL, *e = NULL;
	int len, n = gfarm_stringlist_length(paths);
	char path_buffer[GLOB_PATH_BUFFER_SIZE + 1];

	skip_gfarm_prefix(&pattern);
	if (*pattern == '~') {
		if (pattern[1] == '\0' || pattern[1] == '/') {
			s = gfarm_get_global_username();
			len = strlen(s);
			pattern++;
		} else {
			s = pattern + 1;
			len = strcspn(s, "/");
			pattern += 1 + len;
		}
		p = malloc(1 + len + strlen(pattern));
		if (p == NULL) {
			e = GFARM_ERR_PATHNAME_TOO_LONG;
		} else {
			p[0] = '/';
			memcpy(p + 1, s, len);
			strcpy(p + 1 + len, pattern);
			pattern = p;
		}
	} else {
		strcpy(path_buffer, ".");
	}
	if (e == NULL) {
		e = gfs_glob_sub(path_buffer, path_buffer, pattern,
		    paths, types);
	}
	if (gfarm_stringlist_length(paths) <= n) {
		gfarm_stringlist_add(paths, strdup_gfarm_prefix(pattern));
		gfarm_dtypelist_add(types, GFS_DT_UNKNOWN);
	}
	if (p != NULL)
		free(p);
	return (e);
}

void
usage(void)
{
	fprintf(stderr, "Usage: %s [-1CFRSTdlrt] <path>...\n", program_name);
	exit(EXIT_FAILURE);
}

int
main(int argc, char **argv)
{
	char *e;
	gfarm_stringlist paths;
	gfarm_dtypelist types;
	int i, c, exit_code = EXIT_SUCCESS;
	extern char *optarg;

	if (argc > 0)
		program_name = basename(argv[0]);
	e = gfarm_initialize(&argc, &argv);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
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
	while ((c = getopt(argc, argv, "1CFRSTdlrt")) != -1) {
		switch (c) {
		case '1': option_output_format = OF_ONE_PER_LINE; break;
		case 'C': option_output_format = OF_MULTI_COLUMN; break;
		case 'F': option_type_suffix = 1; break;
		case 'R': option_recursive = 1; break;
		case 'S': option_sort_order = SO_SIZE; break;
		case 'T': option_complete_time = 1; break;
		case 'd': option_directory_itself =  1; break;
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

	remember_dirtree();

	e = gfarm_stringlist_init(&paths);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(EXIT_FAILURE);
	}
	e = gfarm_dtypelist_init(&types);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(EXIT_FAILURE);
	}
	if (argc < 1) {
		gfarm_stringlist_add(&paths, "gfarm:.");
		gfarm_dtypelist_add(&types, GFS_DT_DIR);
	} else {
		for (i = 0; i < argc; i++) {
			int last;

			/* do not treat glob error as an error */
			gfs_glob(argv[i], &paths, &types);

			last = gfarm_dtypelist_length(&types) - 1;
			if (last >= 0 && gfarm_dtypelist_elem(&types, last) ==
			    GFS_DT_UNKNOWN) {
				/*
				 * this only happens if argv[i] doesn't
				 * contain any meta character.
				 * In such case, the number of entries which
				 * were added by gfs_glob() is 1.
				 */
				struct gfs_stat s;
				char *path = gfarm_stringlist_elem(&paths,
				    last);

				e = gfs_stat_fake(path, &s);
				if (e != NULL) {
					fprintf(stderr, "%s: %s\n", path, e);
					exit_code = EXIT_FAILURE;
					/* remove last entry */
					/* XXX: FIXME layering violation */
					free(paths.array[last]);
					paths.length--;
					types.length--;
				} else {
					GFARM_DTYPELIST_ELEM(types, last) =
					    GFARM_S_ISDIR(s.st_mode) ?
					    GFS_DT_DIR : GFS_DT_REG;
					gfs_stat_free(&s);
				}
			}
		}
	}
	if (gfarm_stringlist_length(&paths) > 0) {
		int need_newline = 0;

#if 1
		if (list(&paths, &types, &need_newline) != NULL) {
			/* warning is already printed in list() */
			exit_code = EXIT_FAILURE;
		}
#else
		for (i = 0; i < gfarm_stringlist_length(&paths); i++)
			printf("<%s>\n", gfarm_stringlist_elem(&paths, i));
#endif
	}
	return (exit_code);
}
