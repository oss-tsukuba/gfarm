/*
 * $Id$
 */

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <openssl/evp.h>

#if defined(__GNUC__) || defined(__STDC_VERSION__) && __STDC_VERSION__>=199901L
# define gfarm_inline	inline
#else
# define gfarm_inline
#endif


#define USE_HASH	1

#include <gfarm/gfarm.h>

#if USE_HASH
#include "hash.h"
#else
#include "tree.h"
#endif

#include "gfutil.h"

#include "metadb_access.h"
#include "gfs_misc.h"	/* gfarm_path_expand_home() */


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
	pi.status.st_ctimespec.tv_nsec =
	    now.tv_usec * GFARM_MILLISEC_BY_MICROSEC;
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

	e = gfarm_url_make_path(pathname, &canonic_path);
	/* We permit missing gfarm: prefix here as a special case */
	if (e == GFARM_ERR_GFARM_URL_PREFIX_IS_MISSING)
		e = gfarm_canonical_path(pathname, &canonic_path);
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
				continue; /* "." or ".." */
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
	static int cwd_len = 0;
	static char env_name[] = "GFS_PWD=";
	static char *env = NULL;
	static int env_len = 0;
	int len;
	char *tmp;

	len = 1 + strlen(canonic_dir) + 1;
	if (cwd_len < len) {
		tmp = realloc(gfarm_current_working_directory, len);
		if (tmp == NULL)
			return (GFARM_ERR_NO_MEMORY);
		gfarm_current_working_directory = tmp;
		cwd_len = len;
	}
	sprintf(gfarm_current_working_directory, "/%s", canonic_dir);

	len += sizeof(env_name) - 1 + GFARM_URL_PREFIX_LENGTH;
	tmp = getenv("GFS_PWD");
	if (tmp == NULL || tmp != env + sizeof(env_name) - 1) {
		/* changed by an application instead of this func */
		/* probably already free()ed.  In this case realloc
		 * does not work well at least using bash.  allocate again. */
		env = NULL;
		env_len = 0;
	}
	if (env_len < len) {
		tmp = realloc(env, len);
		if (tmp == NULL)
			return (GFARM_ERR_NO_MEMORY);
		env = tmp;
		env_len = len;
	}
	sprintf(env, "%s%s%s",
	    env_name, GFARM_URL_PREFIX, gfarm_current_working_directory);

	if (putenv(env) != 0)
		return (gfarm_errno_to_error(errno));

	return (NULL);
}

