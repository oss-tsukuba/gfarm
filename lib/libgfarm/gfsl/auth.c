#include <pthread.h>
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
#include <gfarm/gflog.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>

#include "hash.h"
#include "gfutil.h"
#include "thrsubr.h"

#include "gfsl_config.h"
#include "gfarm_auth.h"
#include "misc.h"

#define AUTH_TABLE_SIZE       139
static struct gfarm_hash_table *authTable = NULL;

#if GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS
static struct gfarm_hash_table *userToDNTable = NULL;
#endif

static pthread_mutex_t authTable_mutex = PTHREAD_MUTEX_INITIALIZER;
static const char authTableDiag[] = "authTable";
static void gfarmAuthDestroyUserEntry_unlocked(gfarmAuthEntry *);

#if 0
static void
dumpAuthEntry(gfarmAuthEntry *aePtr)
{
    if (aePtr->authType != GFARM_AUTH_USER &&
	aePtr->authType != GFARM_AUTH_HOST) {
	fprintf(stderr, "Invalid auth entry.\n");
	return;
    }
    fprintf(stderr, "Type:\t%s\n",
	    (aePtr->authType == GFARM_AUTH_USER) ? "user" : "host");
    fprintf(stderr, "DN:\t%s\n", aePtr->distName);
    if (aePtr->authType == GFARM_AUTH_USER) {
	fprintf(stderr, "Local:\t%s\n", aePtr->authData.userAuth.localName);
	fprintf(stderr, "uid:\t%d\n", aePtr->authData.userAuth.uid);
	fprintf(stderr, "gid:\t%d\n", aePtr->authData.userAuth.gid);
	fprintf(stderr, "home:\t%s\n", aePtr->authData.userAuth.homeDir);
	fprintf(stderr, "shell:\t%s\n", aePtr->authData.userAuth.loginShell);
    } else {
	fprintf(stderr, "FQDN:\t%s\n", aePtr->authData.hostAuth.FQDN);
    }
    fprintf(stderr, "\n");
}
#endif

static pthread_mutex_t authFile_mutex = PTHREAD_MUTEX_INITIALIZER;
static char *authFile = NULL;
static const char authFileDiag[] = "authFile";

static int
setAuthFile(char *usermap)
{
    char *err = NULL;
    static const char diag[] = "gfsl/setAuthFile()";

    if (usermap != NULL && usermap[0] != '\0') {
	gfarm_mutex_lock(&authFile_mutex, diag, authFileDiag);
	if (authFile != NULL)
	    free(authFile);
	authFile = strdup(usermap);
	if (authFile == NULL)
	    err = "no memory";
	gfarm_mutex_unlock(&authFile_mutex, diag, authFileDiag);
	if (err != NULL) {
	    gflog_auth_warning(GFARM_MSG_1000642, "%s: %s", diag, err);
	    return (-1);
	}
    }
    return (0);
}

static void
unsetAuthFile(void)
{
    static const char diag[] = "gfsl/unsetAuthFile()";

    gfarm_mutex_lock(&authFile_mutex, diag, authFileDiag);
    if (authFile != NULL)
	free(authFile);
    authFile = NULL;
    gfarm_mutex_unlock(&authFile_mutex, diag, authFileDiag);
}

/* returned string should be free'ed if it is not NULL */
static char *
getAuthFile(void)
{
    char *file;
    static const char diag[] = "gfsl/getAuthFile()";

    gfarm_mutex_lock(&authFile_mutex, diag, authFileDiag);
    if (authFile != NULL)
	file = strdup(authFile);
    else
	file = NULL;
    gfarm_mutex_unlock(&authFile_mutex, diag, authFileDiag);
    return (file);
}

static struct stat authFileStat;
static pthread_mutex_t authFileStat_mutex = PTHREAD_MUTEX_INITIALIZER;
static const char authFileStatDiag[] = "authFileStat";

static int
getAuthFileStat(struct stat *sb)
{
    char *file = getAuthFile();
    static const char diag[] = "gfsl/getAuthFileStat()";

    if (file == NULL) {
	gflog_auth_warning(GFARM_MSG_1000643,
	    "%s: AuthFile not set or no memory", diag);
	return (-1);
    }
    if (stat(file, sb) < 0) {
	gflog_auth_warning(GFARM_MSG_1000644, "%s: not found: %s", diag, file);
    	free(file);
	return (-1);
    }
    free(file);
    return (0);
}

