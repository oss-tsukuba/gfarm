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
	char *the_peer_dn_;	/* malloc'd */
	EVP_PKEY *prvkey_;	/* malloc'd */
	SSL_CTX *ssl_ctx_;	/* malloc'd */
	SSL *ssl_;		/* malloc'd */
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

static bool is_tty_saved = false;
static struct termios saved_tty = { 0 };

static bool is_allowed_passwd_stdin = true;

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
 * Passwords are enabled to be acquired from non-tty input, means
 * non-keyboard-interactive password acquisition is allowed which is
 * not totally safe.
 */
static inline size_t
get_passwd_from_stdin(char *buf, size_t maxlen, const char *prompt)
{
	size_t ret = 0;

	if (likely(stdin != NULL)) {
		if (likely(buf != NULL && maxlen > 1)) {
			char *rst = NULL;
			int s_errno;
			int f = fileno(stdin);
			bool is_tty = (isatty(f) == 1) ? true : false;
#if 1
			FILE *msgout = stderr;
#else
			FILE *msgout = stdout;
#endif /* 1 */

			if (is_tty == true) {
				(void)fprintf(msgout, "%s", prompt);
				tty_echo_off(f);
			} else {
				gflog_warning(GFARM_MSG_UNFIXED,
					"A password is about to be acquired "
					"from a non-tty descriptor.");
				(void)fprintf(msgout, "%s", prompt);
			}
			(void)fflush(msgout);

			(void)memset(buf, 0, maxlen);
			errno = 0;
			rst = fgets(buf, maxlen, stdin);
			s_errno = errno;

			if (is_tty == true) {
				tty_reset(f);
			}
			(void)fprintf(msgout, "\n");
			(void)fflush(msgout);

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
				"Invalid buffer and/or buffer length "
				"for password input: %p, %zu", buf, maxlen);
		}
	} else {
		gflog_warning(GFARM_MSG_UNFIXED,
			"The process has a closed stdin, which is needed to "
			"opened for password acquisition.");
	}

	return(ret);
}

/*
 * Passwords are must be acquired from /dev/tty.
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
		if (is_valid_string(arg->pw_buf_) == true) {
		has_a_passwd_cache:
			(void)snprintf(buf, maxlen, "%s", arg->pw_buf_);
			ret = strlen(arg->pw_buf_);
		} else if (arg->pw_buf_ != NULL && arg->pw_buf_maxlen_ > 0) {
			char p[4096];
			size_t pw_len;

			if (arg->filename_ != NULL) {
				(void)snprintf(p, sizeof(p),
					"Passphrase for \"%s\": ",
					arg->filename_);
			} else {
				(void)snprintf(p, sizeof(p), "Passphrase: ");
			}
			if (is_allowed_passwd_stdin == true) {
				pw_len = get_passwd_from_stdin(
					arg->pw_buf_, arg->pw_buf_maxlen_, p);
			} else {
				pw_len = get_passwd_from_tty(
					arg->pw_buf_, arg->pw_buf_maxlen_, p);
			}
			if (pw_len > 0) {
				goto has_a_passwd_cache;
			}
		}
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

		if (stat(file, &s) == 0) {
			if (likely(!S_ISDIR(s.st_mode))) {
				uid_t uid = geteuid();
				if (likely((s.st_uid == uid &&
					    (s.st_mode & S_IRUSR) != 0) ||
					   (is_user_in_group(uid, s.st_gid)
					    == true &&
					    (s.st_mode & S_IRGRP) != 0) ||
					   ((s.st_mode & S_IROTH) != 0))) {
					ret = true;
				} else {
					gflog_error(GFARM_MSG_UNFIXED,
						"The file perrmssion "
						"of the specified "
						"file %s is "
						"insufficient for "
						"read. ", file);
				}
			} else {
				gflog_error(GFARM_MSG_UNFIXED,
					"%s is a directory.", file);
			}
		} else {
			gflog_error(GFARM_MSG_UNFIXED,
				"Failed to stat(\"%s\"): %s",
				file, strerror(errno));
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

		if (stat(dir, &s) == 0) {
			if (S_ISDIR(s.st_mode)) {
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
				gflog_error(GFARM_MSG_UNFIXED,
					"%s is not a directory.", dir);
			}
		} else {
			gflog_error(GFARM_MSG_UNFIXED,
				"Can't access to %s: %s",
				dir, strerror(errno));
		}
	} else {
		gflog_error(GFARM_MSG_UNFIXED,
			"Specified CA cert directory name is nul.");
	}

	return(ret);
}

static inline bool
is_valid_prvkey_file_permission(const char *file)
{
	bool ret = false;

	if (likely(is_valid_string(file) == true)) {
		struct stat s;

		errno = 0;
		if (likely(stat(file, &s) == 0)) {
			if (likely(!S_ISDIR(s.st_mode))) {
				uid_t uid;
				
				if (likely((uid = geteuid()) == s.st_uid)) {
					if (likely((s.st_mode &
						(S_IRGRP | S_IWGRP |
						S_IROTH | S_IWOTH)) == 0)) {
						ret = true;
					} else {
						gflog_error(GFARM_MSG_UNFIXED,
							"The file perrmssion "
							"of the specified "
							"file %s is open too "
							"widely. It would be "
							"nice if the file "
							"permission were "
							"0600.", file);
					}
				} else {
					gflog_error(GFARM_MSG_UNFIXED,
						"The process is about to "
						"read other uid(%d)'s "
						" private key file %s, which "
						"is strongly discouraged even "
						"if he process could read it "
						"for privacy and security.",
						uid, file);
				}
			} else {
				gflog_error(GFARM_MSG_UNFIXED,
					"%s is a directory, not a file", file);
			}
		} else {
			gflog_error(GFARM_MSG_UNFIXED,
				"Can't access %s: %s", file, strerror(errno));
		}
	} else {
		gflog_error(GFARM_MSG_UNFIXED,
			"The specified filename is a nul.");
	}

	return(ret);
}



/*
 * Certificate/Private key loaders
 */
