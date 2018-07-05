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

struct gfarm_auth_entry {
    pthread_mutex_t mutex;
    int sesRefCount;	/* Reference count (from sessions). */
    int authType;	/* GFARM_AUTH_HOST or GFARM_AUTH_USER */
    char *distName;	/* Distinguish name for a user. Heap alloc'd. */

    union {
	struct userAuthData {
	    char *localName;	/* Local user account name. Heap alloc'd. */
	    uid_t uid;
	    gid_t gid;
	    char *homeDir;	/* Home directory. Heap alloc'd. */
	    char *loginShell;	/* Login shell. Heap alloc'd. */
	} userAuth;
	struct hostAuthData {
	    char *FQDN;	/* FQDN for the host. Heap alloc'd. */
	} hostAuth;
    } authData;
};

#define AUTH_TABLE_SIZE       139
static struct gfarm_hash_table *authTable = NULL;

#if GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS
static struct gfarm_hash_table *userToDNTable = NULL;
#endif

static pthread_mutex_t authTable_mutex = PTHREAD_MUTEX_INITIALIZER;
static const char authTableDiag[] = "authTable";
static const char authEntryDiag[] = "authEntry";

static void
authDestroyUserEntry(gfarmAuthEntry *aePtr)
{
	static const char diag[] = "authDestroyUserEntry";

	free(aePtr->distName);
	switch (aePtr->authType) {
	case GFARM_AUTH_USER:
		free(aePtr->authData.userAuth.localName);
		free(aePtr->authData.userAuth.homeDir);
		free(aePtr->authData.userAuth.loginShell);
		break;
	case GFARM_AUTH_HOST:
		free(aePtr->authData.hostAuth.FQDN);
		break;
	}
	gfarm_mutex_destroy(&aePtr->mutex, diag, authEntryDiag);
	free(aePtr);
}

static void
gfarmAuthRefUserEntry(gfarmAuthEntry *aePtr)
{
	static const char diag[] = "gfarmAuthRefUserEntry";

	gfarm_mutex_lock(&aePtr->mutex, diag, authEntryDiag);
	aePtr->sesRefCount++;
	gfarm_mutex_unlock(&aePtr->mutex, diag, authEntryDiag);
}

void
gfarmAuthFreeUserEntry(gfarmAuthEntry *aePtr)
{
	int destroy = 0;
	static const char diag[] = "gfarmAuthFreeUserEntry";

	gfarm_mutex_lock(&aePtr->mutex, diag, authEntryDiag);
	if (--aePtr->sesRefCount <= 0)
		destroy = 1;
	gfarm_mutex_unlock(&aePtr->mutex, diag, authEntryDiag);
	if (destroy)
		authDestroyUserEntry(aePtr);
}

char *
gfarmAuthGetDistName(gfarmAuthEntry *aePtr)
{
	char *name = NULL;
	static const char diag[] = "gfarmAuthGetDistName";

	gfarm_mutex_lock(&aePtr->mutex, diag, authEntryDiag);
	name = aePtr->distName;
	gfarm_mutex_unlock(&aePtr->mutex, diag, authEntryDiag);
	return (name);
}

char *
gfarmAuthGetLocalName(gfarmAuthEntry *aePtr)
{
	char *name = NULL;
	static const char diag[] = "gfarmAuthGetLocalName";

	gfarm_mutex_lock(&aePtr->mutex, diag, authEntryDiag);
	if (aePtr->authType == GFARM_AUTH_USER)
		name = aePtr->authData.userAuth.localName;
	gfarm_mutex_unlock(&aePtr->mutex, diag, authEntryDiag);
	return (name);
}

uid_t
gfarmAuthGetUid(gfarmAuthEntry *aePtr)
{
	uid_t uid = 0;
	static const char diag[] = "gfarmAuthGetUid";

	gfarm_mutex_lock(&aePtr->mutex, diag, authEntryDiag);
	if (aePtr->authType == GFARM_AUTH_USER)
		uid = aePtr->authData.userAuth.uid;
	gfarm_mutex_unlock(&aePtr->mutex, diag, authEntryDiag);
	return (uid);
}

