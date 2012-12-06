#ifndef _POLL_H_
#define _POLL_H_
#include <sys/poll.h>

int poll(struct pollfd *fds, nfds_t nfds, int timeout);
#endif /* _POLL_H_ */

