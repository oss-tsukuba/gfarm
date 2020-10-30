#include <gfarm/gfarm_config.h>

#ifdef HAVE_TLS



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

#include <openssl/ssl.h>
#include <openssl/rand.h>
#include <openssl/err.h>

#include <gfarm/gflog.h>
#include <gfarm/error.h>

#include "context.h"

#include "tlsutils.h"

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
#define is_valid_string(x)	((x != NULL && *x != '\0') ? true : false)



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
	tls_role_t role_;
	char *peer_dn_;		/* malloc'd */
	EVP_PKEY *prvkey_;	/* API alloc'd */
	SSL_CTX *ssl_ctx_;	/* API alloc'd */
	SSL *ssl_;		/* API alloc'd */
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

static inline void
tty_lock(void)
{
	(void)pthread_mutex_lock(&pwd_cb_lock);
}

static inline void
tty_unlock(void)
{
	(void)pthread_mutex_unlock(&pwd_cb_lock);
}

static inline void
tty_save(int ttyfd)
{
	if (likely(ttyfd >= 0)) {
		if (is_tty_saved == false) {
			(void)tcgetattr(ttyfd, &saved_tty);
			is_tty_saved = true;
		}
	}
}

static inline void
tty_reset(int ttyfd)
{
	if (likely(ttyfd >= 0)) {
		if (is_tty_saved == true) {
			(void)tcsetattr(ttyfd, TCSAFLUSH, &saved_tty);
		} else {
			/*
			 * A wild guess: Assume only an ECHO flag is
			 * dropped.
			 */
			struct termios ts;

			(void)tcgetattr(ttyfd, &ts);
			ts.c_lflag |= ECHO;
			(void)tcsetattr(ttyfd, TCSAFLUSH, &ts);
		}
	}
}
		
static inline void
tty_echo_off(int ttyfd)
{
	if (likely(ttyfd >= 0)) {
		struct termios ts;

		tty_save(ttyfd);
		ts = saved_tty;
		ts.c_lflag &= ~ECHO;
		(void)tcsetattr(ttyfd, TCSAFLUSH, &ts);
	}
}
		
static inline void
trim_string_tail(char *buf)
{
	if (is_valid_string(buf) == true) {
		size_t l = strlen(buf);
		char *s = buf;
		char *e = buf + l - 1;

		while (e >= s) {
			if (*e == '\r' || *e == '\n') {
				*e = '\0';
				e--;
			} else {
				break;
			}
		}
	}

	return;
}

/*
 * A password must be acquired from the /dev/tty.
 */
static inline size_t
get_passwd_from_tty(char *buf, size_t maxlen, const char *prompt)
{
	size_t ret = 0;

	if (likely(buf != NULL && maxlen > 1)) {
		int ttyfd = -1;
		FILE *fd = NULL;
		int s_errno;
		
		errno = 0;
		ttyfd = open("/dev/tty", O_RDWR);
		s_errno = errno;
		if (ttyfd >= 0 && (fd = fdopen(ttyfd, "rw")) != NULL) {
			int s_errno;
			char *rst = NULL;
			FILE *fd = fdopen(ttyfd, "rw");

			(void)fprintf(fd, "%s", prompt);

			tty_echo_off(ttyfd);

			(void)memset(buf, 0, maxlen);
			errno = 0;
			rst = fgets(buf, maxlen, fd);
			s_errno = errno;

			tty_reset(ttyfd);
			(void)fprintf(fd, "\n");
			(void)fflush(fd);

			(void)fclose(fd);
			(void)close(ttyfd);
			
			if (likely(rst != NULL)) {
				trim_string_tail(buf);
				ret = strlen(buf);
			} else {
				if (s_errno != 0) {
					gflog_error(GFARM_MSG_UNFIXED,
						"Failed to get a password: %s",
						strerror(s_errno));
				}
			}
		} else {
			gflog_debug(GFARM_MSG_UNFIXED,
				"Failed to open a control terminal: %s",
				strerror(s_errno));
			
		}
	} else {
		gflog_warning(GFARM_MSG_UNFIXED,
			"Invalid buffer and/or buffer length "
			"for password input: %p, %zu", buf, maxlen);
	}

	return(ret);
}

