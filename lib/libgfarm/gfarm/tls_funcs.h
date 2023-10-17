#pragma once

#if defined(HAVE_TLS_1_3) && defined(IN_TLS_CORE)

/*
 * misc. utils.
 */
static inline int
escape_slash(const char *in, char **outptr, int outlen)
{
	int ret = 0;

	if (outptr == NULL) {
		*outptr = NULL;
	}

	if (likely(is_valid_string(in) == true)) {
		char *d;
		const char *s = in;
		int maxlen;
		int len;
		if (outptr != NULL) {
			d = *outptr;
			maxlen = outlen - 1;
		} else {
			maxlen = strlen(in) * 2 - 1;
			d = (char *)malloc(maxlen);
		}
		do {
			if (*s == '/') {
				*d++ = '\\';
			}
			*d++ = *s++;
			len++;
		} while (*s != '\0' && (s - in) < outlen);
		*d = '\0';
		ret = len;
	}

	return (ret);
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
static inline gfarm_error_t
tty_get_passwd(char *buf, size_t maxlen, const char *prompt, int *lenptr)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;

	if (likely(buf != NULL && maxlen > 1 && lenptr != NULL)) {
		int s_errno = -1;
		int ttyfd = -1;
		int is_tty = 0;

		*lenptr = 0;

		errno = 0;
		ttyfd = fileno(stdin);
		s_errno = errno;
		if (unlikely(s_errno != 0)) {
			goto ttyerr;
		}

		errno = 0;
		is_tty = isatty(ttyfd);
		s_errno = errno;
		if (likely(is_tty == 1)) {
			char *rst = NULL;

			(void)fprintf(stdout, "%s", prompt);

			tty_echo_off(ttyfd);

			(void)memset(buf, 0, maxlen);
			errno = 0;
			rst = fgets(buf, maxlen, stdin);
			s_errno = errno;

			tty_reset(ttyfd);
			(void)fprintf(stdout, "\n");
			(void)fflush(stdout);

			if (likely(rst != NULL)) {
				trim_string_tail(buf);
				*lenptr = strlen(buf);
				ret = GFARM_ERR_NO_ERROR;
			} else {
				if (s_errno != 0) {
					gflog_tls_error(GFARM_MSG_1005517,
						"Failed to get a password: %s",
						strerror(s_errno));
					ret = gfarm_errno_to_error(s_errno);
				}
			}
		} else {
ttyerr:
			ret = gfarm_errno_to_error(s_errno);
			gflog_tls_error(GFARM_MSG_1005518,
				"stdin is not a terminal: %s",
				gfarm_error_string(ret));
		}
	} else {
		gflog_tls_error(GFARM_MSG_1005519,
			"Invalid buffer and/or buffer length "
			"for password input: %p, %zu", buf, maxlen);
		ret = GFARM_ERR_INVALID_ARGUMENT;
	}

	return (ret);
}


/*
 * Validators
 */

/*
 * Directory/File permission check primitives
 */

static inline gfarm_error_t
is_user_in_group(uid_t uid, gid_t gid)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;

	if (geteuid() == uid && getegid() == gid) {
		ret = GFARM_ERR_NO_ERROR;
	} else {
		struct passwd u;
		struct passwd *ures = NULL;
		char ubuf[256];

		errno = 0;
		if (likely(getpwuid_r(uid, &u,
			ubuf, sizeof(ubuf), &ures) == 0 && ures != NULL)) {
			if (u.pw_gid == gid) {
				ret = GFARM_ERR_NO_ERROR;
			} else {
				struct group g;
				struct group *gres = NULL;
				char gbuf[8192];

				errno = 0;
				if (likely(getgrgid_r(gid, &g,
					gbuf, sizeof(gbuf), &gres) == 0 &&
					gres != NULL)) {
					char **p = g.gr_mem;

					while (*p != NULL) {
						if (strcmp(u.pw_name,
							*p) == 0) {
							ret =
							GFARM_ERR_NO_ERROR;
							break;
						}
						p++;
					}
				} else {
					if (errno != 0) {
						gflog_tls_error(
							GFARM_MSG_1005520,
							"Failed to acquire a "
							"group entry for "
							"gid %d: %s",
							gid, strerror(errno));
						ret = gfarm_errno_to_error(
							errno);
					} else {
						gflog_tls_error(
							GFARM_MSG_1005521,
							"Can't find the group "
							"%d.", gid);
						ret = gfarm_errno_to_error(
							errno);
					}
				}
			}
		} else {
			if (errno != 0) {
				gflog_tls_error(GFARM_MSG_1005522,
					"Failed to acquire a passwd entry "
					"for uid %d: %s",
					uid, strerror(errno));
				ret = gfarm_errno_to_error(errno);
			} else {
				gflog_tls_error(GFARM_MSG_1005523,
					"Can't find the user %d.", uid);
				ret = GFARM_ERR_INVALID_ARGUMENT;
			}
		}
	}

	return (ret);
}

static inline gfarm_error_t
is_file_readable(int fd, const char *file)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;

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
				   (is_user_in_group(uid, s.st_gid) ==
				    GFARM_ERR_NO_ERROR &&
				    (s.st_mode & S_IRGRP) != 0) ||
				   ((s.st_mode & S_IROTH) != 0))) {
				ret = GFARM_ERR_NO_ERROR;
			} else {
				ret = GFARM_ERR_PERMISSION_DENIED;
				gflog_tls_debug(GFARM_MSG_1005524,
					"%s: %s", file,
					gfarm_error_string(ret));

			}
		} else {
			if (errno != 0) {
				gflog_tls_debug(GFARM_MSG_1005525,
					"Failed to stat(\"%s\"): %s",
					file, strerror(errno));
				ret = gfarm_errno_to_error(errno);
			} else {
				gflog_tls_debug(GFARM_MSG_1005526,
					"%s is a directory.", file);
				ret = GFARM_ERR_IS_A_DIRECTORY;
			}
		}
	} else {
		gflog_tls_error(GFARM_MSG_1005527,
			"Specified filename is nul or "
			"file invalid file descriptor.");
		ret = GFARM_ERR_INVALID_ARGUMENT;
	}

	return (ret);
}

static inline gfarm_error_t
is_valid_prvkey_file_permission(int fd, const char *file)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;

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
			uid_t uid;

			if (likely((uid = geteuid()) == s.st_uid)) {
				if (likely(((s.st_mode &
					(S_IRGRP | S_IWGRP |
					 S_IROTH | S_IWOTH)) == 0) &&
					((s.st_mode &
					  S_IRUSR) != 0))) {
					ret = GFARM_ERR_NO_ERROR;
				} else {
					gflog_tls_error(GFARM_MSG_1005528,
						"The file perrmssion of the "
						"specified file \"%s\" is "
						"open too widely. It would "
						"be nice if the file "
						"permission was 0600.", file);
					ret = GFARM_ERR_INVALID_CREDENTIAL;
				}
			} else {
				gflog_tls_error(GFARM_MSG_1005529,
					"This process is about to read other "
					"uid(%d)'s private key file \"%s\", "
					"which is strongly discouraged even "
					"this process can read it for privacy "
					"and security.", uid, file);
				ret = GFARM_ERR_INVALID_CREDENTIAL;
			}
		} else {
			if (errno != 0) {
				gflog_tls_debug(GFARM_MSG_1005530,
					"Can't access %s: %s",
					file, strerror(errno));
				ret = gfarm_errno_to_error(errno);
			} else {
				gflog_tls_error(GFARM_MSG_1005531,
					"%s is a directory, not a file", file);
				ret = GFARM_ERR_IS_A_DIRECTORY;
			}
		}
	} else {
		gflog_tls_error(GFARM_MSG_1005532,
			"Specified filename is nul or "
			"file invalid file descriptor.");
		ret = GFARM_ERR_INVALID_ARGUMENT;
	}

	return (ret);
}

static inline gfarm_error_t
is_valid_cert_store_dir(const char *dir)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;

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
				ret = GFARM_ERR_NO_ERROR;
			} else {
				ret = GFARM_ERR_PERMISSION_DENIED;
				gflog_tls_error(GFARM_MSG_1005533,
					"%s: %s", dir,
					gfarm_error_string(ret));
			}
		} else {
			if (errno != 0) {
				gflog_tls_error(GFARM_MSG_1005534,
					"Can't access to %s: %s",
					dir, strerror(errno));
				ret = gfarm_errno_to_error(errno);
			} else {
				gflog_tls_error(GFARM_MSG_1005535,
					"%s is not a directory.", dir);
				ret = GFARM_ERR_NOT_A_DIRECTORY;
			}
		}
	} else {
		gflog_tls_error(GFARM_MSG_1005536,
			"Specified CA cert directory name is nul.");
		ret = GFARM_ERR_INVALID_ARGUMENT;
	}

	return (ret);
}

static inline char *
has_proxy_cert(void)
{
	char *ret = NULL;
	char *tmp = NULL;
	char buf[PATH_MAX];
	gfarm_error_t ge = GFARM_ERR_UNKNOWN;

	if ((tmp =  getenv("X509_USER_PROXY")) != NULL) {
		snprintf(buf, sizeof(buf), "%s", tmp);
	} else {
		snprintf(buf, sizeof(buf), "/tmp/x509up_u%u", geteuid());
	}
	ge = is_valid_prvkey_file_permission(-1, buf);
	if (ge == GFARM_ERR_NO_ERROR) {
		ret = strdup(buf);
	}
	return (ret);
}

/*
 * TLS thingies
 */

/*
 * Implementation: TLS support version of gflog_message()
 */
static inline void
tlslog_tls_message(int msg_no, int priority,
	const char *file, int line_no, const char *func,
	const char *format, ...)
{
	char msgbuf[TLS_LOG_MSG_LEN];
	va_list ap;
	unsigned int err;

	va_start(ap, format);
	(void)vsnprintf(msgbuf, sizeof(msgbuf), format, ap);
	va_end(ap);

	if (ERR_peek_error() == 0) {
		gflog_message(msg_no, priority, file, line_no, func,
			"%s", msgbuf);
	} else {
		char msgbuf2[TLS_LOG_MSG_LEN * 3];
		char tlsmsg[TLS_LOG_MSG_LEN];
		const char *tls_file, *tls_data;
		int tls_line, tls_flags;
#ifdef HAVE_ERR_GET_ERROR_ALL /* since OpenSSL-3.0 */
		const char *tls_func;

		err = ERR_get_error_all(&tls_file, &tls_line, &tls_func,
			&tls_data, &tls_flags);
		ERR_error_string_n(err, tlsmsg, sizeof(tlsmsg));
		(void)snprintf(msgbuf2, sizeof(msgbuf2),
			"%s: [OpenSSL error info: %s:%d: %s%s%s%s%s]",
			msgbuf, tls_file, tls_line,
			tls_func,
			tls_func[0] != '\0' ? ": " : "",
			tlsmsg,
			(tls_flags & ERR_TXT_STRING) != 0 ? ": " : "",
			(tls_flags & ERR_TXT_STRING) != 0 ? tls_data : "");
#else /* deprecated since OpenSSL-3.0 */

		err = ERR_get_error_line_data(&tls_file, &tls_line,
			&tls_data, &tls_flags);
		ERR_error_string_n(err, tlsmsg, sizeof(tlsmsg));
		(void)snprintf(msgbuf2, sizeof(msgbuf2),
			"%s: [OpenSSL error info: %s:%d: %s%s%s]",
			msgbuf, tls_file, tls_line, tlsmsg,
			(tls_flags & ERR_TXT_STRING) != 0 ? ": " : "",
			(tls_flags & ERR_TXT_STRING) != 0 ? tls_data : "");
#endif
		gflog_message(msg_no, priority, file, line_no, func,
			"%s", msgbuf2);
	}
}

/*
 * TLS runtime library initialization
 */
static inline void
tls_runtime_init_once_body(void)
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


static inline gfarm_error_t
tls_session_runtime_initialize(void)
{
	(void)pthread_once(&tls_init_once, tls_runtime_init_once);
	return ((is_tls_runtime_initd == true) ?
		GFARM_ERR_NO_ERROR : GFARM_ERR_INTERNAL_ERROR);
}

/*
 * TLS runtime error handling
 */
static inline bool
tls_has_runtime_error(void)
{
	return (ERR_peek_error() != 0);
}

static inline void
tls_runtime_flush_error(void)
{
	for ((void)ERR_get_error(); ERR_get_error() != 0;)
		;
}

/*
 * X509_NAME to string
 */
static inline gfarm_error_t
get_peer_dn(X509_NAME *pn, int mode, char **nameptr, int maxlen)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;
	BIO *bio = BIO_new(BIO_s_mem());

	if (likely(pn != NULL && nameptr != NULL && bio != NULL)) {
		int len = 0;
		char *buf = NULL;

		(void)X509_NAME_print_ex(bio, pn, 0, mode);
		len = BIO_pending(bio);

		if (*nameptr != NULL && maxlen > 0) {
			buf = *nameptr;
			len = (maxlen > len) ? len : maxlen - 1;
		} else {
			*nameptr = NULL;
			buf = (char *)malloc(len + 1);
		}
		if (likely(len > 0 && buf != NULL)) {
			(void)BIO_read(bio, buf, len);
			buf[len] = '\0';
			ret = GFARM_ERR_NO_ERROR;
			if (*nameptr != NULL) {
				*nameptr = buf;
			}
			*nameptr = buf;
		} else {
			if (buf == NULL && len > 0) {
				ret = GFARM_ERR_NO_MEMORY;
				gflog_tls_error(GFARM_MSG_1005537,
					"Can't allocate a %d bytes buffer for "
					"a peer SubjectDN.", len);
			} else if (len <= 0) {
				ret = GFARM_ERR_INTERNAL_ERROR;
				gflog_tls_error(GFARM_MSG_1005538,
					"Failed to acquire a length of peer "
					"SubjectDN.");
			}
		}
	} else {
		if (bio == NULL) {
			ret = GFARM_ERR_NO_MEMORY;
			gflog_tls_error(GFARM_MSG_1005539,
				"Can't allocate a BIO.");
		} else {
			ret = GFARM_ERR_INVALID_ARGUMENT;
		}
	}

	if (bio != NULL) {
		BIO_free(bio);
	}

	return (ret);
}