#if 0
static inline X509 *
load_cert(const char *file)
{
	X509 *ret = NULL;

	if (likely(is_valid_string(file) == true)) {
		struct stat s;

		if (likely(stat(file, &s) == 0)) {
			if (likely(!S_ISDIR(s.st_mode))) {
				FILE *f = fopen(file, "r");

				if (likely(f != NULL)) {
					ret = PEM_read_X509(f, NULL,
						NULL, NULL);
					(void)fclose(f);
					if (unlikely(ret == NULL)) {
						char b[4096];

						ERR_error_string_n(
							ERR_get_error(), b,
							sizeof(b));
						gflog_error(GFARM_MSG_UNFIXED,
							"Can't read a PEM "
							"format certificate "
							"from %s: %s", file,
							b);
					}
				} else {
					gflog_error(GFARM_MSG_UNFIXED,
						"Can't open %s: %s", file,
						strerror(errno));
				}
			} else {
				gflog_error(GFARM_MSG_UNFIXED,
						"%s is a directory, "
						"not a file", file);
			}
		} else {
			gflog_error(GFARM_MSG_UNFIXED,
				"Can't open %s: %s", file,
				strerror(errno));
		}
	} else {
		gflog_error(GFARM_MSG_UNFIXED,
			"Specified cert file name is nul.");
	}

	return(ret);
}
#endif

static inline EVP_PKEY *
load_prvkey(const char *file)
{
	EVP_PKEY *ret = NULL;

	if (likely(is_valid_prvkey_file_permission(file) == true)) {
		FILE *f;

		errno = 0;
		if (likely((f = fopen(file, "r")) != NULL)) {
			struct tls_passwd_cb_arg_struct a = {
				.pw_buf_maxlen_ = sizeof(the_privkey_passwd),
				.pw_buf_ = the_privkey_passwd,
				.filename_ = file };
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
			gflog_error(GFARM_MSG_UNFIXED,
				"Can't open %s: %s", file,
				strerror(errno));
		}
	}

	return(ret);
}



/*
 * Cert-chain verifier
 */