static int
passwd_callback(char *buf, int maxlen, int rwflag, void *u)
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
				if (get_passwd_from_tty(arg->pw_buf_,
					arg->pw_buf_maxlen_, p) > 0) {
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

	return(ret);
}



/*
 * Certificate store & private key file permission check primitives
 */

static inline bool
is_user_in_group(uid_t uid, gid_t gid)
{
	bool ret = false;

	if (geteuid() == uid && getegid() == gid) {
		ret = true;
	} else {
		struct passwd u;
		struct passwd *ures = NULL;
		char ubuf[256];

		errno = 0;
		if (likely(getpwuid_r(uid, &u,
			ubuf, sizeof(ubuf), &ures) == 0 && ures != NULL)) {
			if (u.pw_gid == gid) {
				ret = true;
			} else {
				struct group g;
				struct group *gres = NULL;
				char gbuf[8192];

				errno = 0;
				if (likely(getgrgid_r(gid, &g,
					gbuf, sizeof(gbuf), &gres) == 0 &&
					gres != NULL)) {
					char **p = g.gr_mem;

					while (p != NULL) {
						if (strcmp(u.pw_name,
							*p) == 0) {
							ret = true;
							break;
						}
						p++;
					}
				} else {
					if (errno != 0) {
						gflog_error(GFARM_MSG_UNFIXED,
							"Failed to acquire a "
							"group entry for "
							"gid %d: %s",
							gid, strerror(errno));
					} else {
						gflog_error(GFARM_MSG_UNFIXED,
							"Can't find the group "
							"%d.", gid);
					}
				}
			}
		} else {
			if (errno != 0) {
				gflog_error(GFARM_MSG_UNFIXED,
					"Failed to acquire a passwd entry "
					"for uid %d: %s",
					uid, strerror(errno));
			} else {
				gflog_error(GFARM_MSG_UNFIXED,
					    "Can't find the user %d.", uid);
			}
		}
	}

	return(ret);
}

static inline bool
is_file_readable(const char *file)
{
	bool ret = false;

	if (likely(is_valid_string(file) == true)) {
		struct stat s;

		errno = 0;
		if (likely((stat(file, &s) == 0) &&
			(S_ISDIR(s.st_mode) == 0))) {
			uid_t uid = geteuid();

			if (likely((s.st_uid == uid &&
				    (s.st_mode & S_IRUSR) != 0) ||
				   (is_user_in_group(uid, s.st_gid) == true &&
				    (s.st_mode & S_IRGRP) != 0) ||
				   ((s.st_mode & S_IROTH) != 0))) {
				ret = true;
			} else {
				gflog_error(GFARM_MSG_UNFIXED,
					"The file perrmssion of the specified "
					"file %s is insufficient for read.",
					file);
			}
		} else {
			if (errno != 0) {
				gflog_error(GFARM_MSG_UNFIXED,
					"Failed to stat(\"%s\"): %s",
					file, strerror(errno));
			} else {
				gflog_error(GFARM_MSG_UNFIXED,
					"%s is a directory.", file);
			}
		}
	} else {
		gflog_error(GFARM_MSG_UNFIXED,
			"Specified filename is nul.");
	}

	return(ret);
}

static inline bool
is_valid_ca_dir(const char *dir)
{
	bool ret = false;

	if (is_valid_string(dir) == true) {
		struct stat s;

		errno = 0;
		if (likely((stat(dir, &s) == 0) &&
			(S_ISDIR(s.st_mode) != 0))) {
			uid_t uid = geteuid();

			if (((s.st_mode & S_IRWXO) != 0) ||
				((s.st_mode & S_IRWXU) != 0 &&
				 s.st_uid == uid) ||
				((s.st_mode & S_IRWXG) != 0 &&
				 is_user_in_group(uid, s.st_gid) ==
				 true)) {
				ret = true;
			} else {
				gflog_error(GFARM_MSG_UNFIXED,
					    "%s seems not accessible for "
					    "uid %d.", dir, uid);
			}
		} else {
			if (errno != 0) {
				gflog_error(GFARM_MSG_UNFIXED,
					"Can't access to %s: %s",
					dir, strerror(errno));
			} else {
				gflog_error(GFARM_MSG_UNFIXED,
					"%s is not a directory.", dir);
			}
		}
	} else {
		gflog_error(GFARM_MSG_UNFIXED,
			"Specified CA cert directory name is nul.");
	}

	return(ret);
}