char *
gfs_chdir(const char *dir)
{
	char *e, *canonic_path;
	struct gfs_stat st;

	if ((e = gfs_stat(dir, &st)) != NULL)
		return (e);
	if (!GFARM_S_ISDIR(st.st_mode)) {
		gfs_stat_free(&st);
		return (GFARM_ERR_NOT_A_DIRECTORY);
	}
	gfs_stat_free(&st);

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

#if USE_HASH

typedef struct gfarm_hash_table *Dir;
typedef struct gfarm_hash_iterator DirIterator;

#else /* !USE_HASH */

RB_HEAD(rb_dir, node);

typedef struct rb_dir Dir;
typedef struct node *DirIterator;

#endif /* !USE_HASH */

struct node {
#if !USE_HASH
	RB_ENTRY(node) rb_dir_entry;
	int namelen;
#endif

	struct node *parent;
	char *name;
	int flags;
#define		NODE_FLAG_IS_DIR	1
#define		NODE_FLAG_MARKED	2
#define		NODE_FLAG_PURGED	4	/* removed, and to be freed */
#define		NODE_FLAG_INVALID	8
	union node_u {
		struct dir {
			Dir children;

			struct timeval mtime;
		} d;
	} u;
};

static gfarm_inline struct node *
init_node_name_primitive(struct node *n, const char *name, int len)
{
	n->name = malloc(len + 1);
	if (n->name == NULL)
		return (NULL);
	memcpy(n->name, name, len);
	n->name[len] = '\0';
	return (n);
}

static void recursive_free_nodes(struct node *);

#if USE_HASH

#define NODE_HASH_SIZE 53 /* prime */

static struct node *
init_node_name(struct node *n, const char *name, int len)
{
	return (init_node_name_primitive(n, name, len));
}

static gfarm_inline int
dir_init(Dir *dirp)
{
	Dir d = gfarm_hash_table_alloc(NODE_HASH_SIZE,
	    gfarm_hash_default, gfarm_hash_key_equal_default);

	if (d == NULL)
		return (0);
	*dirp = d;
	return (1);
}

static gfarm_inline void
dir_init_empty(Dir *dirp)
{
	*dirp = NULL;
}

static gfarm_inline int
dir_make_valid(Dir *dirp)
{
	if (*dirp == NULL) {
		*dirp = gfarm_hash_table_alloc(NODE_HASH_SIZE,
		    gfarm_hash_default, gfarm_hash_key_equal_default);
		/* XXX check GFARM_ERR_NO_MEMORY */
		return (1);
	}
	return (0);
}

static struct node *
dir_lookup(struct node *dir, const char *name, int len)
{
	struct gfarm_hash_entry *entry;

	entry = gfarm_hash_lookup(dir->u.d.children, name, len);
	if (entry == NULL)
		return (NULL);
	return (gfarm_hash_entry_data(entry));
}

static void
dir_for_each(struct node *n, void (*f)(void *, struct node *), void *cookie)
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
			dir_for_each(gfarm_hash_entry_data(child), f, cookie);
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

static gfarm_inline void
purge_node(struct node *parent, struct node *n, const char *name, int len)
{
	recursive_free_nodes(n);
	gfarm_hash_purge(parent->u.d.children, name, len);
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

static void
dir_iterator_init(DirIterator *iterator, Dir *dir)
{
	gfarm_hash_iterator_begin(*dir, iterator);
}

static struct node *
dir_iterator_next(DirIterator *iterator, Dir *dir)
{
	struct gfarm_hash_entry *he = gfarm_hash_iterator_access(iterator);
	struct node *n;

	if (he == NULL)
		return (NULL);
	n = gfarm_hash_entry_data(he);
	gfarm_hash_iterator_next(iterator);
	return (n);	
}

#else /* !USE_HASH */

static struct node *
init_node_name(struct node *n, const char *name, int len)
{
	if (init_node_name_primitive(n, name, len) == NULL)
		return (NULL);
	n->namelen = len;
	return (n);
}

static gfarm_inline int
node_compare(struct node *a, struct node *b)
{
	int len = a->namelen < b->namelen ? a->namelen : b->namelen;
	int cmp;

#if 0
	cmp = memcmp(a->name, b->name, len);
#else	/* to reduce number of function calls */
	if (len == 0) { /* shouldn't happen? */
		cmp = 0;
	} else {
		if (a->name[0] < b->name[0])
			return (-1);
		else if (a->name[0] > b->name[0])
			return (1);
		if (len == 1)
			cmp = 0;
		else
			cmp = memcmp(a->name + 1, b->name + 1, len - 1);
#endif
	}
	if (cmp != 0 || a->namelen == b->namelen)
		return (cmp);
	if (a->namelen < b->namelen)
		return (-1);
	else
		return (1);
}

RB_PROTOTYPE(rb_dir, node, rb_dir_entry, node_compare)
RB_GENERATE(rb_dir, node, rb_dir_entry, node_compare)

static gfarm_inline int
dir_init(Dir *dirp)
{
	RB_INIT(dirp);
	return (1);
}

static gfarm_inline void
dir_init_empty(Dir *dirp)
{
	RB_INIT(dirp);
}

static gfarm_inline int
dir_make_valid(Dir *dirp)
{
	if (RB_EMPTY(dirp)) {
		RB_INIT(dirp);
		return (1);
	}
	return (0);
}

static struct node *
dir_lookup(struct node *dir, const char *name, int len)
{
	struct node entry;

	entry.name = (char *)name; /* XXX UNCONST */
	entry.namelen = len;
	return (RB_FIND(rb_dir, &dir->u.d.children, &entry));
}

static void
dir_for_each(struct node *n, void (*f)(void *, struct node *), void *cookie)
{
#if 0
	if ((n->flags & NODE_FLAG_IS_DIR) != 0)
#else
	if (!RB_EMPTY(&n->u.d.children))
#endif
	{
		struct node *child;

		RB_FOREACH(child, rb_dir, &n->u.d.children) {
			dir_for_each(child, f, cookie);
		}
	}

	(*f)(cookie, n);
}

static void
free_node(void *cookie, struct node *n)
{
	/*
	 * free_all_nodes() doesn't work with this,
	 * because this implementation cannot free the root node.
	 * but it's OK, because free_all_nodes() is currently commented out.
	 */
	RB_REMOVE(rb_dir, &n->parent->u.d.children, n);
	free(n->name);
	free(n);
}

static gfarm_inline void
purge_node(struct node *parent, struct node *n, const char *name, int len)
{
	recursive_free_nodes(n);
}

static void
recursive_free_children(struct node *n)
{
	struct node *child;

	RB_FOREACH(child, rb_dir, &n->u.d.children) {
		recursive_free_nodes(child);
	}
}

static void
dir_iterator_init(DirIterator *iterator, Dir *dir)
{
	*iterator = RB_MIN(rb_dir, dir);
}


static struct node *
dir_iterator_next(DirIterator *iterator, Dir *dir)
{
	struct node *n = *iterator;

	if (n == NULL)
		return (NULL);
	*iterator = RB_NEXT(rb_dir, dir, n);
	return (n);
}

#endif /* !USE_HASH */

static struct node *root;

#define DIR_NODE_SIZE \
	(sizeof(struct node) - sizeof(union node_u) + sizeof(struct dir))

static struct node *
init_dir_node(struct node *n, const char *name, int len)
{
	if (init_node_name(n, name, len) == NULL)
		return (NULL);
	n->flags = NODE_FLAG_IS_DIR;
	if (!dir_init(&n->u.d.children)) {
		free(n);
		return (NULL);
	}
	n->u.d.mtime.tv_sec = n->u.d.mtime.tv_usec = 0;
	return (n);
}

/* FILE_NODE_SIZE is not really used now */
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
	dir_init_empty(&n->u.d.children);
#endif
	return (n);
}

static void
change_file_node_to_dir(struct node *n)
{
	n->flags |= NODE_FLAG_IS_DIR;
	if (dir_make_valid(&n->u.d.children)) {
		n->u.d.mtime.tv_sec = n->u.d.mtime.tv_usec = 0;
	}
}

static void
change_dir_node_to_file(struct node *n)
{
	n->flags &= ~NODE_FLAG_IS_DIR;
}

static void
delayed_purge_node(void *cookie, struct node *n)
{
	n->flags |= NODE_FLAG_PURGED;
}

static void
recursive_delayed_purge_nodes(struct node *n)
{
	dir_for_each(n, delayed_purge_node, NULL);
}

static void
recursive_free_nodes(struct node *n)
{
	dir_for_each(n, free_node, NULL);
}

/* to inhibit dirctory uncaching while some directories are opened */
static int opendir_count = 0;

#if USE_HASH

static char *
enter_node(struct node *parent, const char *name, int len, int is_dir,
	struct node **np, int *createdp)
{
	struct gfarm_hash_entry *entry;
	struct node *n;

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
	    createdp);
	if (entry == NULL)
		return (GFARM_ERR_NO_MEMORY);

	n = gfarm_hash_entry_data(entry);
	if (*createdp) {
		if (is_dir)
			init_dir_node(n, name, len);
		else
			init_file_node(n, name, len);
		n->parent = parent;
	}

	*np = n;
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

	/* cached entries in this directory are now valid */
	n->flags &= ~NODE_FLAG_INVALID;
}

