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

#include "dircache.h"

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

	return (e);
}

char *
gfs_rmdir(const char *pathname)
{
	char *canonic_path, *e, *e_tmp;
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
	if (e != NULL)
		goto error_free_canonic_path;

	if (!GFARM_S_ISDIR(pi.status.st_mode)) {
		gfarm_path_info_free(&pi);
		e = GFARM_ERR_NOT_A_DIRECTORY;
		goto error_free_canonic_path;
	}
	gfarm_path_info_free(&pi);

	e = gfs_opendir(pathname, &dir);
	if (e == NULL) {
		while ((e = gfs_readdir(dir, &entry)) == NULL) {
			if (entry == NULL) {
				/* OK, remove the directory */
				e = gfarm_path_info_remove(canonic_path);
				break;
			}
			if ((entry->d_namlen == 1 &&
			     entry->d_name[0] == '.') ||
			    (entry->d_namlen == 2 &&
			     entry->d_name[0] == '.' &&
			     entry->d_name[1] == '.'))
				continue;
			/* Not OK */
			e = GFARM_ERR_DIRECTORY_NOT_EMPTY;
			break;
		}

		e_tmp = gfs_closedir(dir);
		if (e == NULL)
			e = e_tmp;
	}
 error_free_canonic_path:
	free(canonic_path);
	return (e);
}

char *
gfs_chdir_canonical(const char *canonic_dir)
{
	static char *dir;
	static int dir_len;
	int len;

	len = GFARM_URL_PREFIX_LENGTH + 1 + strlen(canonic_dir) + 1;
	if (dir_len < len) {
		dir = realloc(dir, len);
		if (dir == NULL) {
			dir_len = 0;
			return (GFARM_ERR_NO_MEMORY);
		}
		dir_len = len;
	}

	sprintf(dir, "%s/%s", GFARM_URL_PREFIX, canonic_dir);
	gfarm_current_working_directory = dir + GFARM_URL_PREFIX_LENGTH;
	setenv("GFS_PWD", dir, 1);

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
	int flags;
#define		NODE_FLAG_IS_DIR	1
#define		NODE_FLAG_MARKED	2
#define		NODE_FLAG_PURGED	4	/* removed, and to be freed */
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
	n->flags = NODE_FLAG_IS_DIR;
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
	n->flags = 0;
#if 1
	/*
	 * We hold this even on a file_node,
	 * this field can be non-NULL, if this node is changed from
	 * a dir_node to a file_node.
	 */
	n->u.d.children = NULL;
#endif
	return (n);
}

static void
change_file_node_to_dir(struct node *n)
{
	n->flags |= NODE_FLAG_IS_DIR;
	if (n->u.d.children == NULL) {
		n->u.d.children = gfarm_hash_table_alloc(NODE_HASH_SIZE,
		    gfarm_hash_default, gfarm_hash_key_equal_default);
	}
}

static void
change_dir_node_to_file(struct node *n)
{
	n->flags &= ~NODE_FLAG_IS_DIR;
}

static void
for_each_node(struct node *n, void (*f)(void *, struct node *), void *cookie)
{
#if 0
	if ((n->flags & NODE_FLAG_IS_DIR) != 0)
#else
	if (n->u.d.children != NULL)
#endif
	{
		struct gfarm_hash_iterator i;
		struct gfarm_hash_entry *child;

		for (gfarm_hash_iterator_begin(n->u.d.children, &i);
		    (child = gfarm_hash_iterator_access(&i)) != NULL;
		    gfarm_hash_iterator_next(&i)) {
			for_each_node(gfarm_hash_entry_data(child), f, cookie);
		}
	}
	(*f)(cookie, n);
}

static void
free_node(void *cookie, struct node *n)
{
#if 0
	if ((n->flags & NODE_FLAG_IS_DIR) != 0)
#else
	if (n->u.d.children != NULL)
#endif
		gfarm_hash_table_free(n->u.d.children);
	free(n->name);
}

static void
recursive_free_nodes(struct node *n)
{
	for_each_node(n, free_node, NULL);
}

static void
delayed_purge_node(void *cookie, struct node *n)
{
	n->flags |= NODE_FLAG_PURGED;
}

static void
recursive_delayed_purge_nodes(struct node *n)
{
	for_each_node(n, delayed_purge_node, NULL);
}