static int
chain_verify_callback(int ok, X509_STORE_CTX *sctx)
{
	if (ok != 1 && sctx != NULL) {
		int err = X509_STORE_CTX_get_error(sctx);
		X509 *cert = X509_STORE_CTX_get_current_cert(sctx);
		int depth = X509_STORE_CTX_get_error_depth(sctx);
		char errbuf[4096];

		ERR_error_string_n(err, errbuf, sizeof(errbuf));
		if (likely(cert != NULL)) {
			X509_NAME *xname = X509_get_subject_name(cert);

			if (likely(xname != NULL)) {
				char sbjDN[4096];

				if (likely(X509_NAME_oneline(xname, sbjDN,
					sizeof(sbjDN)) != NULL)) {
					gflog_error(GFARM_MSG_UNFIXED,
						"Certiticate chain verify "
						"failed for \"%s\", depth %d: "
						"%s", sbjDN, depth, errbuf);
				} else {
					gflog_error(GFARM_MSG_UNFIXED,
						"Certiticate chain verify "
						"failed, no DN acquired, "
						"depth %d: %s", depth, errbuf);
				}
			} else {
				gflog_error(GFARM_MSG_UNFIXED,
					"Certiticate chain verify failed, no "
					"X509 name acquired, depth %d: %s",
					depth, errbuf);
			}
		} else {
			gflog_error(GFARM_MSG_UNFIXED,
				"Certiticate chain verify failed, no "
				"cert acquired, depth %d: %s", depth, errbuf);
		}
	} else if (sctx == NULL) {
		if (ok == 0) {
			gflog_error(GFARM_MSG_UNFIXED,
				"Certiticate chain verify failed, no verify "
				"context.");
		} else {
			gflog_error(GFARM_MSG_UNFIXED,
				"Something wrong is going on, certificate "
				"verify succeeded without a verify context.");
		}
		ok = 0;
	}

	return(ok);
}



/*
 * Internal TLS context constructor
 */

static inline tls_session_ctx_t
tls_session_ctx_create(tls_role_t role, bool do_mutual_auth)
{
	tls_session_ctx_t ret = NULL;
	char *cert_file = NULL;
	char *cert_chain_file = NULL;
	char *prvkey_file = NULL;
	char *cert_to_use = NULL;
	bool is_chain_cert = false;
	EVP_PKEY *prvkey = NULL;
	SSL_CTX *ssl_ctx = NULL;
	bool need_cert = false;

	if (do_mutual_auth == true || role == TLS_ROLE_SERVER) {
		need_cert = true;
	}

	/*
	 * only load prvkey and cert/cert chain file.
	 * Any other attributes vary depend on role (server/client)
	 */
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

	if (need_cert == true) {
#ifdef TLS_SODA_OK
		cert_file = gfarm_ctxp->tls_certificate_file;
		cert_chain_file = gfarm_ctxp->tls_certificate_chain_file;
#endif /* TLS_SODA_OK */
		/*
		 * Use cert_chainfile if both cert_file and
		 * cert_chain_file are specified.
		 */
		if (is_valid_string(cert_chain_file) == true &&
			is_file_readable(cert_chain_file) == true) {
			cert_to_use = cert_chain_file;
			is_chain_cert = true;
		} else if (is_valid_string(cert_file) == true &&
			is_file_readable(cert_file) == true) {
			cert_to_use = cert_file;
		}
		if (unlikely(is_valid_string(cert_to_use) == false)) {
			gflog_error(GFARM_MSG_UNFIXED,
				"None of a certificate file or a "
				"certificate chain file is specified.");
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

		/*
		 * Load a cert into the SSL_CTX
		 */
		if (need_cert == true) {
			if (is_chain_cert == true) {
				osst = SSL_CTX_use_certificate_chain_file(
					ssl_ctx, cert_to_use);
			} else {
				osst = SSL_CTX_use_certificate_file(
					ssl_ctx, cert_to_use,
					SSL_FILETYPE_PEM);
			}
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
		SSL_CTX_free(ssl_ctx);
	}
	free(ret);

ok:
	return(ret);
}

#else

const int tls_not_used = 1;



#endif /* HAVE_TLS */
