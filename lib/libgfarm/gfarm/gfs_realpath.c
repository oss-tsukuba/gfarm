#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#define GFARM_INTERNAL_USE
#include <gfarm/gfarm.h>

#include "gfm_client.h"
#include "lookup.h"

struct gfm_realpath_closure {
	char *path;
};

static gfarm_error_t
gfm_realpath_request(struct gfm_connection *gfm_server, void *closure)
{
	return (GFARM_ERR_NO_ERROR);
}


static gfarm_error_t
gfm_realpath_result(struct gfm_connection *gfm_server, void *closure)
{
	return (GFARM_ERR_NO_ERROR);
}


static gfarm_error_t
gfm_realpath_success(struct gfm_connection *gfm_server, void *closure,
	int type, const char *path, gfarm_ino_t ino, gfarm_uint64_t igen)
{
	gfarm_error_t e;
	char *buf, *b;
	const char *p, *q;
	const char *hostname;
	size_t len;
	int level = 0;

	len = strlen(path);
	GFARM_MALLOC_ARRAY(buf, len + 2); /* 2 for b[0]='/' + last '\0' */
	if (buf == NULL) {
		gfm_client_connection_free(gfm_server);
		return (GFARM_ERR_NO_MEMORY);
	}
	b = buf;
	p = path;
	e = GFARM_ERR_NO_ERROR;

	while (*p) {
		while (*p == '/')
			p++;
		q = p;
		while (*q != '/' && *q != 0)
			q++;
		len = q - p;
		if (len == 0) {
			if (*q != 0)
				e = GFARM_ERR_INVALID_ARGUMENT;
			break;
		}
		if (len == 1 && p[0] == '.') {
			p = q;
			continue;
		}
		if (len == 2 && p[0] == '.' && p[1] == '.') {
			if (level <= 0) {
				p = q;
				continue;
			}
			--b;
			while (*b != '/' && b > buf)
				b--;
			p = q;
			--level;
			continue;
		}
		*b++ = '/';
		memcpy(b, p, len);
		p = q;
		b += len;
		++level;
	}

	if (level == 0)
		*b++ = '/';
	*b = 0;

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002669,
		    "gfm_realpath_success(%s) : %s\n",
		    path, gfarm_error_string(e));
		goto end;
	}

	len = strlen(buf);
	hostname = gfm_client_hostname(gfm_server);
	GFARM_MALLOC_ARRAY(b, len + strlen(hostname) + GFARM_URL_PREFIX_LENGTH +
	    2 + 1 + 5 + 1);
	    /* (gfarm:=PREFIX_LENGTH)(//=2)(:=1)(port=5)(\0=1) */
	if (b == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto end;
	}
	sprintf(b, "%s//%s:%d%s", GFARM_URL_PREFIX,
	    hostname, gfm_client_port(gfm_server), buf);
	((struct gfm_realpath_closure *)closure)->path = b;
end:
	free(buf);
	gfm_client_connection_free(gfm_server);
	return (e);
}


gfarm_error_t
gfs_realpath(const char *path, char **resolved)
{
	gfarm_error_t e;
	struct gfm_realpath_closure closure;

	closure.path = NULL;
	if ((e = gfm_inode_op_readonly(path, GFARM_FILE_LOOKUP,
	    gfm_realpath_request,
	    gfm_realpath_result,
	    gfm_realpath_success,
	    NULL,
	    &closure)) == GFARM_ERR_NO_ERROR) {
		*resolved = closure.path;
	}
	return (e);
}