static inline bool
is_valid_prvkey_file_permission(int fd, const char *file)
{
	bool ret = false;

	if (fd >= 0) {
		struct stat s;

		errno = 0;
		if (likely((fstat(fd, &s) == 0) &&
			(S_ISDIR(s.st_mode) == 0))) {
			uid_t uid;
				
			if (likely((uid = geteuid()) == s.st_uid)) {
				if (likely(((s.st_mode &
					(S_IRGRP | S_IWGRP |
					 S_IROTH | S_IWOTH)) == 0) &&
					((s.st_mode &
					  S_IRUSR) != 0))) {
					ret = true;
				} else {
					gflog_error(GFARM_MSG_UNFIXED,
						"The file perrmssion of the "
						"specified file \"%s\" is "
						"open too widely. It would "
						"be nice if the file "
						"permission was 0600.", file);
				}
			} else {
				gflog_error(GFARM_MSG_UNFIXED,
					"This process is about to read other "
					"uid(%d)'s private key file \"%s\", "
					"which is strongly discouraged even "
					"this process can read it for privacy "
					"and security.", uid, file);
			}

		} else {
			if (errno != 0) {
				gflog_error(GFARM_MSG_UNFIXED,
					"Can't access %s: %s",
					file, strerror(errno));
			} else {
				gflog_error(GFARM_MSG_UNFIXED,
					"%s is a directory, not a file", file);
			}
		}
	} else {
		gflog_error(GFARM_MSG_UNFIXED, "Invalid file descriptor: %d.",
			fd);
	}

	return(ret);
}



/*
 * Private key loader
 */

static inline EVP_PKEY *
load_prvkey(const char *file)
{
	EVP_PKEY *ret = NULL;

	if (likely(is_valid_string(file) == true)) {
		FILE *f = NULL;

		errno = 0;
		if (likely(((f = fopen(file, "r")) != NULL) &&
			(is_valid_prvkey_file_permission(fileno(f), file)
			 == true))) {

			struct tls_passwd_cb_arg_struct a = {
				.pw_buf_maxlen_ = sizeof(the_privkey_passwd),
				.pw_buf_ = the_privkey_passwd,
				.filename_ = file
			};
			ret = PEM_read_PrivateKey(f, NULL, passwd_callback,
				(void *)&a);
			(void)fclose(f);
			if (unlikely(ret == NULL)) {
				char b[4096];

				ERR_error_string_n(ERR_get_error(), b,
					sizeof(b));
				gflog_error(GFARM_MSG_UNFIXED,
					"Can't read a PEM format private key "
					"from %s: %s", file, b);
			}
		} else {
			if (errno != 0) {
				gflog_error(GFARM_MSG_UNFIXED,
					"Can't open %s: %s", file,
					strerror(errno));
			}
		}
	}

	return(ret);
}



/*
 * TLS runtime library initialization/finalization
 */

static pthread_once_t tls_init_once = PTHREAD_ONCE_INIT;
static bool is_tls_runtime_initd = false;
	
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

static inline bool
tls_session_runtime_initialize(void)
{
	(void)pthread_once(&tls_init_once, tls_runtime_init_once);
	return is_tls_runtime_initd;
}




/*
 * Internal TLS context constructor/destructor
 */

/*
 * Default ciphersuites for TLSv1.3
 *
 * See also:
 *	https://www.openssl.org/docs/manmaster/man3/\
 *	SSL_CTX_set_ciphersuites.html
 *	https://wiki.openssl.org/index.php/TLS1.3
 *		"Ciphersuites"
 */
const char *tls_default_ciphersuites = 
	"TLS_AES_128_GCM_SHA256:TLS_AES_256_GCM_SHA384:"
	"TLS_CHACHA20_POLY1305_SHA256:TLS_AES_128_CCM_SHA256:"
	"TLS_AES_128_CCM_8_SHA256";