static void
recursive_free_children(struct node *n)
{
	struct gfarm_hash_iterator i;
	struct gfarm_hash_entry *child;

	for (gfarm_hash_iterator_begin(n->u.d.children, &i);
	    (child = gfarm_hash_iterator_access(&i)) != NULL;
	    gfarm_hash_iterator_next(&i)) {
		recursive_free_nodes(gfarm_hash_entry_data(child));
	}
	gfarm_hash_table_free(n->u.d.children);
	n->u.d.children = NULL;
}

enum gfarm_node_lookup_op {
	GFARM_INODE_LOOKUP,
	GFARM_INODE_CREATE,
	GFARM_INODE_REMOVE,
	GFARM_INODE_MARK
};

/* to inhibit dirctory uncaching while some directories are opened */
static int opendir_count = 0;

/*
 * if (op != GFARM_INODE_CREATE), (is_dir) may be -1,
 * and that means "don't care".
 */
char *
lookup_node(struct node *parent, const char *name,
	int len, int is_dir, enum gfarm_node_lookup_op op,
	struct node **np)
{
	struct gfarm_hash_entry *entry;
	int created, already_purged;
	struct node *n;

	if ((parent->flags & NODE_FLAG_IS_DIR) == 0)
		return (GFARM_ERR_NOT_A_DIRECTORY);
	if (len == 0) {
		/* We don't handle GFARM_INODE_MARK for this case */
		if (op == GFARM_INODE_REMOVE)
			return (GFARM_ERR_INVALID_ARGUMENT);
		*np = parent;
		return (NULL);
	} else if (len == 1 && name[0] == '.') {
		/* We don't handle GFARM_INODE_MARK for this case */
		if (op == GFARM_INODE_REMOVE)
			return (GFARM_ERR_INVALID_ARGUMENT);
		*np = parent;
		return (NULL);
	} else if (len == 2 && name[0] == '.' && name[1] == '.') {
		/* We don't handle GFARM_INODE_MARK for this case */
		if (op == GFARM_INODE_REMOVE)
			return (GFARM_ERR_DIRECTORY_NOT_EMPTY);
		*np = parent->parent;
		return (NULL);
	}
	if (len > GFS_MAXNAMLEN)
		len = GFS_MAXNAMLEN;
	if (op == GFARM_INODE_MARK) {
		entry = gfarm_hash_lookup(parent->u.d.children, name, len);
		/* We should not honor the PURGED flag here */
		if (entry != NULL) {
			n = gfarm_hash_entry_data(entry);
			if ((n->flags & NODE_FLAG_IS_DIR) == is_dir) {
				/* abandon the PURGED flag at the mark phase */
				n->flags &= ~NODE_FLAG_PURGED;
				n->flags |= NODE_FLAG_MARKED;
				*np = n;
				return (NULL);
			}
			if (opendir_count > 0) {
				if (is_dir) {
					change_file_node_to_dir(n);
				} else {
					recursive_delayed_purge_nodes(n);
					change_dir_node_to_file(n);
				}
				/* abandon the PURGED flag at the mark phase */
				n->flags &= ~NODE_FLAG_PURGED;
				n->flags |= NODE_FLAG_MARKED;
				*np = n;
				return (NULL);
			}
			recursive_free_nodes(n);
			gfarm_hash_purge(parent->u.d.children, name, len);
		}
		/* do create */
	} else if (op != GFARM_INODE_CREATE) {
		entry = gfarm_hash_lookup(parent->u.d.children, name, len);
		if (entry == NULL)
			return (GFARM_ERR_NO_SUCH_OBJECT);
		n = gfarm_hash_entry_data(entry);
		already_purged = (n->flags & NODE_FLAG_PURGED) != 0;
		if (already_purged || op == GFARM_INODE_REMOVE) {
			if (opendir_count > 0) {
				recursive_delayed_purge_nodes(n);
			} else {
				recursive_free_nodes(n);
				gfarm_hash_purge(parent->u.d.children,
				    name, len);
			}
			if (already_purged)
				return (GFARM_ERR_NO_SUCH_OBJECT);
			*np = NULL;
			return (NULL);
		}
		*np = n;
		return (NULL);
	}

