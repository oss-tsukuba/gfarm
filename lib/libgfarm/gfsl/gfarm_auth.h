#ifndef _GFARM_AUTH_H_
#define _GFARM_AUTH_H_

/*
 * Authentication information struct
 */
struct gfarm_auth_entry;
typedef struct gfarm_auth_entry gfarmAuthEntry;

/* authType */
#define GFARM_AUTH_HOST		0
#define GFARM_AUTH_USER		1
#define GFARM_AUTH_UNKNOWN	-1

/* Prototypes. */

extern char *gfarmGetDefaultConfigPath(char *dir, char *file);
extern char *gfarmGetDefaultConfigFile(char *file);

extern int	gfarmAuthInitialize(char *usermapFile);
extern void	gfarmAuthFinalize(void);

extern gfarmAuthEntry *
		gfarmAuthGetUserEntry(char *distUserName);
extern void	gfarmAuthFreeUserEntry(gfarmAuthEntry *aePtr);
extern char *gfarmAuthGetDistName(gfarmAuthEntry *aePtr);
extern char *gfarmAuthGetLocalName(gfarmAuthEntry *aePtr);
extern uid_t	gfarmAuthGetUid(gfarmAuthEntry *aePtr);
extern gid_t	gfarmAuthGetGid(gfarmAuthEntry *aePtr);
extern char *gfarmAuthGetHomeDir(gfarmAuthEntry *aePtr);
extern char *gfarmAuthGetFQDN(gfarmAuthEntry *aePtr);
extern char *gfarmAuthGetPrintableName(gfarmAuthEntry *aePtr);

extern int	gfarmAuthGetAuthEntryType(gfarmAuthEntry *aePtr);

extern void	gfarmAuthMakeThisAlone(gfarmAuthEntry *laePtr);

#if GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS
gfarmAuthEntry *gfarmAuthGetLocalUserEntry(char *localUserName);
#endif /* GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS */

#endif /* _GFARM_AUTH_H_ */