#else /* !USE_HASH */

static char *
enter_node(struct node *parent, const char *name, int len, int is_dir,
	struct node **np, int *createdp)
{
	struct node entry, *n;

	entry.name = (char *)name; /* XXX UNCONST */
	entry.namelen = len;
	n = RB_FIND(rb_dir, &parent->u.d.children, &entry);
	if (n != NULL) {
		*createdp = 0;
		*np = n;
		return (NULL);
	}
	n = malloc(
#if 0
	    is_dir ? DIR_NODE_SIZE : FILE_NODE_SIZE
#else
	/*
	 * always allocate DIR_NODE_SIZE
	 * to make it possible to change a file to a dir
	 */
	    DIR_NODE_SIZE
#endif
	);
	if (n == NULL)
		return (GFARM_ERR_NO_MEMORY);

	if (is_dir)
		init_dir_node(n, name, len);
	else
		init_file_node(n, name, len);
	n->parent = parent;
	RB_INSERT(rb_dir, &parent->u.d.children, n);

	*createdp = 1;
	*np = n;
	return (NULL);
}

static void
sweep_nodes(struct node *n)
{
	struct node *c;

	RB_FOREACH(c, rb_dir, &n->u.d.children) {
		if ((c->flags & NODE_FLAG_MARKED) == 0) {
			if (opendir_count > 0) {
				recursive_delayed_purge_nodes(c);
			} else {
				recursive_free_nodes(c);
			}
		} else {
			if ((c->flags & NODE_FLAG_IS_DIR) != 0)
				sweep_nodes(c);
			else if (opendir_count == 0 &&
			    !RB_EMPTY(&c->u.d.children))
				recursive_free_children(c);
			c->flags &= ~NODE_FLAG_MARKED;
		}
	}

	/* cached entries in this directory are now valid */
	n->flags &= ~NODE_FLAG_INVALID;
}

