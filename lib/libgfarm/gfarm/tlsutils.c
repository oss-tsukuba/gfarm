#include <gfarm/gfarm_config.h>

#ifdef HAVE_TLS_1_3



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
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

#include "context.h"
#include "iobuffer.h"
#include "gfp_xdr.h"
#include "tlsutils.h"
#include "config.h"



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



#define TLS_TEST

#ifdef TLS_TEST

#define gfarm_ctxp	o_r_g_gfarm_ctxp__

struct tls_test_ctx_struct {
	char *tls_cipher_suite;
	char *tls_ca_certificate_path;
	char *tls_ca_revocation_path;
	char *tls_client_ca_certificate_path;
	char *tls_client_ca_revocation_path;
	char *tls_certificate_file;
	char *tls_certificate_chain_file;
	char *tls_key_file;
};
typedef struct tls_test_ctx_struct *tls_test_ctx_p;

static tls_test_ctx_p gfarm_ctxp = NULL;

#endif /* TLS_TEST */



/*
 * Logger
 */

/*
 * gflog with TLS runtime message
 */
#define gflog_tls_error(msg_no, ...)	     \
	tlslog_runtime_message(msg_no, LOG_ERR, \
		__FILE__, __LINE__, __func__, __VA_ARGS__)
#define gflog_tls_warning(msg_no, ...)	     \
	tlslog_runtime_message(msg_no, LOG_WARNING, \
		__FILE__, __LINE__, __func__, __VA_ARGS__)
/*
 * Declaration: TLS support version of gflog_message()
 */
