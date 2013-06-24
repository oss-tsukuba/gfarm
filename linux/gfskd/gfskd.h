#ifndef _GFSKD_H_
#define _GFSKD_H_

#include <sys/uio.h>

struct gfskd_req_t {
	int	r_fd;
	struct gfskdev_in_header *r_in;
	ssize_t	r_len;
	int	r_alloc;
};

extern int gfskd_term;
void gfskd_set_term(gfarm_error_t);

gfarm_error_t gfskd_req_connect_gfmd(struct gfskd_req_t *req, void *arg);
gfarm_error_t gfskd_req_connect_gfsd(struct gfskd_req_t *req, void *arg);
gfarm_error_t gfskd_req_term(struct gfskd_req_t *req, void *arg);

gfarm_error_t gfskd_loop(int fd, int bufsize);
gfarm_error_t gfskd_send_reply(struct gfskd_req_t *req, gfarm_error_t error,
	void *data, ssize_t len);
gfarm_error_t gfskd_send_iov(struct gfskd_req_t *req, gfarm_error_t error,
	struct iovec *iov, int count);

#endif /* _GFSKD_H_ */