static int
setAuthFileStat(void)
{
    struct stat sb;
    static const char diag[] = "gfsl/setAuthFileStat()";

    if (getAuthFileStat(&sb) < 0) {
	gflog_debug(GFARM_MSG_1000805, "getAuthFileStat() failed");
	return (-1);
    }
    gfarm_mutex_lock(&authFileStat_mutex, diag, authFileStatDiag);
    authFileStat = sb;
    gfarm_mutex_unlock(&authFileStat_mutex, diag, authFileStatDiag);
    return (0);
}

static int
checkAuthFileStat(void)
{
    struct stat sb;
    int update;
    static const char diag[] = "gfsl/checkAuthFileStat()";

    if (getAuthFileStat(&sb) < 0) {
	gflog_debug(GFARM_MSG_1000806, "getAuthFileStat() failed");
	return (-1);
    }
    gfarm_mutex_lock(&authFileStat_mutex, diag, authFileStatDiag);
    update = (authFileStat.st_mtime < sb.st_mtime);
    authFileStat = sb;
    gfarm_mutex_unlock(&authFileStat_mutex, diag, authFileStatDiag);
    return (update);
}

int
gfarmAuthInitialize(char *usermapFile)
{
    int ret = 1;
    static const char diag[] = "gfarmAuthInitialize()";

    gfarm_mutex_lock(&authTable_mutex, diag, authTableDiag);
    if (authTable == NULL) {
	char mapFile[PATH_MAX];
	FILE *mFd = NULL;
	char lineBuf[65536];
	gfarmAuthEntry *aePtr;
	struct gfarm_hash_entry *ePtr;

	/*
	 * Read global users -> local users mapping file
	 * and create a translation table.
	 */
	if (usermapFile == NULL || usermapFile[0] == '\0') {
	    char *confDir = gfarmGetEtcDir();
	    if (confDir == NULL) {
		gflog_auth_error(GFARM_MSG_1000645, "%s: no memory", diag);
		ret = -1;
		goto done;
	    }
#ifdef HAVE_SNPRINTF
	    snprintf(mapFile, sizeof mapFile, "%s/%s",
		     confDir, GFARM_DEFAULT_USERMAP_FILE);
#else
	    sprintf(mapFile, "%s/%s", confDir, GFARM_DEFAULT_USERMAP_FILE);
#endif
	    usermapFile = mapFile;
	    (void)free(confDir);
	}
	if ((mFd = fopen(usermapFile, "r")) == NULL) {
	    gflog_auth_error(GFARM_MSG_1000646, "%s: cannot open: %s",
		usermapFile, strerror(errno));
	    ret = -1;
	    goto done;
	}

	authTable = gfarm_hash_table_alloc(AUTH_TABLE_SIZE,
					   gfarm_hash_default,
					   gfarm_hash_key_equal_default);
	if (authTable == NULL) { /* no memory */
	    gflog_auth_error(GFARM_MSG_1000647, "%s: no memory", diag);
	    ret = -1;
	    goto done;
	}

        if (setAuthFile(usermapFile) == -1) {
	    gflog_debug(GFARM_MSG_1000807, "setAuthFile() failed");
	    gfarm_hash_table_free(authTable);
	    ret = -1;
	    goto done;
        }
        if (setAuthFileStat() == -1) {
	    gflog_debug(GFARM_MSG_1000808, "setAuthFileStat() failed");
	    gfarm_hash_table_free(authTable);
	    ret = -1;
	    goto done;
        }

#if GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS
	userToDNTable = gfarm_hash_table_alloc(AUTH_TABLE_SIZE,
					      gfarm_hash_default,
					      gfarm_hash_key_equal_default);
	if (userToDNTable == NULL) { /* no memory */
	    gflog_auth_error(GFARM_MSG_1000648, "%s: no memory", diag);
	    gfarm_hash_table_free(authTable);
	    ret = -1;
	    goto done;
	}
#endif /* GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS */
	while (fgets(lineBuf, sizeof lineBuf, mFd) != NULL) {
	    char *token[64];
	    int nToken = gfarmGetToken(lineBuf, token, sizeof token);
	    char *distName = NULL;
	    char *mode = NULL;
	    char *localName = NULL;
	    struct passwd *pPtr;
	    int isNew;

	    if (nToken <= 1) {
		continue;
	    }
	    if (*token[0] == '#') {
		continue;
	    }

	    distName = token[0];
	    if (nToken == 2) {
		mode = "@user@";
		localName = token[1];
	    } else if (nToken >= 3) {
		mode = token[1];
		localName = token[2];
	    } else if (nToken == 0) {
		continue;
	    } else {
		gflog_warning(GFARM_MSG_1000649,
		    "%s: WARNING: missing local username for DN."
			      " Ignored.", distName);
		continue;
	    }

	    /*
	     * Unquote distinguished name
	     */
	    if (distName[0] == '\'' || distName[0] == '"') {
		int quote = distName[0];
		int dLen = strlen(distName);
		dLen--;
		if (distName[dLen] == quote) {
		    distName[dLen] = '\0';
		    distName++;
		}
	    }

	    if (strcmp(mode, "@user@") == 0) {
		pPtr = getpwnam(localName);
		if (pPtr == NULL) {
		    gflog_auth_info(GFARM_MSG_1000650,
			"%s: WARNING: Account doesn't exist. Ignored.",
			localName);
		    continue;
		}
		if (pPtr->pw_uid == 0) {
		    gflog_warning(GFARM_MSG_1000651,
			"%s: WARNING: This user is a super user."
				  " Ignored.", localName);
		    continue;
		}
		GFARM_MALLOC(aePtr);
		if (aePtr == NULL) {
		    gflog_auth_error(GFARM_MSG_1000652, "%s: no memory", diag);
		    ret = -1;
		    goto initDone;
		}
		(void)memset(aePtr, 0, sizeof(gfarmAuthEntry));

		aePtr->sesRefCount = 0;
		aePtr->orphaned = 0;
		aePtr->authType = GFARM_AUTH_USER;
		aePtr->distName = strdup(distName);
		aePtr->authData.userAuth.localName = strdup(localName);
		aePtr->authData.userAuth.uid = pPtr->pw_uid;
		aePtr->authData.userAuth.gid = pPtr->pw_gid;
		aePtr->authData.userAuth.homeDir = strdup(pPtr->pw_dir);
		aePtr->authData.userAuth.loginShell = strdup(pPtr->pw_shell);
#if GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS
		ePtr = gfarm_hash_enter(userToDNTable, localName,
					strlen(localName) + 1,
					sizeof(aePtr), &isNew);
		if (ePtr == NULL) { /* no memory */
		    gflog_warning(GFARM_MSG_1000653,
			"%s: WARNING: no memory for DN. Ignored.",
				  localName);
		} else if (!isNew) {
		    gflog_auth_warning(GFARM_MSG_1000654,
			"%s: WARNING: multiple X.509 Distinguish name "
			"for a UNIX user account. Ignored.", localName);
		} else {
		    *(gfarmAuthEntry **)gfarm_hash_entry_data(ePtr) = aePtr;
		}
#endif /* GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS */
	    } else if (strcmp(mode, "@host@") == 0) {
		GFARM_MALLOC(aePtr);
		if (aePtr == NULL) {
		    gflog_auth_error(GFARM_MSG_1000655, "%s: no memory", diag);
		    ret = -1;
		    goto initDone;
		}
		(void)memset(aePtr, 0, sizeof(gfarmAuthEntry));

		aePtr->sesRefCount = 0;
		aePtr->orphaned = 0;
		aePtr->authType = GFARM_AUTH_HOST;
		aePtr->distName = strdup(distName);
		aePtr->authData.hostAuth.FQDN = strdup(localName);
	    } else {
		gflog_warning(GFARM_MSG_1000656,
		    "%s: WARNING: Unknown keyword at second field."
			      " Ignored.", localName);
		continue;
	    }

	    ePtr = gfarm_hash_enter(authTable, aePtr->distName,
				    strlen(aePtr->distName) + 1,
				    sizeof(aePtr), &isNew);
	    if (ePtr == NULL) { /* no memory */
		gflog_warning(GFARM_MSG_1000657,
		    "%s: WARNING: no memory for DN. Ignored.",
			      distName);
#if GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS
		if (aePtr->authType == GFARM_AUTH_USER)
		    gfarm_hash_purge(userToDNTable,
				     localName, strlen(localName) + 1);
#endif /* GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS */
		aePtr->orphaned = 1;
		gfarmAuthDestroyUserEntry_unlocked(aePtr);
		goto initDone;
	    }
	    if (!isNew) {
		gflog_notice(GFARM_MSG_1000658,
		    "%s: duplicate DN. Ignored.", distName);
#if GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS
		if (aePtr->authType == GFARM_AUTH_USER)
		    gfarm_hash_purge(userToDNTable,
				     localName, strlen(localName) + 1);
#endif /* GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS */
		aePtr->orphaned = 1;
		gfarmAuthDestroyUserEntry_unlocked(aePtr);
		continue;
	    }
	    *(gfarmAuthEntry **)gfarm_hash_entry_data(ePtr) = aePtr;
#if 0
	    dumpAuthEntry(aePtr);
#endif
	}
	initDone:
	fclose(mFd);

	if (ret == -1) {
	    /*
	     * Destroy mapping table.
	     */
	    struct gfarm_hash_iterator it;
	    for (gfarm_hash_iterator_begin(authTable, &it);
		 !gfarm_hash_iterator_is_end(&it);
		 gfarm_hash_iterator_next(&it)) {
		aePtr = *(gfarmAuthEntry **)gfarm_hash_entry_data(
			gfarm_hash_iterator_access(&it));
		aePtr->orphaned = 1;
		gfarmAuthDestroyUserEntry_unlocked(aePtr);
	    }
	    gfarm_hash_table_free(authTable);
	    authTable = NULL;
#if GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS
	    gfarm_hash_table_free(userToDNTable);
	    userToDNTable = NULL;
#endif /* GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS */
	}
    }

    done:
    gfarm_mutex_unlock(&authTable_mutex, diag, authTableDiag);
    return ret;
}