static inline void
tlslog_runtime_message(int msg_no, int priority,
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
 * TLS session re-key threshold (in bytes)
 *
 * XXX FIXME:	how many bytes should we choose?
 */
#define TLS_SESSION_REKEY_BYTES	1024*1024*1024



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
	EVP_PKEY *prvkey_;	/* API alloc'd */
	SSL_CTX *ssl_ctx_;	/* API alloc'd */
	SSL *ssl_;		/* API alloc'd */
	char *peer_dn_;		/* malloc'd */
	size_t r_total_;	/* total bytes read */
	size_t w_total_;	/* total bytes written */
	bool got_fatal_ssl_error_;
				/* got SSL_ERROR_SYSCALL or SSL_ERROR_SSL */
	int last_ssl_error_;
	gfarm_error_t last_gfarm_error_;
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
 * SSL_{read|write} function type
 */
typedef int (*SSL_io_func_t)(SSL *ssl, void *buf, int num);



/*
 * misc. utils.
 */

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

/*
 * A password must be acquired from the /dev/tty.
 */
static inline size_t
tty_get_passwd(char *buf, size_t maxlen, const char *prompt)
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
 * Validators
 */

/*
 * TLSv1.3 suitable cipher checker
 */
static inline bool
is_str_a_tls13_allowed_cipher(const char *str)
{
	bool ret = false;

	if (likely(is_valid_string(str) == true)) {
		const char * const *c = tls13_valid_cyphers;
		do {
			if (strcmp(str, *c) == 0) {
				ret = true;
				break;
			}
		} while (*++c != NULL);

		if (ret != true) {
			gflog_error(GFARM_MSG_UNFIXED,
				"\"%s\" is not a TLSv1.3 suitable cipher.",
				str);
		}
	}

	return(ret);
}

static inline bool
is_ciphersuites_ok(const char *cipher_list)
{
	bool ret = false;

	if (likely(is_valid_string(cipher_list) == true)) {
		if (strchr(cipher_list, ':') == NULL) {
			ret = is_str_a_tls13_allowed_cipher(cipher_list);
		} else {
			char *str = strdup(cipher_list);
			char *p = str;
			char *c;
			bool loop = true;

			do {
				c = strchr(p, ':');
				if (c != NULL) {
					*c = '\0';
				} else {
					loop = false;
				}
				ret = is_str_a_tls13_allowed_cipher(p);
				if (ret == true) {
					break;
				}
				p = ++c;
			} while (loop == true);

			free(str);
		}
	}

	return(ret);
}

/*
 * Directory/File permission check primitives
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
is_file_readable(int fd, const char *file)
{
	bool ret = false;

	if (likely(is_valid_string(file) == true || fd >= 0)) {
		struct stat s;
		int st;

		errno = 0;
		if (fd >= 0) {
			st = fstat(fd, &s);
		} else {
			st = stat(file, &s);
		}
		if (likely((st == 0) &&	(S_ISDIR(s.st_mode) == 0))) {
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
is_valid_prvkey_file_permission(int fd, const char *file)
{
	bool ret = false;

	if (fd >= 0) {
		struct stat s;
		int st;

		errno = 0;
		if (fd >= 0) {
			st = fstat(fd, &s);
		} else {
			st = stat(file, &s);
		}
		if (likely((st == 0) &&	(S_ISDIR(s.st_mode) == 0))) {
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

static inline bool
is_valid_cert_store_dir(const char *dir)
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



/*
 * TLS thingies
 */

/*
 * Implementation: TLS support version of gflog_message()
 */
static inline void
tlslog_runtime_message(int msg_no, int priority,
	const char *file, int line_no, const char *func,
	const char *format, ...)
{
#ifdef BASIC_BUFSZ
#define BASIC_BUFSZ_ORG	BASIC_BUFSZ	
#endif /* BASIC_BUFSZ */
#define BASIC_BUFSZ	PATH_MAX

	char msgbuf[BASIC_BUFSZ];
	va_list ap;

	va_start(ap, format);
	(void)vsnprintf(msgbuf, sizeof(msgbuf), format, ap);
	va_end(ap);

	if (ERR_peek_error() == 0) {
		gflog_message(msg_no, priority, file, line_no, func,
			"%s", msgbuf);
	} else {
		char msgbuf2[BASIC_BUFSZ * 3];
		char tlsmsg[BASIC_BUFSZ];
		const char *tls_file = NULL;
		int tls_line = -1;
		unsigned int err;

		/*
		 * NOTE:
		 *	OpenSSL 1.1.1 has no ERR_get_error_all().
		 */
		err = ERR_get_error_line_data(&tls_file, &tls_line,
			NULL, NULL);

		ERR_error_string_n(err, tlsmsg, sizeof(tlsmsg));

		(void)snprintf(msgbuf2, sizeof(msgbuf2),
			"%s: [TLS runtime info:%s:%d: %s]",
			msgbuf, tls_file, tls_line, tlsmsg);

		gflog_message(msg_no, priority, file, line_no, func,
			"%s", msgbuf2);
	}
#undef BASIC_BUFSZ
#ifdef BASIC_BUFSZ_ORG
#define BASIC_BUFSZ BASIC_BUFSZ_ORG
#undef BASIC_BUFSZ_ORG
#endif /* BASIC_BUFSZ_ORG */
}

/*
 * TLS runtime library initialization
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
 * TLS runtime error handling
 */

static inline bool
tls_has_runtime_error(void)
{
	return((ERR_peek_error() == 0) ? false : true);
}

static inline void
tls_runtime_flush_error(void)
{
	for ((void)ERR_get_error(); ERR_get_error() != 0;);
}

/*
 * Private key loader
 */
static inline EVP_PKEY *
tls_load_prvkey(const char *file)
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

			tls_runtime_flush_error();
			ret = PEM_read_PrivateKey(f, NULL, tty_passwd_callback,
				(void *)&a);
			if (unlikely(ret == NULL ||
				tls_has_runtime_error() == true)) {
				gflog_tls_error(GFARM_MSG_UNFIXED,
					"Can't read a PEM format private key "
					"from %s.", file);
			}
		} else {
			if (errno != 0) {
				gflog_error(GFARM_MSG_UNFIXED,
					"Can't open %s: %s", file,
					strerror(errno));
			}
		}
		if (f != NULL) {
			(void)fclose(f);
		}

	}

	return(ret);
}

/*
 * Internal TLS context constructor/destructor
 */

