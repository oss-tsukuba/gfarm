#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <gfarm/gfarm.h>
#include "hash.h"

char *
gfs_getcwd(char *cwd, int cwdsize)
{
	char *path;
	int *len;
	
	if ((path = getenv("GFS_PWD")) != NULL)
		path = gfarm_url_prefix_skip(path);
	else
		path = gfarm_get_global_username();
	len = strlen(path);
	if (len < cwdsize) {
		strcpy(cwd, path);
	} else {
		memcpy(cwd, path, cwdsize - 1);
		cwd[cwdsize - 1] = '\0';
	}
	return (NULL);
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

static struct node *
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

static struct node *
init_file_node(struct node *n, char *name, int len)
{
	if (init_node_name(n, name, len) == NULL)
		return (NULL);
	n->is_dir = 0;
	return (n);
}

static struct node *
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
static char *
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
static char *
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

void
gfs_uncachedir(void)
{
	if (root != NULL) {
		free_node(root);
		free(root);
		root = NULL;
	}
}

char *
gfs_realpath(char *path, char **abspathp)
{
	struct node *n, *p;
	char *e, *abspath;
	int l, len;

	e = gfs_cachedir();
	if (e != NULL) 
		return (e);
	e = lookup_path(path, -1, 0, &n);
	if (e != NULL)
		return (e);
	len = 0;
	for (p = n; p != root; p = p->parent)
		len += strlen(p->name) + 1;
	len += GFARM_URL_PREFIX_LENGTH;
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
	memcpy(abspath, GFARM_URL_PREFIX, GFARM_URL_PREFIX_LENGTH);
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

	e = gfs_cachedir();
	if (e != NULL) 
		return (e);
	path = gfarm_url_prefix_skip(path);
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

static char *
gfs_stat_sub(char *gfarm_url, struct gfs_stat *s)
{
	char *e, *gfarm_file;
	int i, nsections;
	struct gfarm_file_section_info *sections;
	struct gfarm_path_info pi;

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL)
		return (e);
	e = gfarm_path_info_get(gfarm_file, &pi);
	if (e != NULL) {
		free(gfarm_file);
		return (e);
	}

	*s = pi.status;
	s->st_user = strdup(s->st_user);
	s->st_group = strdup(s->st_group);
	gfarm_path_info_free(&pi);
	if (s->st_user == NULL || s->st_group == NULL) {
		gfs_stat_free(s);
		free(gfarm_file);
		return (GFARM_ERR_NO_MEMORY);
	}

	if (!GFARM_S_ISREG(s->st_mode)) {
		free(gfarm_file);
		return (NULL);
	}

	e = gfarm_file_section_info_get_all_by_file(gfarm_file,
	    &nsections, &sections);
	free(gfarm_file);
	if (e != NULL) {
		gfs_stat_free(s);
		/*
		 * If GFARM_ERR_NO_SUCH_OBJECT is returned here,
		 * gfs_stat() incorrectly assumes that this is a directory,
		 * and reports GFARM_ERR_NOT_A_DIRECTORY.
		 */
		return ("no fragment information");
	}

	s->st_size = 0;
	for (i = 0; i < nsections; i++)
		s->st_size += sections[i].filesize;
	s->st_nsections = nsections;

	gfarm_file_section_info_free_all(nsections, sections);
	return (NULL);
}

char *
gfs_stat(char *path, struct gfs_stat *s)
{
	char *e, *p;
	struct node *n;

	e = gfs_cachedir();
	if (e != NULL) 
		return (e);
	path = gfarm_url_prefix_skip(path);
	e = gfs_realpath(path, &p);
	if (e != NULL)
		return (e);
	e = gfs_stat_sub(p, s);
	free(p);
	if (e == NULL)
		return (NULL);
	if (e != GFARM_ERR_NO_SUCH_OBJECT)
		return (e);
	/* XXX - assume that it's a directory. */
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

void
gfs_stat_free(struct gfs_stat *s)
{
	if (s->st_user != NULL)
		free(s->st_user);
	if (s->st_group != NULL)
		free(s->st_group);
}

