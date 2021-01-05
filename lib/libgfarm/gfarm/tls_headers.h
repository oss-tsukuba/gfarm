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
	char *tls_client_ca_certificate_path;
	char *tls_client_ca_revocation_path;
	char *tls_certificate_file;
	char *tls_certificate_chain_file;
	char *tls_key_file;
	int tls_key_update;
	int tls_build_chain_local;
	int tls_allow_crl_absence;
	int network_receive_timeout;
};
typedef struct tls_test_ctx_struct *tls_test_ctx_p;

extern tls_test_ctx_p gfarm_ctxp;

#endif /* TLS_TEST */

/*
 * Logger
 */

/*
 * gflog with TLS runtime message
 */
#define gflog_tls_error(msg_no, ...)	     \
	tlslog_tls_message(msg_no, LOG_ERR, \
		__FILE__, __LINE__, __func__, __VA_ARGS__)
#define gflog_tls_warning(msg_no, ...)	     \
	tlslog_tls_message(msg_no, LOG_WARNING, \
		__FILE__, __LINE__, __func__, __VA_ARGS__)
#define gflog_tls_debug(msg_no, ...)	     \
	tlslog_tls_message(msg_no, LOG_DEBUG, \
		__FILE__, __LINE__, __func__, __VA_ARGS__)
#define gflog_tls_info(msg_no, ...)	     \
	tlslog_tls_message(msg_no, LOG_DEBUG, \
		__FILE__, __LINE__, __func__, __VA_ARGS__)
#define gflog_tls_notice(msg_no, ...)	     \
	tlslog_tls_message(msg_no, LOG_NOTICE, \
		__FILE__, __LINE__, __func__, __VA_ARGS__)
/*
 * Declaration: TLS support version of gflog_message()
 */
static inline void
tlslog_tls_message(int msg_no, int priority,
	const char *file, int line_no, const char *func,
	const char *format, ...) GFLOG_PRINTF_ARG(6, 7);



/*
 * Default ciphersuites for TLSv1.3
 *
 * See also:
 *	https://www.openssl.org/docs/manmaster/man3/\
 *	SSL_CTX_set_ciphersuites.html
 *	https://wiki.openssl.org/index.php/TLS1.3
 *		"Ciphersuites"
 */
#define TLS13_DEFAULT_CIPHERSUITES \
	"TLS_AES_128_GCM_SHA256:" \
	"TLS_AES_256_GCM_SHA384:" \
	"TLS_CHACHA20_POLY1305_SHA256:" \
	"TLS_AES_128_CCM_SHA256:" \
	"TLS_AES_128_CCM_8_SHA256"

static const char *const tls13_valid_cyphers[] = {
	"TLS_AES_128_GCM_SHA256",
	"TLS_AES_256_GCM_SHA384",
	"TLS_CHACHA20_POLY1305_SHA256",
	"TLS_AES_128_CCM_SHA256:",
	"TLS_AES_128_CCM_8_SHA256",

	NULL
};



/*
 * TLS role
 */
typedef enum {
	TLS_ROLE_UNKNOWN = 0,
	TLS_ROLE_CLIENT,
	TLS_ROLE_SERVER
} tls_role_t;
#define TLS_ROLE_INITIATOR	TLS_ROLE_CLIENT
#define TLS_ROLE_ACCEPTOR	TLS_ROLE_SERVER

/*
 * The cookie for TLS
 */
struct tls_session_ctx_struct {
	/*
	 * Cache aware alignment
	 */
	SSL *ssl_;		/* API alloc'd */

	gfarm_error_t last_gfarm_error_;
	int last_ssl_error_;
	bool got_fatal_ssl_error_;
				/* got SSL_ERROR_SYSCALL or SSL_ERROR_SSL */

	tls_role_t role_;
	bool do_mutual_auth_;
	int n_cert_chain_;
	bool is_verified_;
	int cert_verify_callback_error_;
	int cert_verify_result_error_;
	bool is_build_chain_;
	bool is_allow_no_crls_;
	size_t io_total_;	/* How many bytes transmitted */
	size_t io_key_update_;	/* KeyUpdate water level (bytes) */
	ssize_t keyupd_thresh_;	/* KeyUpdate threshold (bytes) */

	SSL_CTX *ssl_ctx_;	/* API alloc'd */
	STACK_OF(X509_NAME) *trusted_certs_;
				/* API alloc'd */
	EVP_PKEY *prvkey_;	/* API alloc'd */
	char *peer_dn_oneline_;	/* malloc'd */
	char *peer_dn_rfc2253_;	/* malloc'd */

	/*
	 * gfarm_ctxp contents backup
	 */
	char *cert_file_;
	char *cert_chain_file_;
	char *prvkey_file_;
	char *ciphersuites_;
	char *ca_path_;
	char *acceptable_ca_path_;
	char *revoke_path_;
};
typedef struct tls_session_ctx_struct *tls_session_ctx_t;

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
typedef struct tls_passwd_cb_arg_struct *tls_passwd_cb_arg_t;

/*
 * Password thingies
*/

/*
 * Static passwd buffer
 */
static char the_privkey_passwd[4096] = { 0 };

/*
 * tty control
 */
static bool is_tty_saved = false;
static struct termios saved_tty = { 0 };

/*
 * MT safeness guarantee, for in case.
 */
static pthread_mutex_t pwd_cb_lock = PTHREAD_MUTEX_INITIALIZER;

static int
tty_passwd_callback(char *buf, int maxlen, int rwflag, void *u);

/*
 * Verify callback
 */
static int
tls_verify_callback(int ok, X509_STORE_CTX *sctx);

#else

#error Don not include this header unless you know what you need.

#endif /* HAVE_TLS_1_3 && IN_TLS_CORE */
