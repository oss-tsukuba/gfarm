#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <openssl/evp.h>
#include <gfarm/gfarm.h>
#include "hash.h"
#include "gfs_pio.h"	/* gfarm_path_expand_home */
#include "gfutil.h"

static char *gfarm_current_working_directory;

char *
gfs_mkdir(const char *pathname, gfarm_mode_t mode)
{
	char *user, *canonic_path, *e;
	struct gfarm_path_info pi;
	struct timeval now;
	mode_t mask;

	user = gfarm_get_global_username();
	if (user == NULL)
		return ("unknown user");

	e = gfarm_url_make_path_for_creation(pathname, &canonic_path);
	/* We permit missing gfarm: prefix here as a special case */
	if (e == GFARM_ERR_GFARM_URL_PREFIX_IS_MISSING)
		e = gfarm_canonical_path_for_creation(pathname, &canonic_path);
	if (e != NULL)
		return (e);

	if (gfarm_path_info_get(canonic_path, &pi) == NULL) {
		gfarm_path_info_free(&pi);
		free(canonic_path);
		return (GFARM_ERR_ALREADY_EXISTS);
	}

	mask = umask(0);
	umask(mask);
	mode &= ~mask;

	gettimeofday(&now, NULL);
	pi.pathname = canonic_path;
	pi.status.st_mode = (GFARM_S_IFDIR | mode);
	pi.status.st_user = user;
	pi.status.st_group = "*"; /* XXX for now */
	pi.status.st_atimespec.tv_sec =
	pi.status.st_mtimespec.tv_sec =
	pi.status.st_ctimespec.tv_sec = now.tv_sec;
	pi.status.st_atimespec.tv_nsec =
	pi.status.st_mtimespec.tv_nsec =
	pi.status.st_ctimespec.tv_nsec = now.tv_usec * 1000;
	pi.status.st_size = 0;
	pi.status.st_nsections = 0;

	e = gfarm_path_info_set(canonic_path, &pi);
	free(canonic_path);

	gfs_uncachedir();

	return (e);
}

char *
gfs_rmdir(const char *pathname)
{
	char *canonic_path, *e;
	struct gfarm_path_info pi;
	GFS_Dir dir;
	struct gfs_dirent *entry;

	e = gfarm_url_make_path_for_creation(pathname, &canonic_path);
	/* We permit missing gfarm: prefix here as a special case */
	if (e == GFARM_ERR_GFARM_URL_PREFIX_IS_MISSING)
		e = gfarm_canonical_path_for_creation(pathname, &canonic_path);
	if (e != NULL)
		return (e);

	e = gfarm_path_info_get(canonic_path, &pi);
	if (e != NULL) {
		free(canonic_path);
		return (e);
	}
	if (!GFARM_S_ISDIR(pi.status.st_mode)) {
		free(canonic_path);
		gfarm_path_info_free(&pi);
		return (GFARM_ERR_NOT_A_DIRECTORY);
	}
	gfarm_path_info_free(&pi);

	e = gfs_opendir(pathname, &dir);
	if (e != NULL) {
		free(canonic_path);
		return (e);
	}
	e = gfs_readdir(dir, &entry);
	if (e != NULL) {
		free(canonic_path);
		return (e);
	}
	if (entry != NULL) {
		free(canonic_path);
		return (GFARM_ERR_DIRECTORY_NOT_EMPTY);
	}

	e = gfarm_path_info_remove(canonic_path);
	free(canonic_path);

	gfs_uncachedir();

	return (e);
}

char *
gfs_chdir_canonical(const char *canonic_dir)
{
	char *new_dir;

	if (gfarm_current_working_directory != NULL) {
		free(gfarm_current_working_directory);
		gfarm_current_working_directory = NULL;
	}
	new_dir = malloc(strlen(canonic_dir) + 2);
	if (new_dir == NULL)
		return (GFARM_ERR_NO_MEMORY);
	sprintf(new_dir, "/%s", canonic_dir);
	gfarm_current_working_directory = new_dir;

	return (NULL);
}