/*
 * Destructor
 */
static inline void
tls_session_ctx_destroy(tls_session_ctx_t x)
{
	if (x != NULL) {
		free(x->peer_dn_);
		if (x->prvkey_ != NULL) {
			EVP_PKEY_free(x->prvkey_);
		}
		if (x->ssl_ctx_ != NULL) {
			(void)SSL_CTX_clear_chain_certs(x->ssl_ctx_);
			SSL_CTX_free(x->ssl_ctx_);
		}
		if (x->ssl_ != NULL) {
			SSL_free(x->ssl_);
		}
		free(x);
	}
}

/*
 * Constructor
 */
static inline tls_session_ctx_t
tls_session_ctx_create(tls_role_t role, bool do_mutual_auth)
{
	tls_session_ctx_t ret = NULL;
	char *cert_file = NULL;
	char *cert_chain_file = NULL;
	char *prvkey_file = NULL;
	char *cert_to_use = NULL;
	EVP_PKEY *prvkey = NULL;
	SSL_CTX *ssl_ctx = NULL;
	bool need_self_cert = false;
	bool need_cert_merge = false;
	bool has_cert_file = false;
	bool has_cert_chain_file = false;

	if (unlikely(gfarm_ctxp == NULL)) {
		gflog_error(GFARM_MSG_UNFIXED,
			"fatal: NULL gfarm_ctxp.");
		goto bailout;
	}
	if (unlikely(role != TLS_ROLE_SERVER && role != TLS_ROLE_CLIENT)) {
		gflog_error(GFARM_MSG_UNFIXED,
			"fatal: invalid TLS role.");
		goto bailout;
	}
	if (unlikely(tls_session_runtime_initialize() == false)) {
		gflog_error(GFARM_MSG_UNFIXED,
			"TLS runtime library initialization failed.");
		goto bailout;
	}

	if (do_mutual_auth == true || role == TLS_ROLE_SERVER) {
		need_self_cert = true;
	}
	if (need_self_cert == true) {
#ifdef TLS_SODA_OK
		cert_file = gfarm_ctxp->tls_certificate_file;
		cert_chain_file = gfarm_ctxp->tls_certificate_chain_file;
#endif /* TLS_SODA_OK */

		if (is_valid_string(cert_chain_file) == true &&
			is_file_readable(cert_chain_file) == true) {
			has_cert_chain_file = true;
		}
		if (is_valid_string(cert_file) == true &&
			is_file_readable(cert_file) == true) {
			has_cert_file = true;
		}

		if (has_cert_chain_file == true && has_cert_file == true) {
			need_cert_merge = true;
		} else if (has_cert_chain_file == true &&
				has_cert_file == false) {
			cert_to_use = cert_chain_file;
		} else if (has_cert_chain_file == false &&
				has_cert_file == true) {
			cert_to_use = cert_file;
		} else {
			gflog_error(GFARM_MSG_UNFIXED,
				"Neither a cert file nor a cert chain "
				"file is specified.");
			goto bailout;
		}
	}

	/*
	 * Load a private key
	 */
#ifdef TLS_SODA_OK
	prvkey_file = gfarm_ctxp->tls_key_file;
#endif /* TLS_SODA_OK */
	if (unlikely(is_valid_string(prvkey_file) == false)) {
		gflog_error(GFARM_MSG_UNFIXED,
			"A private key file is not specified.");
		goto bailout;
	}
	prvkey = load_prvkey(prvkey_file);
	if (unlikely(prvkey == NULL)) {
		gflog_error(GFARM_MSG_UNFIXED,
			"Can't load a private key file \"%s\".",
			prvkey_file);
		goto bailout;
	}

	/*
	 * Create a SSL_CTX
	 */
	if (role == TLS_ROLE_SERVER) {
		ssl_ctx = SSL_CTX_new(TLS_server_method());
	} else if (role == TLS_ROLE_CLIENT) {
		ssl_ctx = SSL_CTX_new(TLS_client_method());
	}
	if (likely(ssl_ctx != NULL)) {
		int osst;
		char *ciphersuites = NULL;

		/*
		 * Clear cert chain for our sanity.
		 */
		(void)SSL_CTX_clear_chain_certs(ssl_ctx);

		/*
		 * Inhibit other than TLSv1.3
		 *	NOTE:
		 *		For environment not support setting
		 *		min/max proto. to only TLS1.3 by
		 *		SSL_CTX_set_{min|max}_proto_version(),
		 *		we use SSL_CTX_set_options().
		 */
		(void)SSL_CTX_set_options(ssl_ctx,
			(SSL_OP_NO_SSLv3 |
			 SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1 |
			 SSL_OP_NO_TLSv1_2 |
			 SSL_OP_NO_DTLSv1 | SSL_OP_NO_DTLSv1_2));

		/*
		 * Set only TLSv1.3 Ciphersuites to the SSL_CTX
		 */
#ifdef TLS_SODA_OK		
		if (is_valid_string(gfarm_ctxp->tls_cipher_suite) == true) {
			ciphersuites = gfarm_ctxp->tls_cipher_suite;
		} else {
			ciphersuites = tls_default_ciphersuites;
		}
#endif /* TLS_SODA_OK */
		if (unlikely(SSL_CTX_set_ciphersuites(ssl_ctx, ciphersuites)
			!= 1)) {
			gflog_error(GFARM_MSG_UNFIXED,
				"Failed to set ciphersuites \"%s\" to the "
				"SSL_CTX.", ciphersuites);
			goto bailout;
		} else {
			/*
			 * XXX FIXME:
			 *	How one can check the ciphers are
			 *	successfully set?
			 *
			 *	call SSL_CTX_get_ciphers() and check each
			 *	STACK_OF(SSL_CIPHER)?
			 */
		}

		/*
		 * Load a cert into the SSL_CTX
		 */
		if (need_self_cert == true) {
			/*
			 * XXX FIXME:
			 *	Using both cert file and cert chain file is
			 *	not supported at this moment. Marging both
			 *	files into single chained cert is needed and
			 *	now we are working on it.
			 *
			 *	IMO, SSL_CTX_add0_chain_cert() siblings and
			 *	SSL_CTX_set_current_cert(SSL_CERT_SET_FIRST)
			 *	should work.
			 */
			if (need_cert_merge == true) {
				gflog_warning(GFARM_MSG_UNFIXED,
					"Merging a cert file and a cert chain "
					"file is not supported at this "
					"moment. It continues with the cert "
					"file \"%s\".", cert_file);
				cert_to_use = cert_file;
			}
			osst = SSL_CTX_use_certificate_chain_file(
				ssl_ctx, cert_to_use);
			if (unlikely(osst != 1)) {
				gflog_error(GFARM_MSG_UNFIXED,
					"Can't load a certificate "
					"file \"%s\" into a SSL_CTX.",
					cert_to_use);
				goto bailout;
			}
		}

		/*
		 * Set a private key into the SSL_CTX
		 */
		if (unlikely((osst = SSL_CTX_use_PrivateKey(
				      ssl_ctx, prvkey)) != 1)) {
			gflog_error(GFARM_MSG_UNFIXED,
				"Can't set a private key to a SSL_CTX.");
			goto bailout;
		}
	}

	/*
	 * Create a new tls_session_ctx_t
	 */
	ret = (tls_session_ctx_t)malloc(
		sizeof(struct tls_session_ctx_struct));
	if (likely(ret != NULL)) {
		(void)memset(ret, 0,
			sizeof(struct tls_session_ctx_struct));
				
		ret->role_ = TLS_ROLE_UNKNOWN;
		ret->prvkey_ = prvkey;
		ret->ssl_ctx_ = ssl_ctx;
		goto ok;
	}

bailout:
	if (prvkey != NULL) {
		EVP_PKEY_free(prvkey);
	}
	if (ssl_ctx != NULL) {
		(void)SSL_CTX_clear_chain_certs(ssl_ctx);
		SSL_CTX_free(ssl_ctx);
	}
	free(ret);

ok:
	return(ret);
}



#else

const int tls_not_used = 1;



#endif /* HAVE_TLS */