static inline gfarm_error_t
get_peer_dn_gsi_ish(X509_NAME *pn, char **nameptr, int maxlen)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;

	if (likely(pn != NULL && nameptr != NULL)) {
		char buf[4096];
		char *dn = buf;
		char *cnp = NULL;

#define DN_FORMAT_GLOBUS						\
		(XN_FLAG_RFC2253 & ~(ASN1_STRFLGS_ESC_MSB|XN_FLAG_DN_REV))
		ret = get_peer_dn(pn, DN_FORMAT_GLOBUS,
				 &dn, sizeof(buf));
#undef DN_FORMAT_GLOBUS
		if (likely(ret == GFARM_ERR_NO_ERROR &&
				(cnp = (char *)memmem(buf, sizeof(buf),
						"CN=", 3)) != NULL &&
				*(cnp += 3) != '\0')) {
			char result[4096];
			char *r = result;
			char *d = buf;

			*r++ = '/';
			do {
				switch (*d) {
				case '/':
					if (likely(r < cnp)) {
						*r++ = '\\';
					}
					*r++ = *d++;
					break;
				case ',':
					*r++ = '/';
					d++;
					break;
				case '\\':
					if (d[1] == ',')
						d++;
					/*FALLTHROUGH*/
				default:
					*r++ = *d++;
					break;
				}
			} while (*d != '\0' &&
				r < (&result[0] + sizeof(result)));
			result[r - &result[0]] = '\0';

			if (*nameptr != NULL && maxlen > 0) {
				snprintf(*nameptr, maxlen, "%s", result);
				ret = GFARM_ERR_NO_ERROR;
			} else {
				char *dn = strdup(result);

				if (likely(dn != NULL)) {
					ret = GFARM_ERR_NO_ERROR;
					*nameptr = dn;
				} else {
					ret = GFARM_ERR_NO_MEMORY;
					gflog_tls_error(GFARM_MSG_1005540,
						"Can't allocate a buffer for "
						"a GSI-compat SubjectDN.");
				}
			}
		} else {
			if (unlikely(ret == GFARM_ERR_NO_ERROR &&
					cnp == NULL)) {
				ret = GFARM_ERR_INVALID_CREDENTIAL;
				gflog_tls_error(GFARM_MSG_1005541,
					"A SubjectDN \"%s\" has no CN.", buf);
			}
		}
	} else {
		ret = GFARM_ERR_INVALID_ARGUMENT;
	}

	return (ret);
}

static inline gfarm_error_t
get_peer_cn(X509_NAME *pn, char **nameptr, int maxlen, bool allow_many_cn)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;

	if (likely(pn != NULL && nameptr != NULL)) {
		int pos = -1;
		int pos2 = -1;
		X509_NAME_ENTRY *ne = NULL;
		ASN1_STRING *as = NULL;

		/*
		 * Assumption: pn has only one CN.
		 */
		pos = X509_NAME_get_index_by_NID(pn, NID_commonName, pos);
		pos2 = X509_NAME_get_index_by_NID(pn, NID_commonName, pos);
		if (likely(((allow_many_cn == true && pos != -1) ||
			((pos != -1 && pos != -2) &&
			(pos2 == -1 || pos2 == -2))) &&
			(ne = X509_NAME_get_entry(pn, pos)) != NULL &&
			(as = X509_NAME_ENTRY_get_data(ne)) != NULL)) {
			unsigned char *u8 = NULL;
			int u8len = ASN1_STRING_to_UTF8(&u8, as);
			char *cn = NULL;

			if (likely(u8len > 0)) {
				if (*nameptr != NULL && maxlen > 0) {
					snprintf(*nameptr, maxlen, "%s", u8);
					ret = GFARM_ERR_NO_ERROR;
				} else {
					cn = strdup((char *)u8);
					if (likely(cn != NULL)) {
						ret = GFARM_ERR_NO_ERROR;
						*nameptr = cn;
					} else {
						ret = GFARM_ERR_NO_MEMORY;
						gflog_tls_error(
							GFARM_MSG_1005542,
							"Can't allocate a "
							"buffer for a CN.");
						*nameptr = NULL;
					}
				}
			}
			if (u8 != NULL) {
				OPENSSL_free(u8);
			}
		} else if (pos >= 0 && pos2 >= 0) {
			ret = GFARM_ERR_INVALID_CREDENTIAL;
			gflog_tls_notice(GFARM_MSG_1005543,
				"More than one CNs are included.");
		} else if (pos == -1 || pos == -2) {
			ret = GFARM_ERR_INVALID_CREDENTIAL;
			gflog_tls_notice(GFARM_MSG_1005544,
				"No CN is included.");
		}
	} else {
		ret = GFARM_ERR_INVALID_ARGUMENT;
	}

	return (ret);
}

/*
 * Private key loader
 */
static inline int
tty_passwd_callback_body(char *buf, int maxlen, int rwflag, void *u)
{
	int ret = 0;
	struct tls_passwd_cb_arg_struct *arg =
		(struct tls_passwd_cb_arg_struct *)u;

	(void)rwflag;

	if (likely(arg != NULL)) {
		char p[4096];
		bool has_passwd_cache = is_valid_string(arg->pw_buf_);
		bool do_passwd =
			(has_passwd_cache == false &&
			 arg->pw_buf_ != NULL &&
			 arg->pw_buf_maxlen_ > 0);

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

/*
 * An iterator for every file in a directory
 *
 * PREREQUISITE: nothing
 * LOCKS:
 *  - gfarm_privilege_lock
 */
static inline gfarm_error_t
iterate_file_in_a_dir(const char *dir,
	gfarm_error_t (*func)(const char *, void *, int *), void *funcarg,
	int *nptr)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;
	DIR *d = NULL;
	struct stat s;
	struct dirent *de = NULL;
	char filebuf[PATH_MAX];
	int nadd = 0;
	int iter_n = 0;

	gfarm_privilege_lock(dir);

	errno = 0;
	if (unlikely(dir == NULL || nptr == NULL ||
		   (d = opendir(dir)) == NULL || errno != 0)) {
		if (errno != 0) {
			ret = gfarm_errno_to_error(errno);
			gflog_tls_error(GFARM_MSG_1005545,
				"Can't open a directory %s: %s", dir,
				gfarm_error_string(ret));
		} else {
			ret = GFARM_ERR_INVALID_ARGUMENT;
		}
		goto done;
	}

	*nptr = 0;
	do {
		errno = 0;
		if (likely((de = readdir(d)) != NULL)) {
			if ((de->d_name[0] == '.' && de->d_name[1] == '\0') ||
				(de->d_name[0] == '.' && de->d_name[1] == '.'
				 && de->d_name[2] == '\0')) {
				continue;
			}
			if (func == NULL) {
				nadd++;
				continue;
			}
			(void)snprintf(filebuf, sizeof(filebuf),
				"%s/%s", dir, de->d_name);
			errno = 0;
			if (stat(filebuf, &s) == 0 &&
				S_ISREG(s.st_mode) != 0 &&
				(ret = is_file_readable(-1, filebuf)) ==
				GFARM_ERR_NO_ERROR) {
				iter_n = 0;
				ret = (func)(filebuf, funcarg, &iter_n);
				if (likely(ret == GFARM_ERR_NO_ERROR)) {
					if (iter_n > 0) {
						nadd += iter_n;
					}
				}
				/*
				 * ignore errors. iterate all the files.
				 */
			} else {
				gflog_tls_warning(GFARM_MSG_1005546,
					"Skip to treat %s.", filebuf);
				continue;
			}
		} else {
			if (errno == 0) {
				ret = GFARM_ERR_NO_ERROR;
			} else {
				ret = gfarm_errno_to_error(errno);
				gflog_tls_error(GFARM_MSG_1005547,
					"readdir(3) error: %s (%d)",
					gfarm_error_string(ret), errno);
			}
			break;
		}
	} while (true);

done:
	if (likely(ret == GFARM_ERR_NO_ERROR)) {
		*nptr = nadd;
	}
	if (d != NULL) {
		(void)closedir(d);
	}

	gfarm_privilege_unlock(dir);

	return (ret);
}

/*
 * PREREQUISITE: nothing
 * LOCKS:
 *  - gfarm_privilege_lock
 */
static inline gfarm_error_t
tls_load_prvkey(const char *file, EVP_PKEY **keyptr)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;

	if (likely(is_valid_string(file) == true && keyptr != NULL)) {
		FILE *f = NULL;
		EVP_PKEY *pkey = NULL;

		gfarm_privilege_lock(file);

		*keyptr = NULL;
		errno = 0;
		if (likely(((f = fopen(file, "r")) != NULL) &&
			((ret = is_valid_prvkey_file_permission(fileno(f),
					file)) == GFARM_ERR_NO_ERROR))) {

			struct tls_passwd_cb_arg_struct a = {
				.pw_buf_maxlen_ = sizeof(the_privkey_passwd),
				.pw_buf_ = the_privkey_passwd,
				.filename_ = file
			};

			tls_runtime_flush_error();
			pkey = PEM_read_PrivateKey(f, NULL,
					tty_passwd_callback, (void *)&a);
			if (likely(pkey != NULL &&
				tls_has_runtime_error() == false)) {
				ret = GFARM_ERR_NO_ERROR;
				*keyptr = pkey;
			} else {
				int rsn = ERR_GET_REASON(ERR_peek_error());
				if (rsn == PEM_R_BAD_DECRYPT ||
					rsn == EVP_R_BAD_DECRYPT) {
					gflog_tls_error(GFARM_MSG_1005548,
						"Wrong passphrase for "
						"private key file %s.", file);
				} else {
					gflog_tls_error(GFARM_MSG_1005549,
						"Can't read a PEM format "
						"private key from %s.", file);
				}
				ret = GFARM_ERR_INVALID_CREDENTIAL;
			}
		} else {
			if (errno != 0) {
				gflog_tls_error(GFARM_MSG_1005550,
					"Can't open %s: %s", file,
					strerror(errno));
				ret = gfarm_errno_to_error(errno);
			}
		}
		if (f != NULL) {
			(void)fclose(f);
		}

		gfarm_privilege_unlock(file);
	}

	return (ret);
}

/*
 * Accumulate X509_NAMEs from X509s in a file.
 *
 * PREREQUISITE: gfarm_privilege_lock
 */
static inline gfarm_error_t
accumulate_x509_names_from_file(const char *file,
	STACK_OF(X509_NAME) (*stack), int *n_added)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;
	FILE *fd = NULL;

	errno = 0;
	tls_runtime_flush_error();
	if (likely(is_valid_string(file) == true && stack != NULL &&
		((fd = fopen(file, "r")) != NULL))) {
		X509 *x = NULL;
		X509_NAME *xn = NULL;
		X509_NAME *xndup = NULL;
		int n_certs = 0;
		int total_certs = 0;
		bool got_failure = false;
		char b[4096];
		char *bp = b;
		int found = INT_MAX;
		gfarm_error_t got_dn = GFARM_ERR_UNKNOWN;
		bool do_dbg_msg = (gflog_get_priority_level() >= LOG_DEBUG) ?
			true : false;

		if (n_added != NULL) {
			*n_added = 0;
		}

		while ((x = PEM_read_X509(fd, NULL, NULL, NULL)) != NULL &&
			got_failure == false) {
			if (likely((xn = X509_get_subject_name(x)) != NULL &&
				(found = sk_X509_NAME_find(stack, xn)) == -1 &&
				((do_dbg_msg == true) ?
				(((got_dn = get_peer_dn_gsi_ish(xn, &bp,
					sizeof(b))) == GFARM_ERR_NO_ERROR) ?
						true : false) : true) &&
				(xndup = X509_NAME_dup(xn)) != NULL &&
				(n_certs = sk_X509_NAME_push(stack, xndup)) !=
				0)) {
				if (n_certs > 1) {
					sk_X509_NAME_sort(stack);
				}
				total_certs++;
				if (do_dbg_msg == true) {
					gflog_tls_debug(GFARM_MSG_1005551,
						"push a cert \"%s\" to a "
						"stack from %s.", b, file);
				}
			} else if (found == -1 &&
					(xndup == NULL || n_certs == 0)) {
				got_failure = true;
				if (xndup == NULL) {
					ret = GFARM_ERR_NO_MEMORY;
				} else if (n_certs == 0) {
					ret = GFARM_ERR_TLS_RUNTIME_ERROR;
				}
				if (xndup != NULL) {
					X509_NAME_free(xndup);
				}
				if (do_dbg_msg == true) {
					gflog_tls_debug(GFARM_MSG_1005552,
						"failed to push a cert \"%s\" "
						"to a stack from %s.",
						b, file);
				}
			} else if (found == -1) {
				got_failure = true;
				ret = GFARM_ERR_TLS_RUNTIME_ERROR;
				if (xn != NULL && got_dn ==
					GFARM_ERR_NO_ERROR) {
					gflog_tls_error(GFARM_MSG_1005553,
						"Can't add a cert \"%s\" "
						"from %s.", b, file);
				} else {
					gflog_tls_error(GFARM_MSG_1005554,
						"Can't add a cert from %s.",
						file);
				}
			}
			found = INT_MAX;
			got_dn = GFARM_ERR_UNKNOWN;
			n_certs = 0;
			X509_free(x);
			x = NULL;
			b[0] = '\0';
			xn = NULL;
			xndup = NULL;
		}
		if (likely(got_failure == false)) {
			tls_runtime_flush_error();
			ret = GFARM_ERR_NO_ERROR;
			if (n_added != NULL) {
				*n_added = total_certs;
			}
			if (total_certs == 0) {
				gflog_tls_warning(GFARM_MSG_1005555,
					"No cert is added from %s.", file);
			}
		}
	} else {
		if (is_valid_string(file) == true && fd == NULL) {
			ret = gfarm_errno_to_error(errno);
			gflog_tls_error(GFARM_MSG_1005556,
				"Can't open %s: %s.",
				file, gfarm_error_string(ret));
		} else {
			ret = GFARM_ERR_INVALID_ARGUMENT;
		}
	}

	if (fd != NULL) {
		(void)fclose(fd);
	}

	return (ret);
}

/*
 * Cert files collector for acceptable certs list.
 *
 * PREREQUISITE: nothing
 * LOCKS:
 *  - gfarm_privilege_lock
 */
static inline gfarm_error_t
tls_get_x509_name_stack_from_dir(const char *dir,
	STACK_OF(X509_NAME) (*stack), int *nptr)
{
	return (iterate_file_in_a_dir(dir,
			iterate_file_for_x509_name, stack, nptr));
}