char *
gfs_chdir(const char *dir)
{
	char *e, *canonic_path;

	e = gfarm_canonical_path(gfarm_url_prefix_skip(dir), &canonic_path);
	if (e != NULL)
		return (e);
	e = gfs_chdir_canonical(canonic_path);
	free (canonic_path);
	return (e);
}

char *
gfs_getcwd(char *cwd, int cwdsize)
{
	const char *path;
	char *default_cwd = NULL, *e, *p;
	int len;
	
	if (gfarm_current_working_directory != NULL)
		path = gfarm_current_working_directory;
	else if ((path = getenv("GFS_PWD")) != NULL)
		path = gfarm_url_prefix_skip(path);
	else { /* default case, use user's home directory */
		char *e;

		e = gfarm_path_expand_home("~", &default_cwd);
		if (e != NULL)
			return (e);
		path = default_cwd;
	}

	/* check the existence */
	e = gfarm_canonical_path(path, &p);
	if (e != NULL)
		goto finish;
	free(p);

	len = strlen(path);
	if (len < cwdsize) {
		strcpy(cwd, path);
		e = NULL;
	} else {
		e = GFARM_ERR_NUMERICAL_RESULT_OUT_OF_RANGE;
	}
finish:

	if (default_cwd != NULL)
		free(default_cwd);

	return (e);
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

static struct node *root;

static struct node *
init_node_name(struct node *n, const char *name, int len)
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

static struct node *
init_dir_node(struct node *n, const char *name, int len)
{
	if (init_node_name(n, name, len) == NULL)
		return (NULL);
	n->is_dir = 1;
	n->u.d.children = gfarm_hash_table_alloc(NODE_HASH_SIZE,
	    gfarm_hash_default, gfarm_hash_key_equal_default);
	return (n);
}

#define FILE_NODE_SIZE (sizeof(struct node) - sizeof(union node_u))

static struct node *
init_file_node(struct node *n, const char *name, int len)
{
	if (init_node_name(n, name, len) == NULL)
		return (NULL);
	n->is_dir = 0;
	return (n);
}

static struct node *
lookup_node(struct node *parent, const char *name,
	    int len, int is_dir, int create)
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
static char *
lookup_relative(struct node *n, const char *path, int is_dir, int create,
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
static char *
lookup_path(const char *path, int is_dir, int create, struct node **np)
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
		e = lookup_relative(root, cwd, 1, 0, &n);
		if (e != NULL)
			return (e);
	}
	return (lookup_relative(n, path, is_dir, create, np));
}

static char *
root_node(void)
{
	root = malloc(DIR_NODE_SIZE);
	if (root == NULL)
		return (GFARM_ERR_NO_MEMORY);
	init_dir_node(root, "", 0);
	root->parent = root;
	return (NULL);
}

static void
remember_path(void *closure, struct gfarm_path_info *info)
{
	lookup_relative(root, info->pathname,
	    GFARM_S_ISDIR(info->status.st_mode), 1, NULL);
}

static char *
gfs_cachedir(void)
{
	char *e;

	if (root != NULL)
		return (NULL);

	e = root_node();
	if (e != NULL)
		return (e);
	gfarm_path_info_get_all_foreach(remember_path, NULL);
	return (NULL);
}

static void
free_node(struct node *n)
{
	if (n->is_dir) {
		struct gfarm_hash_iterator i;
		struct gfarm_hash_entry *child;

		for (gfarm_hash_iterator_begin(n->u.d.children, &i);
		    (child = gfarm_hash_iterator_access(&i)) != NULL;
		    gfarm_hash_iterator_next(&i)) {
			free_node(gfarm_hash_entry_data(child));
		}
		gfarm_hash_table_free(n->u.d.children);
	}
	free(n->name);
}

/* to inhibit dirctory uncaching while some directories are opened */
static int opendir_count = 0;
static int need_to_clear_cache = 0;

