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
static int debug_level = 0;
static bool is_server = false;
static bool is_mutual_authentication = false;
static bool is_verify_only = false;
static bool is_once_loop = false;
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
	-INT_MAX,
	-INT_MAX
};
tls_test_ctx_p gfarm_ctxp = &ttcs;

tls_session_ctx_t tls_ctx = NULL;

static void
tls_runtime_init_once(void)
{
	tls_runtime_init_once_body();
}

static int 
tls_verify_callback(int ok, X509_STORE_CTX *sctx) {
	return tls_verify_callback_body(ok, sctx);
}

static int
tty_passwd_callback(char *buf, int maxlen, int rwflag, void *u)
{
	return tty_passwd_callback_body(buf, maxlen, rwflag, u);
}

static inline void
usage()
{
	fprintf(stderr, "usage:\n"
		"\t-h, --help\t\t\t\tthis help.\n"
		"\t-s, --server\t\t\t\trun as server.\n"
		"\t-a, --adress <IP address>\t\tspecify the ip address.\n"
		"\t-p, --port <port number>\t\tspecify the port number.\n"
		"\t--tls_cipher_suite\n"
		"\t--tls_ca_certificate_path\n"
		"\t--tls_ca_revocation_path\n"
		"\t--tls_client_ca_certificate_path\n"
		"\t--tls_client_ca_revocation_path\n"
		"\t--tls_certificate_file\n"
		"\t--tls_certificate_chain_file\n"
		"\t--tls_key_file\n"
		"\t--tls_key_update\n"
		"\t--network_receive_timeout\n"
		"\t--mutual_authentication\n"
		"\t--verify_only\n"
		"\t--once\n"
		"\t--build_chain\n"
		"\t--debug_level\n");
	return;
}

static inline bool
safe_strtol(const char *str, long *result, int base)
{
	bool ret = false;
	long retval_strtol;
	char *endptr;
	size_t len;

	if (str == NULL) {
		return ret;
	} else if (result != NULL) {
		*result = 0;
	}

	if ((len = strlen(str)) > 0) {
		errno = 0;
		retval_strtol = strtol(str, &endptr, base);
		if ((str + len) == endptr) {
			if (errno == 0) {
				if (result != NULL) {
					*result = retval_strtol;
				}
				ret = true;
			}
		}
	}

	return ret;
}

static inline bool
string_to_int(const char *str, int *result, int base)
{
	bool ret = false;
	long retval_strtol;
	if (safe_strtol(str, &retval_strtol, base)) {
		if (retval_strtol <= INT_MAX && retval_strtol >= INT_MIN){
			*result = (int)retval_strtol;
			ret = true;
		} else {
			fprintf(stderr, "out of integer range.\n");
		}
	} else if (errno != 0) {
		perror("strtol");
	}

	return ret;
}

static inline void
ctx_dump()
{
	fprintf(stderr, "tls_cipher_suite: '%s'\n",
			gfarm_ctxp->tls_cipher_suite);
	fprintf(stderr, "tls_ca_certificate_path: '%s'\n",
			gfarm_ctxp->tls_ca_certificate_path);
	fprintf(stderr, "tls_ca_revocation_path: '%s'\n",
			gfarm_ctxp->tls_ca_revocation_path);
	fprintf(stderr, "tls_client_ca_certificate_path: '%s'\n",
			gfarm_ctxp->tls_client_ca_certificate_path);
	fprintf(stderr, "tls_client_ca_revocation_path: '%s'\n",
			gfarm_ctxp->tls_client_ca_revocation_path);
	fprintf(stderr, "tls_certificate_file: '%s'\n",
			gfarm_ctxp->tls_certificate_file);
	fprintf(stderr, "tls_certificate_chain_file: '%s'\n",
			gfarm_ctxp->tls_certificate_chain_file);
	fprintf(stderr, "tls_key_file: '%s'\n",
			gfarm_ctxp->tls_key_file);
	fprintf(stderr, "tls_key_update: %d\n",
			gfarm_ctxp->tls_key_update);
	fprintf(stderr, "tls_build_certificate_chain: %d\n",
			gfarm_ctxp->tls_build_chain_local);
	fprintf(stderr, "network_receive_timeout: %d\n",
			gfarm_ctxp->network_receive_timeout);

	return;
}