/*
 * PREREQUISITE: nothing
 * LOCKS:
 *  - gfarm_privilege_lock
 */
static inline gfarm_error_t
tls_set_ca_path(SSL_CTX *ssl_ctx,
	const char *ca_path, const char* acceptable_ca_path,
	STACK_OF(X509_NAME) (**trust_ca_list))
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;
	int st;

	/*
	 * NOTE: What Apache 2.4 does for this are:
	 *
	 *	SSL_CTX_load_verify_locations(ctx,
	 *		tls_ca_certificate_path);
	 *	if (tls_ca_peer_verify_chain_path) {
	 *		dir = tls_ca_peer_verify_chain_path;
	 *	} else {
	 *		dir = tls_ca_certificate_path;
	 *	}
	 *	STACK_OF(X509_NAME) *ca_list;
	 *
	 *	while (opendir(dir)/readdir()) {
	 *		SSL_add_file_cert_subjects_to_stack(ca_list,
	 *			file);
	 *	}
	 *	SSL_CTX_set_client_CA_list(ctx, ca_list);
	 */

	/*
	 * NOTE: And the above won't works since the CA list that
	 *	server sent is just an advisory.
	 *
	 *	What we do is:
	 *
	 *	Making the ca_list and compare the x509_NAME in
	 *	ca_list with peer cert one by one in verify callback
	 *	func, for both the client and the server.
	 *
	 *	And, in case we WON'T do this, it is going to be a
	 *	massive security problem since:
	 *
	 *	1) Sendig the CA list from server to client is easily
	 *	ignored by client side. E.g.) Apache 2.4 sends the CA
	 *	list in TLSv1.3 session, OpenSSL 1.1.1 s_client
	 *	ignores it and send a complet chained cert which
	 *	includes any certs not in the CA list sent by the
	 *	server, the server accepts it and returns "200 OK."
	 *
	 *	2) Futhere more, any clients can send a complete
	 *	chained certificate and servers won't reject it if the
	 *	server has the root CA cert of the given chain in CA
	 *	cert path.
	 *
	 *	3) In Gfarm, clients' end entity CN are the key for
	 *	authorization and the CN could be easily acquireable
	 *	by social hacking. If a malcious one somehow creates
	 *	an intermediate CA which root CA is in servers' CA
	 *	cert path, a cert having the CN could be forgeable for
	 *	impersonation.
	 */

#ifndef HAVE_OPENSSL_3_0
	if (likely(ssl_ctx != NULL && is_valid_string(ca_path) == true)) {
		if (trust_ca_list != NULL) {
			*trust_ca_list = NULL;
		}
		tls_runtime_flush_error();
		gfarm_privilege_lock(ca_path);
		st = SSL_CTX_load_verify_locations(ssl_ctx, NULL, ca_path);
		gfarm_privilege_unlock(ca_path);
		if (likely(st == 1)) {
			ret = GFARM_ERR_NO_ERROR;
		} else {
			gflog_tls_error(GFARM_MSG_1005557,
				"Failed to set CA path to a SSL_CTX.");
			ret = GFARM_ERR_TLS_RUNTIME_ERROR;
			goto done;
		}
		if (is_valid_string(acceptable_ca_path) == true) {
			bool need_free_ca_list = false;
			int ncerts = 0;
			const char *dir = acceptable_ca_path;
			STACK_OF(X509_NAME) (*ca_list) =
				sk_X509_NAME_new(x509_name_compare);
			if (likely(ca_list != NULL &&
				(ret = tls_get_x509_name_stack_from_dir(
					dir, ca_list, &ncerts)) ==
				GFARM_ERR_NO_ERROR)) {
				if (ncerts > 0) {
					if (trust_ca_list != NULL) {
						*trust_ca_list = ca_list;
					}
				} else {
					need_free_ca_list = true;
				}
			} else {
				if (ca_list == NULL) {
					gflog_tls_error(GFARM_MSG_1005558,
						"Can't allocate "
						"STACK_OF(X509_NAME).");
					ret = GFARM_ERR_NO_MEMORY;
					goto done;
				} else if (ret == GFARM_ERR_NO_ERROR &&
						ncerts == 0) {
					gflog_tls_warning(GFARM_MSG_1005559,
						"No cert file is "
						"added as a valid cert under "
						"%s directory.", dir);
					need_free_ca_list = true;
				}
			}
			if (need_free_ca_list == true) {
				sk_X509_NAME_pop_free(ca_list,
					      X509_NAME_free);
			}
		}
	} else {
		ret = GFARM_ERR_INVALID_ARGUMENT;
	}

done:

#else
	X509_STORE *ch = NULL;
	X509_STORE *ve = NULL;
	const char *ve_path = (is_valid_string(acceptable_ca_path) == true) ?
		acceptable_ca_path : ca_path;

	if (trust_ca_list != NULL) {
		*trust_ca_list = NULL;
	}

	/* chain */
	tls_runtime_flush_error();
	if (likely(ssl_ctx != NULL && is_valid_string(ca_path) == true &&
		(ch = X509_STORE_new()) != NULL)) {

		tls_runtime_flush_error();
		gfarm_privilege_lock(ca_path);
		st = X509_STORE_load_path(ch, ca_path);
		gfarm_privilege_unlock(ca_path);

		if (likely(st == 1)) {
			tls_runtime_flush_error();
			if (likely(SSL_CTX_set0_chain_cert_store(
					ssl_ctx, ch) == 1)) {
				ret = GFARM_ERR_NO_ERROR;
			} else {
				gflog_tls_error(GFARM_MSG_1005560,
					"Failed to set a CA chain path to a "
					"SSL_CTX");
				ret = GFARM_ERR_TLS_RUNTIME_ERROR;
				goto done;
			}
		} else {
			gflog_tls_error(GFARM_MSG_1005561,
				"Failed to load a CA cnain path");
			ret = GFARM_ERR_TLS_RUNTIME_ERROR;
			goto done;
		}
	} else {
		if (ch == NULL) {
			ret = GFARM_ERR_NO_MEMORY;
			gflog_tls_error(GFARM_MSG_1005562,
				"Can't allocate a X509_STORE: %s",
				gfarm_error_string(ret));
		} else {
			ret = GFARM_ERR_INVALID_ARGUMENT;
		}
		goto done;
	}

	/* verify */
	tls_runtime_flush_error();
	if (likely(ssl_ctx != NULL && is_valid_string(ca_path) == true &&
		(ve = X509_STORE_new()) != NULL)) {

		tls_runtime_flush_error();
		gfarm_privilege_lock(ve_path);
		st = X509_STORE_load_path(ve, ve_path);
		gfarm_privilege_unlock(ve_path);

		if (likely(st == 1)) {
			tls_runtime_flush_error();
			if (likely(SSL_CTX_set0_verify_cert_store(
					ssl_ctx, ve) == 1)) {
				ret = GFARM_ERR_NO_ERROR;
			} else {
				gflog_tls_error(GFARM_MSG_1005563,
					"Failed to set a CA verify path to a "
					"SSL_CTX");
				ret = GFARM_ERR_TLS_RUNTIME_ERROR;
				goto done;
			}
		} else {
			gflog_tls_error(GFARM_MSG_1005564,
				"Failed to load a CA verify path");
			ret = GFARM_ERR_TLS_RUNTIME_ERROR;
			goto done;
		}
	} else {
		if (ch == NULL) {
			ret = GFARM_ERR_NO_MEMORY;
			gflog_tls_error(GFARM_MSG_1005565,
				"Can't allocate a X509_STORE: %s",
				gfarm_error_string(ret));
		} else {
			ret = GFARM_ERR_INVALID_ARGUMENT;
		}
		goto done;
	}

done:
	if (unlikely(ret != GFARM_ERR_NO_ERROR)) {
		if (ch != NULL) {
			X509_STORE_free(ch);
		}
		if (ve != NULL) {
			X509_STORE_free(ve);
		}
	}
#endif /* ! HAVE_OPENSSL_3_0 */

	return (ret);
}

/*
 * Add extra cert(s) from a file into SSL_CTX
 *
 * PREREQUISITE: nothing
 * LOCKS:
 *  - gfarm_privilege_lock
 */
static inline gfarm_error_t
tls_add_extra_certs(SSL_CTX *ssl_ctx, const char *file, int *n_added)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;
	FILE *fd = NULL;
	int osst = -INT_MAX;

	gfarm_privilege_lock(file);

	errno = 0;
	tls_runtime_flush_error();
	if (likely(ssl_ctx != NULL && is_valid_string(file) == true &&
		((fd = fopen(file, "r")) != NULL))) {
		X509 *x = NULL;
		X509_NAME *xn = NULL;
		int n_certs = 0;
		bool got_failure = false;
		char b[4096];
		char *bp = b;
		bool do_dbg_msg = (gflog_get_priority_level() >= LOG_DEBUG) ?
			true : false;

		if (n_added != NULL) {
			*n_added = 0;
		}

		(void)SSL_CTX_clear_extra_chain_certs(ssl_ctx);
		while ((x = PEM_read_X509(fd, NULL, NULL, NULL)) != NULL &&
			got_failure == false) {
			tls_runtime_flush_error();
			osst = SSL_CTX_add_extra_chain_cert(ssl_ctx, x);
			if (likely(osst == 1)) {
				n_certs++;
				if (unlikely((do_dbg_msg == true) &&
					(xn = X509_get_subject_name(x)) !=
					NULL)) {
					get_peer_dn_gsi_ish(xn,
						&bp, sizeof(b));
					gflog_tls_debug(GFARM_MSG_1005566,
						"Add a cert \"%s\" from %s.",
						b, file);
				}
			} else {
				got_failure = true;
				xn = X509_get_subject_name(x);
				if (xn != NULL) {
					get_peer_dn_gsi_ish(xn,
						&bp, sizeof(b));
					gflog_tls_error(GFARM_MSG_1005567,
						"Can't add a cert \"%s\" "
						"from %s.", b, file);
				} else {
					gflog_tls_error(GFARM_MSG_1005568,
						"Can't add a cert from %s.",
						file);
				}
				X509_free(x);
			}
		}
		if (likely(got_failure == false)) {
			tls_runtime_flush_error();
			ret = GFARM_ERR_NO_ERROR;
			if (n_added != NULL) {
				*n_added = n_certs;
			}
			if (n_certs == 0) {
				gflog_tls_warning(GFARM_MSG_1005569,
					"No cert is added from %s.", file);
			}
		} else {
			ret = GFARM_ERR_TLS_RUNTIME_ERROR;
		}
	} else {
		if (osst == -INT_MAX && fd == NULL) {
			ret = GFARM_ERR_INVALID_ARGUMENT;
		} else if (fd == NULL) {
			ret = gfarm_errno_to_error(errno);
			gflog_tls_error(GFARM_MSG_1005570,
				"Can't open %s: %s.",
				file, gfarm_error_string(ret));
		}
	}

	if (fd != NULL) {
		(void)fclose(fd);
	}

	gfarm_privilege_unlock(file);

	return (ret);
}

/*
 * helper function
 *
 * PREREQUISITE: nothing
 * LOCKS:
 *  - gfarm_privilege_lock
 */
static inline int
use_certificate_chain_file_w_lock(SSL_CTX *ssl_ctx, const char *file)
{
	int st;

	gfarm_privilege_lock(file);
	st = SSL_CTX_use_certificate_chain_file(ssl_ctx, file);
	gfarm_privilege_unlock(file);
	return (st);
}

/*
 * Load both cert file and cert chain file
 *
 * PREREQUISITE: nothing
 * LOCKS:
 *  - gfarm_privilege_lock
 */
static inline gfarm_error_t
tls_load_cert_and_chain(SSL_CTX *ssl_ctx,
	const char *cert_file, const char *cert_chain_file, int *nptr)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;

	if (likely(ssl_ctx != NULL)) {
		int n_certs = 0;
		int n;
		int ost = INT_MAX - 1;

		if (nptr != NULL) {
			*nptr = 0;
		}

		tls_runtime_flush_error();
		if (is_valid_string(cert_file) == true &&
			is_valid_string(cert_chain_file) == false &&
			(ost = use_certificate_chain_file_w_lock(ssl_ctx,
				cert_file)) == 1) {
			ret = GFARM_ERR_NO_ERROR;
			n_certs = 1;
		} else if (is_valid_string(cert_file) == false &&
			is_valid_string(cert_chain_file) == true &&
			(ost = use_certificate_chain_file_w_lock(ssl_ctx,
				cert_chain_file)) == 1) {
			ret = GFARM_ERR_NO_ERROR;
			n_certs = 1;
		} else {
			if (is_valid_string(cert_file) == true &&
				(ost = use_certificate_chain_file_w_lock(
					ssl_ctx, cert_file)) == 1) {
				if (is_valid_string(cert_chain_file) == true) {
					n = 0;
					ret = tls_add_extra_certs(ssl_ctx,
						cert_chain_file, &n);
					if (likely(ret ==
						GFARM_ERR_NO_ERROR)) {
						n_certs += n;
					} else {
						goto done;
					}
				}
			}
		}

		if (nptr != NULL) {
			*nptr = n_certs;
		}
	}

done:
	return (ret);
}

/*
 * Set revocation path
 *
 * PREREQUISITE: nothing
 * LOCKS:
 *  - gfarm_privilege_lock
 */
static inline gfarm_error_t
tls_set_revoke_path(SSL_CTX *ssl_ctx, const char *revoke_path)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;
	X509_STORE *store = NULL;
	int nent = -1;

	tls_runtime_flush_error();
	if (likely(ssl_ctx != NULL &&
		(ret = iterate_file_in_a_dir(revoke_path,
			NULL, NULL, &nent)) == GFARM_ERR_NO_ERROR &&
		nent > 0 &&
		(store = SSL_CTX_get_cert_store(ssl_ctx)) != NULL &&
		is_valid_string(revoke_path) == true)) {
		int st;

		tls_runtime_flush_error();
		gfarm_privilege_lock(revoke_path);
		st = X509_STORE_load_locations(store, NULL, revoke_path);
		gfarm_privilege_unlock(revoke_path);
		if (likely(st == 1)) {
			tls_runtime_flush_error();
			st = X509_STORE_set_flags(store,
				X509_V_FLAG_CRL_CHECK |
				X509_V_FLAG_CRL_CHECK_ALL);
			if (likely(st == 1)) {
				ret = GFARM_ERR_NO_ERROR;
			} else {
				gflog_tls_error(GFARM_MSG_1005571,
					"Failed to set CRL flags "
					"to an X509_STORE.");
				ret = GFARM_ERR_TLS_RUNTIME_ERROR;
			}
		} else {
			gflog_tls_error(GFARM_MSG_1005572,
				"Failed to set CRL path to an SSL_CTX.");
			ret = GFARM_ERR_TLS_RUNTIME_ERROR;
		}
	} else if (ret != GFARM_ERR_NO_ERROR) {
		if (tls_has_runtime_error() == true) {
			gflog_tls_error(GFARM_MSG_1005573,
				"Failed to get current X509_STORE from "
				"an SSL_CTX.");
			ret = GFARM_ERR_TLS_RUNTIME_ERROR;
		} else {
			ret = GFARM_ERR_INVALID_ARGUMENT;
		}
	}

	return (ret);
}