#endif /* !USE_HASH */

enum gfarm_node_lookup_op {
	GFARM_INODE_LOOKUP,
	GFARM_INODE_CREATE,
	GFARM_INODE_REMOVE,
	GFARM_INODE_MARK
};

/*
 * if (op != GFARM_INODE_CREATE), (is_dir) may be -1,
 * and that means "don't care".
 */
char *
lookup_node(struct node *parent, const char *name,
	int len, int is_dir, enum gfarm_node_lookup_op op,
	struct node **np)
{
	char *e;
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
		n = dir_lookup(parent, name, len);
		if (n != NULL) {
		/* We should not honor the PURGED flag here */
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
			purge_node(parent, n, name, len);
		}
		/* do create */
	} else if (op != GFARM_INODE_CREATE) {
		n = dir_lookup(parent, name, len);
		if (n == NULL)
			return (GFARM_ERR_NO_SUCH_OBJECT);
		already_purged = (n->flags & NODE_FLAG_PURGED) != 0;
		if (already_purged || op == GFARM_INODE_REMOVE) {
			if (opendir_count > 0) {
				recursive_delayed_purge_nodes(n);
			} else {
				purge_node(parent, n, name, len);
			}
			if (already_purged)
				return (GFARM_ERR_NO_SUCH_OBJECT);
			*np = NULL;
			return (NULL);
		}
		*np = n;
		return (NULL);
	}

	e = enter_node(parent, name, len, is_dir, &n, &created);
	if (e != NULL)
		return (e);
	if (!created) {
		n->flags &= ~NODE_FLAG_PURGED;
		/* assert(op == GFARM_INODE_CREATE); */
		*np = n;
		return (NULL);
	}
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

static char *
gfs_dircache_modify_parent(const char *pathname)
{
	char *e = NULL;
	char *parent = strdup(pathname), *b;
	struct gfarm_path_info pi;
	struct timeval now;

	if (parent == NULL)
		return (GFARM_ERR_NO_MEMORY);

	/* create parent directory canonic path */
	for (b = (char *)gfarm_path_dir_skip(parent);
	    b > parent && b[-1] == '/'; --b)
		;
	*b = '\0';

	/* NOTE: We don't have path_info for the root directory */
	if (b > parent &&
	    (e = gfarm_metadb_path_info_get(parent, &pi)) == NULL) {
		gettimeofday(&now, NULL);
		pi.status.st_mtimespec.tv_sec = now.tv_sec;
		pi.status.st_mtimespec.tv_nsec = now.tv_usec *
		    GFARM_MILLISEC_BY_MICROSEC;
		/* the following calls gfs_dircache_enter_path() internally */
		e = gfarm_path_info_replace(parent, &pi);
		gfarm_path_info_free(&pi);
	}
	free(parent);
	return (e);
}

