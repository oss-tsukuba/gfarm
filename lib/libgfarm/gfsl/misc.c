#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <ctype.h>
#include <pwd.h>

#include <gfarm/gfarm_config.h>
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
	return -1;
    }
    len = strlen(buf);
    if (len == 0) {
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


int
gfarmGetToken(buf, tokens, max)
     char *buf;
     char *tokens[];
     int max;
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


char *
gfarmGetEtcDir()
{
    char buf[PATH_MAX];
    char *dir = getenv(GFARM_INSTALL_DIR_ENV), *path;
    struct stat st;

    if (dir != NULL) {
#ifdef HAVE_SNPRINTF
	snprintf(buf, sizeof buf, "%s/%s", dir, GFARM_DEFAULT_INSTALL_ETC_DIR);
#else
	sprintf(buf, "%s/%s", dir, GFARM_DEFAULT_INSTALL_ETC_DIR);
#endif
	path = buf;
    } else {
	path = GFARM_INSTALL_ETC_DIR;
    }

    if (stat(path, &st) == 0 &&
	S_ISDIR(st.st_mode)) {
	return strdup(path);
    }

    return NULL;
}