/*
 * Certificate verification callback
 */
static inline int
tls_verify_callback_body(int ok, X509_STORE_CTX *sctx)
{
	int ret = ok;
	int org_ok = ok;
	SSL *ssl = X509_STORE_CTX_get_ex_data(sctx,
			SSL_get_ex_data_X509_STORE_CTX_idx());
	struct tls_session_ctx_struct *ctx = (ssl != NULL) ?
		(struct tls_session_ctx_struct *)SSL_get_app_data(ssl) : NULL;
	int verr = X509_STORE_CTX_get_error(sctx);
	int org_verr = verr;
	int vdepth = X509_STORE_CTX_get_error_depth(sctx);
	const char *verrstr = NULL;
	bool do_dbg_msg = (gflog_get_priority_level() >= LOG_DEBUG) ?
		true : false;
	X509 *p = X509_STORE_CTX_get_current_cert(sctx);
	X509_NAME *pn = (p != NULL) ? X509_get_subject_name(p) : NULL;

	if (likely(ok == 1)) {

		/*
		 * Here we can deny auth for our own purpose even it
		 * is accpetable.
		 */

		/*
		 * NOTE: The certs gonna coming here in order of top
		 *	to bottom (root CA, ... some intermediate CAs,
		 *	... EEC, proxy cert, proxy cert 1, ...)
		 */

		PROXY_CERT_INFO_EXTENSION *pci = NULL;

		if (ctx->is_got_proxy_cert_ == false &&
			(p != NULL &&
			((X509_get_extension_flags(p) & EXFLAG_PROXY) != 0) &&
			(pci = X509_get_ext_d2i(p, NID_proxyCertInfo,
					NULL, NULL)) != NULL)) {
			/*
			 * got a proxy cert.
			 */
			if (ctx->do_allow_proxy_cert_ == true) {
				X509_NAME *xn = X509_get_issuer_name(p);
				if (likely(xn != NULL)) {
					/*
					 * Acquire X509_NAME of the
					 * issuer only for the first
					 * proxy cert.
					 */
					ctx->is_got_proxy_cert_ = true;
					ctx->proxy_issuer_ =
						X509_NAME_dup(xn);
					if (do_dbg_msg == true) {
						char b[4096];
						char *bp = b;
						get_peer_dn_gsi_ish(
							ctx->proxy_issuer_,
							&bp, sizeof(b));
						gflog_tls_debug(
							GFARM_MSG_1005574,
							"got proxy issure: "
							"\"%s\"", b);
					}
				} else {
					gflog_tls_error(GFARM_MSG_1005575,
						"Can't acquire an issure name "
						"of the proxy cert.");
					/* make the auth failure. */
					ok = ret = 0;
/* checkpatch */
#define VFYERR_GET_ISSUER \
	X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT
					verr = VFYERR_GET_ISSUER;
#undef VFYERR_GET_ISSUER
					X509_STORE_CTX_set_error(sctx, verr);
				}
			} else {
				/*
				 * Must not happen.
				 */
				gflog_tls_warning(GFARM_MSG_1005576,
					"Something wrong: got a proxy cert "
					"and it's authorized by the verify "
					"flags, but Gfarm itself doesn't "
					"allow the internal proxy use??");
			}
			PROXY_CERT_INFO_EXTENSION_free(pci);
			goto done;
		}

		/*
		 * Trusted cert check
		 */
		if (likely(vdepth > 0 && pn != NULL &&
			ctx->trusted_certs_ != NULL &&
			sk_X509_NAME_find(ctx->trusted_certs_, pn) == -1)) {
			ok = ret = 0;
			verr = X509_V_ERR_CERT_UNTRUSTED;
			X509_STORE_CTX_set_error(sctx, verr);
			goto done;
		}

	} else {

		/*
		 * Here we can accept auth for our own purpose even it
		 * must be denied.
		 */

		if (ctx->do_allow_no_crls_ == true &&
			verr == X509_V_ERR_UNABLE_TO_GET_CRL) {
			/* CRL error recovery */
			X509_STORE_CTX_set_error(sctx, X509_V_OK);
			verr = X509_V_OK;
			ok = ret = 1;
			goto done;
		}
	}

done:
	ctx->cert_verify_callback_error_ = verr;

	if (do_dbg_msg == true) {
		char dnbuf[4096];
		char *dn = dnbuf;

		if (org_ok == 0 && org_verr != X509_V_OK) {
			verrstr = X509_verify_cert_error_string(org_verr);
		} else {
			verrstr = X509_verify_cert_error_string(verr);
		}

		if (pn != NULL &&
			get_peer_dn_gsi_ish(pn, &dn, sizeof(dnbuf)) ==
			GFARM_ERR_NO_ERROR) {
			dn = dnbuf;
		} else {
			dn = NULL;
		}

		gflog_tls_debug(GFARM_MSG_1005577, "depth %d; ok %d -> %d; "
			" cert \"%s\"; error %d -> %d: error string \"%s.\"",
			vdepth, org_ok, ok, dn, org_verr, verr, verrstr);
	}

	return (ret);
}

static gfarm_error_t
tls_verify_callback_simple(int ok, X509_STORE_CTX *store_ctx)
{
	int error;

	if (ok)
		return (ok);

	error = X509_STORE_CTX_get_error(store_ctx);

	/* XXX: layering violation: should use ctx->do_allow_no_crls */
	if (error == X509_V_ERR_UNABLE_TO_GET_CRL &&
	    gfarm_ctxp->tls_allow_no_crl) {
		/* CRL error recovery */
		X509_STORE_CTX_set_error(store_ctx, X509_V_OK);
		return (1);
	}

	if (gflog_auth_get_verbose()) {
		int depth = X509_STORE_CTX_get_error_depth(store_ctx);
		X509 *cert = X509_STORE_CTX_get_current_cert(store_ctx);
		X509_NAME *sn = X509_get_subject_name(cert);
		char dnbuf[4096], *dn = dnbuf;

		if (get_peer_dn_gsi_ish(sn, &dn, sizeof(dnbuf)) !=
		    GFARM_ERR_NO_ERROR)
			dn = "<unprintable_subject_name>";
		gflog_tls_error(GFARM_MSG_1005578,
		    "self certificate verification error: "
		    "depth %d, cert \"%s\": error %d (%s)",
		    depth, dn, error, X509_verify_cert_error_string(error));
	}
	return (0);
}

static inline gfarm_error_t
tls_verify_self_certificate(SSL_CTX *ssl_ctx)
{
	X509_STORE *cert_store;
	X509_VERIFY_PARAM *tmpvpm = NULL;
	X509 *self_cert;
	STACK_OF(X509) *chain;
	X509_STORE_CTX *store_ctx;
	int st;

	tls_runtime_flush_error();

	cert_store = SSL_CTX_get_cert_store(ssl_ctx);
	if (cert_store == NULL) {
		gflog_tls_error(GFARM_MSG_1005579,
		    "verify self certificate: "
		    "SSL_CTX_get_cert_store() failed");
		return (GFARM_ERR_TLS_RUNTIME_ERROR);
	}
	tmpvpm = X509_STORE_get0_param(cert_store);
	if (tmpvpm != NULL) {
		/* keep the flags match with tls_session_create_ctx() */

		unsigned long flags = 0;

		flags |= (X509_V_FLAG_CRL_CHECK |
				X509_V_FLAG_CRL_CHECK_ALL);

		/*
		 * XXX: layering violation:
		 * should use GFP_XDR_TLS_CLIENT_USE_PROXY_CERTIFICATE
		 */
		if (gfarm_ctxp->tls_proxy_certificate)
			flags |= X509_V_FLAG_ALLOW_PROXY_CERTS;

		st = X509_VERIFY_PARAM_set_flags(tmpvpm, flags);
		if (st != 1) {
			gflog_tls_error(GFARM_MSG_1005580,
			    "verify self certificate: "
			    "X509_VERIFY_PARAM_set_flags() failed");
			return (GFARM_ERR_TLS_RUNTIME_ERROR);
		}
	}
	X509_STORE_set_verify_cb(cert_store, tls_verify_callback_simple);

	self_cert = SSL_CTX_get0_certificate(ssl_ctx);

	st = SSL_CTX_get0_chain_certs(ssl_ctx, &chain);
	if (st != 1) {
		gflog_tls_error(GFARM_MSG_1005581,
		    "verify self certificate: "
		    "SSL_CTX_get0_chain_certs() failed");
		return (GFARM_ERR_TLS_RUNTIME_ERROR);
	}

	store_ctx = X509_STORE_CTX_new();
	if (store_ctx == NULL) {
		gflog_tls_error(GFARM_MSG_1005582,
		    "verify self certificate: "
		    "X509_STORE_CTX_new() failed");
		return (GFARM_ERR_TLS_RUNTIME_ERROR);
	}

	st = X509_STORE_CTX_init(store_ctx, cert_store, self_cert, chain);
	if (st != 1) {
		gflog_tls_error(GFARM_MSG_1005583,
		    "verify self certificate: X509_STORE_CTX_init() failed");
	} else {
		st = X509_verify_cert(store_ctx);
		if (st != 1 && gflog_auth_get_verbose()) {
			/*
			 * this is usually unnecessary,
			 * because tls_verify_callback_simple() shows it.
			 */
			gflog_tls_debug(GFARM_MSG_1005584,
			    "self certificate verification error");
		}
	}
	X509_STORE_CTX_free(store_ctx);

	return (st == 1 ? GFARM_ERR_NO_ERROR : GFARM_ERR_TLS_RUNTIME_ERROR);
}

/*
 * Internal TLS context constructor/destructor
 */
static inline void
tls_session_clear_ctx(struct tls_session_ctx_struct *ctx, int flags)
{
#define free_n_nullify(free_func, obj)			\
	do {						\
		if (ctx->obj != NULL) {		\
			(void)free_func(ctx->obj);	\
			ctx->obj = NULL;		\
		}					\
	} while (false)

	if (likely(ctx != NULL)) {
		if ((flags & CTX_CLEAR_RECONN) != 0) {
			if (ctx->ssl_ != NULL) {
				(void)SSL_clear(ctx->ssl_);
			}
		}
		if ((flags & CTX_CLEAR_SSL) != 0) {
			free_n_nullify(SSL_free, ssl_);
		}
		if ((flags & (CTX_CLEAR_VAR | CTX_CLEAR_RECONN)) != 0) {
			ctx->last_ssl_error_ = SSL_ERROR_SSL;
			ctx->is_got_fatal_ssl_error_ = false;
			ctx->io_total_ = 0;
			ctx->io_key_update_accum_ = 0;
		}

		/*
		 * ssize_t io_key_update_thresh_;
		 */

		if ((flags & (CTX_CLEAR_VAR | CTX_CLEAR_RECONN)) != 0) {
			ctx->last_gfarm_error_ = GFARM_ERR_UNKNOWN;
		}

		/*
		 * enum tls_role role_;
		 * bool do_mutual_auth_;
		 * bool do_build_chain_;
		 * bool do_allow_no_crls_;
		 * bool do_allow_proxy_cert_;
		 */

		if ((flags & CTX_CLEAR_CTX) != 0) {
			free_n_nullify(free, cert_file_);
			free_n_nullify(free, cert_chain_file_);
			free_n_nullify(free, prvkey_file_);
			free_n_nullify(free, ciphersuites_);
			free_n_nullify(free, ca_path_);
			free_n_nullify(free, acceptable_ca_path_);
			free_n_nullify(free, revoke_path_);
		}

		if ((flags & (CTX_CLEAR_CTX | CTX_CLEAR_VAR)) != 0) {
			free_n_nullify(free, peer_dn_oneline_);
			free_n_nullify(free, peer_dn_rfc2253_);
			free_n_nullify(free, peer_dn_gsi_);
			free_n_nullify(free, peer_cn_);
		}

		if ((flags & CTX_CLEAR_VAR) != 0) {
			ctx->is_handshake_tried_ = false;
			ctx->is_verified_ = false;
			ctx->is_got_proxy_cert_ = false;

			ctx->cert_verify_callback_error_ =
				X509_V_ERR_UNSPECIFIED;
			ctx->cert_verify_result_error_ =
				X509_V_ERR_UNSPECIFIED;
		}

		if ((flags & CTX_CLEAR_CTX) != 0) {
			if (ctx->trusted_certs_ != NULL) {
				sk_X509_NAME_pop_free(ctx->trusted_certs_,
						X509_NAME_free);
			}
			ctx->trusted_certs_ = NULL;
			free_n_nullify(SSL_CTX_free, ssl_ctx_);
			free_n_nullify(EVP_PKEY_free, prvkey_);
			free_n_nullify(X509_NAME_free, proxy_issuer_);

			free(ctx);
		}
	}
#undef free_n_nullify
}

static inline gfarm_error_t
tls_session_clear_ctx_for_reconnect(struct tls_session_ctx_struct *ctx)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;

	if (likely(ctx != NULL)) {
		tls_session_clear_ctx(ctx,
			CTX_CLEAR_READY_FOR_RECONNECT);
	} else {
		ret = GFARM_ERR_INVALID_ARGUMENT;
	}

	return (ret);
}

static inline gfarm_error_t
tls_session_clear_ctx_for_reestablish(struct tls_session_ctx_struct *ctx)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;

	if (likely(ctx != NULL)) {
		(void)tls_session_shutdown(ctx);
		tls_session_clear_ctx(ctx,
		      CTX_CLEAR_READY_FOR_ESTABLISH);
	} else {
		ret = GFARM_ERR_INVALID_ARGUMENT;
	}

	return (ret);
}