/*
 * Constructor
 */
static inline gfarm_error_t
tls_session_ctx_create(tls_session_ctx_t *ctxptr,
		       tls_role_t role, bool do_mutual_auth)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;
	tls_session_ctx_t ctxret = NULL;
	char *cert_file = NULL;
	char *cert_chain_file = NULL;
	char *prvkey_file = NULL;
	char *ciphersuites = NULL;
	char *cert_to_use = NULL;
	EVP_PKEY *prvkey = NULL;
	SSL_CTX *ssl_ctx = NULL;
	bool need_self_cert = false;
	bool need_cert_merge = false;
	bool has_cert_file = false;
	bool has_cert_chain_file = false;

	/*
	 * Parameter check
	 */
	if (unlikely(ctxptr == NULL)) {
		gflog_error(GFARM_MSG_UNFIXED,
			"return pointer is NULL.");
		ret = GFARM_ERR_INVALID_ARGUMENT;
		goto bailout;
	} else {
		*ctxptr = NULL;
	}
	if (unlikely(role != TLS_ROLE_SERVER && role != TLS_ROLE_CLIENT)) {
		gflog_error(GFARM_MSG_UNFIXED,
			"fatal: invalid TLS role.");
		ret = GFARM_ERR_INVALID_ARGUMENT;
		goto bailout;
	}

	/*
	 * Gfarm context check
	 */
	if (unlikely(gfarm_ctxp == NULL)) {
		gflog_error(GFARM_MSG_UNFIXED,
			"fatal: NULL gfarm_ctxp.");
		ret = GFARM_ERR_INTERNAL_ERROR;
		goto bailout;
	}

	/*
	 * Self certificate check
	 */
	if (do_mutual_auth == true || role == TLS_ROLE_SERVER) {
		need_self_cert = true;
	}
	if (need_self_cert == true) {
		cert_file = gfarm_ctxp->tls_certificate_file;
		cert_chain_file = gfarm_ctxp->tls_certificate_chain_file;

		if (is_valid_string(cert_chain_file) == true &&
			is_file_readable(-1, cert_chain_file) == true) {
			has_cert_chain_file = true;
		}
		if (is_valid_string(cert_file) == true &&
			is_file_readable(-1, cert_file) == true) {
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
			ret = GFARM_ERR_INVALID_ARGUMENT;
			goto bailout;
		}
	}

	/*
	 * Ciphersuites check
	 * Set only TLSv1.3 allowed ciphersuites
	 */
	if (is_valid_string(gfarm_ctxp->tls_cipher_suite) == true) {
		if (is_ciphersuites_ok(
			gfarm_ctxp->tls_cipher_suite) == true) {
			ciphersuites = gfarm_ctxp->tls_cipher_suite;
		} else {
			ret = GFARM_ERR_INVALID_ARGUMENT;
			goto bailout;
		}
	} else {
		ciphersuites = TLS13_DEFAULT_CIPHERSUITES;
	}

	/*
	 * Private key check
	 */
	prvkey_file = gfarm_ctxp->tls_key_file;
	if (unlikely(is_valid_string(prvkey_file) == false)) {
		gflog_error(GFARM_MSG_UNFIXED,
			"A private key file is not specified.");
		ret = GFARM_ERR_INVALID_ARGUMENT;
		goto bailout;
	}

	/*
	 * TLS runtime initialize
	 */
	if (unlikely(tls_session_runtime_initialize() == false)) {
		gflog_error(GFARM_MSG_UNFIXED,
			"TLS runtime library initialization failed.");
		ret = GFARM_ERR_TLS_RUNTIME_ERROR;
		goto bailout;
	}

	/*
	 * OK, ready to build a TSL environment up.
	 */

	/*
	 * Load a private key
	 */
	prvkey = tls_load_prvkey(prvkey_file);
	if (unlikely(prvkey == NULL)) {
		gflog_error(GFARM_MSG_UNFIXED,
			"Can't load a private key file \"%s\".",
			prvkey_file);
		ret = GFARM_ERR_INVALID_CREDENTIAL;
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
		 *
		 *	XXX FIXMR:
		 *		Since the manual doesn't mention about
		 *		failure return code, checking TLS
		 *		errors instead checking return code.
		 */
		tls_runtime_flush_error();
		(void)SSL_CTX_set_options(ssl_ctx,
			(SSL_OP_NO_SSLv3 |
			 SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1 |
			 SSL_OP_NO_TLSv1_2 |
			 SSL_OP_NO_DTLSv1 | SSL_OP_NO_DTLSv1_2));
		if (unlikely(tls_has_runtime_error() == true)) {
			gflog_tls_error(GFARM_MSG_UNFIXED,
				"Undocumented runtime failure.");
			ret = GFARM_ERR_TLS_RUNTIME_ERROR;
			goto bailout;
		}

		/*
		 * Set ciphersuites
		 */
		if (unlikely(SSL_CTX_set_ciphersuites(ssl_ctx, ciphersuites)
			!= 1)) {
			gflog_error(GFARM_MSG_UNFIXED,
				"Failed to set ciphersuites \"%s\" to the "
				"SSL_CTX.", ciphersuites);
			ret = GFARM_ERR_TLS_RUNTIME_ERROR;
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
			tls_runtime_flush_error();			
			osst = SSL_CTX_use_certificate_chain_file(
				ssl_ctx, cert_to_use);
			if (unlikely(osst != 1 ||
				tls_has_runtime_error() == true)) {
				gflog_tls_error(GFARM_MSG_UNFIXED,
					"Can't load a certificate "
					"file \"%s\" into a SSL_CTX.",
					cert_to_use);
				ret = GFARM_ERR_TLS_RUNTIME_ERROR;
				goto bailout;
			}
		}

		/*
		 * Set a private key into the SSL_CTX
		 */
		tls_runtime_flush_error();
		if (unlikely((osst = SSL_CTX_use_PrivateKey(
				      ssl_ctx, prvkey) != 1) ||
			(tls_has_runtime_error() == true))) {
			gflog_tls_error(GFARM_MSG_UNFIXED,
				"Can't set a private key to a SSL_CTX.");
			ret = GFARM_ERR_TLS_RUNTIME_ERROR;
			goto bailout;
		}

		/*
		 * CA store/Revocation path
		 */
	}

	/*
	 * Create a new tls_session_ctx_t
	 */
	ctxret = (tls_session_ctx_t)malloc(
		sizeof(struct tls_session_ctx_struct));
	if (likely(ctxret != NULL)) {
		(void)memset(ctxret, 0,
			sizeof(struct tls_session_ctx_struct));
		ctxret->role_ = role;
		ctxret->prvkey_ = prvkey;
		ctxret->ssl_ctx_ = ssl_ctx;

		/*
		 * All done.
		 */
		*ctxptr = ctxret;
		ret = GFARM_ERR_NO_ERROR;
		goto ok;
	} else {
		gflog_error(GFARM_MSG_UNFIXED,
			"Can't allocate a TLS session context.");
		ret = GFARM_ERR_NO_MEMORY;
	}

