#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/param.h>
#include <gfarm/gfarm.h>
#include <gfarm/gflog.h>
#include "context.h"
#include "gfsk_if.h"
#include "gfskd.h"

static inline size_t
iov_length(const struct iovec *iov, unsigned long nr_segs)
{
	unsigned long seg;
	size_t ret = 0;

	for (seg = 0; seg < nr_segs; seg++)
		ret += iov[seg].iov_len;
	return (ret);
}

gfarm_error_t
gfskd_send_iov(struct gfskd_req_t *req, gfarm_error_t error,
			struct iovec *iov, int count)
{
	int	err;
	struct gfskdev_out_header	out;
	struct iovec iovec[1];
	ssize_t len;

	if (!iov) {
		iov = iovec;
		count = 1;
	}
	out.unique = req->r_in->unique;
	/* NOTE: out.error must be <= 0 */
	out.error = -gfarm_error_to_errno(error);
	iov[0].iov_base = &out;
	iov[0].iov_len = sizeof(struct gfskdev_out_header);
	out.len = iov_length((const struct iovec *)iov, count);
	gflog_debug(GFARM_MSG_UNFIXED, "unique=%llu, gfarm_error=%d, "
		"out.error=%d, len=%d", out.unique, error, out.error, out.len);

	error = GFARM_ERR_NO_ERROR;
	len = writev(req->r_fd, iov, count);
	if (len != out.len) {
		err = errno;
		if (len >= 0)
			errno = EIO;
		error = gfarm_errno_to_error(errno);
		gflog_error(GFARM_MSG_UNFIXED,
			"writev fail req=%d : %ld, %s", out.len, len,
			gfarm_error_string(error));

		if (len < 0) {
			switch (err) {
			case EINTR:
			case EAGAIN:
				break;
			default:
				gfskd_set_term(error);
				break;
			}
		}
	}
	if (req->r_alloc) {
		free(req->r_in);
		free(req);
	}
	return (error);

}

gfarm_error_t
gfskd_send_reply(struct gfskd_req_t *req, gfarm_error_t error,
			void *data, ssize_t len)
{
	struct iovec iovec[2];
	iovec[1].iov_base = data;
	iovec[1].iov_len = len;
	return (gfskd_send_iov(req, error, iovec, 2));
}

void
gfskd_recv_req(int fd, const char *buf, size_t len)
{
	gfarm_error_t error;
	struct gfskd_req_t req, *reqp;
	struct gfskdev_in_header *in = (struct gfskdev_in_header *) buf;
	void *inarg;
	int	async = 0;
	const char *bufp = NULL;

	switch (in->opcode) {
	case GFSK_OP_CONNECT_GFMD:
	case GFSK_OP_CONNECT_GFSD:
		async = 1;
		break;
	default:
		break;
	}
	if (async) {
		GFARM_MALLOC(reqp);
		GFARM_MALLOC_ARRAY(bufp, len);
		if (!reqp || !bufp) {
			error = GFARM_ERR_NO_MEMORY;
			gflog_error(GFARM_MSG_UNFIXED, "len:%ld, %s", len,
				gfarm_error_string(error));
			if (reqp)
				free(reqp);
			gfskd_set_term(error);
			return;
		}
		memcpy((char *)bufp, buf, len);
	} else{
		reqp = &req;
		bufp = buf;
	}
	memset(reqp, 0, sizeof(*reqp));
	reqp->r_in = (struct gfskdev_in_header *) bufp;
	reqp->r_len = len;
	reqp->r_alloc = async;
	reqp->r_fd = fd;
	inarg = (void *)(bufp + sizeof(struct gfskdev_in_header));

	switch (in->opcode) {
	case GFSK_OP_CONNECT_GFMD:
		error = gfskd_req_connect_gfmd(reqp, inarg);
		break;
	case GFSK_OP_CONNECT_GFSD:
		error = gfskd_req_connect_gfsd(reqp, inarg);
		break;
	case GFSK_OP_TERM:
		error = gfskd_req_term(reqp, inarg);
		break;
	default:
		error = GFARM_ERR_OPERATION_NOT_SUPPORTED;
		break;
	}
	if (error || !async) {
		gfskd_send_iov(reqp, error, NULL, 0);
	}

}

gfarm_error_t
gfskd_loop(int fd, int bufsize)
{
	gfarm_error_t	error = GFARM_ERR_NO_ERROR;
	int err;
	ssize_t len;
	char *buf;

	GFARM_MALLOC_ARRAY(buf, bufsize);
	if (!buf) {
		error = GFARM_ERR_NO_MEMORY;
		gflog_error(GFARM_MSG_UNFIXED,
			"buf=%d : %s", bufsize, gfarm_error_string(error));
		return (error);
	}

	for ( ; !gfskd_term; ) {
		len = read(fd, buf, bufsize);
		if (len < 0) {
			err = errno;
			switch (err) {
			case ENOENT:
			case EINTR:
			case EAGAIN:
				continue;
			default:
				error = gfarm_errno_to_error(err);
				gflog_error(GFARM_MSG_UNFIXED, "read fail, %s",
						 gfarm_error_string(error));
				break;
			}
			break;
		}
		if (len < sizeof(struct gfskdev_in_header)) {
			error = GFARM_ERR_INPUT_OUTPUT;
			gflog_error(GFARM_MSG_UNFIXED, "len:%ld, %s", len,
				gfarm_error_string(error));
			break;
		}
		gfskd_recv_req(fd, buf, len);
	}
	free(buf);
	return (error);
}
