#include <gfarm/gfarm_config.h>

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_XATTR_H
#include <sys/xattr.h>
#endif
#include <stdlib.h>
#include <errno.h>

#include <gfarm/gfarm.h>

gfarm_error_t
gfarm_realpath_by_gfarm2fs(const char *path, char **pathp)
{
	char *p;
	size_t s = 0;
	int saved_errno;

#ifdef HAVE_SYS_XATTR_H
	s = lgetxattr(path, "gfarm2fs.path", NULL, 0);
	if (s == -1)
		return (GFARM_ERR_NO_SUCH_OBJECT);
	if (pathp == NULL)
		return (GFARM_ERR_NO_ERROR);
	GFARM_MALLOC_ARRAY(p, s + 1);
	if (p == NULL)
		return (GFARM_ERR_NO_MEMORY);
	if (lgetxattr(path, "gfarm2fs.path", p, s) == -1) {
		saved_errno = errno;
		free(p);
		return (gfarm_errno_to_error(saved_errno));
	}
	p[s] = '\0';
	*pathp = p;
	return (GFARM_ERR_NO_ERROR);
#else
	return (GFARM_ERR_NO_SUCH_OBJECT);
#endif
}