bailout:
	if (prvkey != NULL) {
		EVP_PKEY_free(prvkey);
	}
	if (ssl_ctx != NULL) {
		(void)SSL_CTX_clear_chain_certs(ssl_ctx);
		SSL_CTX_free(ssl_ctx);
	}
	free(ctxret);

ok:
	return(ret);
}

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
 * TLS I/O operations
 */

/*
 * SSL_ERROR_ handler
 */
static inline bool
tls_io_continuable(int sslerr, tls_session_ctx_t ctx)
{
	bool ret = false;

	/*
	 * NOTE:
	 *	This routine must be transparent among all the type of
	 *	BIO.  So whole the causable SSL_ERROR_* must be care
	 *	about.
	 */

	ctx->last_ssl_error_ = sslerr;

	switch (sslerr) {

	case SSL_ERROR_WANT_READ:
	case SSL_ERROR_WANT_ASYNC:
	case SSL_ERROR_WANT_ASYNC_JOB:
		/*
		 * just retry.
		 */
		ctx->last_gfarm_error_ = GFARM_ERR_NO_ERROR;
		ret = true;
		break;

	case SSL_ERROR_SYSCALL:
		/*
		 * fetch errno
		 */
		if (unlikely(errno == 0)) {
			/*
			 * NOTE:
			 *	This happend on OpenSSL version < 3.0.0
			 *	means "unexpected EOF from the peer."
			 */
			ctx->last_gfarm_error_ = GFARM_ERR_UNEXPECTED_EOF;
		} else {
			ctx->last_gfarm_error_ = gfarm_errno_to_error(errno);
		}
		ctx->got_fatal_ssl_error_ = true;
		break;

	case SSL_ERROR_SSL:
		/*
		 * TLS runtime error
		 */
		ctx->last_gfarm_error_ = GFARM_ERR_TLS_RUNTIME_ERROR;
		ctx->got_fatal_ssl_error_ = true;
		break;

	case SSL_ERROR_ZERO_RETURN:
		/*
		 * Peer sent close_notify. Not retryable.
		 */
		ctx->last_gfarm_error_ = GFARM_ERR_TLS_GOT_CLOSE_NOTIFY;
		break;

	case SSL_ERROR_WANT_X509_LOOKUP:
	case SSL_ERROR_WANT_CLIENT_HELLO_CB:
	case SSL_ERROR_WANT_CONNECT:
	case SSL_ERROR_WANT_ACCEPT:
		/*
		 * MUST not occured, connect/accept must be done
		 * BEFORE gfp_* thingies call this function.
		 */
		gflog_error(GFARM_MSG_UNFIXED,
			"The TLS handshake must be done before begining "
			"data I/O in Gfarm.");
		ctx->last_gfarm_error_ = GFARM_ERR_INTERNAL_ERROR;
		break;

	default:
		gflog_error(GFARM_MSG_UNFIXED,
			"All the TLS I/O error must be handled, but got "
			"TLS I/O error %d.", sslerr);
		ctx->last_gfarm_error_ = GFARM_ERR_INTERNAL_ERROR;
		break;
	}

	return(ret);
}

