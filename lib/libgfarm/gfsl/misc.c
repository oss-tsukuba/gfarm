#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <ctype.h>
#include <pwd.h>
#include <pthread.h>

#include <gfarm/gfarm_config.h>
#include <gfarm/gflog.h>

#include "thrsubr.h"

#include "gfsl_config.h"
#include "gfarm_auth.h"


int
gfarmGetInt(char *str, int *val)
{
    char *ePtr = NULL;
    int ret = -1;
    int t = 1;
    char *buf = NULL;
    int tmp;
    int base = 10;
    int len;
    int neg = 1;

    switch ((int)str[0]) {
	case '-': {
	    neg = -1;
	    str++;
	    break;
	}
	case '+': {
	    str++;
	    break;
	}
    }
    if (strncmp(str, "0x", 2) == 0) {
	base = 16;
	str += 2;
    }

    buf = strdup(str);
    if (buf == NULL) {
	gflog_debug(GFARM_MSG_1000842, "strdup() failed");
	return -1;
    }
    len = strlen(buf);
    if (len == 0) {
	gflog_debug(GFARM_MSG_1000843, "Buffer length is zero");
	return -1;
    }

    if (base == 10) {
	int lC = len - 1;
	switch ((int)(buf[lC])) {
	    case 'k': case 'K': {
		t = 1024;
		buf[lC] = '\0';
		len--;
		break;
	    }
	    case 'm': case 'M': {
		t = 1024 * 1024;
		buf[lC] = '\0';
		len--;
		break;
	    }
	}
    }

    tmp = (int)strtol(buf, &ePtr, base);
    if (ePtr == (buf + len)) {
	ret = 1;
	*val = tmp * t * neg;
    }

    (void)free(buf);
    return ret;
}

/* thread safe */
int
gfarmGetToken(char *buf, char *tokens[], int max)
{
    int n = 0;
    int nonSpace = 0;
    int quote;

    while (*buf != '\0' && n < max) {
	while (isspace((int)*buf) && *buf != '\0') {
	    buf++;
	}
	if (*buf == '\0') {
	    break;
	}
	tokens[n] = buf;

	nonSpace = 0;
	spaceScan:
	while (!isspace((int)*buf) && *buf != '\0') {
	    if (*buf == '\'' || *buf == '"') {
		char *tmp;
		quote = *buf;
		tmp = buf;
		searchEndQuote:
		tmp++;
		if ((tmp = strchr(tmp, quote)) != NULL) {
		    if (*(tmp - 1) == '\\') {
			goto searchEndQuote;
		    } else {
			buf = ++tmp;
			nonSpace++;
			goto spaceScan;
		    }
		}
	    }
	    nonSpace++;
	    buf++;
	}
	if (*buf == '\0') {
	    if (nonSpace > 0) {
		n++;
	    }
	    break;
	}
	*buf = '\0';
	n++;
	if (*(buf + 1) == '\0') {
	    break;
	} else {
	    buf++;
	}
    }

    return n;
}

/* thread safe */
static char *etc_dir = NULL;
static pthread_mutex_t etc_dir_mutex = PTHREAD_MUTEX_INITIALIZER;
static const char etc_dir_diag[] = "etc_dir";

/* returned pointer should *not* be free'ed */
char *
gfarmGetEtcDir(void)
{
	char *path, *dir;
	struct stat st;
	static const char diag[] = "gfarmGetEtcDir";

	gfarm_mutex_lock(&etc_dir_mutex, etc_dir_diag, diag);
	path = etc_dir;
	gfarm_mutex_unlock(&etc_dir_mutex, etc_dir_diag, diag);
	if (path != NULL)
		return (path);

	dir = getenv(GFARM_INSTALL_DIR_ENV);
	if (dir != NULL)
		path = gfarmGetDefaultConfigPath(
			dir, GFARM_DEFAULT_INSTALL_ETC_DIR);
	else
		path = strdup(GFARM_INSTALL_ETC_DIR);
	if (path == NULL)
		gflog_error(GFARM_MSG_1004243, "no memory");
	else if (stat(path, &st) == -1 || !S_ISDIR(st.st_mode)) {
		gflog_debug(GFARM_MSG_1000844,
		    "%s: no configuration directory", path);
		free(path);
	} else {
		gfarm_mutex_lock(&etc_dir_mutex, etc_dir_diag, diag);
		etc_dir = path;
		gfarm_mutex_unlock(&etc_dir_mutex, etc_dir_diag, diag);
		return (path);
	}
	return (NULL);
}
