#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <ctype.h>
#include <pwd.h>

#include "gfsl_config.h"
#include "gfarm_hash.h"
#include "gfarm_auth.h"

static int authInited = 0;
static gfarm_HashTable authTable;

extern char *	getGfarmEtcDir(void);
extern int	getToken(char *buf, char *tokens[], int max);


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
    if (authInited != 1) {
	char mapFile[PATH_MAX];
	FILE *mFd = NULL;
	char lineBuf[65536];
	gfarmAuthEntry *aePtr;
	gfarm_HashEntry *ePtr;

	/*
	 * Read global users -> local users mapping file
	 * and create a translation table.
	 */
	if (usermapFile == NULL || usermapFile[0] == '\0') {
	    char *confDir = getGfarmEtcDir();
	    if (confDir == NULL) {
		ret = -1;
		goto done;
	    }
	    sprintf(mapFile, "%s/%s", confDir, GFARM_DEFAULT_USERMAP_FILE);
	    usermapFile = mapFile;
	    (void)free(confDir);
	}
	if ((mFd = fopen(usermapFile, "r")) == NULL) {
	    ret = -1;
	    goto initDone;
	}

	gfarm_InitHashTable(&authTable, GFARM_STRING_KEYS);
	while (fgets(lineBuf, 65536, mFd) != NULL) {
	    char *token[64];
	    int nToken = getToken(lineBuf, token, sizeof token);
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
		fprintf(stderr, "WARINIG: missing local username for '%s'."
			" Ignored.\n",
			distName);
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
		    fprintf(stderr, "WARINIG: Can't determine account"
			    " information of user '%s'. Ignored.\n",
			    localName);
		    continue;
		}
		if (pPtr->pw_uid == 0) {
		    fprintf(stderr, "WARNING: User '%s' is a super user. Ignored.\n",
			    localName);
		    continue;
		}
		aePtr = (gfarmAuthEntry *)malloc(sizeof(gfarmAuthEntry));
		if (aePtr == NULL) {
		    ret = -1;
		    fclose(mFd);
		    goto initDone;
		}
		(void)memset(aePtr, 0, sizeof(gfarmAuthEntry));

		aePtr->sesRefCount = 0;
		aePtr->orphaned = 0;
		aePtr->authType = GFARM_AUTH_USER;
		aePtr->distName = strdup(distName);
		aePtr->authData.userAuth.localName = strdup(token[1]);
		aePtr->authData.userAuth.uid = pPtr->pw_uid;
		aePtr->authData.userAuth.gid = pPtr->pw_gid;
		aePtr->authData.userAuth.homeDir = strdup(pPtr->pw_dir);
		aePtr->authData.userAuth.loginShell = strdup(pPtr->pw_shell);
	    } else if (strcmp(mode, "@host@") == 0) {
		aePtr = (gfarmAuthEntry *)malloc(sizeof(gfarmAuthEntry));
		if (aePtr == NULL) {
		    ret = -1;
		    fclose(mFd);
		    goto initDone;
		}
		(void)memset(aePtr, 0, sizeof(gfarmAuthEntry));

		aePtr->sesRefCount = 0;
		aePtr->orphaned = 0;
		aePtr->authType = GFARM_AUTH_HOST;
		aePtr->distName = strdup(distName);
		aePtr->authData.hostAuth.FQDN = strdup(localName);
	    } else {
		fprintf(stderr, "WARINIG: Unknown keyword '%s'"
			" for user '%s'. Ignored.\n",
			mode, localName);
		continue;
	    }

	    ePtr = gfarm_CreateHashEntry(&authTable, aePtr->distName, &isNew);
	    gfarm_SetHashValue(ePtr, (ClientData)aePtr);
#if 0
	    dumpAuthEntry(aePtr);
#endif
	}
	fclose(mFd);

	initDone:
	if (ret == -1) {
	    /*
	     * Destroy mapping table.
	     */
	    gfarm_HashSearch s;
	    for (ePtr = gfarm_FirstHashEntry(&authTable, &s);
		 ePtr != NULL;
		 ePtr = gfarm_NextHashEntry(&s)) {
		aePtr = (gfarmAuthEntry *)gfarm_GetHashValue(ePtr);
		gfarm_DeleteHashEntry(ePtr);
		aePtr->orphaned = 1;
		gfarmAuthDestroyUserEntry(aePtr);
	    }
	    gfarm_DeleteHashTable(&authTable);
	} else {
	    authInited = 1;
	}
    }

    done:
    return ret;
}


void
gfarmAuthFinalize()
{
    if (authInited == 1) {
	gfarmAuthEntry *aePtr;
	gfarm_HashEntry *ePtr;
	gfarm_HashSearch s;
	for (ePtr = gfarm_FirstHashEntry(&authTable, &s);
	     ePtr != NULL;
	     ePtr = gfarm_NextHashEntry(&s)) {
	    aePtr = (gfarmAuthEntry *)gfarm_GetHashValue(ePtr);
	    gfarm_DeleteHashEntry(ePtr);
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
	gfarm_DeleteHashTable(&authTable);
	authInited = 0;
    }
}


gfarmAuthEntry *
gfarmAuthGetUserEntry(distUserName)
     char *distUserName;
{
    gfarmAuthEntry *ret = NULL;
    if (authInited == 1) {
	gfarm_HashEntry *ePtr = gfarm_FindHashEntry(&authTable, distUserName);
	ret = (gfarmAuthEntry *)gfarm_GetHashValue(ePtr);
#if 0
	dumpAuthEntry(ret);
#endif
    }
    return ret;
}


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
	gfarm_HashEntry *ePtr;
	gfarm_HashSearch s;
	for (ePtr = gfarm_FirstHashEntry(&authTable, &s);
	     ePtr != NULL;
	     ePtr = gfarm_NextHashEntry(&s)) {
	    aePtr = (gfarmAuthEntry *)gfarm_GetHashValue(ePtr);
	    gfarm_DeleteHashEntry(ePtr);
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
    }
}