static inline gfarm_error_t
tls_session_setup_ssl(struct tls_session_ctx_struct *ctx)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;
	SSL *ssl = NULL;
	SSL_CTX *ssl_ctx = NULL;
	enum tls_role role = TLS_ROLE_UNKNOWN;

	if (unlikely(ctx == NULL || (ssl_ctx = ctx->ssl_ctx_) == NULL ||
		(role = ctx->role_) == TLS_ROLE_UNKNOWN)) {
		ret = GFARM_ERR_INVALID_ARGUMENT;
		goto done;
	}

	tls_runtime_flush_error();
	if (ctx->ssl_ == NULL) {
		ssl = SSL_new(ssl_ctx);
	} else {
		ssl = ctx->ssl_;
	}
	if (likely(ssl != NULL)) {
		/*
		 * Make this SSL only for TLSv1.3, for sure.
		 */
		int osst = -1;

		if ((osst = SSL_get_min_proto_version(ssl)) !=
			TLS1_3_VERSION ||
			(osst = SSL_get_max_proto_version(ssl)) !=
			TLS1_3_VERSION) {

			tls_runtime_flush_error();
			if (unlikely((osst = SSL_set_min_proto_version(ssl,
						TLS1_3_VERSION)) != 1 ||
					(osst = SSL_set_max_proto_version(ssl,
						TLS1_3_VERSION)) != 1)) {
				gflog_tls_error(GFARM_MSG_1005585,
					"Failed to set an SSL "
					"only using TLSv1.3.");
				ret = GFARM_ERR_TLS_RUNTIME_ERROR;
				goto done;
			} else {
				if ((osst = SSL_get_min_proto_version(ssl)) !=
					TLS1_3_VERSION ||
					(osst = SSL_get_max_proto_version(
						ssl)) != TLS1_3_VERSION) {
					gflog_tls_error(GFARM_MSG_1005586,
						"Failed to check if the SSL "
						"only using TLSv1.3.");
					ret = GFARM_ERR_TLS_RUNTIME_ERROR;
					goto done;
				} else {
					ret = GFARM_ERR_NO_ERROR;
				}
			}
		} else {
			ret = GFARM_ERR_NO_ERROR;
		}

		if (ret == GFARM_ERR_NO_ERROR) {
			/*
			 * Set a verify callback user arg.
			 */
			tls_runtime_flush_error();
			osst = SSL_set_app_data(ssl, ctx);
			if (osst != 1) {
				gflog_tls_error(GFARM_MSG_1005587,
					"Failed to set an arg for the verify "
					"callback");
				ret = GFARM_ERR_TLS_RUNTIME_ERROR;
			}
		}

#if 0
		/*
		 * XXX FIXME:
		 *
		 * calling SSL_verify_client_post_handshake() always
		 * returns 0, with "wrong protocol version" error even
		 * the SSL is setup for TLSv1.3. Is calling the API
		 * not needed?  Actually, there's no source code
		 * calling the function in OpenSSL sources. I thought
		 * s_server calls it but it does not. Or maybe it must
		 * be called "AFTER" the handshake done, when the
		 * server really needs client certs...
		 */
		if (ctx->do_mutual_auth_ == true &&
			role == TLS_ROLE_SERVER) {
			tls_runtime_flush_error();
			if (likely(SSL_verify_client_post_handshake(
					ssl) == 1)) {
				ret = GFARM_ERR_NO_ERROR;
			} else {
				gflog_tls_error(GFARM_MSG_1005588,
					"Failed to set a "
					"server SSL to use "
					"post-handshake.");
				ret = GFARM_ERR_TLS_RUNTIME_ERROR;
			}
		}
#endif

		if (ret == GFARM_ERR_NO_ERROR) {
			tls_session_clear_ctx(ctx,
				CTX_CLEAR_READY_FOR_ESTABLISH);
			ctx->ssl_ = ssl;
		}
	} else {
		ret = GFARM_ERR_INVALID_ARGUMENT;
	}

done:
	if (ssl != NULL && ret != GFARM_ERR_NO_ERROR) {
		SSL_free(ssl);
		ctx->ssl_ = NULL;
	}

	return (ret);
}

/*
 * Official xported APIs
 */

/*
 * Constructor
 *
 * PREREQUISITE: nothing
 * LOCKS:
 *  - gfarm_privilege_lock
 */
static inline gfarm_error_t
tls_session_create_ctx(struct tls_session_ctx_struct **ctxptr,
		       enum tls_role role,
		       bool do_mutual_auth, bool use_proxy_cert)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;

	struct tls_session_ctx_struct *ctxret = NULL;

	EVP_PKEY *prvkey = NULL;
	SSL_CTX *ssl_ctx = NULL;

	bool do_build_chain = false;
	bool need_self_cert = false;
	bool do_proxy_auth = false;

	char *tmp = NULL;
	char *tmp_proxy_cert_file = NULL;

	/*
	 * Following strings must be copied to *ctxret
	 */
	char *cert_file = NULL;		/* required always */
	char *cert_chain_file = NULL;	/* required for server/mutual */
	char *prvkey_file = NULL;	/* required for server/mutual */
	char *ca_path = NULL;		/* required for server/mutual */
	char *acceptable_ca_path = NULL;
	char *revoke_path = NULL;
	char *ciphersuites = NULL;
	STACK_OF(X509_NAME) (*trust_ca_list) = NULL;

	/*
	 * Parameter check
	 */
	if (unlikely(ctxptr == NULL)) {
		gflog_tls_error(GFARM_MSG_1005589,
			"return pointer is NULL.");
		ret = GFARM_ERR_INVALID_ARGUMENT;
		goto bailout;
	} else {
		*ctxptr = NULL;
	}
	if (unlikely(role != TLS_ROLE_SERVER && role != TLS_ROLE_CLIENT)) {
		gflog_tls_error(GFARM_MSG_1005590,
			"fatal: invalid TLS role.");
		ret = GFARM_ERR_INVALID_ARGUMENT;
		goto bailout;
	}
	if (unlikely(role == TLS_ROLE_CLIENT && use_proxy_cert == true &&
		do_mutual_auth == false)) {
		ret = GFARM_ERR_INVALID_ARGUMENT;
		goto bailout;
	}

	/*
	 * No doamin check for following variables in gfarm_ctxp:
	 *	tls_build_chain_local
	 *	tls_allow_no_crl
	 * Callers must guarantee that values are 0 or 1.
	 */

	/*
	 * Gfarm context check
	 */
	if (unlikely(gfarm_ctxp == NULL)) {
		gflog_tls_error(GFARM_MSG_1005591,
			"fatal: NULL gfarm_ctxp.");
		ret = GFARM_ERR_INTERNAL_ERROR;
		goto bailout;
	}

#define str_or_NULL(x)					\
	((is_valid_string((x)) == true) ? (x) : NULL)

	/*
	 * CA certs path (mandatory always)
	 */
	tmp = str_or_NULL(gfarm_ctxp->tls_ca_certificate_path);
	if ((is_valid_string(tmp) == true) &&
		((ret = is_valid_cert_store_dir(tmp))
		== GFARM_ERR_NO_ERROR)) {
		ca_path = strdup(tmp);
		if (unlikely(ca_path == NULL)) {
			ret = GFARM_ERR_NO_MEMORY;
			gflog_tls_error(GFARM_MSG_1005592,
				"Can't duplicate a CA certs directory "
				" name: %s", gfarm_error_string(ret));
			goto bailout;
		}
	} else {
		if (tmp == NULL) {
			gflog_tls_error(GFARM_MSG_1005593,
				"A CA cert path is not specified.");
			ret = GFARM_ERR_INVALID_ARGUMENT;
		} else {
			gflog_tls_error(GFARM_MSG_1005594,
				"Failed to check a CA certs directory %s: %s",
				tmp, gfarm_error_string(ret));
		}
		goto bailout;
	}

	/*
	 * Revocation path (optional)
	 */
	tmp = str_or_NULL(gfarm_ctxp->tls_ca_revocation_path);
	if ((is_valid_string(tmp) == true) &&
		((ret = is_valid_cert_store_dir(tmp)) == GFARM_ERR_NO_ERROR)) {
		revoke_path = strdup(tmp);
		if (unlikely(revoke_path == NULL)) {
			ret = GFARM_ERR_NO_MEMORY;
			gflog_tls_error(GFARM_MSG_1005595,
				"Can't duplicate a revoked CA certs "
				"directory name: %s",
				gfarm_error_string(ret));
			goto bailout;
		}
	} else {
		if (tmp != NULL) {
			gflog_tls_warning(GFARM_MSG_1005596,
				"Failed to check revoked certs directory "
				"%s: %s", tmp, gfarm_error_string(ret));
		}
	}

	/*
	 * Acceptable CA cert path (optional)
	 */
	tmp = str_or_NULL(gfarm_ctxp->tls_ca_peer_verify_chain_path);
	if (is_valid_string(tmp) == true &&
		((ret = is_valid_cert_store_dir(tmp)) == GFARM_ERR_NO_ERROR)) {
		acceptable_ca_path = strdup(tmp);
		if (unlikely(acceptable_ca_path == NULL)) {
			ret = GFARM_ERR_NO_MEMORY;
			gflog_tls_error(GFARM_MSG_1005597,
				"Can't duplicate an acceptable CA "
				"certs directory nmae: %s",
				gfarm_error_string(ret));
			goto bailout;
		}
	} else {
		if (tmp != NULL) {
			gflog_tls_warning(GFARM_MSG_1005598,
				"Failed to check peer certs verification "
				"directory %s: %s",
				tmp, gfarm_error_string(ret));
		}
	}

	/*
	 * Self certificate check
	 */
	if (do_mutual_auth == true || role == TLS_ROLE_SERVER) {
		need_self_cert = true;
	}
	if (need_self_cert == true) {
		char *tmp_cert_file =
			str_or_NULL(gfarm_ctxp->tls_certificate_file);
		char *tmp_cert_chain_file =
			str_or_NULL(gfarm_ctxp->tls_certificate_chain_file);
		char *tmp_prvkey_file =
			str_or_NULL(gfarm_ctxp->tls_key_file);

		tmp_proxy_cert_file =
			(use_proxy_cert == true && role == TLS_ROLE_CLIENT) ?
			has_proxy_cert() : NULL;

		/*
		 * cert/cert chain file (mandatory)
		 */
		if ((is_valid_string(tmp_cert_chain_file) == true) &&
			((ret = is_file_readable(-1, tmp_cert_chain_file))
			== GFARM_ERR_NO_ERROR)) {
			cert_chain_file = strdup(tmp_cert_chain_file);
			if (unlikely(cert_chain_file == NULL)) {
				ret = GFARM_ERR_NO_MEMORY;
				gflog_tls_warning(GFARM_MSG_1005599,
					"can't duplicate a cert chain "
					"filename: %s",
					gfarm_error_string(ret));
			}
		}
		if ((is_valid_string(tmp_cert_file) == true) &&
			((ret = is_file_readable(-1, tmp_cert_file))
			== GFARM_ERR_NO_ERROR)) {
			cert_file = strdup(tmp_cert_file);
			if (unlikely(cert_file == NULL)) {
				ret = GFARM_ERR_NO_MEMORY;
				gflog_tls_warning(GFARM_MSG_1005600,
					"Can't duplicate a cert filename: %s",
					gfarm_error_string(ret));
			}
		}
		if (unlikely(is_valid_string(cert_chain_file) == false &&
			is_valid_string(cert_file) == false &&
			is_valid_string(tmp_proxy_cert_file) == false)) {
			/*
			 * We still have a chance to go if we had a
			 * usable proxy cert.
			 */
			if (gflog_auth_get_verbose()) {
				gflog_tls_info(GFARM_MSG_1005601,
				    "None of a cert file, a cert chain file, "
				    "and a proxy cert file is specified.");
			}
			/* Don't overwrite return code ever set */
			if (ret == GFARM_ERR_UNKNOWN ||
				ret == GFARM_ERR_NO_ERROR) {
				ret = GFARM_ERR_INVALID_ARGUMENT;
			}
			goto bailout;
		}

		/*
		 * Private key (mandatory)
		 */
		if (likely(is_valid_string(tmp_prvkey_file) == true)) {
			prvkey_file = strdup(tmp_prvkey_file);
			if (unlikely(prvkey_file == NULL)) {
				ret = GFARM_ERR_NO_MEMORY;
				gflog_tls_error(GFARM_MSG_1005602,
					"Can't duplicate a private key "
					"filename: %s",
					gfarm_error_string(ret));
				goto bailout;
			}
		} else if (is_valid_string(tmp_proxy_cert_file) == false) {
			/*
			 * We still have a chance to go if we had a
			 * usable proxy cert.
			 */
			gflog_tls_error(GFARM_MSG_1005603,
				"A private key file is not specified.");
			ret = GFARM_ERR_INVALID_ARGUMENT;
			goto bailout;
		}

	}

	/*
	 * Ciphersuites (optional)
	 * Set only TLSv1.3 allowed ciphersuites
	 */
	ciphersuites =
		(tmp = str_or_NULL(gfarm_ctxp->tls_cipher_suite)) != NULL ?
		strdup(tmp) : NULL;

	/*
	 * Final parameter check
	 */
	if (role == TLS_ROLE_SERVER) {
		if (unlikely(is_valid_string(ca_path) != true ||
			(is_valid_string(cert_file) != true &&
			is_valid_string(cert_chain_file) != true) ||
			is_valid_string(prvkey_file) != true)) {
			gflog_tls_error(GFARM_MSG_1005604,
				"As a TLS server, at least a CA ptth, a cert "
				"file/cert chain file and a private key file "
				"must be presented.");
			goto bailout;
		}
	} else {
		if (do_mutual_auth == true) {
			if (is_valid_string(ca_path) == true &&
			    is_valid_string(tmp_proxy_cert_file) == true) {
				free(cert_file);
				free(prvkey_file);
				cert_file = strdup(tmp_proxy_cert_file);
				prvkey_file = strdup(tmp_proxy_cert_file);
				do_proxy_auth = true;
				goto runtime_init;
			} else if (likely(is_valid_string(ca_path) == true &&
				(is_valid_string(cert_file) == true ||
				is_valid_string(cert_chain_file) == true) &&
				is_valid_string(prvkey_file) == true)) {
				goto runtime_init;
			} else {
				gflog_tls_error(GFARM_MSG_1005605,
					"For TLS client auth, at least "
					"a CA ptth, a cert file/cert chain "
					"file and a private key file, or a "
					"CA path and GSI/GCT proxy cert "
					"must be presented.");
				goto bailout;
			}
		} else {
			if (unlikely(is_valid_string(ca_path) != true)) {
				gflog_tls_error(GFARM_MSG_1005606,
					"At least a CA path must be "
					"specified.");
				goto bailout;
			}
		}
	}