gid_t
gfarmAuthGetGid(gfarmAuthEntry *aePtr)
{
	gid_t gid = 0;
	static const char diag[] = "gfarmAuthGetGid";

	gfarm_mutex_lock(&aePtr->mutex, diag, authEntryDiag);
	if (aePtr->authType == GFARM_AUTH_USER)
		gid = aePtr->authData.userAuth.gid;
	gfarm_mutex_unlock(&aePtr->mutex, diag, authEntryDiag);
	return (gid);
}

char *
gfarmAuthGetHomeDir(gfarmAuthEntry *aePtr)
{
	char *dir = NULL;
	static const char diag[] = "gfarmAuthGetHomeDir";

	gfarm_mutex_lock(&aePtr->mutex, diag, authEntryDiag);
	if (aePtr->authType == GFARM_AUTH_USER)
		dir = aePtr->authData.userAuth.homeDir;
	gfarm_mutex_unlock(&aePtr->mutex, diag, authEntryDiag);
	return (dir);
}

char *
gfarmAuthGetFQDN(gfarmAuthEntry *aePtr)
{
	char *fqdn = NULL;
	static const char diag[] = "gfarmAuthGetFQDN";

	gfarm_mutex_lock(&aePtr->mutex, diag, authEntryDiag);
	if (aePtr->authType == GFARM_AUTH_HOST)
		fqdn = aePtr->authData.hostAuth.FQDN;
	gfarm_mutex_unlock(&aePtr->mutex, diag, authEntryDiag);
	return (fqdn);
}

char *
gfarmAuthGetPrintableName(gfarmAuthEntry *aePtr)
{
	char *name = NULL;
	static const char diag[] = "gfarmAuthGetPrintableName";

	gfarm_mutex_lock(&aePtr->mutex, diag, authEntryDiag);
	switch (aePtr->authType) {
	case GFARM_AUTH_HOST:
		name = aePtr->authData.hostAuth.FQDN;
		break;
	case GFARM_AUTH_USER:
		name = aePtr->authData.userAuth.localName;
		break;
	default:
		name = "unknown auth entry type";
		break;
	}
	gfarm_mutex_unlock(&aePtr->mutex, diag, authEntryDiag);
	return (name);
}

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
static struct stat authFileStat;
static const char authFileDiag[] = "authFile";

/* returned pointer should be free'ed */
char *
gfarmGetDefaultConfigPath(char *dir, char *file)
{
	char *path;

	if (dir == NULL || file == NULL)
		return (NULL);
	GFARM_MALLOC_ARRAY(path, strlen(dir) + 1 + strlen(file) + 1);
	if (path == NULL) {
		gflog_error(GFARM_MSG_1004236, "no memory");
		return (NULL);
	}
	sprintf(path, "%s/%s", dir, file);
	return (path);
}

/* returned pointer should be free'ed */
char *
gfarmGetDefaultConfigFile(char *file)
{
	char *dir = gfarmGetEtcDir();

	return (gfarmGetDefaultConfigPath(dir, file));
}

static int
setAuthFile(char *usermap)
{
	struct stat sb;
	int ret = -1;
	static const char diag[] = "gfsl/setAuthFile()";

	if (usermap != NULL && stat(usermap, &sb) == -1) {
		gflog_auth_error(GFARM_MSG_1004237, "%s: %s", usermap,
			strerror(errno));
		return (ret);
	}
	gfarm_mutex_lock(&authFile_mutex, diag, authFileDiag);
	if (authFile == NULL) {
		if (usermap == NULL)
			authFile = gfarmGetDefaultConfigFile(
				GFARM_DEFAULT_USERMAP_FILE);
		else
			authFile = strdup(usermap);
	} else if (usermap != NULL && strcmp(authFile, usermap) != 0) {
		free(authFile);
		authFile = strdup(usermap);
	}
	if (authFile == NULL)
		gflog_auth_error(GFARM_MSG_1004238, "no memory");
	else if (usermap == NULL && stat(authFile, &sb) == -1)
		gflog_auth_error(GFARM_MSG_1004239, "%s: %s", authFile,
			strerror(errno));
	else {
		authFileStat = sb;
		ret = 0;
	}
	gfarm_mutex_unlock(&authFile_mutex, diag, authFileDiag);
	return (ret);
}

