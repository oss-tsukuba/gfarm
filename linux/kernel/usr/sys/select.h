#ifndef _SELECT_H_
#define _SELECT_H_
extern int select(int __nfds, fd_set * __restrict __readfds,
	fd_set * __restrict __writefds,
	fd_set * __restrict __exceptfds,
	struct timeval *__restrict __timeout);

#endif /* _SELECT_H_ */

