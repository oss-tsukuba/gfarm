#ifdef HAVE_TLS



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
 * The cookie for TLS
 */
struct tls_session_ctx_struct {
	const char *the_peer_dn_;
	const EVP_PKEY *the_privkey_;
	const X509 *the_cert_;
	const SSL_CTX *ssl_ctx_;
	SSL *ssl_;
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
 * Static passwd buffers
 */
static char the_cert_passwd[4096] = { 0 };
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
			/* A wild guess: Assume only an ECHO flag is
			 * dropped. */
			struct termios ts;

			(void)tcgetattr(ttyfd, &ts);
			ts.c_lflags |= ECHO;
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
		ts.c_lflags &= ~ECHO;
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
			struct termios t;
			int s_errno;
			tcflag_t saved_lflag;
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

		errno = 0;
		ttyfd = open("/dev/tty", O_RDWR);
		s_errno = errno;
		if (ttyfd >= 0 && (fd = fdopen(ttyfd, "rw")) != NULL) {
			int s_errno;
			char *rst = NULL;
			struct termios t;
			tcflag_t saved_lflag;
			FILE *fd = fdopen(ttyfd, "rw");

			(void)frpintf(fd, "%s", prompt);

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
						passwd_callback,
						(void *)file);
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


static inline EVP_PKEY *
load_prvkey(const char *file)
{
	EVP_PKEY *ret = NULL;

	if (likely(is_valid_pkey_file_permission(file) == true)) {
		FILE *f;

		errno = 0;
		if (likely((f = fopen(file, "r")) != NULL)) {
			ret = PEM_read_PrivateKey(f, NULL, passwd_callback,
				(void *)file);
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



static gfarm_error_t
tls_init_context(const char *ca_dir,
		 const char *cert_file, const char *key_file)
{
	return GFARM_ERR_NO_ERROR;
}

#else

const int tls_not_used = 1;



#endif /* HAVE_TLS */