static FILE *
openAuthFile(void)
{
	FILE *f = NULL;
	static const char diag[] = "gfsl/openAuthFile()";

	gfarm_mutex_lock(&authFile_mutex, diag, authFileDiag);
	if (authFile == NULL)
		gflog_auth_notice(GFARM_MSG_1004240, "authFile not set");
	else {
		f = fopen(authFile, "r");
		if (f == NULL)
			gflog_auth_error(GFARM_MSG_1000646,
			    "%s: cannot open: %s", authFile, strerror(errno));
	}
	gfarm_mutex_unlock(&authFile_mutex, diag, authFileDiag);
	return (f);
}

static int
checkAuthFileStat(void)
{
	struct stat sb;
	int update = -1;
	static const char diag[] = "gfsl/checkAuthFileStat()";

	gfarm_mutex_lock(&authFile_mutex, diag, authFileDiag);
	if (authFile == NULL)
		gflog_auth_notice(GFARM_MSG_1004241, "authFile not set");
	else if (stat(authFile, &sb) == -1)
		gflog_auth_warning(GFARM_MSG_1004242, "%s: %s", authFile,
			strerror(errno));
	else {
		update = (authFileStat.st_mtime < sb.st_mtime);
		authFileStat = sb;
	}
	gfarm_mutex_unlock(&authFile_mutex, diag, authFileDiag);
	return (update);
}

