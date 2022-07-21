#pragma once

#if defined(HAVE_TLS_1_3) && defined(IN_TLS_CORE)

#include <inttypes.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>
#include <stdio.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <sys/socket.h>
#include <pthread.h>
#include <termios.h>
#include <dirent.h>
#ifdef HAVE_POLL
#include <poll.h>
#endif /* HAVE_POLL */
#include <sys/time.h>

#include <openssl/ssl.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <openssl/x509_vfy.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/obj_mac.h>
#include <openssl/objects.h>

#include <gfarm/gflog.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

#include "liberror.h"
#ifndef TLS_TEST
#include "context.h"
#endif /* ! TLS_TEST */
#include "iobuffer.h"
#include "gfp_xdr.h"
#include "config.h"

#include "io_tls.h"

#ifdef likely
#undef likely
#endif /* likely */
#ifdef unlikely
#undef unlikely
#endif /* unlikely */
#ifdef __GNUC__
#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)
#else
#define likely(x)       (x)
#define unlikely(x)     (x)
#endif /* __GNUC__ */

#ifdef is_valid_string
#undef is_valid_string
#endif /* is_valid_string */
#define is_valid_string(x)	(((x) != NULL && *(x) != '\0') ? true : false)

#ifndef GFP_XDR_TLS_ROLE
#define GFP_XDR_TLS_ROLE    1
#endif /* GFP_XDR_TLS_ROLE */
#ifndef GFP_XDR_TLS_ACCEPT
#define GFP_XDR_TLS_ACCEPT    0
#endif /* GFP_XDR_TLS_ACCEPT */
#ifndef GFP_XDR_TLS_INITIATE
#define GFP_XDR_TLS_INITIATE    1
#endif /* GFP_XDR_TLS_INITIATE */
#ifndef GFP_XDR_TLS_CLIENT_AUTHENTICATION
#define GFP_XDR_TLS_CLIENT_AUTHENTICATION    2
#endif /* GFP_XDR_TLS_CLIENT_AUTHENTICATION */
#ifndef GFP_XDR_TLS_ROLE_IS_INITIATOR
#define GFP_XDR_TLS_ROLE_IS_INITIATOR(flags)			\
	(((flags) & GFP_XDR_TLS_ROLE) == GFP_XDR_TLS_INITIATE)
#endif /* GFP_XDR_TLS_ROLE_IS_INITIATOR */

#ifdef TLS_TEST

/* Copied from logutil.c */
#define LOG_VERBOSE_COMPACT	0
#define LOG_VERBOSE_LINENO	(1<<0)
#define LOG_VERBOSE_FUNC	(1<<1)
#define LOG_VERBOSE_LINENO_FUNC	(LOG_VERBOSE_LINENO|LOG_VERBOSE_FUNC)

struct tls_test_ctx_struct {
	char *tls_cipher_suite;
	char *tls_ca_certificate_path;
	char *tls_ca_revocation_path;
	char *tls_ca_peer_verify_chain_path;
	char *tls_certificate_file;
	char *tls_certificate_chain_file;
	char *tls_key_file;
	int tls_key_update;		/* gfarm: bool, test: int */
	int tls_build_chain_local;	/* bool */
	int tls_allow_no_crl;		/* bool */
	int tls_security_level;
	int network_receive_timeout;
	int network_send_timeout;
};

extern struct tls_test_ctx_struct *gfarm_ctxp;

#endif /* TLS_TEST */

/*
 * TLS role
 */
enum tls_role {
	TLS_ROLE_UNKNOWN = 0,
	TLS_ROLE_CLIENT,
	TLS_ROLE_SERVER
};
#define TLS_ROLE_INITIATOR	TLS_ROLE_CLIENT
#define TLS_ROLE_ACCEPTOR	TLS_ROLE_SERVER

/*
 * The cookie for TLS
 */
struct tls_session_ctx_struct {
	SSL *ssl_;		/* API alloc'd */

	int last_ssl_error_;
	bool is_got_fatal_ssl_error_;
				/* got SSL_ERROR_SYSCALL or SSL_ERROR_SSL */
	size_t io_total_;	/* How many bytes transmitted */
	size_t io_key_update_accum_;
				/* KeyUpdate current water level (bytes) */
	ssize_t io_key_update_thresh_;
				/* KeyUpdate threshold (bytes) */

