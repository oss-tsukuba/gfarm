/* alloc */
size_t gfarm_size_add(int *, size_t, size_t);
size_t gfarm_size_mul(int *, size_t, size_t);

/* daemon */

#ifndef HAVE_DAEMON
int gfarm_daemon(int, int);
#else
#define gfarm_daemon	daemon
#endif

/* limit */

void gfarm_unlimit_nofiles(int *);

/* send_no_sigpipe */

void gfarm_sigpipe_ignore(void);
ssize_t gfarm_send_no_sigpipe(int, const void *, size_t);

/* timeval */

#define GFARM_MILLISEC_BY_MICROSEC	1000
#define GFARM_SECOND_BY_MICROSEC	1000000

struct timeval;
int gfarm_timeval_cmp(const struct timeval *, const struct timeval *);
void gfarm_timeval_add(struct timeval *, const struct timeval *);
void gfarm_timeval_sub(struct timeval *, const struct timeval *);
void gfarm_timeval_add_microsec(struct timeval *, long);