	entry = gfarm_hash_enter(parent->u.d.children, name, len,
#if 0
	    is_dir ? DIR_NODE_SIZE : FILE_NODE_SIZE,
#else
	/*
	 * always allocate DIR_NODE_SIZE
	 * to make it possible to change a file to a dir
	 */
	    DIR_NODE_SIZE,
#endif
	    &created);
	if (entry == NULL)
		return (GFARM_ERR_NO_MEMORY);
	n = gfarm_hash_entry_data(entry);
	if (!created) {
		n->flags &= ~NODE_FLAG_PURGED;
		/* assert(op == GFARM_INODE_CREATE); */
		*np = n;
		return (NULL);
	}
	if (is_dir)
		init_dir_node(n, name, len);
	else
		init_file_node(n, name, len);
	n->parent = parent;
	if (op == GFARM_INODE_MARK)
		n->flags |= NODE_FLAG_MARKED;
	*np = n;
	return (NULL);
}

/*
 * is_dir must be -1 (don't care), 0 (not a dir) or NODE_FLAG_IS_DIR.
 *
 * if (op != GFARM_INODE_CREATE), (is_dir) may be -1,
 * and that means "don't care".
 */
static char *
lookup_relative(struct node *n, const char *path, int is_dir,
	enum gfarm_node_lookup_op op, struct node **np)
{
	char *e;
	int len;

	if ((n->flags & NODE_FLAG_IS_DIR) == 0)
		return (GFARM_ERR_NOT_A_DIRECTORY);
	for (;;) {
		while (*path == '/')
			path++;
		for (len = 0; path[len] != '/'; len++) {
			if (path[len] == '\0') {
				e = lookup_node(n, path, len, is_dir, op, &n);
				if (e != NULL)
					return (e);
				if (is_dir != -1 &&
				    (n->flags & NODE_FLAG_IS_DIR) != is_dir)
					return ((n->flags & NODE_FLAG_IS_DIR) ?
					    GFARM_ERR_IS_A_DIRECTORY :
					    GFARM_ERR_NOT_A_DIRECTORY);
				if (np != NULL)
					*np = n;
				return (NULL);
			}
		}
		e = lookup_node(n, path, len, NODE_FLAG_IS_DIR,
		    op == GFARM_INODE_MARK ?
		    GFARM_INODE_MARK : GFARM_INODE_LOOKUP, &n);
		if (e != NULL)
			return (e);
		if ((n->flags & NODE_FLAG_IS_DIR) == 0)
			return (GFARM_ERR_NOT_A_DIRECTORY);
		path += len;
	}
}

/*
 * if (op != GFARM_INODE_CREATE), (is_dir) may be -1,
 * and that means "don't care".
 */
static char *
lookup_path(const char *path, int is_dir, enum gfarm_node_lookup_op op,
	struct node **np)
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
		e = lookup_relative(root, cwd, NODE_FLAG_IS_DIR,
		    GFARM_INODE_LOOKUP, &n);
		if (e != NULL)
			return (e);
	}
	return (lookup_relative(n, path, is_dir, op, np));
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

#if 0 /* not used */
static void
free_all_nodes(void)
{
	if (root != NULL) {
		recursive_free_nodes(root);
		free(root);
		root = NULL;
	}
}
#endif

static void
remember_path(void *closure, struct gfarm_path_info *info)
{
	lookup_relative(root, info->pathname,
	    GFARM_S_ISDIR(info->status.st_mode) ? NODE_FLAG_IS_DIR : 0,
	    GFARM_INODE_MARK, NULL);
}

static char *
gfs_cachedir(void)
{
	char *e;

	/* assert(root == NULL); */
	e = root_node();
	if (e != NULL)
		return (e);
	gfarm_path_info_get_all_foreach(remember_path, NULL);
	return (NULL);
}

static void
sweep_nodes(struct node *n)
{
	struct gfarm_hash_iterator i;
	struct gfarm_hash_entry *child;

	/* assert((n->flags & NODE_FLAG_IS_DIR) != 0); */

	/*
	 * We don't have to honor the PURGED flag here,
	 * because the mark phase overrides the flag.
	 */

	for (gfarm_hash_iterator_begin(n->u.d.children, &i);
	    (child = gfarm_hash_iterator_access(&i)) != NULL;
	    gfarm_hash_iterator_next(&i)) {
		struct node *c = gfarm_hash_entry_data(child);

		if ((c->flags & NODE_FLAG_MARKED) == 0) {
			if (opendir_count > 0) {
				recursive_delayed_purge_nodes(c);
			} else {
				recursive_free_nodes(c);
				gfarm_hash_iterator_purge(&i);
			}
		} else {
			if ((c->flags & NODE_FLAG_IS_DIR) != 0)
				sweep_nodes(c);
			else if (opendir_count == 0 && c->u.d.children != NULL)
				recursive_free_children(c);
			c->flags &= ~NODE_FLAG_MARKED;
		}
	}
}