runtime_init:
	/*
	 * TLS runtime initialize
	 */
	if (unlikely((ret = tls_session_runtime_initialize())
		!= GFARM_ERR_NO_ERROR)) {
		gflog_tls_error(GFARM_MSG_1005607,
			"TLS runtime library initialization failed.");
		goto bailout;
	}

	if (need_self_cert == true) {
		/*
		 * Load a private key
		 */
		ret = tls_load_prvkey(prvkey_file, &prvkey);
		if (unlikely(ret != GFARM_ERR_NO_ERROR || prvkey == NULL)) {
			gflog_tls_error(GFARM_MSG_1005608,
				"Can't load a private key file \"%s\".",
				prvkey_file);
			goto bailout;
		}
	}

	/*
	 * Create a SSL_CTX
	 */
	tls_runtime_flush_error();
	if (role == TLS_ROLE_SERVER) {
		ssl_ctx = SSL_CTX_new(TLS_server_method());
	} else if (role == TLS_ROLE_CLIENT) {
		ssl_ctx = SSL_CTX_new(TLS_client_method());
	}
	if (likely(ssl_ctx != NULL)) {
		int osst;
		X509_VERIFY_PARAM *tmpvpm = NULL;

		if (gfarm_ctxp->tls_security_level !=
		    GFARM_CONFIG_MISC_DEFAULT) {
			SSL_CTX_set_security_level(ssl_ctx,
			    gfarm_ctxp->tls_security_level);
		}

		/*
		 * Clear cert chain for our sanity.
		 */
		(void)SSL_CTX_clear_chain_certs(ssl_ctx);

		/*
		 * Inhibit other than TLSv1.3
		 */
		tls_runtime_flush_error();
		if (unlikely((osst = SSL_CTX_set_min_proto_version(ssl_ctx,
					TLS1_3_VERSION)) != 1 ||
			     (osst = SSL_CTX_set_max_proto_version(ssl_ctx,
					TLS1_3_VERSION)) != 1)) {
			gflog_tls_error(GFARM_MSG_1005609,
					"Failed to set an SSL_CTX "
					"only using TLSv1.3.");
			ret = GFARM_ERR_TLS_RUNTIME_ERROR;
			goto bailout;
		} else {
			if ((osst = SSL_CTX_get_min_proto_version(ssl_ctx)) !=
				TLS1_3_VERSION ||
				(osst = SSL_CTX_get_max_proto_version(
						ssl_ctx)) != TLS1_3_VERSION) {
				gflog_tls_error(GFARM_MSG_1005610,
					"Failed to check if the SSL_CTX "
					"only using TLSv1.3.");
				ret = GFARM_ERR_TLS_RUNTIME_ERROR;
				goto bailout;
			}
		}

#define VERIFY_DEPTH	50
		/*
		 * XXX FIXME:
		 *	50 is too much?
		 */
		if (role == TLS_ROLE_SERVER) {
			if (do_mutual_auth == true) {
				SSL_CTX_set_verify_depth(ssl_ctx,
					VERIFY_DEPTH);
#define SERVER_MUTUAL_VERIFY_FLAGS			     \
	(SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT | \
	 SSL_VERIFY_CLIENT_ONCE)
				SSL_CTX_set_verify(ssl_ctx,
					SERVER_MUTUAL_VERIFY_FLAGS,
					tls_verify_callback);
#undef SERVER_MUTUAL_VERIFY_FLAGS
			} else {
				SSL_CTX_set_verify(ssl_ctx,
					SSL_VERIFY_NONE, NULL);
			}
		} else {
			SSL_CTX_set_verify_depth(ssl_ctx,
				VERIFY_DEPTH);
#define CLIENT_VERIFY_FLAGS					\
	(SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT)
			SSL_CTX_set_verify(ssl_ctx, CLIENT_VERIFY_FLAGS,
				tls_verify_callback);
#undef CLIENT_VERIFY_FLAGS
			if (do_mutual_auth == true) {
				SSL_CTX_set_post_handshake_auth(ssl_ctx, 1);
			}
		}
#undef VERIFY_DEPTH

		/*
		 * Set ciphersuites
		 */
		if (is_valid_string(ciphersuites) == true) {
			tls_runtime_flush_error();
			if (unlikely(SSL_CTX_set_ciphersuites(ssl_ctx,
						ciphersuites) != 1)) {
				gflog_tls_error(GFARM_MSG_1005611,
					"Failed to set ciphersuites "
					"\"%s\" to the SSL_CTX.",
					ciphersuites);
				ret = GFARM_ERR_TLS_RUNTIME_ERROR;
				goto bailout;
			}
		}

		/*
		 * Set CA path
		 */
		ret = tls_set_ca_path(ssl_ctx, ca_path, acceptable_ca_path,
				&trust_ca_list);
		if (likely(ret == GFARM_ERR_NO_ERROR)) {
			if (is_valid_string(acceptable_ca_path) == true) {
				if (trust_ca_list != NULL &&
					sk_X509_NAME_num(trust_ca_list) > 0) {
					if (sk_X509_NAME_is_sorted(
						    trust_ca_list) != 1) {
						sk_X509_NAME_sort(
							trust_ca_list);
					}
				} else if (sk_X509_NAME_num(
						   trust_ca_list) <= 0) {
					gflog_tls_warning(GFARM_MSG_1005612,
						"No cert is collected "
						"in %s for peer chain "
						"verifiation.",
						acceptable_ca_path);
					if (trust_ca_list != NULL) {
						sk_X509_NAME_pop_free(
							trust_ca_list,
							X509_NAME_free);
					}
					trust_ca_list = NULL;
				}
			}
		} else {
			goto bailout;
		}

		if (need_self_cert == true) {
			/*
			 * Load a cert/cert chain into the SSL_CTX
			 */
			int n_certs = 0;
			ret = tls_load_cert_and_chain(
				ssl_ctx, cert_file, cert_chain_file, &n_certs);
			if (unlikely(ret != GFARM_ERR_NO_ERROR)) {
				if (is_valid_string(cert_file) == true &&
					is_valid_string(cert_chain_file) ==
					true) {
					gflog_tls_error(GFARM_MSG_1005613,
						"Can't load both %s and %s: "
						"%s.",
						cert_file, cert_chain_file,
						gfarm_error_string(ret));
				} else if (is_valid_string(cert_file) ==
						true) {
					gflog_tls_error(GFARM_MSG_1005614,
						"Can't load %s: %s.",
						cert_file,
						gfarm_error_string(ret));
				} else if (is_valid_string(cert_chain_file) ==
						true) {
					gflog_tls_error(GFARM_MSG_1005615,
						"Can't load %s: %s.",
						cert_chain_file,
						gfarm_error_string(ret));
				}
				goto bailout;
			} else if (unlikely(n_certs == 0)) {
				gflog_tls_error(GFARM_MSG_1005616,
					"No cert is load both %s and %s: %s.",
					cert_file, cert_chain_file,
					gfarm_error_string(ret));
				goto bailout;
			}

			/*
			 * Set a private key into the SSL_CTX
			 */
			tls_runtime_flush_error();
			osst = SSL_CTX_use_PrivateKey(ssl_ctx, prvkey);
			if (unlikely(osst != 1)) {
				gflog_tls_error(GFARM_MSG_1005617,
					"Can't set a private key to a "
					"SSL_CTX.");
				ret = GFARM_ERR_INVALID_CREDENTIAL;
				goto bailout;
			}

			/*
			 * Then check prvkey in SSL_CTX by
			 * SSL_CTX_check_private_key
			 */
			tls_runtime_flush_error();
			osst = SSL_CTX_check_private_key(ssl_ctx);
			if (unlikely(osst != 1)) {
				gflog_tls_error(GFARM_MSG_1005618,
					"Wrong private key file for the "
					"current certificate.");
				ret = GFARM_ERR_TLS_RUNTIME_ERROR;
				goto bailout;
			}

			/* no domain check */
			do_build_chain = gfarm_ctxp->tls_build_chain_local;
			if (do_build_chain == true) {
				/*
				 * Build a complete cert chain locally.
				 */
				/*
				 * Don't set
				 * SSL_BUILD_CHAIN_FLAG_NO_ROOT or
				 * build chain always fail.
				 *
				 * Neither SSL_BUILD_CHAIN_FLAG_CHECK
				 * shouldn't be set since the
				 * SSL_CTX_build_cert_chain() seems
				 * try to use all the existing chain
				 * cert file under CA path for build
				 * ths local chain.
				 *
				 * Conlusion: only zero is suitable
				 * for our usage.
				 */
				tls_runtime_flush_error();
				osst = SSL_CTX_build_cert_chain(ssl_ctx, 0);
				if (unlikely(osst != 1)) {
					gflog_tls_error(GFARM_MSG_1005619,
						"Can't build a certificate "
						"chain.");
					ret = GFARM_ERR_TLS_RUNTIME_ERROR;
					goto bailout;
				}
			}
		}

		/*
		 * Set revocation path
		 */
		/*
		 * NOTE: XXX FIXME:
		 *
		 *	Setup the revoke thingies AFTER building cert
		 *	chain since setting a revocation path not
		 *	containing any CLRs (e.g. cert chain path)
		 *	makes invalidates all the certs. It's too
		 *	annoying to check all the CLRs under
		 *	directories...
		 */
		if (is_valid_string(revoke_path) == true) {
			ret = tls_set_revoke_path(ssl_ctx,
				revoke_path);
			if (unlikely(ret != GFARM_ERR_NO_ERROR)) {
				goto bailout;
			}
		}

		/*
		 * Final verify param tweaks
		 */
		tmpvpm = SSL_CTX_get0_param(ssl_ctx);

		if (likely(tmpvpm != NULL)) {
			unsigned long flags = 0;

			/*
			 * Seems revoked certs in
			 * tls_ca_certificate_path should be
			 * rejected. openssl s_{client|server} add
			 * following flags.
			 */
			flags |= (X509_V_FLAG_CRL_CHECK |
					X509_V_FLAG_CRL_CHECK_ALL);

			/*
			 * Allow RFC 3820 proxy cert authentication
			 */
			if (role == TLS_ROLE_SERVER &&
				do_mutual_auth == true &&
				use_proxy_cert == true) {
				flags |= X509_V_FLAG_ALLOW_PROXY_CERTS;
			}

			tls_runtime_flush_error();
			osst = X509_VERIFY_PARAM_set_flags(tmpvpm, flags);
			if (unlikely(osst != 1)) {
				gflog_tls_error(GFARM_MSG_1005620,
					"Failed to set CRL check, etc. flags "
					"to a X509_VERIFY_PARAM");
				ret = GFARM_ERR_TLS_RUNTIME_ERROR;
				goto bailout;
			}
		}

		if (need_self_cert) {
			ret = tls_verify_self_certificate(ssl_ctx);
			if (ret != GFARM_ERR_NO_ERROR)
				goto bailout;
		}

	} else {
		gflog_tls_error(GFARM_MSG_1005621,
			"Failed to create a SSL_CTX.");
		ret = GFARM_ERR_TLS_RUNTIME_ERROR;
		goto bailout;
	}

	/*
	 * Create a new tls_session_ctx_struct
	 */
	ctxret = (struct tls_session_ctx_struct *)malloc(
			sizeof(struct tls_session_ctx_struct));
	if (likely(ctxret != NULL)) {
		tls_runtime_flush_error();

		(void)memset(ctxret, 0,
			sizeof(struct tls_session_ctx_struct));
		tls_session_clear_ctx(ctxret, CTX_CLEAR_READY_FOR_ESTABLISH);

		ctxret->role_ = role;
		ctxret->do_mutual_auth_ = do_mutual_auth;
		if (gfarm_ctxp->tls_key_update > 0) {
			ctxret->io_key_update_thresh_ = TLS_KEY_UPDATE_THRESH;
		} else {
			ctxret->io_key_update_thresh_ = 0;
		}
		ctxret->prvkey_ = prvkey;
		ctxret->ssl_ctx_ = ssl_ctx;
		/* no domain check */
		ctxret->do_build_chain_ = do_build_chain;
		ctxret->do_allow_no_crls_ = gfarm_ctxp->tls_allow_no_crl;
		ctxret->do_allow_proxy_cert_ = (role == TLS_ROLE_SERVER) ?
			use_proxy_cert : do_proxy_auth;
		ctxret->cert_file_ = cert_file;
		ctxret->cert_chain_file_ = cert_chain_file;
		ctxret->prvkey_file_ = prvkey_file;
		ctxret->ciphersuites_ = ciphersuites;
		ctxret->ca_path_ = ca_path;
		ctxret->acceptable_ca_path_ = acceptable_ca_path;
		ctxret->revoke_path_ = revoke_path;
		ctxret->trusted_certs_ = trust_ca_list;

		/*
		 * All done.
		 */
		*ctxptr = ctxret;
		ret = GFARM_ERR_NO_ERROR;
		goto ok;
	} else {
		gflog_tls_error(GFARM_MSG_1005622,
			"Can't allocate a TLS session context.");
		ret = GFARM_ERR_NO_MEMORY;
	}

bailout:
	free(cert_file);
	free(cert_chain_file);
	free(prvkey_file);
	free(ciphersuites);
	free(ca_path);
	free(acceptable_ca_path);
	free(revoke_path);

	/*
	 * not forget to release trusted certs if it is used.
	 */

	if (prvkey != NULL) {
		EVP_PKEY_free(prvkey);
	}
	if (ssl_ctx != NULL) {
		(void)SSL_CTX_clear_chain_certs(ssl_ctx);
		SSL_CTX_free(ssl_ctx);
	}
	free(ctxret);

ok:
	free(tmp_proxy_cert_file);

	return (ret);

#undef str_or_NULL
}

/*
 * Destructor
 */
static inline void
tls_session_destroy_ctx(struct tls_session_ctx_struct *ctx)
{
	tls_session_clear_ctx(ctx, CTX_CLEAR_FREEUP);
}

/*
 * TLS I/O operations
 */

