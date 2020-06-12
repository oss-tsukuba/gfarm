#include "tlsutils.h"

#define CA_CERT_DIR	"./cacerts"
#define OWN_CERT	"./self.crt"
#define OWN_KEY		"./self.key"

SSL_CTX *own_sslctx = NULL;

static const char *ca_dir_ = CA_CERT_DIR;
static const char *cert_file_ = OWN_CERT;
static const char *key_file_ = OWN_KEY;

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

static inline size_t
get_passwd_from_stdin(char *buf, size_t maxlen)
{
	size_t ret = 0;

	if (likely(stdin != NULL)) {
		if (likely(buf != NULL && maxlen > 1)) {
			char *rst = NULL;
			struct termios t;
			tcflag_t saved_lflag;
			int f = fileno(stdin);
			bool is_tty = (isatty(f) == 1) ? true : false;

			if (is_tty == true) {
				(void)tcgetattr(f, &t);
				saved_lflag = t.c_lflag;
				t.c_lflag &= ~ECHO;
				(void)tcsetattr(f, TCSAFLUSH, &t);
			} else {
				gflog_warning(GFARM_MSG_UNFIXED,
					"A password is about to be acquired "
					"from a non-tty descriptor.");
			}

			(void)memset(buf, 0, maxlen);
			errno = 0;
			rst = fgets(buf, maxlen, stdin);
			if (likely(rst != NULL)) {
				trim_string_tail(buf);
				ret = strlen(buf);
			} else {
				if (errno != 0) {
					gflog_error(GFARM_MSG_UNFIXED,
						"Failed to get a password: %s",
						strerror(errno));
				}
			}
			if (is_tty == true) {
				t.c_lflag = saved_lflag;
				(void)tcsetattr(f, TCSAFLUSH, &t);
			}
		} else {
			gflog_debug(GFARM_MSG_UNFIXED,
				"Invalid buffer and/or buffer length "
				"for password input: %p, %zu", buf, maxlen);
		}
	} else {
		gflog_warning(GFARM_MSG_UNFIXED,
			"The process has a closed stdin, which is needed "
			"to input a password.");
	}

	return(ret);
}

static int
passwd_callback(char *buf, int maxlen, int rwflag, void *u)
{
	int ret = 0;

	(void)rwflag;
	fprintf(stderr, "Enter a passphrase for '%s': ", (char *)u);
	ret = get_passwd_from_stdin(buf, maxlen);
	fprintf(stderr, "\n");

	return(ret);
}

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

static inline bool
is_valid_pkey_file_permission(const char *file)
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

static inline EVP_PKEY *
load_pkey(const char *file)
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
							"Can't read a PEM "
							"format private key "
							"from %s: %s", file,
							b);
			}
		} else {
			gflog_error(GFARM_MSG_UNFIXED,
				"Can't open %s: %s", file,
				strerror(errno));
		}
	}

	return(ret);
}

static gfarm_error_t
tls_init_context(const char *ca_dir,
		 const char *cert_file, const char *key_file)
{
	if (unlikely(own_sslctx == NULL)) {
		own_sslctx = SSL_CTX_new(TLS_method());
		if (likely(own_sslctx != NULL)) {
		}
	}

	return GFARM_ERR_NO_ERROR;
}