	gfarm_error_t last_gfarm_error_;

	/*
	 * Read-only parameters start
	 */
	enum tls_role role_;
	bool do_mutual_auth_;
	bool do_build_chain_;
	bool do_allow_no_crls_;
	bool do_allow_proxy_cert_;
	char *cert_file_;
	char *cert_chain_file_;
	char *prvkey_file_;
	char *ciphersuites_;
	char *ca_path_;
	char *acceptable_ca_path_;
	char *revoke_path_;
	/*
	 * Read-only parameters end
	 */

	char *peer_dn_oneline_;		/* malloc'd */
	char *peer_dn_rfc2253_;		/* malloc'd */
	char *peer_dn_gsi_;		/* malloc'd */
	char *peer_cn_;			/* malloc'd */

	bool is_handshake_tried_;
	bool is_verified_;
	bool is_got_proxy_cert_;

	int cert_verify_callback_error_;
	int cert_verify_result_error_;

	STACK_OF(X509_NAME) (*trusted_certs_);
					/* API alloc'd */

	SSL_CTX *ssl_ctx_;		/* API alloc'd */
	EVP_PKEY *prvkey_;		/* API alloc'd */
	X509_NAME *proxy_issuer_;	/* API alloc'd */
};

#define CTX_CLEAR_RECONN	1
#define CTX_CLEAR_VAR	2
#define	CTX_CLEAR_SSL	4
#define CTX_CLEAR_CTX	8

#define CTX_CLEAR_READY_FOR_RECONNECT \
	CTX_CLEAR_RECONN
#define CTX_CLEAR_READY_FOR_ESTABLISH \
	(CTX_CLEAR_VAR | CTX_CLEAR_SSL)
#define CTX_CLEAR_FREEUP \
	(CTX_CLEAR_VAR | CTX_CLEAR_SSL | CTX_CLEAR_CTX)

/*
 * Password callback arg
 */
struct tls_passwd_cb_arg_struct {
	size_t pw_buf_maxlen_;
	char *pw_buf_;
		/*
		 * == '\0':
		 *	the callback acquires a passwd string from a controll
		 *	terminal asigned for this process, copies the acquired
		 *	passwd string to pw_buf_.
		 *
		 * != '\0':
		 *	the callback returns pw_buf_.
		 *
		 * == NULL:
		 *	the callback does nothing and returns NULL.
		 */
	const char *filename_;
};

struct cert_add_method_struct {
	int (*f)(SSL_CTX *ctx, X509 *cert);
	char *name;
};

/*
 * Pre-declarations
 */
static inline gfarm_error_t
tls_session_shutdown(struct tls_session_ctx_struct *ctx);

static inline char *
tls_session_peer_cn(struct tls_session_ctx_struct *ctx);

/*
 * Logger
 */

#define TLS_LOG_MSG_LEN	2048

/*
 *TLS support version of gflog_message()
 */
static inline void
tlslog_tls_message(int msg_no, int priority,
	const char *file, int line_no, const char *func,
	const char *format, ...) GFLOG_PRINTF_ARG(6, 7);

/*
 * gflog with TLS runtime message
 */
#define tls_log_template(msg_no, level, ...)	     \
	do {					     \
		if (gflog_auth_get_verbose() != 0 && \
			gflog_get_priority_level() >= level) {		\
			tlslog_tls_message(msg_no, level,		\
				__FILE__, __LINE__, __func__, __VA_ARGS__); \
		}							\
	} while (false)

#define gflog_tls_error(msg_no, ...)		\
	tls_log_template(msg_no, LOG_ERR, __VA_ARGS__)
#define gflog_tls_warning(msg_no, ...)		\
	tls_log_template(msg_no, LOG_WARNING, __VA_ARGS__)
#define gflog_tls_debug(msg_no, ...)	     \
	tls_log_template(msg_no, LOG_DEBUG, __VA_ARGS__)
#define gflog_tls_info(msg_no, ...)	     \
	tls_log_template(msg_no, LOG_INFO, __VA_ARGS__)
#define gflog_tls_notice(msg_no, ...)	       \
	tls_log_template(msg_no, LOG_NOTICE, __VA_ARGS__)

#else

#error Don not include this header unless you know what you need.

#endif /* HAVE_TLS_1_3 && IN_TLS_CORE */