static inline void
getopt_arg_dump()
{
	fprintf(stderr, "is_server: %d\n", is_server);
	fprintf(stderr, "address: '%s'\n", ipaddr);
	fprintf(stderr, "portnum: '%s'\n", portnum);
	fprintf(stderr, "tls_cipher_suite: '%s'\n",
			gfarm_ctxp->tls_cipher_suite);
	fprintf(stderr, "tls_ca_certificate_path: '%s'\n",
			gfarm_ctxp->tls_ca_certificate_path);
	fprintf(stderr, "tls_ca_revocation_path: '%s'\n",
			gfarm_ctxp->tls_ca_revocation_path);
	fprintf(stderr, "tls_client_ca_certificate_path: '%s'\n",
			gfarm_ctxp->tls_client_ca_certificate_path);
	fprintf(stderr, "tls_client_ca_revocation_path: '%s'\n",
			gfarm_ctxp->tls_client_ca_revocation_path);
	fprintf(stderr, "tls_certificate_file: '%s'\n",
			gfarm_ctxp->tls_certificate_file);
	fprintf(stderr, "tls_certificate_chain_file: '%s'\n",
			gfarm_ctxp->tls_certificate_chain_file);
	fprintf(stderr, "tls_key_file: '%s'\n",
			gfarm_ctxp->tls_key_file);
	fprintf(stderr, "tls_key_update: %d\n",
			gfarm_ctxp->tls_key_update);
	fprintf(stderr, "tls_build_chain_local: %d\n",
			gfarm_ctxp->tls_build_chain_local);
	fprintf(stderr, "network_receive_timeout: %d\n",
			gfarm_ctxp->network_receive_timeout);
	fprintf(stderr, "mutual_authentication: %d\n",
			is_mutual_authentication);
	fprintf(stderr, "verify_only: %d\n", is_verify_only);
	fprintf(stderr, "once: %d\n", is_once_loop);
	fprintf(stderr, "debug_level: %d\n", debug_level);

	return;
}