static int
gfarmAuthInitialize_unlocked(char *usermapFile)
{
    struct gfarm_hash_table *auth_table;
#if GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS
    struct gfarm_hash_table *user_to_dn_table;
#endif /* GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS */
    gfarmAuthEntry *aePtr;
    FILE *mFd = NULL;
    int ret = 1;
    static const char diag[] = "gfarmAuthInitialize_unlocked()";

    if (authTable != NULL)
	return (ret);

    {
	char lineBuf[65536];
	struct gfarm_hash_entry *ePtr;

	/*
	 * Read global users -> local users mapping file
	 * and create a translation table.
	 */
	if (setAuthFile(usermapFile) == -1)
	    return (-1);

	gfarm_privilege_lock(diag);
	mFd = openAuthFile();
	gfarm_privilege_unlock(diag);
	if (mFd == NULL)
	    return (-1);

	auth_table = gfarm_hash_table_alloc(AUTH_TABLE_SIZE,
		gfarm_hash_default, gfarm_hash_key_equal_default);
	if (auth_table == NULL) { /* no memory */
	    gflog_auth_error(GFARM_MSG_1000647, "%s: no memory", diag);
	    ret = -1;
	    goto initDone;
	}
#if GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS
	user_to_dn_table = gfarm_hash_table_alloc(AUTH_TABLE_SIZE,
		gfarm_hash_default, gfarm_hash_key_equal_default);
	if (user_to_dn_table == NULL) { /* no memory */
	    gflog_auth_error(GFARM_MSG_1000648, "%s: no memory", diag);
	    ret = -1;
	    goto initDone;
	}
#endif /* GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS */
	while (fgets(lineBuf, sizeof lineBuf, mFd) != NULL) {
	    char *token[64];
	    int nToken = gfarmGetToken(lineBuf, token, sizeof token);
	    char *distName, *mode = NULL, *localName = NULL;
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

		gfarm_mutex_init(&aePtr->mutex, diag, authEntryDiag);
		aePtr->sesRefCount = 1;
		aePtr->authType = GFARM_AUTH_USER;
		aePtr->distName = strdup(distName);
		aePtr->authData.userAuth.localName = strdup(localName);
		aePtr->authData.userAuth.uid = pPtr->pw_uid;
		aePtr->authData.userAuth.gid = pPtr->pw_gid;
		aePtr->authData.userAuth.homeDir = strdup(pPtr->pw_dir);
		aePtr->authData.userAuth.loginShell = strdup(pPtr->pw_shell);
#if GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS
		ePtr = gfarm_hash_enter(user_to_dn_table, localName,
			strlen(localName) + 1, sizeof(aePtr), &isNew);
		if (ePtr == NULL) { /* no memory */
		    gflog_warning(GFARM_MSG_1000653,
			"%s: WARNING: no memory for DN. Ignored.", localName);
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

		gfarm_mutex_init(&aePtr->mutex, diag, authEntryDiag);
		aePtr->sesRefCount = 1;
		aePtr->authType = GFARM_AUTH_HOST;
		aePtr->distName = strdup(distName);
		aePtr->authData.hostAuth.FQDN = strdup(localName);
	    } else {
		gflog_warning(GFARM_MSG_1000656,
		    "%s: WARNING: Unknown keyword at second field."
			      " Ignored.", localName);
		continue;
	    }

	    ePtr = gfarm_hash_enter(auth_table, aePtr->distName,
			strlen(aePtr->distName) + 1, sizeof(aePtr), &isNew);
	    if (ePtr == NULL) { /* no memory */
		gflog_warning(GFARM_MSG_1000657,
		    "%s: WARNING: no memory for DN. Ignored.",
			      distName);
#if GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS
		if (aePtr->authType == GFARM_AUTH_USER)
		    gfarm_hash_purge(user_to_dn_table,
			localName, strlen(localName) + 1);
#endif /* GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS */
		gfarmAuthFreeUserEntry(aePtr);
		goto initDone;
	    }
	    if (!isNew) {
		gflog_debug(GFARM_MSG_1000658,
		    "%s: duplicate DN. Ignored.", distName);
#if GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS
		if (aePtr->authType == GFARM_AUTH_USER)
		    gfarm_hash_purge(userToDNTable,
				     localName, strlen(localName) + 1);
#endif /* GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS */
		gfarmAuthFreeUserEntry(aePtr);
		continue;
	    }
	    *(gfarmAuthEntry **)gfarm_hash_entry_data(ePtr) = aePtr;
#if 0
	    dumpAuthEntry(aePtr);
#endif
	}
    }

initDone:
    if (ret == -1) {
	if (auth_table != NULL) {
	    /*
	     * Destroy mapping table.
	     */
	    struct gfarm_hash_iterator it;

	    for (gfarm_hash_iterator_begin(auth_table, &it);
		 !gfarm_hash_iterator_is_end(&it);
		 gfarm_hash_iterator_next(&it)) {
		aePtr = *(gfarmAuthEntry **)gfarm_hash_entry_data(
		    gfarm_hash_iterator_access(&it));
		gfarmAuthFreeUserEntry(aePtr);
	    }
	    gfarm_hash_table_free(auth_table);
	}
#if GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS
	if (user_to_dn_table != NULL)
	    gfarm_hash_table_free(user_to_dn_table);
#endif /* GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS */
    }
    fclose(mFd);
    if (ret == 1) {
	authTable = auth_table;
#if GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS
	userToDNTable = user_to_dn_table;
#endif /* GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS */
    }
    return (ret);
}

int
gfarmAuthInitialize(char *usermapFile)
{
    int r;
    static const char diag[] = "gfarmAuthInitialize()";

    gfarm_mutex_lock(&authTable_mutex, diag, authTableDiag);
    r = gfarmAuthInitialize_unlocked(usermapFile);
    gfarm_mutex_unlock(&authTable_mutex, diag, authTableDiag);
    return (r);
}

