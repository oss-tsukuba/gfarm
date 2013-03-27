#ifndef _NETDB_H_
#define _NETDB_H_

#include <netinet/in.h>


struct servent {
	char  *s_name;       /* official service name */
	char **s_aliases;    /* alias list */
	int    s_port;       /* port number */
	char  *s_proto;      /* protocol to use */
};

extern struct servent *getservbyport(int port, const char *proto);
extern struct servent *getservbyname(const char *name, const char *proto);

struct protoent {
	char  *p_name;       /* official protocol name */
	char **p_aliases;    /* alias list */
	int    p_proto;      /* protocol number */
};
struct protoent *getprotobyname(const char *name);

struct addrinfo {
	int ai_flags;                 /* Input flags.  */
	int ai_family;                /* Protocol family for socket.  */
	int ai_socktype;              /* Socket type.  */
	int ai_protocol;              /* Protocol for socket.  */
	socklen_t ai_addrlen;         /* Length of socket address.  */
	struct sockaddr *ai_addr;     /* Socket address for socket.  */
	char *ai_canonname;	/* Canonical name for service location. */
	struct addrinfo *ai_next;     /* Pointer to next in list.  */
};
/* Possible values for `ai_flags' field in `addrinfo' structure.  */
# define AI_PASSIVE     0x0001  /* Socket address is intended for `bind'.  */

int getaddrinfo(const char *node, const char *service,
	const struct addrinfo *hints, struct addrinfo **res);

void freeaddrinfo(struct addrinfo *res);

struct hostent {
	char  *h_name;            /* official name of host */
	char **h_aliases;         /* alias list */
	int    h_addrtype;        /* host address type */
	int    h_length;          /* length of address */
	char **h_addr_list;       /* list of addresses */
};
struct hostent *gethostbyname(const char *name);
void free_gethost_buff(void *buf);

int getnameinfo(const struct sockaddr *sa, socklen_t salen,
	char *host, size_t hostlen,
	char *serv, size_t servlen, int flags);

# define NI_NUMERICHOST	1	/* Don't try to look up hostname.  */
# define NI_NUMERICSERV 2	/* Don't convert port number to name.  */
#  define NI_MAXSERV      32
#  define NI_MAXHOST      1025

#endif /* _NETDB_H_ */

