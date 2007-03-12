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
#include <gfarm/gfarm_misc.h>
#include "hash.h"
#include "gfutil.h"

#include "gfsl_config.h"
#include "gfarm_auth.h"
#include "misc.h"

#define AUTH_TABLE_SIZE       139
static struct gfarm_hash_table *authTable = NULL;

#if GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS
static struct gfarm_hash_table *userToDNTable = NULL;
#endif

#if 0
static void
dumpAuthEntry(aePtr)
     gfarmAuthEntry *aePtr;
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


int
gfarmAuthInitialize(usermapFile)
     char *usermapFile;
{
    int ret = 1;
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
		gflog_auth_error("gfarmAuthInitialize(): no memory");
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
	    gflog_auth_error("%s: cannot open: %s",
		usermapFile, strerror(errno));
	    ret = -1;
	    goto done;
	}

	authTable = gfarm_hash_table_alloc(AUTH_TABLE_SIZE,
					   gfarm_hash_default,
					   gfarm_hash_key_equal_default);
	if (authTable == NULL) { /* no memory */
	    gflog_auth_error("gfarmAuthInitialize(): no memory");
	    ret = -1;
	    goto done;
	}
#if GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS
	userToDNTable = gfarm_hash_table_alloc(AUTH_TABLE_SIZE,
					      gfarm_hash_default,
					      gfarm_hash_key_equal_default);
	if (userToDNTable == NULL) { /* no memory */
	    gflog_auth_error("gfarmAuthInitialize(): no memory");
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
		gflog_warning("%s: WARNING: missing local username for DN."
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
		    gflog_auth_warning("%s: WARNING: Account doesn't exist."
				       " Ignored.", localName);
		    continue;
		}
		if (pPtr->pw_uid == 0) {
		    gflog_warning("%s: WARNING: This user is a super user."
				  " Ignored.", localName);
		    continue;
		}
		GFARM_MALLOC(aePtr);
		if (aePtr == NULL) {
		    gflog_auth_error("gfarmAuthInitialize(): no memory");
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
		    gflog_warning("%s: WARNING: no memory for DN. Ignored.",
				  localName);
		} else if (!isNew) {
		    gflog_auth_warning(
			"%s: WARNING: multiple X.509 Distinguish name "
			"for a UNIX user account. Ignored.", localName);
		} else {
		    *(gfarmAuthEntry **)gfarm_hash_entry_data(ePtr) = aePtr;
		}
#endif /* GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS */
	    } else if (strcmp(mode, "@host@") == 0) {
		GFARM_MALLOC(aePtr);
		if (aePtr == NULL) {
		    gflog_auth_error("gfarmAuthInitialize(): no memory");
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
		gflog_warning("%s: WARNING: Unknown keyword at second field."
			      " Ignored.", localName);
		continue;
	    }

	    ePtr = gfarm_hash_enter(authTable, aePtr->distName,
				    strlen(aePtr->distName) + 1,
				    sizeof(aePtr), &isNew);
	    if (ePtr == NULL) { /* no memory */
		gflog_warning("%s: WARNING: no memory for DN. Ignored.",
			      distName);
#if GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS
		if (aePtr->authType == GFARM_AUTH_USER)
		    gfarm_hash_purge(userToDNTable,
				     localName, strlen(localName) + 1);
#endif /* GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS */
		aePtr->orphaned = 1;
		gfarmAuthDestroyUserEntry(aePtr);
		goto initDone;
	    }
	    if (!isNew) {
		gflog_warning("%s: WARNING: duplicate DN. Ignored.", distName);
#if GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS
		if (aePtr->authType == GFARM_AUTH_USER)
		    gfarm_hash_purge(userToDNTable,
				     localName, strlen(localName) + 1);
#endif /* GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS */
		aePtr->orphaned = 1;
		gfarmAuthDestroyUserEntry(aePtr);
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
		gfarmAuthDestroyUserEntry(aePtr);
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
    return ret;
}


void
gfarmAuthFinalize()
{
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
		gfarmAuthDestroyUserEntry(aePtr);
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
}


gfarmAuthEntry *
gfarmAuthGetUserEntry(distUserName)
     char *distUserName;
{
    gfarmAuthEntry *ret = NULL;
    if (authTable != NULL) {
	struct gfarm_hash_entry *ePtr = gfarm_hash_lookup(authTable,
		distUserName, strlen(distUserName) + 1);
	if (ePtr != NULL) {
	    ret = *(gfarmAuthEntry **)gfarm_hash_entry_data(ePtr);
#if 0
	    dumpAuthEntry(ret);
#endif
	}
    }
    return ret;
}


#if GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS
gfarmAuthEntry *
gfarmAuthGetLocalUserEntry(localUserName)
     char *localUserName;
{
    gfarmAuthEntry *ret = NULL;
    if (userToDNTable != NULL) {
	struct gfarm_hash_entry *ePtr = gfarm_hash_lookup(userToDNTable,
		localUserName, strlen(localUserName) + 1);
	if (ePtr != NULL) {
	    ret = *(gfarmAuthEntry **)gfarm_hash_entry_data(ePtr);
#if 0
	    dumpAuthEntry(ret);
#endif
	}
    }
    return ret;
}
#endif /* GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS */


int
gfarmAuthGetAuthEntryType(aePtr)
     gfarmAuthEntry *aePtr;
{
    if (aePtr == NULL) {
	return GFARM_AUTH_UNKNOWN;
    } else {
	if (aePtr->authType == GFARM_AUTH_USER ||
	    aePtr->authType == GFARM_AUTH_HOST) {
	    return aePtr->authType;
	} else {
	    return GFARM_AUTH_UNKNOWN;
	}
    }
}


void
gfarmAuthDestroyUserEntry(aePtr)
     gfarmAuthEntry *aePtr;
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


static void	cleanString(char *str);
static void
cleanString(str)
     char *str;
{
    if (str == NULL || str[0] == '\0') {
	return;
    } else {
	int len = strlen(str);
	(void)memset(str, 0, len);
    }
}


void
gfarmAuthMakeThisAlone(laePtr)
     gfarmAuthEntry *laePtr;
{
    if (laePtr->orphaned == 1) {
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
		gfarmAuthDestroyUserEntry(aePtr);
	    }
	}
	gfarm_hash_table_free(authTable);
	authTable = NULL;
#if GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS
	gfarm_hash_table_free(userToDNTable);
	userToDNTable = NULL;
#endif /* GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS */
    }
}