void
gfarmAuthFinalize(void)
{
    static const char diag[] = "gfarmAuthFinalize()";

    unsetAuthFile();

    gfarm_mutex_lock(&authTable_mutex, diag, authTableDiag);
    if (authTable != NULL) {
	gfarmAuthEntry *aePtr;
	struct gfarm_hash_iterator it;
	for (gfarm_hash_iterator_begin(authTable, &it);
	     !gfarm_hash_iterator_is_end(&it);
	     gfarm_hash_iterator_next(&it)) {
	    aePtr = *(gfarmAuthEntry **)gfarm_hash_entry_data(
		    gfarm_hash_iterator_access(&it));
	    if (aePtr->sesRefCount <= 0) {
		/*
		 * If any sessions reffer this entry, don't free it.
		 */
		aePtr->orphaned = 1;
		gfarmAuthDestroyUserEntry_unlocked(aePtr);
	    } else {
		aePtr->orphaned = 1;
	    }
	}
	gfarm_hash_table_free(authTable);
	authTable = NULL;
#if GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS
	gfarm_hash_table_free(userToDNTable);
	userToDNTable = NULL;
#endif /* GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS */
    }
    gfarm_mutex_unlock(&authTable_mutex, diag, authTableDiag);
}


gfarmAuthEntry *
gfarmAuthGetUserEntry(char *distUserName)
{
    gfarmAuthEntry *ret = NULL;
    static const char diag[] = "gfarmAuthGetUserEntry";

    /* update a usermap if needed */
    if (checkAuthFileStat() > 0) {
	char *usermap = getAuthFile();

	if (usermap) {
	    gfarmAuthFinalize();
	    (void)gfarmAuthInitialize(usermap);
	    free(usermap);
	}
    }

    gfarm_mutex_lock(&authTable_mutex, diag, authTableDiag);
    if (authTable != NULL) {
	struct gfarm_hash_entry *ePtr = gfarm_hash_lookup(authTable,
		distUserName, strlen(distUserName) + 1);
	if (ePtr != NULL) {
	    ret = *(gfarmAuthEntry **)gfarm_hash_entry_data(ePtr);
#if 0
	    dumpAuthEntry(ret);
#endif
	} else {
	    gflog_debug(GFARM_MSG_1000809, "lookup from authTable (%s) failed",
		distUserName);
	}
    } else {
	gflog_debug(GFARM_MSG_1000810, "authTable is NULL");
    }
    gfarm_mutex_unlock(&authTable_mutex, diag, authTableDiag);
    return ret;
}


