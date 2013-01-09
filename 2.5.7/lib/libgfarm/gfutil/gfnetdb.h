struct sockaddr;
struct addrinfo;

int gfarm_getaddrinfo(const char *,
         const char *,
         const struct addrinfo *,
         struct addrinfo **);
void gfarm_freeaddrinfo(struct addrinfo *);

int gfarm_getnameinfo(const struct sockaddr *, socklen_t,
	char *, size_t, char *,
	size_t, int);