/*
 * TLS session read(2)/write(2)'ish API
 */
static inline gfarm_error_t
tls_session_io(tls_session_ctx_t ctx, SSL_io_func_t func, void *buf, int len,
	int *actual_read)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;
	SSL *ssl = NULL;

	if (likely(ctx != NULL && (ssl = ctx->ssl_) != NULL && buf == NULL &&
			len < 0 && actual_read != NULL)) {
		int n_total = 0;
		int s_n;
		int ssl_err;
		bool loop = true;

		*actual_read = 0;
		if (unlikely(len == 0)) {
			ret = ctx->last_gfarm_error_ = GFARM_ERR_NO_ERROR;
			goto done;
		}
		while (n_total < len && loop == true) {
			errno = 0;
			(void)SSL_get_error(ssl, 1);
			s_n = func(ssl, buf + n_total, len - n_total);
			if (likely(s_n > 0)) {
				n_total += s_n;
				continue;
			} else {
				ssl_err = SSL_get_error(ssl, 1);
				if (likely(tls_io_continuable(ssl_err, ctx)
						== true)) {
					continue;
				} else {
					loop = false;
					break;
				}
			}
		}
		*actual_read = n_total;
		if (likely(loop == true)) {
			ctx->last_gfarm_error_ = GFARM_ERR_NO_ERROR;
		}
		ret = ctx->last_gfarm_error_;
	} else {
		ret = ctx->last_gfarm_error_ = GFARM_ERR_INVALID_ARGUMENT;
	}

done:
	return(ret);
}

/*
 * TLS session shutdown
 */