static char *
gfs_dircache_enter_path(enum gfarm_node_lookup_op op,
	const char *pathname, struct gfarm_path_info *info)
{
	struct node *n;
	char *e = lookup_relative(root, info->pathname,
	    GFARM_S_ISDIR(info->status.st_mode) ? NODE_FLAG_IS_DIR : 0,
	    op, &n);

	if (e != NULL)
		return (e);
	if (GFARM_S_ISDIR(info->status.st_mode)) {
		n->u.d.mtime.tv_sec = info->status.st_mtimespec.tv_sec;
		n->u.d.mtime.tv_usec = info->status.st_mtimespec.tv_nsec /
		    GFARM_MILLISEC_BY_MICROSEC;
	}
	return (NULL);
}

static char *
gfs_dircache_purge_path(const char *pathname)
{
	return (lookup_relative(root, pathname, -1,
	    GFARM_INODE_REMOVE, NULL));
}

static void
mark_path(void *closure, struct gfarm_path_info *info)
{
	gfs_dircache_enter_path(GFARM_INODE_MARK, info->pathname, info);
}

/* refresh directories as soon as possible */
static int need_to_clear_cache = 0;

struct timeval gfarm_dircache_timeout = { 86400, 0 }; /* 1 day */
static struct timeval last_dircache = {0, 0};

static char *
gfs_cachedir(struct timeval *now)
{
	char *e;

	/* assert(root != NULL); */
	e = gfarm_metadb_path_info_get_all_foreach(mark_path, NULL);
	if (e != NULL && e != GFARM_ERR_NO_SUCH_OBJECT)
		return (e);
	sweep_nodes(root);
	need_to_clear_cache = 0;
	last_dircache = *now;
	return (NULL);
}

void
gfs_i_uncachedir(void)
{
	need_to_clear_cache = 1;
}

void
gfs_dircache_set_timeout(struct gfarm_timespec *timeout)
{
	gfarm_dircache_timeout.tv_sec = timeout->tv_sec;
	gfarm_dircache_timeout.tv_usec =
	    timeout->tv_nsec / GFARM_MILLISEC_BY_MICROSEC;
}

static char *
gfs_refreshdir(void)
{
	char *e, *s;
	static int initialized = 0;
	struct timeval now, elapsed;

	if (!initialized) {
		if ((s = getenv("GFARM_DIRCACHE_TIMEOUT")) != NULL)
			gfarm_dircache_timeout.tv_sec = atoi(s);
		gfarm_dircache_timeout.tv_usec = 0;
		initialized = 1;
	}
	gettimeofday(&now, NULL);
	if (root == NULL) {
		e = root_node();
		if (e != NULL)
			return (e);
		return (gfs_cachedir(&now));
	}
	if (need_to_clear_cache)
		return (gfs_cachedir(&now));
	elapsed = now;
	gfarm_timeval_sub(&elapsed, &last_dircache);
	if (gfarm_timeval_cmp(&elapsed, &gfarm_dircache_timeout) >= 0)
		return (gfs_cachedir(&now));
	return (NULL);
}

/*
 * metadatabase interface wrappers provided by dircache layer.
 */

