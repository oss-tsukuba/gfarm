extern int	ConnectPort(unsigned long addr, int port);
extern int	BindPort(int port);
extern unsigned long int
		GetIPAddressOfHost(char *host);
extern char *	GetHostOfIPAddress(unsigned long int addr);
extern unsigned long int
		GetNameOfSocket(int sock, int *portPtr);
extern unsigned long int
		GetPeernameOfSocket(int sock, int *portPtr);

extern int	WaitReadable(int fd);
extern int	ReadBytes(int fd, char *buf, int len);
extern int	ReadShorts(int fd, short *buf, int len);
extern int	ReadLongs(int fd, long *buf, int len);
extern int	WriteBytes(int fd, char *buf, int len);
extern int	WriteShorts(int fd, short *buf, int len);
extern int	WriteLongs(int fd, long *buf, int len);