/*
 * SSL_ERROR_* handler
 */
static inline bool
tls_session_io_continuable(int sslerr, struct tls_session_ctx_struct *ctx,
	bool in_handshake, const char *diag)
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

	case SSL_ERROR_NONE:
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
		ctx->is_got_fatal_ssl_error_ = true;
		break;

	case SSL_ERROR_SSL:
		/*
		 * TLS runtime error
		 */
		if (BIO_eof(SSL_get_rbio(ctx->ssl_))) {
			ctx->last_gfarm_error_ =
			    GFARM_ERR_UNEXPECTED_EOF;
			gflog_tls_info(GFARM_MSG_1005652,
			    "TLS EOF during %s", diag);
		} else  {
			ctx->last_gfarm_error_ =
			    GFARM_ERR_TLS_RUNTIME_ERROR;
			gflog_tls_error(GFARM_MSG_1005623,
			    "TLS error during %s", diag);
		}
		ctx->is_got_fatal_ssl_error_ = true;
		break;

	case SSL_ERROR_ZERO_RETURN:
		/*
		 * Peer sent close_notify. Not retryable.
		 */
		ctx->last_gfarm_error_ = GFARM_ERR_PROTOCOL;
		break;

	case SSL_ERROR_WANT_X509_LOOKUP:
	case SSL_ERROR_WANT_CLIENT_HELLO_CB:
	case SSL_ERROR_WANT_CONNECT:
	case SSL_ERROR_WANT_ACCEPT:
		if (likely(in_handshake == false)) {
			/*
			 * MUST not occured, connect/accept must be
			 * done BEFORE gfp_* thingies call this
			 * function.
			 */
			gflog_tls_error(GFARM_MSG_1005624,
				    "The TLS handshake must be done before "
				    "begining data I/O in Gfarm.");
			ctx->last_gfarm_error_ = GFARM_ERR_INTERNAL_ERROR;
		} else {
			ctx->last_gfarm_error_ = GFARM_ERR_NO_ERROR;
			ret = true;
		}
		break;

	default:
		gflog_tls_error(GFARM_MSG_1005625,
			"All the TLS I/O error must be handled, but got "
			"TLS I/O error %d.", sslerr);
		ctx->last_gfarm_error_ = GFARM_ERR_INTERNAL_ERROR;
		break;
	}

	return (ret);
}

/*
 * TLS session read/write timeout checker
 */
static inline gfarm_error_t
tls_session_wait_io(struct tls_session_ctx_struct *ctx,
	int fd, int tous, bool to_read)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;
	char *method = NULL;

	if (gflog_auth_get_verbose()) {
		gflog_tls_debug(GFARM_MSG_1005626, "%s(): wait enter.",
			__func__);
	}

	if (to_read && SSL_has_pending(ctx->ssl_)) {
		method = "SSL_has_pending";
		ret = GFARM_ERR_NO_ERROR;
	} else {
		int st;
		bool loop = true;

#ifdef HAVE_POLL
		struct pollfd fds[1];
		int tos_save = (tous >= 0) ? tous / 1000 : -1;
		int tos;

		method = "poll";

		while (loop == true) {
			fds[0].fd = fd;
			fds[0].events = to_read ? POLLIN : POLLOUT;
			tos = tos_save;

			st = poll(fds, 1, tos);
#else
		fd_set fds;
		struct timeval tv_save;
		struct timeval *tvp = MULL;
		if (tous >= 0) {
			tv_save.tv_usec = tous % (1000 * 1000);
			tv_save.tv_sec = tous / (1000 * 1000);
		}

		method = "select";

		while (loop == true) {
			FD_ZERO(&fds);
			FD_SET(fd, &fds);
			tv = rv_save;
			tvp = &tv;

			if (to_read)
				st = select(fd + 1, &fds, NULL, NULL, tvp);
			else
				st = select(fd + 1, NULL, &fds, NULL, tvp);
#endif /* HAVE_POLL */

			switch (st) {
			case 0:
				ret = GFARM_ERR_OPERATION_TIMED_OUT;
				loop = false;
				break;

			case -1:
				if (errno != EINTR) {
					ret = gfarm_errno_to_error(errno);
					loop = false;
				}
				break;

			default:
				ret = GFARM_ERR_NO_ERROR;
				loop = false;
				break;
			}
		}
	}

	ctx->last_gfarm_error_ = ret;

	if (gflog_auth_get_verbose()) {
		gflog_tls_debug(GFARM_MSG_1005627, "%s(): wait (%s) end : %s",
			__func__, method, gfarm_error_string(ret));
	}

	return (ret);
}

static inline gfarm_error_t
tls_session_wait_readable(struct tls_session_ctx_struct *ctx, int fd, int tous)
{
	return (tls_session_wait_io(ctx, fd, tous, true));
}

static inline gfarm_error_t
tls_session_wait_writable(struct tls_session_ctx_struct *ctx, int fd, int tous)

{
	return (tls_session_wait_io(ctx, fd, tous, false));
}

static inline gfarm_error_t
tls_session_get_pending_read_bytes_n(struct tls_session_ctx_struct *ctx,
	int *nptr)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;
	SSL *ssl = NULL;

	if (likely(ctx != NULL && (ssl = ctx->ssl_) != NULL && nptr != NULL)) {
		*nptr = SSL_pending(ssl);
		ret = GFARM_ERR_NO_ERROR;
	} else {
		if (nptr != NULL) {
			*nptr = 0;
		}
		ret = GFARM_ERR_INVALID_ARGUMENT;
	}

	return (ret);
}

/*
 * Session establish
 */
static inline gfarm_error_t
tls_session_verify(struct tls_session_ctx_struct *ctx, bool *is_verified)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;
	SSL *ssl = NULL;
	X509 *p = NULL;
	X509_NAME *pn = NULL;

	if (is_verified != NULL) {
		*is_verified = false;
	}

	if (likely(ctx != NULL && (ssl = ctx->ssl_) != NULL &&
		(ctx->role_ == TLS_ROLE_CLIENT ||
		ctx->do_mutual_auth_ == true))) {
		/*
		 * No matter verified or not, get a peer cert.
		 */
		ctx->peer_dn_oneline_ = NULL;
		ctx->peer_dn_rfc2253_ = NULL;
		ctx->peer_dn_gsi_ = NULL;
		ctx->peer_cn_ = NULL;
		tls_runtime_flush_error();

		if (likely(((ctx->do_allow_proxy_cert_ == true &&
			ctx->is_got_proxy_cert_ == true &&
			(pn = ctx->proxy_issuer_) != NULL)) ||
			(((p = SSL_get_peer_certificate(ssl)) != NULL) &&
			((pn = X509_get_subject_name(p)) != NULL)))) {
			char *dn_oneline = NULL;
			char *dn_rfc2253 = NULL;
			char *dn_gsi = NULL;
			char *cn = NULL;
			bool v = false;
			int vres = -INT_MAX;

			X509_free(p);
			p = NULL;
#define DN_FORMAT_ONELINE	(XN_FLAG_ONELINE & ~ASN1_STRFLGS_ESC_MSB)
#define DN_FORMAT_RFC2253	(XN_FLAG_RFC2253 & ~ASN1_STRFLGS_ESC_MSB)
			if (likely((ret = get_peer_dn(pn,
						DN_FORMAT_ONELINE,
						&dn_oneline, 0)) ==
				GFARM_ERR_NO_ERROR)) {
				ctx->peer_dn_oneline_ = dn_oneline;
			}
			if (likely((ret = get_peer_dn(pn,
						DN_FORMAT_RFC2253,
						&dn_rfc2253, 0)) ==
				GFARM_ERR_NO_ERROR)) {
				ctx->peer_dn_rfc2253_ = dn_rfc2253;
			}
			if (likely((ret = get_peer_dn_gsi_ish(pn,
						&dn_gsi, 0)) ==
				GFARM_ERR_NO_ERROR)) {
				ctx->peer_dn_gsi_ = dn_gsi;
			}
			if (likely((ret = get_peer_cn(pn, &cn, 0,
						ctx->do_allow_proxy_cert_))
				   == GFARM_ERR_NO_ERROR)) {
				ctx->peer_cn_ = cn;
			}
#undef DN_FORMAT_ONELINE
#undef DN_FORMAT_RFC2253
			if (unlikely(ret != GFARM_ERR_NO_ERROR)) {
				goto done;
			}

			tls_runtime_flush_error();
			vres = SSL_get_verify_result(ssl);
			if (vres == X509_V_OK) {
				v = true;
			} else {
				v = false;
				gflog_tls_notice(GFARM_MSG_1005628,
					"Certificate verification failed: %s",
					X509_verify_cert_error_string(vres));
				ret = ctx->last_gfarm_error_ =
					GFARM_ERR_AUTHENTICATION;
			}
			ctx->cert_verify_result_error_ = vres;
			ctx->is_verified_ = v;
		} else {
			ret = GFARM_ERR_TLS_RUNTIME_ERROR;
			gflog_tls_notice(GFARM_MSG_1005629,
				"Failed to acquire peer certificate.");
		}
	} else {
		if (ctx == NULL || ssl == NULL) {
			ret = GFARM_ERR_INVALID_ARGUMENT;
		} else {
			/* not mutual auth */
			ctx->peer_dn_oneline_ = NULL;
			ctx->peer_dn_rfc2253_ = NULL;
			ctx->peer_dn_gsi_ = NULL;
			ctx->peer_cn_ = NULL;
			ctx->cert_verify_result_error_ = X509_V_OK;
			ctx->is_verified_ = true;
			ret = GFARM_ERR_NO_ERROR;
		}
	}

done:
	if (ctx != NULL) {
		ctx->last_gfarm_error_ = ret;
		if (is_verified != NULL) {
			*is_verified = ctx->is_verified_;
		}
	}

	return (ret);
}

static inline gfarm_error_t
tls_session_establish(struct tls_session_ctx_struct *ctx, int fd,
	struct gfp_xdr *conn, gfarm_error_t prior_error)
{
	gfarm_error_t e, ret = GFARM_ERR_UNKNOWN;
	struct sockaddr sa;
	socklen_t salen = sizeof(sa);
	int eof, pst = -1;
	bool negotiation_sent = false, negotiation_received = false;
	gfarm_int32_t negotiation_error;
	typedef int (*tls_handshake_proc_t)(SSL *ssl);
	tls_handshake_proc_t p = NULL;
	SSL *ssl = NULL;

	errno = 0;
	if (unlikely(prior_error != GFARM_ERR_NO_ERROR)) {
		ret = prior_error;
	} else if (likely(fd >= 0 &&
		(pst = getpeername(fd, &sa, &salen)) == 0 &&
		ctx != NULL)) {

		/*
		 * Create an SSL
		 */
		ret = tls_session_setup_ssl(ctx);
		if (unlikely(ret != GFARM_ERR_NO_ERROR || ctx->ssl_ == NULL)) {
			if (ret == GFARM_ERR_NO_ERROR)
				ret = GFARM_ERR_INTERNAL_ERROR;
			goto bailout;
		}
		ssl = ctx->ssl_;

		tls_runtime_flush_error();
		if (likely(SSL_set_fd(ssl, fd) == 1)) {
			int st;
			int ssl_err;
			bool do_cont = false;

			e = gfp_xdr_send(conn, "i",
			    (gfarm_int32_t)GFARM_ERR_NO_ERROR);
			if (e == GFARM_ERR_NO_ERROR)
				e = gfp_xdr_flush(conn);
			negotiation_sent = true;
			if (e == GFARM_ERR_NO_ERROR) {
				e = gfp_xdr_recv(conn, 1, &eof, "i",
				    &negotiation_error);
				negotiation_received = true;
			}
			if (e != GFARM_ERR_NO_ERROR) {
				ret = e;
				goto bailout;
			}
			if (eof) {
				ret = GFARM_ERR_UNEXPECTED_EOF;
				goto bailout;
			}
			if (negotiation_error != GFARM_ERR_NO_ERROR) {
				ret = negotiation_error;
				goto bailout;
			}

			ctx->is_handshake_tried_ = true;
			p = (ctx->role_ == TLS_ROLE_SERVER) ?
				SSL_accept : SSL_connect;

retry:
			errno = 0;
			tls_runtime_flush_error();
			st = p(ssl);
			ssl_err = SSL_get_error(ssl, st);
			do_cont = tls_session_io_continuable(
					ssl_err, ctx, false, "SSL handshake");
			if (likely(st == 1 && ssl_err == SSL_ERROR_NONE)) {
				ret = ctx->last_gfarm_error_ =
					GFARM_ERR_NO_ERROR;
			} else if (st == 0 && do_cont == true) {
				goto retry;
			} else {
				/*
				 * st < 0 but SSL_ERROR_NONE ???
				 */
				if (ctx->last_gfarm_error_ ==
					GFARM_ERR_NO_ERROR) {
					ret = ctx->last_gfarm_error_ =
						GFARM_ERR_TLS_RUNTIME_ERROR;
				} else {
					ret = ctx->last_gfarm_error_;
				}
				gflog_tls_notice(GFARM_MSG_1005630,
					"SSL handshake failed: %s",
					gfarm_error_string(ret));
			}
		} else {
			gflog_tls_error(GFARM_MSG_1005631,
				"Failed to set a file "
				"descriptor %d to an SSL.", fd);
			ret = GFARM_ERR_TLS_RUNTIME_ERROR;
		}
	} else {
		if (pst != 0 && errno != 0) {
			ret = gfarm_errno_to_error(errno);
			if (errno == ENOTCONN) {
				gflog_tls_notice(GFARM_MSG_1005632,
					"The file descriptor %d is not yet "
					"connected: %s",
					fd, gfarm_error_string(ret));
			} else if (errno == ENOTSOCK) {
				gflog_tls_error(GFARM_MSG_1005633,
					"The file descriptor %d is not a "
					"socket: %s",
					fd, gfarm_error_string(ret));
			} else {
				gflog_tls_notice(GFARM_MSG_1005634,
					"Failed to check connection status of "
					"the file descriptor %d: %s",
					fd, gfarm_error_string(ret));
			}
		} else { /* fd < 0 || ctx == NULL */
			ret = GFARM_ERR_INTERNAL_ERROR; /* shouldn't happen */
			gflog_tls_error(GFARM_MSG_1005635,
				"The tls context is not initialized.");
		}
	}