static char *
root_path_info(struct gfarm_path_info *info)
{
	long ino;
	char *e;

	e = gfs_get_ino("", &ino);
	if (e != NULL)
		return (e);
	info->pathname = strdup("");
	if (info->pathname == NULL)
		return (GFARM_ERR_NO_MEMORY);
	info->status.st_ino = ino;
	info->status.st_mode = GFARM_S_IFDIR | 0777;
	info->status.st_user = strdup("root");
	info->status.st_group = strdup("gfarm");
	info->status.st_atimespec.tv_sec = 0;
	info->status.st_atimespec.tv_nsec = 0;
	info->status.st_mtimespec.tv_sec = 0;
	info->status.st_mtimespec.tv_nsec = 0;
	info->status.st_ctimespec.tv_sec = 0;
	info->status.st_ctimespec.tv_nsec = 0;
	info->status.st_size = 0;
	info->status.st_nsections = 0;

	return (NULL);
}

char *
gfarm_i_path_info_get(const char *pathname, struct gfarm_path_info *info)
{
	char *e = gfs_refreshdir(), *e2;
	struct node *n;

	if (e != NULL)
		return (e);

	/* 'root' is a special case not having the metadata */
	if (*pathname == '\0')
		return (root_path_info(info));

	/* real metadata */
	e = gfarm_metadb_path_info_get(pathname, info);
	if (e != NULL && e != GFARM_ERR_NO_SUCH_OBJECT)
		return (e); /* Can't connect LDAP server */

	/* cached metadata */
	e2 = lookup_relative(root, pathname,
	    e != NULL ? -1 :
	    GFARM_S_ISDIR(info->status.st_mode) ? NODE_FLAG_IS_DIR : 0,
	    GFARM_INODE_LOOKUP, &n);

	/* both real and cache do not exist */
	if (e != NULL && e2 != NULL)
		return (e);

	if (e == NULL && e2 == NULL) {
		if (GFARM_S_ISDIR(info->status.st_mode) !=
		    ((n->flags & NODE_FLAG_IS_DIR) != 0)) {
			gfs_i_uncachedir();
			e = gfs_refreshdir();
		}
		else if ((n->flags & NODE_FLAG_IS_DIR) != 0 &&
		    (n->u.d.mtime.tv_sec != info->status.st_mtimespec.tv_sec ||
		     n->u.d.mtime.tv_usec != info->status.st_mtimespec.tv_nsec
		     / GFARM_MILLISEC_BY_MICROSEC)) {
			/* directory entry is modified. need to refresh later */
			n->flags |= NODE_FLAG_INVALID;
		}
		if (e != NULL)
			gfarm_path_info_free(info);
	}
	else if (e == NULL) {
		if (gfs_dircache_enter_path(
			    GFARM_INODE_CREATE, pathname, info) != NULL) {
			gfs_i_uncachedir();
			e = gfs_refreshdir();
		}
		else if (GFARM_S_ISDIR(info->status.st_mode)) {
			e = lookup_relative(root, pathname,
				NODE_FLAG_IS_DIR, GFARM_INODE_LOOKUP, &n);
			if (e == NULL)
				n->flags |= NODE_FLAG_INVALID;
		}
		if (e != NULL)
			gfarm_path_info_free(info);
	}
	else /* if (e2 == NULL) */ {
		if (gfs_dircache_purge_path(pathname) != NULL) {
			gfs_i_uncachedir();
			gfs_refreshdir();
		}
	}
	return (e);
}

char *
gfarm_i_path_info_set(char *pathname, struct gfarm_path_info *info)
{
	char *e = gfs_refreshdir();

	if (e != NULL)
		return (e);
	e = gfarm_metadb_path_info_set(pathname, info);
	if (e == NULL) {
		gfs_dircache_enter_path(GFARM_INODE_CREATE, pathname, info);
		gfs_dircache_modify_parent(pathname);
	}
	return (e);
}

char *
gfarm_i_path_info_replace(char *pathname, struct gfarm_path_info *info)
{
	char *e = gfs_refreshdir();

	if (e != NULL)
		return (e);
	e = gfarm_metadb_path_info_replace(pathname, info);
	if (e == NULL) {
		e = gfs_dircache_enter_path(GFARM_INODE_LOOKUP,
		    pathname, info);
		if (e != NULL) {
			gfs_i_uncachedir();
			e = gfs_refreshdir();
		}
	}
	return (e);
}