static void
gfarmAuthFinalize_unlocked(void)
{
    if (authTable != NULL) {
	gfarmAuthEntry *aePtr;
	struct gfarm_hash_iterator it;

	for (gfarm_hash_iterator_begin(authTable, &it);
	     !gfarm_hash_iterator_is_end(&it);
	     gfarm_hash_iterator_next(&it)) {
	    aePtr = *(gfarmAuthEntry **)gfarm_hash_entry_data(
		    gfarm_hash_iterator_access(&it));
	    gfarmAuthFreeUserEntry(aePtr);
	}
	gfarm_hash_table_free(authTable);
	authTable = NULL;
#if GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS
	gfarm_hash_table_free(userToDNTable);
	userToDNTable = NULL;
#endif /* GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS */
    }
}

void
gfarmAuthFinalize(void)
{
    static const char diag[] = "gfarmAuthFinalize()";

    gfarm_mutex_lock(&authTable_mutex, diag, authTableDiag);
    gfarmAuthFinalize_unlocked();
    gfarm_mutex_unlock(&authTable_mutex, diag, authTableDiag);
}

static void
gfarmAuthReset(void)
{
    static const char diag[] = "gfarmAuthReset()";

    gfarm_mutex_lock(&authTable_mutex, diag, authTableDiag);
    gfarmAuthFinalize_unlocked();
    gfarmAuthInitialize_unlocked(NULL);
    gfarm_mutex_unlock(&authTable_mutex, diag, authTableDiag);
}

/* returned gfarmAuthEntry should be free'ed by gfarmAuthFreeUserEntry */
gfarmAuthEntry *
gfarmAuthGetUserEntry(char *distUserName)
{
    gfarmAuthEntry *ret = NULL;
    static const char diag[] = "gfarmAuthGetUserEntry";

    /* update a usermap if needed */
    if (checkAuthFileStat() > 0)
	gfarmAuthReset();

    gfarm_mutex_lock(&authTable_mutex, diag, authTableDiag);
    if (authTable != NULL) {
	struct gfarm_hash_entry *ePtr = gfarm_hash_lookup(authTable,
		distUserName, strlen(distUserName) + 1);

	if (ePtr != NULL) {
	    ret = *(gfarmAuthEntry **)gfarm_hash_entry_data(ePtr);
	    gfarmAuthRefUserEntry(ret);
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
    return (ret);
}


#if GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS
gfarmAuthEntry *
gfarmAuthGetLocalUserEntry(char *localUserName)
{
    gfarmAuthEntry *ret = NULL;
    static const char diag[] = "gfarmAuthGetLocalUserEntry()";

    /* update a usermap if needed */
    if (checkAuthFileStat() > 0)
	gfarmAuthReset();

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
    return (ret);
}
#endif /* GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS */

int
gfarmAuthGetAuthEntryType(gfarmAuthEntry *aePtr)
{
    int authType;
    static const char diag[] = "gfarmAuthGetAuthEntryType()";

    if (aePtr == NULL) {
	gflog_debug(GFARM_MSG_1000813, "invalid argument: auth entry is NULL");
	return (GFARM_AUTH_UNKNOWN);
    }
    gfarm_mutex_lock(&aePtr->mutex, diag, authEntryDiag);
    authType = aePtr->authType;
    gfarm_mutex_unlock(&aePtr->mutex, diag, authEntryDiag);
    if (authType == GFARM_AUTH_USER || authType == GFARM_AUTH_HOST)
	return (authType);
    gflog_debug(GFARM_MSG_1000814, "Unknown auth type (%d)", authType);
    return (GFARM_AUTH_UNKNOWN);
}

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
    {
	gfarmAuthEntry *aePtr;
	struct gfarm_hash_iterator it;

	for (gfarm_hash_iterator_begin(authTable, &it);
	     !gfarm_hash_iterator_is_end(&it);
	     gfarm_hash_iterator_next(&it)) {
	    aePtr = *(gfarmAuthEntry **)gfarm_hash_entry_data(
		    gfarm_hash_iterator_access(&it));
	    if (laePtr != aePtr) {
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
		gfarmAuthFreeUserEntry(aePtr);
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