void
gfs_uncachedir(void)
{
	if (opendir_count > 0) { /* We cannot clear cache for now */
		need_to_clear_cache = 1; /* schedule it later */
	} else if (root != NULL) {
		need_to_clear_cache = 0;
		free_node(root);
		free(root);
		root = NULL;
	}
}

struct timeval gfarm_dircache_timeout = { 60, 0 }; /* default 60sec. */
static struct timeval last_dircache = {0, 0};

static char *
gfs_refreshdir(void)
{
	static int initialized = 0;
	struct timeval now, elapsed;
	char *s;

	if (!initialized) {
		if ((s = getenv("GFARM_DIRCACHE_TIMEOUT")) != NULL)
			gfarm_dircache_timeout.tv_sec = atoi(s);
		gfarm_dircache_timeout.tv_usec = 0;
		initialized = 1;
	}
	gettimeofday(&now, NULL);
	if (root == NULL) {
		/* assert(need_to_clear_cache == 0); */
		last_dircache = now;
		return (gfs_cachedir());
	}
	if (opendir_count > 0) /* We cannot clear cache for now */
		return (NULL);
	elapsed = now;
	gfarm_timeval_sub(&elapsed, &last_dircache);
	if (need_to_clear_cache ||
	    gfarm_timeval_cmp(&elapsed, &gfarm_dircache_timeout) >= 0) {
		need_to_clear_cache = 0;
		gfs_uncachedir();
		last_dircache = now;
		return (gfs_cachedir());
	}
	return (NULL);
}

/*
 * 'path' is '/' + canonical path, or a relative path.  It is not the
 * same as a canonical path.
 */
char *
gfs_realpath_canonical(const char *path, char **abspathp)
{
	struct node *n, *p;
	char *e, *abspath;
	int l, len;

	e = gfs_refreshdir();
	if (e != NULL) 
		return (e);
	e = lookup_path(path, -1, 0, &n);
	if (e != NULL)
		return (e);
	len = 0;
	for (p = n; p != root; p = p->parent) {
		if (p != n)
			++len; /* for '/' */
		len += strlen(p->name);
	}
	abspath = malloc(len + 1);
	if (abspath == NULL)
		return (GFARM_ERR_NO_MEMORY);
	abspath[len] = '\0';
	for (p = n; p != root; p = p->parent) {
		if (p != n)
			abspath[--len] = '/';
		l = strlen(p->name);
		len -= l;
		memcpy(abspath + len, p->name, l);
	}
	*abspathp = abspath;
	return (NULL);
}

char *gfs_get_ino(const char *canonical_path, long *inop)
{
	struct node *n;
	char *e;
	
	e = gfs_refreshdir();
	if (e != NULL) 
		return (e);
	e = lookup_relative(root, canonical_path, -1, 0, &n);
        if (e != NULL)
		return (e);
	*inop = (long)n;
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
gfs_opendir(const char *path, GFS_Dir *dirp)
{
	char *e, *canonic_path, *abspath;
	struct node *n;
	struct gfs_dir *dir;

	path = gfarm_url_prefix_skip(path);
	e = gfarm_canonical_path(path, &canonic_path);
	if (e != NULL)
		return (e);

	abspath = malloc(strlen(canonic_path) + 2);
	if (abspath == NULL) {
		free(canonic_path);
		return (GFARM_ERR_NO_MEMORY);
	}
	sprintf(abspath, "/%s", canonic_path);
	free(canonic_path);

	e = lookup_path(abspath, 1, 0, &n);
	free(abspath);
	if (e != NULL)
		return (e);

	dir = malloc(sizeof(struct gfs_dir));
	if (dir == NULL)
		return (GFARM_ERR_NO_MEMORY);
	dir->dir = n;
	gfarm_hash_iterator_begin(n->u.d.children, &dir->iterator);
	*dirp = dir;

	++opendir_count;
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
	--opendir_count;
	return (NULL);
}
