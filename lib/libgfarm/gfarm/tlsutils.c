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
is_valid_ca_dir(const char *dir)
{
	bool ret = false;

	if (is_valid_string(dir) == true) {
		struct stat s;

		if (stat(dir, &s) == 0) {
			if (S_ISDIR(s.st_mode)) {
				if (((s.st_mode & S_IRWXU) != 0 &&
					s.st_uid == geteuid()) ||
					((s.st_mode & S_IRWXG) != 0 &&
					s.st_gid == getegid()) ||
					((s.st_mode & S_IRWXO) != 0)) {
					ret = true;
				} else {
					gflog_error(GFARM_MSG_UNFIXED,
						"%s seems not accessible.",
						dir);
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

	if (is_valid_string(file) == true) {
		struct stat s;

		if (stat(file, &s) == 0) {
			if (!S_ISDIR(s.st_mode)) {
				FILE *f = fopen(file, "r");

				if (f != NULL) {
					ret = PEM_read_X509(f, NULL,
						passwd_callback,
						(void *)file);
					(void)fclose(f);
					if (ret == NULL) {
						char b[4096];
						unsigned long e =
							ERR_get_error();

						gflog_error(GFARM_MSG_UNFIXED,
							"Can't read a PEM "
							"format certificate "
							"from %s: %s", file,
							ERR_error_string(
								e, b));
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
load_pkey(const char *file)
{
	EVP_PKEY *ret = NULL;

	if (is_valid_string(file) == true) {
		struct stat s;

		if (stat(file, &s) == 0) {
			if (!S_ISDIR(s.st_mode)) {
				FILE *f = fopen(file, "r");

				if (f != NULL) {
					ret = PEM_read_PrivateKey(f, NULL,
						passwd_callback,
						(void *)file);
					(void)fclose(f);
					if (ret == NULL) {
						char b[4096];
						unsigned long e =
							ERR_get_error();

						gflog_error(GFARM_MSG_UNFIXED,
							"Can't read a PEM "
							"format private key "
							"from %s: %s", file,
							ERR_error_string(
								e, b));
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
			"Specified private key file name is nul.");
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