static inline int
prologue(int argc, char **argv)
{
	int opt, longindex = 0, err, ret = 1;
	uint16_t result;

	struct option longopts[] = {
		{"help", no_argument, NULL, 'h'},
		{"server", no_argument, NULL, 's'},
		{"adress", required_argument, NULL, 'a'},
		{"port", required_argument, NULL, 'p'},
		{"tls_cipher_suite", required_argument, NULL, 0},
		{"tls_ca_certificate_path", required_argument, NULL, 1},
		{"tls_ca_revocation_path", required_argument, NULL, 2},
		{"tls_client_ca_certificate_path", required_argument, NULL, 3},
		{"tls_client_ca_revocation_path", required_argument, NULL, 4},
		{"tls_certificate_file", required_argument, NULL, 5},
		{"tls_certificate_chain_file", required_argument, NULL, 6},
		{"tls_key_file", required_argument, NULL, 7},
		{"tls_key_update", required_argument, NULL, 8},
		{"network_receive_timeout", required_argument, NULL, 9},
		{"mutual_authentication", no_argument, NULL, 10},
		{"debug_level", required_argument, NULL, 11},
		{"verify_only", no_argument, NULL, 12},
		{"once", no_argument, NULL, 13},
		{"build_chain", no_argument, NULL, 14},
		{NULL, 0, NULL, 0}
	};

	while ((opt = getopt_long(argc, argv, "sa:p:h", longopts,
					&longindex)) != -1) {
		switch (opt) {
		case 0:
			gfarm_ctxp->tls_cipher_suite = optarg;
			break;
		case 1:
			gfarm_ctxp->tls_ca_certificate_path = optarg;
			break;
		case 2:
			gfarm_ctxp->tls_ca_revocation_path = optarg;
			break;
		case 3:
			gfarm_ctxp->tls_client_ca_certificate_path = optarg;
			break;
		case 4:
			gfarm_ctxp->tls_client_ca_revocation_path = optarg;
			break;
		case 5:
			gfarm_ctxp->tls_certificate_file = optarg;
			break;
		case 6:
			gfarm_ctxp->tls_certificate_chain_file = optarg;
			break;
		case 7:
			gfarm_ctxp->tls_key_file = optarg;
			break;
		case 8:
			if (!(string_to_int(optarg,
					&gfarm_ctxp->tls_key_update,
					DECIMAL_NUMBER))) {
				fprintf(stderr,
				"fail to set tls_key_update.\n");
			}
			break;
		case 9:
			if (!(string_to_int(optarg,
					&gfarm_ctxp->network_receive_timeout,
					DECIMAL_NUMBER))) {
				fprintf(stderr,
				"fail to set network_receive_timeout.\n");
			}
			break;
		case 10:
			is_mutual_authentication = true;
			break;
		case 11:
			if (!(string_to_int(optarg,
					&debug_level,
					DECIMAL_NUMBER))) {
				fprintf(stderr,
				"fail to set debug_level.\n");
			}
			break;
		case 12:
			is_verify_only = true;
			break;
		case 13:
			is_once_loop = true;
			break;
		case 14:
			gfarm_ctxp->tls_build_chain_local = 1;
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

	if (debug_level == 10000) {
		ctx_dump();
	} else if (debug_level == 10001) {
		getopt_arg_dump();
	}

	if (is_valid_string(gfarm_ctxp->tls_ca_revocation_path) == false) {
		gfarm_ctxp->tls_ca_revocation_path =
			gfarm_ctxp->tls_ca_certificate_path;
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

static inline int
run_server_process()
{
	int acceptfd, ret = 1;
	struct addrinfo clientaddr;
	clientaddr.ai_addrlen = sizeof(clientaddr.ai_addr);
	bool is_loop = true;

	while (is_loop) {
		if (is_once_loop) {
			is_loop = false;
		}
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
			if (debug_level > 0) {
				fprintf(stderr, "TLS session established.\n");
			}

			if (is_verify_only) {
				gerr = tls_session_clear_ctx(tls_ctx);
				if (gerr == GFARM_ERR_NO_ERROR) {
					ret = 0;
				} else {
					gflog_tls_error(GFARM_MSG_UNFIXED,
						"SSL reset failure: %s",
						gfarm_error_string(gerr));
				}
				return ret;
			}
	
			gerr = tls_session_read(tls_ctx, buf, sizeof(buf),
					&r_size);
			if (gerr == GFARM_ERR_NO_ERROR) {
				if (r_size > 0) {
					buf[r_size] = '\0';
					if (debug_level > 0) {
						fprintf(stderr,
							"got: '%s'\n", buf);
					}
				} else if (r_size == 0) {
					if (debug_level > 0) {
						fprintf(stderr,
							"got 0 byte.\n");
					}
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

static inline int
run_server()
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

static inline int
run_client_process()
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
	if (debug_level > 0) {
		fprintf(stderr, "TLS session established.\n");
	}

	if (is_verify_only) {
		gerr = tls_session_clear_ctx(tls_ctx);
		if (gerr == GFARM_ERR_NO_ERROR) {
			ret = 0;
		} else {
			gflog_tls_error(GFARM_MSG_UNFIXED,
				"SSL reset failure: %s",
				gfarm_error_string(gerr));
		}
		return ret;
	}

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
			fprintf(stdout, "%s\n", buf);
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
	} else if (debug_level > 0) {
		fprintf(stderr, "fgets: Can't read string.\n");
	}

done:
	return ret;
}

static inline int
run_client()
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

int
main(int argc, char **argv)
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