#if GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS
gfarmAuthEntry *
gfarmAuthGetLocalUserEntry(char *localUserName)
{
    gfarmAuthEntry *ret = NULL;
    static const char diag[] = "gfarmAuthGetLocalUserEntry()";

    /* update a usermap if needed */
    if (checkAuthFileStat() > 0) {
	char *usermap = getAuthFile();

	if (usermap) {
	    gfarmAuthFinalize();
	    (void)gfarmAuthInitialize(usermap);
	    free(usermap);
	}
    }

    gfarm_mutex_lock(&authTable_mutex, diag, authTableDiag);
    if (userToDNTable != NULL) {
	struct gfarm_hash_entry *ePtr = gfarm_hash_lookup(userToDNTable,
		localUserName, strlen(localUserName) + 1);
	if (ePtr != NULL) {
	    ret = *(gfarmAuthEntry **)gfarm_hash_entry_data(ePtr);
#if 0
	    dumpAuthEntry(ret);
#endif
	} else {
	    gflog_debug(GFARM_MSG_1000811,
		"look up from userToDNTable (%s) failed", localUserName);
	}
    } else {
	gflog_debug(GFARM_MSG_1000812, "userToDNTable is NULL");
    }
    gfarm_mutex_unlock(&authTable_mutex, diag, authTableDiag);
    return ret;
}
#endif /* GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS */


