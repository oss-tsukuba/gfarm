#include <netdb.h>
#include <getopt.h>
#include <gfarm/gfarm_config.h>

#define IN_TLS_CORE
#define TLS_TEST

#include "tls_headers.h"
#include "tls_funcs.h"

#define MAX_PORT_NUMBER 65535
#define MIN_PORT_NUMBER 1024
#define LISTEN_BACKLOG 64
#define DECIMAL_NUMBER 10
#define BUF_SIZE 1024

static int socketfd;
static bool is_server = false, is_mutual_authentication = false;
static char *portnum = "12345";
static char *ipaddr = "127.0.0.1";

static struct addrinfo hints, *res;
static struct sockaddr_in *saddrin;

static struct tls_test_ctx_struct ttcs = {
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	-INT_MAX,
	-INT_MAX
};
tls_test_ctx_p gfarm_ctxp = &ttcs;

tls_session_ctx_t tls_ctx = NULL;

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
		-h                  			this help.\n\
		-s                  			run as server.\n\
		-a <IP address>     			specify the ip address.\n\
		-p <port number>    			specify the port number.\n\
		--tls_cipher_suite\n\
		--tls_ca_certificate_path\n\
		--tls_ca_revocation_path\n\
		--tls_client_ca_certificate_path\n\
		--tls_client_ca_revocation_path\n\
		--tls_certificate_file\n\
		--tls_certificate_chain_file\n\
		--tls_key_file\n\
		--tls_key_update\n\
		--network_receive_timeout\n\
		--mutual_authentication\n");
	return;
}

