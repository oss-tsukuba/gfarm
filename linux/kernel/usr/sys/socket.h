#ifndef _SOCKET_H_
#define _SOCKET_H_
#include <linux/socket.h>

# ifndef __socklen_t_defined
typedef int socklen_t;
#  define __socklen_t_defined
# endif
extern int setsockopt(int sockfd, int level, int optname,
	const void *optval, socklen_t optlen);
extern int getsockopt(int sockfd, int level, int optname,
	void *optval, socklen_t *optlen);
extern int getsockname(int sockfd, struct sockaddr *name,
	socklen_t *namelen);
extern int getpeername(int sockfd, struct sockaddr *name,
	socklen_t *namelen);
extern ssize_t send(int sockfd, const void *buf, size_t len, int flags);

extern int bind(int sockfd, const struct sockaddr *addr,
	socklen_t addrlen);

#include <linux/net.h>	/* for SOCK_STREAM */
#endif /* _SOCKET_H_ */

