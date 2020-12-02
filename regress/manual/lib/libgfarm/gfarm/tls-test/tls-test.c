#include <netdb.h>

#include <gfarm/gfarm_config.h>

#define IN_TLS_CORE
#define TLS_TEST

#include "tls_headers.h"
#include "tls_funcs.h"

#define ARGUMENT_LEN 3
#define IPADDR_LEN 15
#define PORT_LEN 5
#define MAX_PORT_NUMBER 65535
#define MIN_PORT_NUMBER 1024
#define DECIMAL_NUMBER 10
#define LISTEN_BACKLOG 64
#define BUF_SIZE 1024

static int socketfd;
static bool is_server = false;
static char portnum[PORT_LEN + 1] = "12345";
static char ipaddr[IPADDR_LEN + 1] = {0};

static struct addrinfo hints, *res;
struct sockaddr_in *saddrin;

static void
tls_runtime_init_once(void)
{
	/*
	 * XXX FIXME:
	 *	Are option flags sufficient enough or too much?
	 *	I'm not sure about it, hope it would be a OK.
	 */
	if (likely(OPENSSL_init_ssl(
			OPENSSL_INIT_LOAD_SSL_STRINGS |
			OPENSSL_INIT_LOAD_CRYPTO_STRINGS |
			OPENSSL_INIT_NO_ADD_ALL_CIPHERS |
			OPENSSL_INIT_NO_ADD_ALL_DIGESTS |
			OPENSSL_INIT_ENGINE_ALL_BUILTIN,
			NULL) == 1)) {
		is_tls_runtime_initd = true;
	}
}

static int
tty_passwd_callback(char *buf, int maxlen, int rwflag, void *u)
{
	int ret = 0;
	tls_passwd_cb_arg_t arg = (tls_passwd_cb_arg_t)u;

	(void)rwflag;

	if (likely(arg != NULL)) {
		char p[4096];
		bool has_passwd_cache = is_valid_string(arg->pw_buf_);
		bool do_passwd =
			(has_passwd_cache == false &&
			 arg->pw_buf_ != NULL &&
			 arg->pw_buf_maxlen_ > 0) ?
			true : false;

		if (unlikely(do_passwd == true)) {
			/*
			 * Set a prompt
			 */
			if (is_valid_string(arg->filename_) == true) {
				(void)snprintf(p, sizeof(p),
					"Passphrase for \"%s\": ",
					arg->filename_);
			} else {
				(void)snprintf(p, sizeof(p),
					"Passphrase: ");
			}
		}

		tty_lock();
		{
			if (unlikely(do_passwd == true)) {
				if (tty_get_passwd(arg->pw_buf_,
					arg->pw_buf_maxlen_, p, &ret) ==
					GFARM_ERR_NO_ERROR) {
					goto copy_cache;
				}
			} else if (likely(has_passwd_cache == true)) {
			copy_cache:
				ret = snprintf(buf, maxlen, "%s",
						arg->pw_buf_);
			}
		}
		tty_unlock();
	}

	return (ret);
}

static inline void usage()
{
	fprintf(stderr, "usage:\n\
		-s                  run as server.\n\
		-a <IP address>     specify the ip address.\n\
		-p <port number>    specify the port number.\n\
		-h                  this help\n");
	return;
}

static inline int prologue(int argc, char **argv)
{
	int opt, err, ret = 1;
	uint16_t result;

	while ((opt = getopt(argc, argv, "sa:p:h")) != -1) {
		switch (opt) {
		case 's':
			is_server = true;
			break;
		case 'a':
			snprintf(ipaddr, IPADDR_LEN + 1, "%s", optarg);
			break;
		case 'p':
			snprintf(portnum, PORT_LEN + 1, "%s", optarg);
			break;
		case 'h':
		default:
			usage();
			return ret;
		}
	}

	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_family = AF_INET;
	errno = 0;
	if ((err = getaddrinfo(ipaddr, portnum, &hints, &res)) == 0) {
		saddrin = (struct sockaddr_in *)res->ai_addr;
		result = ntohs(saddrin->sin_port);
		if ( result >= MIN_PORT_NUMBER &&
			result <= MAX_PORT_NUMBER) {
			ret = 0;
		} else {
			fprintf(stderr, "out of port number range.\n");
		}
	} else {
		perror("getaddrinfo");
		fprintf(stderr, "getaddrinfo err: %s\n", gai_strerror(err));
	}
	return ret;
}

static inline int run_server_process()
{
	int acceptfd, ret = 1;
	socklen_t len = sizeof(struct sockaddr_in);
	struct sockaddr_in clientaddr;

	while (1) {
		if ((acceptfd = accept(socketfd,
					(struct sockaddr *)&clientaddr,
					&len)) > -1) {
			printf("accept success.\n");

			int recv_size;
			char buf[BUF_SIZE];
			memset(buf, 0, sizeof(buf));
			errno = 0;
			recv_size = recv(acceptfd, buf, sizeof(buf), 0);
			if (recv_size > 0) {
				printf("recv: %s\n", buf);
			} else {
				if (recv_size == 0) {
					fprintf(stderr, "recv 0 byte.\n");
				} else {
					perror("recv");
				}
				(void)close(acceptfd);
				continue;
			}

			errno = 0;
			if (send(acceptfd, buf, recv_size, 0) > -1) {
				printf("send: %s\n", buf);
			} else {
				perror("send");
			}
			(void)close(acceptfd);
			ret = 0;
		} else {
			perror("accept");
		}
	}
	return ret;
}

static inline int run_server()
{
	int optval = 1, ret = 1;

	if ((socketfd = socket(res->ai_family, res->ai_socktype, 0)) > -1) {
		if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &optval,
				sizeof(optval)) > -1) {
			saddrin->sin_addr.s_addr = INADDR_ANY;
			if (bind(socketfd, res->ai_addr,
					sizeof(*res->ai_addr)) > -1) {
				if (listen(socketfd, LISTEN_BACKLOG) > -1) {
					ret = run_server_process();
				} else {
					perror("listen");
				}
			} else {
				perror("bind");
			}
		} else {
			perror("setsockopt");
		}
		(void)close(socketfd);
	} else {
		perror("socket");
	}
	return ret;
}

static inline int run_client_process()
{
	int ret = 1;
	char buf[BUF_SIZE];
	memset(buf, 0, sizeof(buf));
	if (fgets(buf, BUF_SIZE - 1, stdin) != NULL) {
		errno = 0;
		if (send(socketfd, buf, strlen(buf), 0) > -1) {
			printf("send: %s\n", buf);
		} else {
			perror("send");
		}

		errno = 0;
		if (recv(socketfd, buf, sizeof(buf), 0) > -1) {
			printf("recv: %s\n", buf);
		} else {
			perror("recv");
		}

		ret = 0;
	} else {
		fprintf(stderr, "fgets: can't read string.\n");
	}
	return ret;
}

static inline int run_client()
{
	int ret = 1;
	if ((socketfd = socket(res->ai_family, res->ai_socktype, 0)) > -1) {
		if (connect(socketfd, res->ai_addr,
			sizeof(*res->ai_addr)) > -1) {
			printf("connect success.\n");

			ret = run_client_process();
		} else {
			perror("connect");
		}
		(void)close(socketfd);
	} else {
		perror("socket");
	}
	return ret;
}

int main(int argc, char **argv)
{
	int ret = 1;

	snprintf(ipaddr, IPADDR_LEN + 1, "127.0.0.1");

	if (prologue(argc, argv) == 0) {
		if (is_server) {
			ret = run_server();
		} else {
			ret = run_client();
		}
		freeaddrinfo(res);
	}
	return ret;
}