static void
mark_path(void *closure, struct gfarm_path_info *info)
{
	lookup_relative(root, info->pathname,
	    GFARM_S_ISDIR(info->status.st_mode) ? NODE_FLAG_IS_DIR : 0,
	    GFARM_INODE_MARK, NULL);
}

static char *
gfs_recachedir(void)
{
	/* assert(root != NULL); */
	gfarm_path_info_get_all_foreach(mark_path, NULL);
	sweep_nodes(root);
	return (NULL);
}

/* refresh directories as soon as possible */
static int need_to_clear_cache = 0;

void
gfs_uncachedir(void)
{
	need_to_clear_cache = 1;
}

char *
gfs_dircache_enter_dir(const char *gfarm_file)
{
	if (root == NULL) /* not cached yet, no need to handle */
		return (NULL);
	return (lookup_relative(root, gfarm_file, NODE_FLAG_IS_DIR,
	    GFARM_INODE_CREATE, NULL));
}

char *
gfs_dircache_enter_file(const char *gfarm_file)
{
	if (root == NULL) /* not cached yet, no need to handle */
		return (NULL);
	return (lookup_relative(root, gfarm_file, 0,
	    GFARM_INODE_CREATE, NULL));
}

char *
gfs_dircache_purge_path(const char *gfarm_file)
{
	if (root == NULL) /* not cached yet, no need to handle */
		return (NULL);
	return (lookup_relative(root, gfarm_file, -1,
	    GFARM_INODE_REMOVE, NULL));
}

struct timeval gfarm_dircache_timeout = { 10, 0 }; /* default 10sec. */
static struct timeval last_dircache = {0, 0};

void
gfs_dircache_set_timeout(struct gfarm_timespec *timeout)
{
	gfarm_dircache_timeout.tv_sec = timeout->tv_sec;
	gfarm_dircache_timeout.tv_usec = timeout->tv_nsec / 1000;
}

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
		need_to_clear_cache = 0;
		last_dircache = now;
		return (gfs_cachedir());
	}
	if (need_to_clear_cache) {
		need_to_clear_cache = 0;
		last_dircache = now;
		return (gfs_recachedir());
	}
	elapsed = now;
	gfarm_timeval_sub(&elapsed, &last_dircache);
	if (gfarm_timeval_cmp(&elapsed, &gfarm_dircache_timeout) >= 0) {
		need_to_clear_cache = 0;
		last_dircache = now;
		return (gfs_recachedir());
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
	e = lookup_path(path, -1, GFARM_INODE_LOOKUP, &n);
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

#define INUMBER(node)	((long)(node))

char *
gfs_get_ino(const char *canonical_path, long *inop)
{
	struct node *n;
	char *e;
	
	e = gfs_refreshdir();
	if (e != NULL) 
		return (e);
	e = lookup_relative(root, canonical_path, -1, GFARM_INODE_LOOKUP, &n);
        if (e != NULL)
		return (e);
	*inop = INUMBER(n);;
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
	/* gfs_refreshdir() will be called from here */
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

	e = lookup_path(abspath, NODE_FLAG_IS_DIR, GFARM_INODE_LOOKUP, &n);
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
	/* XXX if someone removed a path, while opening a directory... */
	return (NULL);
}

char *
gfs_readdir(GFS_Dir dir, struct gfs_dirent **entry)
{
	struct gfarm_hash_entry *he;
	struct node *n;

	for (;;) {
		he = gfarm_hash_iterator_access(&dir->iterator);
		if (he == NULL) {
			*entry = NULL;
			return (NULL);
		}
		n = gfarm_hash_entry_data(he);
		gfarm_hash_iterator_next(&dir->iterator);
		if ((n->flags & NODE_FLAG_PURGED) == 0)
			break;
	}
	dir->buffer.d_fileno = INUMBER(n);
	dir->buffer.d_type = (n->flags & NODE_FLAG_IS_DIR) ?
	    GFS_DT_DIR : GFS_DT_REG;
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

/*
 * gfs_dirname()
 */

char *
gfs_dirname(GFS_Dir dir)
{
  	return (dir->dir->name);
}