static inline int prologue(int argc, char **argv)
{
	int opt, longindex = 0, err, ret = 1;
	long retval_strtol;
	char *endptr;
	uint16_t result;
	size_t optname_size;

	struct option longopts[] = {
		{"tls_cipher_suite",               required_argument, 0, 0 },
		{"tls_ca_certificate_path",        required_argument, 0, 0 },
		{"tls_ca_revocation_path",         required_argument, 0, 0 },
		{"tls_client_ca_certificate_path", required_argument, 0, 0 },
		{"tls_client_ca_revocation_path",  required_argument, 0, 0 },
		{"tls_certificate_file",           required_argument, 0, 0 },
		{"tls_certificate_chain_file",     required_argument, 0, 0 },
		{"tls_key_file",                   required_argument, 0, 0 },
		{"tls_key_update",                 required_argument, 0, 0 },
		{"network_receive_timeout",        required_argument, 0, 0 },
		{"mutual_authentication",          no_argument,       0, 0 },
		{0,                                0,                 0, 0 }
	};

	while ((opt = getopt_long(argc, argv, "sa:p:h", longopts, &longindex)) != -1) {
		switch (opt) {
		case 0:
			optname_size = sizeof(longopts[longindex].name);
			if (strncmp(longopts[longindex].name, "tls_cipher_suite", optname_size) == 0) {
				gfarm_ctxp->tls_cipher_suite = optarg;
			} else if (strncmp(longopts[longindex].name, "tls_ca_certificate_path", optname_size) == 0) {
				gfarm_ctxp->tls_ca_certificate_path = optarg;
			} else if (strncmp(longopts[longindex].name, "tls_ca_revocation_path", optname_size) == 0) {
				gfarm_ctxp->tls_ca_revocation_path = optarg;
			} else if (strncmp(longopts[longindex].name, "tls_client_ca_certificate_path", optname_size) == 0) {
				gfarm_ctxp->tls_client_ca_certificate_path = optarg;
			} else if (strncmp(longopts[longindex].name, "tls_client_ca_revocation_path", optname_size) == 0) {
				gfarm_ctxp->tls_client_ca_revocation_path = optarg;
			} else if (strncmp(longopts[longindex].name, "tls_certificate_file", optname_size) == 0) {
				gfarm_ctxp->tls_certificate_file = optarg;
			} else if (strncmp(longopts[longindex].name, "tls_certificate_chain_file", optname_size) == 0) {
				gfarm_ctxp->tls_certificate_chain_file = optarg;
			} else if (strncmp(longopts[longindex].name, "tls_key_file", optname_size) == 0) {
				gfarm_ctxp->tls_key_file = optarg;
			} else if (strncmp(longopts[longindex].name, "tls_key_update", optname_size) == 0) {
				errno = 0;
				retval_strtol = strtol(optarg, &endptr, DECIMAL_NUMBER);
				if (errno == 0) {
					if (optarg != '\0' && endptr == '\0'){
						if (retval_strtol <= INT_MAX && retval_strtol >= INT_MIN){
							gfarm_ctxp->tls_key_update = (int)retval_strtol;
						} else {
							fprintf(stderr, "out of integer range.");
						}
					} else {
						fprintf(stderr, "invalid argument: %s\n", endptr);
					}
				} else {
					perror("strtol");
				}
			} else if (strncmp(longopts[longindex].name, "network_receive_timeout", optname_size) == 0) {
				errno = 0;
				retval_strtol = strtol(optarg, &endptr, DECIMAL_NUMBER);
				if (errno == 0) {
					if (optarg != '\0' && endptr == '\0'){
						if (retval_strtol <= INT_MAX && retval_strtol >= INT_MIN){
							gfarm_ctxp->network_receive_timeout = (int)retval_strtol;
						} else {
							fprintf(stderr, "out of integer range.");
						}
					} else {
						fprintf(stderr, "invalid argument: %s\n", endptr);
					}
				} else {
					perror("strtol");
				}
			} else if (strncmp(longopts[longindex].name, "mutual_authentication", optname_size) == 0) {
				is_mutual_authentication = true;
			}

			break;
		case 's':
			is_server = true;
			break;
		case 'a':
			ipaddr = optarg;
			break;
		case 'p':
			portnum = optarg;
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
	struct addrinfo clientaddr;
	clientaddr.ai_addrlen = sizeof(clientaddr.ai_addr);

	while (true) {
		errno = 0;
		if ((acceptfd = accept(socketfd,
					clientaddr.ai_addr,
					&clientaddr.ai_addrlen)) > -1) {
			int r_size = -1;
			int w_size = -1;
			char buf[BUF_SIZE];
			gfarm_error_t gerr = GFARM_ERR_UNKNOWN;

			if ((gerr = tls_session_establish(tls_ctx,
					acceptfd)) != GFARM_ERR_NO_ERROR) {
				close(acceptfd);
				gflog_tls_error(GFARM_MSG_UNFIXED,
					"Can't establish an SSL "
					"connection: %s",
					gfarm_error_string(gerr));
				break;
			}
			fprintf(stderr, "TLS session established.\n");
			
			gerr = tls_session_read(tls_ctx, buf, sizeof(buf),
					&r_size);
			if (gerr == GFARM_ERR_NO_ERROR) {
				if (r_size > 0) {
					buf[r_size] = '\0';
					fprintf(stderr, "got: '%s'\n", buf);
				} else if (r_size == 0) {
					fprintf(stderr, "got 0 byte.\n");
					goto teardown;
				}
			} else {
				gflog_tls_error(GFARM_MSG_UNFIXED,
					"SSL read failure: %s",
					gfarm_error_string(gerr));
			teardown:
				gerr = tls_session_clear_ctx(tls_ctx);
				if (gerr == GFARM_ERR_NO_ERROR || 
					r_size == 0) {
					continue;
				} else {
					gflog_tls_error(GFARM_MSG_UNFIXED,
						"SSL reset failure: %s",
						gfarm_error_string(gerr));
					break;
				}
			}

			buf[r_size] = '\0';
			gerr = tls_session_write(tls_ctx, buf, r_size,
					 &w_size);
			if (gerr == GFARM_ERR_NO_ERROR && w_size == r_size) {
				goto teardown2;
			} else {
				gflog_tls_error(GFARM_MSG_UNFIXED,
					"SSL write failure: %s",
					gfarm_error_string(gerr));
			teardown2:
				gerr = tls_session_clear_ctx(tls_ctx);
				if (gerr == GFARM_ERR_NO_ERROR) {
					continue;
				} else {
					gflog_tls_error(GFARM_MSG_UNFIXED,
						"SSL reset failure: %s",
						gfarm_error_string(gerr));
					break;
				}
			}
		} else {
			perror("accept");
			break;
		}
	}

	return (ret);
}

static inline int run_server()
{
	int optval = 1, ret = 1;

	if ((socketfd = socket(res->ai_family, res->ai_socktype, 0)) > -1) {
		if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &optval,
				sizeof(optval)) > -1) {
			saddrin->sin_addr.s_addr = INADDR_ANY;
			if (bind(socketfd, res->ai_addr,
					res->ai_addrlen) > -1) {
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
	gfarm_error_t gerr = GFARM_ERR_UNKNOWN;
	int r_size = -1;
	int w_size = -1;

	if ((gerr = tls_session_establish(tls_ctx, socketfd)) !=
		GFARM_ERR_NO_ERROR) {
		gflog_tls_error(GFARM_MSG_UNFIXED,
				"Can't establish an SSL "
				"connection: %s",
				gfarm_error_string(gerr));
		return (ret);
	}
	fprintf(stderr, "TLS session established.\n");
	
	errno = 0;
	if (fgets(buf, sizeof(buf) -1, stdin) != NULL) {
		r_size = strlen(buf);
		errno = 0;
		gerr = tls_session_write(tls_ctx, buf, r_size, &w_size);
		if (gerr != GFARM_ERR_NO_ERROR || w_size != r_size) {
			gflog_tls_error(GFARM_MSG_UNFIXED,
				"SSL write failure: %s",
				gfarm_error_string(gerr));
			gerr = tls_session_clear_ctx(tls_ctx);
			if (gerr != GFARM_ERR_NO_ERROR) {
				gflog_tls_error(GFARM_MSG_UNFIXED,
					"SSL reset failure: %s",
					gfarm_error_string(gerr));
			}
			goto done;
		}

		gerr = tls_session_read(tls_ctx, buf, sizeof(buf), &r_size);
		if (gerr == GFARM_ERR_NO_ERROR && r_size == w_size) {
			buf[r_size] = '\0';
			fprintf(stdout, "got reply: '%s'\n", buf);
			fflush(stdout);
			ret = 0;
			goto teardown;
		} else {
			gflog_tls_error(GFARM_MSG_UNFIXED,
				"SSL read failure: %s",
					gfarm_error_string(gerr));
		teardown:
			gerr = tls_session_clear_ctx(tls_ctx);
			if (gerr != GFARM_ERR_NO_ERROR) {
				gflog_tls_error(GFARM_MSG_UNFIXED,
					"SSL reset failure: %s",
					gfarm_error_string(gerr));
			}
		}
	} else {
		fprintf(stderr, "fgets: Can't read string.\n");
	}

done:
	return ret;
}

static inline int run_client()
{
	int ret = 1;
	if ((socketfd = socket(res->ai_family, res->ai_socktype, 0)) > -1) {
		if (connect(socketfd, res->ai_addr,
			res->ai_addrlen) > -1) {
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

	if ((ret = prologue(argc, argv)) == 0) {
		gfarm_error_t gerr = GFARM_ERR_UNKNOWN;
	
		gflog_initialize();
		(void)gflog_auth_set_verbose(100);

		gerr = tls_session_create_ctx(&tls_ctx,
				(is_server == true) ?
				TLS_ROLE_SERVER : TLS_ROLE_CLIENT,
				is_mutual_authentication);
		if (gerr == GFARM_ERR_NO_ERROR) {
			ret = (is_server == true) ?
				run_server() : run_client();
		} else {
			gflog_error(GFARM_MSG_UNFIXED,
				"Can't create a tls session context: %s",
				gfarm_error_string(gerr));
		}

		(void)tls_session_destroy_ctx(tls_ctx);
		freeaddrinfo(res);
	}

	return (ret);
}
