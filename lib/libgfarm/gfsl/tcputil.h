extern int	gfarmTCPConnectPort(unsigned long addr, int port);
extern int	gfarmTCPBindPort(int port);
extern unsigned long int
		gfarmIPGetAddressOfHost(char *host);
extern char *	gfarmIPGetHostOfAddress(unsigned long int addr);
extern unsigned long int
		gfarmIPGetNameOfSocket(int sock, int *portPtr);
extern unsigned long int
		gfarmIPGetPeernameOfSocket(int sock, int *portPtr);

extern int	gfarmWaitReadable(int fd);
extern int	gfarmReadBytes(int fd, char *buf, int len);
extern int	gfarmReadShorts(int fd, short *buf, int len);
extern int	gfarmReadLongs(int fd, long *buf, int len);
extern int	gfarmWriteBytes(int fd, char *buf, int len);
extern int	gfarmWriteShorts(int fd, short *buf, int len);
extern int	gfarmWriteLongs(int fd, long *buf, int len);
