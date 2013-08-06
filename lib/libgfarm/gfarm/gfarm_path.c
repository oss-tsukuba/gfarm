#include <gfarm/gfarm_config.h>

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_XATTR_H
#include <sys/xattr.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <gfarm/gfarm.h>

#ifdef XATTR_NOFOLLOW /* Mac OS X */
#define lgetxattr(path, name, value, size) \
	getxattr(path, name, value, size, 0, XATTR_NOFOLLOW)
#endif

gfarm_error_t
gfarm_realpath_by_gfarm2fs(const char *path, char **pathp)
{
#ifdef HAVE_SYS_XATTR_H
	char *p, *parent = NULL;
	const char *base;
	static const char gfarm2fs_url[] = "gfarm2fs.url";
	size_t s = 0;
	int saved_errno, base_len = 0;
#ifdef __GNUC__ /* workaround gcc warning: may be used uninitialized */
	base = NULL;
#endif
	s = lgetxattr(path, gfarm2fs_url, NULL, 0);
	if (s == -1) {
		parent = gfarm_url_dir(path);
		if (parent == NULL)
			return (GFARM_ERR_NO_MEMORY);
		s = lgetxattr(parent, gfarm2fs_url, NULL, 0);
		if (s == -1) {
			free(parent);
			return (GFARM_ERR_NO_SUCH_OBJECT);
		}
		base = gfarm_url_dir_skip(path);
		base_len = strlen(base) + 1; /* 1 for '/' */
		path = parent;
	}
	if (pathp == NULL) {
		free(parent);
		return (GFARM_ERR_NO_ERROR);
	}
	GFARM_MALLOC_ARRAY(p, s + base_len + 1); /* 1 for '\0' */
	if (p == NULL)
		return (GFARM_ERR_NO_MEMORY);
	if (lgetxattr(path, gfarm2fs_url, p, s) == -1) {
		saved_errno = errno;
		free(p);
		return (gfarm_errno_to_error(saved_errno));
	}
	free(parent);
	p[s] = '\0';
	if (base_len > 0) {
		if (s > 0 && p[s - 1] != '/')
			strcat(p, "/");
		strcat(p, base);
	}
	*pathp = p;
	return (GFARM_ERR_NO_ERROR);
#else
	return (GFARM_ERR_NO_SUCH_OBJECT);
#endif
}