char *
gfarm_i_path_info_remove(const char *pathname)
{
	char *e = gfs_refreshdir();

	if (e != NULL)
		return (e);

	e = gfarm_metadb_path_info_remove(pathname);
	if (e == NULL) {
		gfs_dircache_purge_path(pathname);
		gfs_dircache_modify_parent(pathname);
	}
	return (e);
}

/*
 * 'path' is '/' + canonical path, or a relative path.  It is not the
 * same as a canonical path.
 */
char *gfs_i_realpath_canonical(const char *, char **);

static char *
canonical_pathname(const struct node *n, char **abspathp)
{
	const struct node *p;
	char *abspath;
	int l, len;

	*abspathp = NULL;

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

/*
 * Canonicalize a path using canonicalized pathname of the parent
 * directory and the basename.  Note that the content of 'path1' will
 * be modified.
 */
static char *
gfs_realpath_canonical_candidate(
	char *path1, const char *path2, char **abspathp)
{
	char *e, *p_dir, *abspath;
	const char *base;
	int final = 0;
	struct node *n;

	if (path1 == NULL || *path1 == '\0')
		return (GFARM_ERR_NO_SUCH_OBJECT);

	base = gfarm_path_dir_skip(path1);
	if (*base == '\0' && path1 < base) {
		/* remove unnecessary '/'s following the basename. */
		--base;
		while (path1 < base && *base == '/')
			--base;
		if (path1 == base) /* '//////' */
			return (GFARM_ERR_NO_SUCH_OBJECT);
		path1[base - path1 + 1] = '\0';
		base = gfarm_path_dir_skip(path1);
	}
	if (base == path1) {
		path1 = ".";
		final = 1;
	}
	else if (base == path1 + 1) {
		path1 = "/";
		final = 1;
	}
	else
		path1[base - path1 - 1] = '\0';

	if (path2 != NULL)
		path1[path2 - path1 - 1] = '/';

	e = lookup_path(path1, NODE_FLAG_IS_DIR, GFARM_INODE_LOOKUP, &n);
	if (e != NULL) {
		if (!final)
			return (gfs_realpath_canonical_candidate(
					path1, base, abspathp));
		else
			return (e);
	}
	e = canonical_pathname(n, &p_dir);
	if (e != NULL)
		return (e);

	abspath = malloc(strlen(p_dir) + 1 + strlen(base) + 1);
	if (abspath == NULL) {
		free(p_dir);
		return (GFARM_ERR_NO_MEMORY);
	}
	sprintf(abspath, "%s/%s", p_dir, base);
	free(p_dir);

	*abspathp = abspath;
	return (NULL);
}

char *
gfs_i_realpath_canonical(const char *path, char **abspathp)
{
	struct node *n;
	char *e;

	(void)gfs_refreshdir();
	/*
	 * Even when a metadb server is down, allow read-only access
	 * to cached metadata
	 */

	e = lookup_path(path, -1, GFARM_INODE_LOOKUP, &n);
	if (e != NULL) {
		char *path1, *c_path;
		struct gfarm_path_info info;
		/*
		 * Before uncaching the metadata, try to canonicalize
		 * using canonicalized pathname of the parent directory.
		 */
		path1 = strdup(path);
		if (path1 == NULL)
			return (GFARM_ERR_NO_MEMORY);
		e = gfs_realpath_canonical_candidate(path1, NULL, &c_path);
		free(path1);
		if (e != NULL)
			return (e);
		e = gfarm_i_path_info_get(c_path, &info);
		free(c_path);
		if (e != NULL)
			return (e);
		gfarm_path_info_free(&info);

		/* directory cache is already refreshed in path_info_get(). */
		e = lookup_path(path, -1, GFARM_INODE_LOOKUP, &n);
	}
	if (e != NULL)
		return (e);
	return (canonical_pathname(n, abspathp));
}

#define INUMBER(node)	((long)(node))

char *
gfs_i_get_ino(const char *canonical_path, long *inop)
{
	struct node *n;
	char *e;
	
	(void)gfs_refreshdir();
	/*
	 * Even when a metadb server is down, allow read-only access
	 * to cached metadata
	 */

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
	DirIterator iterator;
	struct gfs_dirent buffer;
	int index;
};

char *
gfs_i_opendir(const char *path, GFS_Dir *dirp)
{
	char *e, *canonic_path;
	struct node *n;
	struct gfs_dir *dir;

	e = gfarm_canonical_path(gfarm_url_prefix_skip(path), &canonic_path);
	if (e != NULL)
		return (e);

	e = lookup_relative(root, canonic_path, NODE_FLAG_IS_DIR,
	    GFARM_INODE_LOOKUP, &n);
	free(canonic_path);
	if (e != NULL)
		return (e);

	/* here, refresh directory cache */
	if (n->flags & NODE_FLAG_INVALID) {
		gfs_i_uncachedir();
		e = gfs_refreshdir();
		if (e != NULL)
			return (e);
		/* NODE_FLAG_INVALID is reset in gfs_cachedir() */
	}

	dir = malloc(sizeof(struct gfs_dir));
	if (dir == NULL)
		return (GFARM_ERR_NO_MEMORY);
	dir->dir = n;
	dir_iterator_init(&dir->iterator, &n->u.d.children);
	dir->index = 0;
	*dirp = dir;

	++opendir_count;
	/* XXX if someone removed a path, while opening a directory... */
	return (NULL);
}

#define GFS_DIRENTSIZE	0x100	/* XXX */

char *
gfs_i_readdir(GFS_Dir dir, struct gfs_dirent **entry)
{
	struct node *n;

	if (dir->index == 0) {
		n = dir->dir;
		dir->buffer.d_namlen = 1;
		dir->buffer.d_name[0] = '.';
		dir->index++;
	} else if (dir->index == 1) {
		n = dir->dir->parent;
		dir->buffer.d_namlen = 2;
		dir->buffer.d_name[0] = dir->buffer.d_name[1] = '.';
		dir->index++;
	} else {
		for (;;) {
			n = dir_iterator_next(&dir->iterator,
			    &dir->dir->u.d.children);
			if (n == NULL) {
				*entry = NULL;
				return (NULL);
			}
			dir->index++;
			if ((n->flags & NODE_FLAG_PURGED) == 0)
				break;
		}
		dir->buffer.d_namlen = strlen(n->name);
		memcpy(dir->buffer.d_name, n->name, dir->buffer.d_namlen);
	}
	dir->buffer.d_name[dir->buffer.d_namlen] = '\0';
	dir->buffer.d_type = (n->flags & NODE_FLAG_IS_DIR) ?
	    GFS_DT_DIR : GFS_DT_REG;
	dir->buffer.d_reclen = GFS_DIRENTSIZE; /* XXX */
	dir->buffer.d_fileno = INUMBER(n);
	*entry = &dir->buffer;
	return (NULL);
}

char *
gfs_i_closedir(GFS_Dir dir)
{
	free(dir);
	--opendir_count;
	return (NULL);
}

char *
gfs_i_seekdir(GFS_Dir dir, file_offset_t off)
{
	char *e;
	int new_index;
	struct gfs_dirent *ent;

	if (off < 0)
		return (GFARM_ERR_INVALID_ARGUMENT);
	new_index = off / GFS_DIRENTSIZE;
	if (new_index < dir->index) {
		/* rewind */
		dir_iterator_init(&dir->iterator, &dir->dir->u.d.children);
		dir->index = 0;
	}
	while (dir->index < new_index) {
		e = gfs_i_readdir(dir, &ent);
		if (e != NULL)
			return (e);
		if (ent == NULL)
			break; /* always OK beyond EOF for now */
	}
	return (NULL); 
}

char *
gfs_i_telldir(GFS_Dir dir, file_offset_t *offp)
{
	*offp = dir->index * GFS_DIRENTSIZE;
	return (NULL);
}

/*
 * gfs_dirname()
 */

char *
gfs_i_dirname(GFS_Dir dir)
{
  	return (dir->dir->name);
}
