#include <gfarm/gfarm_config.h>

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_XATTR_H
#include <sys/xattr.h>
#endif
#include <stdlib.h>

#include <gfarm/gfarm.h>

/* if return value is not path, need to free it */
char *
gfarm_path(char *path)
{
	char *p;
	size_t s = 0;

#ifdef HAVE_SYS_XATTR_H
	s = lgetxattr(path, "gfarm2fs.path", NULL, 0);
	if (s == -1)
		return (path);
	GFARM_MALLOC_ARRAY(p, s + 1);
	if (p == NULL || lgetxattr(path, "gfarm2fs.path", p, s) == -1) {
		free(p);
		return (path);
	}
	p[s] = '\0';
	return (p);
#else
	return (path);
#endif
}
