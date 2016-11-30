#include <gfarm/gfarm_misc.h> /* for gfarm_int16_t and gfarm_int32_t */

#define	GFARM_OCTETS_PER_32BIT	4	/* 32/8 */
#define	GFARM_OCTETS_PER_16BIT	2	/* 16/8 */

extern int	gfarmTCPConnectPortByHost(char *, int port);
extern int	gfarmTCPBindPort(int port);
extern int	gfarmGetNameOfSocket(int sock, int *portPtr);
int	gfarmGetPeernameOfSocket(int sock, int *portPtr, char **hostPtr);

extern int	gfarmWaitReadable(int fd, int timtoueMsec);
extern int	gfarmReadInt8(int fd, gfarm_int8_t *buf, int len,
			      int timtoueMsec);
extern int	gfarmReadInt16(int fd, gfarm_int16_t *buf, int len,
			      int timtoueMsec);
extern int	gfarmReadInt32(int fd, gfarm_int32_t *buf, int len,
			      int timtoueMsec);
extern int	gfarmWriteInt8(int fd, gfarm_int8_t *buf, int len);
extern int	gfarmWriteInt16(int fd, gfarm_int16_t *buf, int len);
extern int	gfarmWriteInt32(int fd, gfarm_int32_t *buf, int len);