	if (ret == GFARM_ERR_NO_ERROR && ctx != NULL) {
		bool is_verified = false;
		ret = tls_session_verify(ctx, &is_verified);
		if (is_verified == false) {
			ret = ctx->last_gfarm_error_ =
				GFARM_ERR_AUTHENTICATION;
			if (is_valid_string(ctx->peer_dn_oneline_) == true) {
				gflog_tls_notice(GFARM_MSG_1005636,
					"Authentication failed between peer: "
					"'%s' with %s.",
					ctx->peer_cn_,
					(ctx->is_got_proxy_cert_ == true) ?
						"proxy certificate" :
						"end-entity certificate");
			} else {
				gflog_tls_notice(GFARM_MSG_1005637,
					"Authentication failed "
					"(no cert acquired.)");
			}
		}

		if (is_valid_string(ctx->peer_dn_oneline_) == true &&
			gflog_auth_get_verbose()) {
			gflog_tls_debug(GFARM_MSG_1005638,
				"Authentication between \"%s\" %s and a "
				"TLS session %s with %s.",
				ctx->peer_dn_gsi_,
				(is_verified == true) ?
					"verified" : "not verified",
				(is_verified == true) ?
					"established" : "not established",
				(ctx->is_got_proxy_cert_ == true) ?
					"proxy certificate" :
					"end-entity certificate");
			gflog_tls_debug(GFARM_MSG_1005639,
				"peer CN \"%s\"", tls_session_peer_cn(ctx));
		}
	}

bailout:
	if (ret != GFARM_ERR_NO_ERROR) {
		/*
		 * For example, if there is a problem in a certificate,
		 * ret == GFARM_ERR_INVALID_ARGUMENT, but that does not match
		 * GFARM_AUTH_ERR_TRY_NEXT_METHOD().
		 * By setting ret to GFARM_ERR_TLS_RUNTIME_ERROR,
		 * we will let it try the next authentication method.
		 */
		if (!IS_CONNECTION_ERROR(ret) &&
		    !GFARM_AUTH_ERR_TRY_NEXT_METHOD(ret))
			ret = GFARM_ERR_TLS_RUNTIME_ERROR;

		/* to make negotiation graceful */
		if (!negotiation_sent) {
			e = gfp_xdr_send(conn, "i", ret);
			if (e == GFARM_ERR_NO_ERROR)
				e = gfp_xdr_flush(conn);
			negotiation_sent = true;
		}
		if (!negotiation_received) {
			e = gfp_xdr_recv(conn, 1, &eof, "i",
			    &negotiation_error);
			negotiation_received = true;
		}
	}

	return (ret);
}

/*
 * TLS 1.3 key update
 */
static inline gfarm_error_t
tls_session_update_key(struct tls_session_ctx_struct *ctx, int delta)
{
	/*
	 * Only clients initiate KeyUpdate.
	 */
	gfarm_error_t ret = GFARM_ERR_NO_ERROR;
	SSL *ssl;

	if (likely(ctx != NULL && (ssl = ctx->ssl_) != NULL &&
		ctx->role_ == TLS_ROLE_CLIENT &&
		ctx->io_key_update_thresh_ > 0 &&
		ctx->is_got_fatal_ssl_error_ == false &&
		((ctx->io_key_update_accum_ += delta) >=
		ctx->io_key_update_thresh_))) {
		if (likely(SSL_key_update(ssl,
				SSL_KEY_UPDATE_REQUESTED) == 1)) {
			ret = ctx->last_gfarm_error_ = GFARM_ERR_NO_ERROR;
			if (gflog_auth_get_verbose()) {
				gflog_tls_debug(GFARM_MSG_1005640,
					"TLS shared key updated after "
					" %zu bytes I/O.",
					ctx->io_key_update_accum_);
			}
		} else {
			/*
			 * XXX FIXME:
			 *	OpenSSL 1.1.1 manual doesn't refer
			 *	what to do when SSL_key_update()
			 *	failure.
			 */
			gflog_tls_warning(GFARM_MSG_1005641,
				"SSL_update_key() failed but we don't know "
				"how to deal with it.");
			ret = ctx->last_gfarm_error_ =
				GFARM_ERR_INTERNAL_ERROR;
		}
		ctx->io_key_update_accum_ = 0;
	} else {
		ret = ctx->last_gfarm_error_;
	}

	return (ret);
}

/*
 * TLS session read(2)'ish
 */
static inline gfarm_error_t
tls_session_read(struct tls_session_ctx_struct *ctx, void *buf, int len,
	int *actual_io_bytes)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;
	SSL *ssl = NULL;

	if (likely(ctx != NULL && (ssl = ctx->ssl_) != NULL && buf != NULL &&
			len > 0 && actual_io_bytes != NULL &&
			ctx->is_verified_ == true &&
			ctx->is_got_fatal_ssl_error_ == false)) {
		int n = 0;
		int ssl_err;
		bool continuable;

		if (gflog_auth_get_verbose()) {
			gflog_tls_debug(GFARM_MSG_1005642,
				"%s(%s): about to read %d (remains %d)",
				__func__, ctx->peer_cn_, len,
				SSL_pending(ssl));
		}

		if (unlikely(len == 0)) {
			ret = ctx->last_gfarm_error_ = GFARM_ERR_NO_ERROR;
			goto done;
		}

		*actual_io_bytes = 0;

retry:
		if (gflog_auth_get_verbose()) {
			gflog_tls_debug(GFARM_MSG_1005643,
				"%s(%s): read %d/%d", __func__,
				ctx->peer_cn_, n, len);
		}

		errno = 0;
		n = SSL_read(ssl, buf, len);
		/*
		 * NOTE:
		 *	To avoid sending key update request on broken
		 *	TLS stream, check SSL_ERROR_ for the session
		 *	continuity.
		 */
		ssl_err = SSL_get_error(ssl, n);
		continuable = tls_session_io_continuable(
			ssl_err, ctx, false, "SSL_read");
		if (likely(n > 0 && ssl_err == SSL_ERROR_NONE)) {
			ctx->last_gfarm_error_ = GFARM_ERR_NO_ERROR;
			ctx->last_ssl_error_ = ssl_err;
			*actual_io_bytes = n;
			ctx->io_total_ += n;
			ret = tls_session_update_key(ctx, n);
		} else {
			if (likely(continuable == true)) {
				goto retry;
			} else {
				ret = ctx->last_gfarm_error_;
			}
		}

		if (gflog_auth_get_verbose()) {
			gflog_tls_debug(GFARM_MSG_1005644,
				"%s(%s): read done %d (remains %d) : %s",
				__func__, ctx->peer_cn_, n, SSL_pending(ssl),
				gfarm_error_string(ret));
		}

	} else {
		ret = ctx->last_gfarm_error_ = GFARM_ERR_UNEXPECTED_EOF;
	}

done:
	return (ret);
}

/*
 * TLS session write(2)'ish
 */
static inline gfarm_error_t
tls_session_write(struct tls_session_ctx_struct *ctx, const void *buf, int len,
	int *actual_io_bytes)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;
	SSL *ssl = NULL;

	if (likely(ctx != NULL && (ssl = ctx->ssl_) != NULL && buf != NULL &&
			len > 0 && actual_io_bytes != NULL &&
			ctx->is_verified_ == true &&
			ctx->is_got_fatal_ssl_error_ == false)) {
		int n = 0;
		int ssl_err;
		bool continuable;

		if (gflog_auth_get_verbose()) {
			gflog_tls_debug(GFARM_MSG_1005645,
				"%s(%s): about to write %d", __func__,
				ctx->peer_cn_, len);
		}

		if (unlikely(len == 0)) {
			ret = ctx->last_gfarm_error_ = GFARM_ERR_NO_ERROR;
			goto done;
		}

		*actual_io_bytes = 0;

retry:
		if (gflog_auth_get_verbose()) {
			gflog_tls_debug(GFARM_MSG_1005646,
				"%s(%s): write %d/%d", __func__,
				ctx->peer_cn_, n, len);
		}

		errno = 0;
		n = SSL_write(ssl, buf, len);
		/*
		 * NOTE:
		 *	To avoid sending key update request on broken
		 *	TLS stream, check SSL_ERROR_ for the session
		 *	continuity.
		 */
		ssl_err = SSL_get_error(ssl, n);
		continuable = tls_session_io_continuable(
			ssl_err, ctx, false, "SSL_write");
		if (likely(n > 0 && ssl_err == SSL_ERROR_NONE)) {
			ctx->last_gfarm_error_ = GFARM_ERR_NO_ERROR;
			ctx->last_ssl_error_ = ssl_err;
			*actual_io_bytes = n;
			ctx->io_total_ += n;
			ret = tls_session_update_key(ctx, n);
		} else {
			if (likely(continuable == true)) {
				goto retry;
			} else {
				ret = ctx->last_gfarm_error_;
			}
		}

		if (gflog_auth_get_verbose()) {
			gflog_tls_debug(GFARM_MSG_1005647,
				"%s(%s): write done %d : %s", __func__,
				ctx->peer_cn_, n, gfarm_error_string(ret));
		}

	} else {
		ret = ctx->last_gfarm_error_ = GFARM_ERR_UNEXPECTED_EOF;
	}

done:
	return (ret);
}

/*
 * tls session io with timeout (includes "forever")
 */
static inline gfarm_error_t
tls_session_timeout_read(struct tls_session_ctx_struct *ctx,
	int fd, void *buf, int len, int timeout, int *actual_read)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;

	ret = tls_session_wait_readable(ctx, fd, timeout);
	if (likely(ret == GFARM_ERR_NO_ERROR)) {
		ret = tls_session_read(ctx, buf, len, actual_read);
	}

	return (ret);
}

static inline gfarm_error_t
tls_session_timeout_write(struct tls_session_ctx_struct *ctx,
	int fd, const void *buf, int len, int timeout, int *actual_io_bytes)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;

	ret = tls_session_wait_writable(ctx, fd, timeout);
	if (likely(ret == GFARM_ERR_NO_ERROR)) {
		ret = tls_session_write(ctx, buf, len, actual_io_bytes);
	}

	return (ret);
}

/*
 * TLS session shutdown
 */
static inline gfarm_error_t
tls_session_shutdown(struct tls_session_ctx_struct *ctx)
{
	gfarm_error_t ret;
	SSL *ssl;

	if (unlikely(ctx == NULL))
		return (GFARM_ERR_NO_ERROR);

	if (unlikely((ssl = ctx->ssl_) == NULL)) {
		ctx->last_gfarm_error_ = GFARM_ERR_UNKNOWN;
		return (GFARM_ERR_UNKNOWN);
	}

	if (gflog_auth_get_verbose()) {
		gflog_tls_debug(GFARM_MSG_1005648,
			"%s(%s): about to shutdown SSL.",
			__func__, ctx->peer_cn_);
	}

	if (!ctx->is_handshake_tried_) {
		ret = GFARM_ERR_NO_ERROR;
	} else if (ctx->is_got_fatal_ssl_error_) {
		/* ctx->last_ssl_error_ is already set, do not override */
		ret = GFARM_ERR_NO_ERROR;
	} else {
#if 1 /* do not call SSL_shutdown() to avoid protocol interaction here */
		ret = GFARM_ERR_NO_ERROR;
#else
		int st = SSL_shutdown(ssl);

		if (gflog_auth_get_verbose()) {
			gflog_tls_debug(GFARM_MSG_1005649,
				"%s(%s): shutdown SSL issued : %s",
				__func__, ctx->peer_cn_,
				(st == 1) ? "OK" : "NG");
		}

		if (st == 1) {
			ctx->last_ssl_error_ = SSL_ERROR_SSL;
			ctx->is_got_fatal_ssl_error_ = true;
			ret = GFARM_ERR_NO_ERROR;
		} else if (st == 0) {
			/*
			 * SSL Bi-diectional shutdown, by calling
			 * SSL_read and waiting for
			 * SSL_ERROR_ZERO_RETURN or SSL_ERROR_NONE
			 * (SSL_read returns >0)
			 */
			uint8_t buf[65536];
			int s_n = -1;

			ret = tls_session_read(ctx, buf, sizeof(buf), &s_n);

			if (gflog_auth_get_verbose()) {
				gflog_tls_debug(GFARM_MSG_1005650,
					"%s(%s): shutdown SSL replies read "
					"%d : %s", __func__, ctx->peer_cn_,
					s_n, gfarm_error_string(ret));
			}

			if ((ret == GFARM_ERR_NO_ERROR && s_n > 0) ||
				(ret == GFARM_ERR_PROTOCOL)) {
				ctx->last_ssl_error_ = SSL_ERROR_SSL;
				ctx->is_got_fatal_ssl_error_ = true;
				ret = GFARM_ERR_NO_ERROR;
			}
		} else {
			ret = GFARM_ERR_UNKNOWN;
		}
		ctx->is_got_fatal_ssl_error_ = true;
		ctx->is_verified_ = false;
		ctx->io_key_update_accum_ = 0;
		ctx->io_total_ = 0;
#endif /* do not call SSL_shutdown() */
	}

	ctx->last_gfarm_error_ = ret;

	if (gflog_auth_get_verbose()) {
		gflog_tls_debug(GFARM_MSG_1005651,
			"%s(%s): shutdown SSL done : %s",
			__func__, ctx->peer_cn_,
			gfarm_error_string(ret));
	}

	return (ret);
}

/*
 * DN, CN
 */
static inline char *
tls_session_peer_subjectdn_oneline(struct tls_session_ctx_struct *ctx)
{
	if (likely(ctx != NULL)) {
		return (ctx->peer_dn_oneline_);
	} else {
		return (NULL);
	}
}

static inline char *
tls_session_peer_subjectdn_rfc2253(struct tls_session_ctx_struct *ctx)
{
	if (likely(ctx != NULL)) {
		return (ctx->peer_dn_rfc2253_);
	} else {
		return (NULL);
	}
}

static inline char *
tls_session_peer_subjectdn_gsi(struct tls_session_ctx_struct *ctx)
{
	if (likely(ctx != NULL)) {
		return (ctx->peer_dn_gsi_);
	} else {
		return (NULL);
	}
}

static inline char *
tls_session_peer_cn(struct tls_session_ctx_struct *ctx)
{
	if (likely(ctx != NULL)) {
		return (ctx->peer_cn_);
	} else {
		return (NULL);
	}
}

#else

#error Don not include this header unless you know what you need.

#endif /* HAVE_TLS_1_3 && IN_TLS_CORE */