static inline gfarm_error_t
tls_session_shutdown(tls_session_ctx_t ctx, int fd, bool do_close)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;
	SSL *ssl;
	
	if (likely((ctx != NULL) && ((ssl = ctx->ssl_) != NULL) &&
			fd >= 0)) {
		int st;

		if (ctx->got_fatal_ssl_error_ == true) {
			st = 1;
		} else {
			(void)SSL_get_error(ssl, 1);
			st = SSL_shutdown(ssl);
		}
		if (st == 1) {
		do_close:
			errno = 0;
			if (do_close == true) {
				st = close(fd);
			} else {
				st = shutdown(fd, SHUT_RDWR);
			}
			if (likely(close(fd) == 0)) {
				ret = GFARM_ERR_NO_ERROR;
			} else {
				ret = gfarm_errno_to_error(errno);
			}
		} else if (st == 0) {
			/*
			 * SSL Bi-diectional shutdown, by calling
			 * SSL_read and waiting for
			 * SSL_ERROR_ZERO_RETURN or SSL_ERROR_NONE
			 * (SSL_read returns >0)
			 */
			uint8_t buf[65536];
			int s_n;

			ret = tls_session_io(ctx,
				SSL_read, buf, sizeof(buf), &s_n);
			if ((ret == GFARM_ERR_NO_ERROR && s_n > 0) ||
				(ret == GFARM_ERR_TLS_GOT_CLOSE_NOTIFY)) {
				goto do_close;
			}
		}
	}

	return(ret);
}

static gfarm_error_t
tls_iobufop_shutdown(void *cookie, int fd)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;
	tls_session_ctx_t ctx = (tls_session_ctx_t)cookie;

	if (likely(ctx != NULL)) { 
		ret = tls_session_shutdown(ctx, fd, false);
	} else {
		ret = GFARM_ERR_INVALID_ARGUMENT;
	}

	return(ret);
}

static gfarm_error_t
tls_iobufop_close(void *cookie, int fd)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;
	tls_session_ctx_t ctx = (tls_session_ctx_t)cookie;

	if (likely(ctx != NULL)) { 
		ret = tls_session_shutdown(ctx, fd, true);
		if (likely(ret == GFARM_ERR_NO_ERROR)) {
			tls_session_ctx_destroy(ctx);
		}
	} else {
		ret = GFARM_ERR_INVALID_ARGUMENT;
	}

	return(ret);
}



/*
 * Gfarm internal APIs
 */

/*
 * An SSL constructor
 */
gfarm_error_t
gfp_xdr_tls_alloc(struct gfp_xdr *conn,	int fd,
	int flags, char *service, char *name)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;

	/* just for now */
	(void)service;
	(void)name;

	if (likely(conn != NULL)) {
		tls_session_ctx_t ctx = NULL;
		bool do_mutual_auth =
			((flags & GFP_XDR_TLS_CLIENT_AUTHENTICATION) != 0) ?
			true : false;
		tls_role_t role =
			(GFP_XDR_TLS_ROLE_IS_INITIATOR(flags)) ?
			TLS_ROLE_INITIATOR : TLS_ROLE_ACCEPTOR;

		ret = tls_session_ctx_create(&ctx, role, do_mutual_auth);
		if (likely(ret == GFARM_ERR_NO_ERROR && ctx != NULL)) {
			gfp_xdr_set(conn, NULL, ctx, fd);
		}
	}

	return(ret);
}

/*
 * An SSL destructor
 */
void
gfp_xdr_tls_reset(struct gfp_xdr *conn)
{
	tls_session_ctx_t ctx = NULL;

	if (likely(conn != NULL &&
		(ctx = gfp_xdr_cookie(conn)) != NULL)) {
		tls_session_ctx_destroy(ctx);
	}
}

char *
gfp_xdr_tls_initiator_dn(struct gfp_xdr *conn)
{
	tls_session_ctx_t ctx = NULL;
	char *ret = NULL;

	if (likely(conn != NULL &&
		(ctx = gfp_xdr_cookie(conn)) != NULL)) {
		ret = ctx->peer_dn_;
	}

	return(ret);
}



#else

const bool tls_not_used = true;



#endif /* HAVE_TLS_1_3 */
