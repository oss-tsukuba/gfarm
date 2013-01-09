#ifndef _GFARM_AUTH_H_
#define _GFARM_AUTH_H_

/*
 * Authentication information struct
 */
typedef struct {
    int sesRefCount;	/* Reference count (from sessions). */
    int orphaned;	/* 1 if this entry no longer exists in the
			 * database and should be free'd when the
			 * sesRefCount == 0. */
    int authType;	/* One of belows. */
#define GFARM_AUTH_HOST	0
#define GFARM_AUTH_USER	1
#define GFARM_AUTH_UNKNOWN	-1
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
} gfarmAuthEntry;


/* Prototypes. */

extern int	gfarmAuthInitialize(char *usermapFile);
extern void	gfarmAuthFinalize(void);

extern gfarmAuthEntry *
		gfarmAuthGetUserEntry(char *distUserName);

extern void	gfarmAuthDestroyUserEntry(gfarmAuthEntry *aePtr);

extern void	gfarmAuthMakeThisAlone(gfarmAuthEntry *laePtr);

extern int	gfarmAuthGetAuthEntryType(gfarmAuthEntry *aePtr);

#if GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS
gfarmAuthEntry *gfarmAuthGetLocalUserEntry(char *localUserName);
#endif /* GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS */

#endif /* _GFARM_AUTH_H_ */