int
gfarmAuthGetAuthEntryType(gfarmAuthEntry *aePtr)
{
    int authType;
    static const char diag[] = "gfarmAuthGetAuthEntryType()";

    if (aePtr == NULL) {
	gflog_debug(GFARM_MSG_1000813, "invalid argument: auth entry is NULL");
	return GFARM_AUTH_UNKNOWN;
    } else {
	gfarm_mutex_lock(&authTable_mutex, diag, authTableDiag);
	authType = aePtr->authType;
	gfarm_mutex_unlock(&authTable_mutex, diag, authTableDiag);
	if (authType == GFARM_AUTH_USER ||
	    authType == GFARM_AUTH_HOST) {
	    return authType;
	} else {
	    gflog_debug(GFARM_MSG_1000814, "Unknown auth type (%d)", authType);
	    return GFARM_AUTH_UNKNOWN;
	}
    }
}

/* this function assumes that authTable_mutex is locked */
static void
gfarmAuthDestroyUserEntry_unlocked(gfarmAuthEntry *aePtr)
{
    if (aePtr->sesRefCount == 0 &&
	aePtr->orphaned == 1) {
	if (aePtr->distName != NULL) {
	    (void)free(aePtr->distName);
	}
	switch (aePtr->authType) {
	    case GFARM_AUTH_USER: {
		if (aePtr->authData.userAuth.localName != NULL) {
		    (void)free(aePtr->authData.userAuth.localName);
		}
		if (aePtr->authData.userAuth.homeDir != NULL) {
		    (void)free(aePtr->authData.userAuth.homeDir);
		}
		if (aePtr->authData.userAuth.loginShell != NULL) {
		    (void)free(aePtr->authData.userAuth.loginShell);
		}
		break;
	    }
	    case GFARM_AUTH_HOST: {
		if (aePtr->authData.hostAuth.FQDN != NULL) {
		    (void)free(aePtr->authData.hostAuth.FQDN);
		}
		break;
	    }
	}
	(void)free(aePtr);
    }
}

void
gfarmAuthDestroyUserEntry(gfarmAuthEntry *aePtr)
{
    static const char diag[] = "gfarmAuthDestroyUserEntry";

    gfarm_mutex_lock(&authTable_mutex, diag, authTableDiag);
    gfarmAuthDestroyUserEntry_unlocked(aePtr);
    gfarm_mutex_unlock(&authTable_mutex, diag, authTableDiag);
}

static void	cleanString(char *str);
static void
cleanString(char *str)
{
    if (str == NULL || str[0] == '\0') {
	return;
    } else {
	int len = strlen(str);
	(void)memset(str, 0, len);
    }
}


void
gfarmAuthMakeThisAlone(gfarmAuthEntry *laePtr)
{
    static const char diag[] = "gfarmAuthMakeThisAlone()";

    gfarm_mutex_lock(&authTable_mutex, diag, authTableDiag);
    if (laePtr->orphaned == 1) {
	gfarm_mutex_unlock(&authTable_mutex, diag, authTableDiag);
	return;
    } else {
	gfarmAuthEntry *aePtr;
	struct gfarm_hash_iterator it;
	for (gfarm_hash_iterator_begin(authTable, &it);
	     !gfarm_hash_iterator_is_end(&it);
	     gfarm_hash_iterator_next(&it)) {
	    aePtr = *(gfarmAuthEntry **)gfarm_hash_entry_data(
		    gfarm_hash_iterator_access(&it));
	    if (laePtr == aePtr) {
		laePtr->orphaned = 1;
	    } else {
		aePtr->sesRefCount = 0;
		aePtr->orphaned = 1;
		cleanString(aePtr->distName);
		switch (aePtr->authType) {
		    case GFARM_AUTH_USER: {
			cleanString(aePtr->authData.userAuth.localName);
			cleanString(aePtr->authData.userAuth.homeDir);
			cleanString(aePtr->authData.userAuth.loginShell);
			break;
		    }
		    case GFARM_AUTH_HOST: {
			cleanString(aePtr->authData.hostAuth.FQDN);
			break;
		    }
		}
		gfarmAuthDestroyUserEntry_unlocked(aePtr);
	    }
	}
	gfarm_hash_table_free(authTable);
	authTable = NULL;
#if GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS
	gfarm_hash_table_free(userToDNTable);
	userToDNTable = NULL;
#endif /* GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS */
    }
    gfarm_mutex_unlock(&authTable_mutex, diag, authTableDiag);
}
