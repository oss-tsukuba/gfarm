#pragma once

#if defined(HAVE_TLS_1_3) && defined(IN_TLS_CORE)



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
		int ttyfd = -1;
		FILE *fd = NULL;
		int s_errno;

		*lenptr = 0;
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
				*lenptr = strlen(buf);
				ret = GFARM_ERR_NO_ERROR;
			} else {
				if (s_errno != 0) {
					gflog_tls_error(GFARM_MSG_UNFIXED,
						"Failed to get a password: %s",
						strerror(s_errno));
					ret = gfarm_errno_to_error(s_errno);
				}
			}
		} else {
			gflog_tls_error(GFARM_MSG_UNFIXED,
				"Failed to open a control terminal: %s",
				strerror(s_errno));
			ret = gfarm_errno_to_error(s_errno);
		}
	} else {
		gflog_tls_error(GFARM_MSG_UNFIXED,
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
 * TLSv1.3 suitable cipher checker
 */
static inline gfarm_error_t
is_str_a_tls13_allowed_cipher(const char *str)
{
	gfarm_error_t ret = GFARM_ERRMSG_TLS_INVALID_CIPHER;

	if (likely(is_valid_string(str) == true)) {
		const char * const *c = tls13_valid_cyphers;
		do {
			if (strcmp(str, *c) == 0) {
				ret = GFARM_ERR_NO_ERROR;
				break;
			}
		} while (*++c != NULL);

		if (ret != GFARM_ERR_NO_ERROR) {
			gflog_tls_error(GFARM_MSG_UNFIXED,
				"\"%s\" is not a TLSv1.3 suitable cipher.",
				str);
		}
	}

	return (ret);
}

static inline gfarm_error_t
is_ciphersuites_ok(const char *cipher_list)
{
	gfarm_error_t ret = GFARM_ERR_INVALID_ARGUMENT;

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
				if (ret == GFARM_ERR_NO_ERROR) {
					break;
				}
				p = ++c;
			} while (loop == true);

			free(str);
		}
	}

	return (ret);
}

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
							GFARM_MSG_UNFIXED,
							"Failed to acquire a "
							"group entry for "
							"gid %d: %s",
							gid, strerror(errno));
						ret = gfarm_errno_to_error(
							errno);
					} else {
						gflog_tls_error(
							GFARM_MSG_UNFIXED,
							"Can't find the group "
							"%d.", gid);
						ret = gfarm_errno_to_error(
							errno);
					}
				}
			}
		} else {
			if (errno != 0) {
				gflog_tls_error(GFARM_MSG_UNFIXED,
					"Failed to acquire a passwd entry "
					"for uid %d: %s",
					uid, strerror(errno));
				ret = gfarm_errno_to_error(errno);
			} else {
				gflog_tls_error(GFARM_MSG_UNFIXED,
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
				gflog_tls_error(GFARM_MSG_UNFIXED,
					"%s: %s", file,
					gfarm_error_string(ret));

			}
		} else {
			if (errno != 0) {
				gflog_tls_error(GFARM_MSG_UNFIXED,
					"Failed to stat(\"%s\"): %s",
					file, strerror(errno));
				ret = gfarm_errno_to_error(errno);
			} else {
				gflog_tls_error(GFARM_MSG_UNFIXED,
					"%s is a directory.", file);
				ret = GFARM_ERR_IS_A_DIRECTORY;
			}
		}
	} else {
		gflog_tls_error(GFARM_MSG_UNFIXED,
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
					gflog_tls_error(GFARM_MSG_UNFIXED,
						"The file perrmssion of the "
						"specified file \"%s\" is "
						"open too widely. It would "
						"be nice if the file "
						"permission was 0600.", file);
					ret = GFARM_ERRMSG_TLS_PRIVATE_KEY_FILE_PERMISSION_TOO_WIDELY_OPEN;
				}
			} else {
				gflog_tls_error(GFARM_MSG_UNFIXED,
					"This process is about to read other "
					"uid(%d)'s private key file \"%s\", "
					"which is strongly discouraged even "
					"this process can read it for privacy "
					"and security.", uid, file);
				ret = GFARM_ERRMSG_TLS_PRIVATE_KEY_FILE_ABOUT_TO_BE_OPENED_BY_OTHERS;
			}

		} else {
			if (errno != 0) {
				gflog_tls_error(GFARM_MSG_UNFIXED,
					"Can't access %s: %s",
					file, strerror(errno));
				ret = gfarm_errno_to_error(errno);
			} else {
				gflog_tls_error(GFARM_MSG_UNFIXED,
					"%s is a directory, not a file", file);
				ret = GFARM_ERR_IS_A_DIRECTORY;
			}
		}
	} else {
		gflog_tls_error(GFARM_MSG_UNFIXED,
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
				gflog_tls_error(GFARM_MSG_UNFIXED,
					"%s: %s", dir,
					gfarm_error_string(ret));
			}
		} else {
			if (errno != 0) {
				gflog_tls_error(GFARM_MSG_UNFIXED,
					"Can't access to %s: %s",
					dir, strerror(errno));
				ret = gfarm_errno_to_error(errno);
			} else {
				gflog_tls_error(GFARM_MSG_UNFIXED,
					"%s is not a directory.", dir);
				ret = GFARM_ERR_NOT_A_DIRECTORY;
			}
		}
	} else {
		gflog_tls_error(GFARM_MSG_UNFIXED,
			"Specified CA cert directory name is nul.");
		ret = GFARM_ERR_INVALID_ARGUMENT;
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
#ifdef BASIC_BUFSZ
#define BASIC_BUFSZ_ORG	BASIC_BUFSZ	
#endif /* BASIC_BUFSZ */
#define BASIC_BUFSZ	PATH_MAX
	char msgbuf[BASIC_BUFSZ];
	va_list ap;
	unsigned int err;

	va_start(ap, format);
	(void)vsnprintf(msgbuf, sizeof(msgbuf), format, ap);
	va_end(ap);

	if (ERR_peek_error() != 0 && gflog_auth_get_verbose() != 0) {
		char msgbuf2[BASIC_BUFSZ * 3];
		char tlsmsg[BASIC_BUFSZ];
		const char *tls_file = NULL;
		int tls_line = -1;

		/*
		 * NOTE:
		 *	OpenSSL 1.1.1 doesn't have ERR_get_error_all()
		 *	but 3.0 does. To dig into 3.0 API, check
		 *	Apache 2.4 source.
		 */
		err = ERR_get_error_line_data(&tls_file, &tls_line,
			NULL, NULL);

		ERR_error_string_n(err, tlsmsg, sizeof(tlsmsg));

		(void)snprintf(msgbuf2, sizeof(msgbuf2),
			"%s: [OpenSSL error info: %s:%d: %s]",
			msgbuf, tls_file, tls_line, tlsmsg);

		gflog_auth_message(msg_no, priority, file, line_no, func,
			"%s", msgbuf2);
	} else {
		if ((err = ERR_get_error()) == 0) {
			gflog_message(msg_no, priority, file, line_no, func,
				"%s", msgbuf);
		} else {
			gflog_message(msg_no, priority, file, line_no, func,
				"%s: [OpenSSL error %ud]", msgbuf, err);
		}
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
tls_runtime_init_once(void);

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
	return ((ERR_peek_error() == 0) ? false : true);
}

static inline void
tls_runtime_flush_error(void)
{
	for ((void)ERR_get_error(); ERR_get_error() != 0;);
}

/*
 * Private key loader
 */

static inline int
tty_passwd_callback_body(char *buf, int maxlen, int rwflag, void *u)
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

static inline gfarm_error_t
tls_load_prvkey(const char *file, EVP_PKEY **keyptr)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;

	if (likely(is_valid_string(file) == true && keyptr != NULL)) {
		FILE *f = NULL;
		EVP_PKEY *pkey = NULL;

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
				gflog_tls_error(GFARM_MSG_UNFIXED,
					"Can't read a PEM format private key "
					"from %s.", file);
				ret = GFARM_ERRMSG_TLS_PRIVATE_KEY_READ_FAILURE;
			}
		} else {
			if (errno != 0) {
				gflog_error(GFARM_MSG_UNFIXED,
					"Can't open %s: %s", file,
					strerror(errno));
				ret = gfarm_errno_to_error(errno);
			}
		}
		if (f != NULL) {
			(void)fclose(f);
		}

	}

	return (ret);
}

/*
 * Cert files collector for acceptable certs list.
 */
static inline gfarm_error_t
scan_dir_for_x509_name(const char *dir,
	STACK_OF(X509_NAME) *stack, int *nptr)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;
	DIR *d = NULL;
	struct stat s;
	struct dirent *de = NULL;
	char cert_file[PATH_MAX];
	char *fpath = NULL;
	int nadd = 0;

	errno = 0;
	if (unlikely(dir == NULL || nptr == NULL ||
		   (d = opendir(dir)) == NULL || errno != 0)) {
		if (errno != 0) {
			ret = gfarm_errno_to_error(errno);
			gflog_tls_error(GFARM_MSG_UNFIXED,
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
			if (stack == NULL) {
				nadd++;
				continue;
			}
			(void)snprintf(cert_file, sizeof(cert_file),
				"%s/%s", dir, de->d_name);
			errno = 0;
			if (stat(cert_file, &s) == 0 &&
				S_ISREG(s.st_mode) != 0 &&
				(ret = is_file_readable(-1, cert_file)) ==
				GFARM_ERR_NO_ERROR) {
				/*
				 * Seems SSL_add_file_cert_subjects_to_stack()
				 * requires heap allocated filename.
				 */
				fpath = strdup(cert_file);
				if (unlikely(fpath == NULL)) {
					ret = GFARM_ERR_NO_MEMORY;
					gflog_tls_error(GFARM_MSG_UNFIXED,
						"Can't allocate a "
						"filename buffer: %s",
						gfarm_error_string(ret));
					break;
				}
				/*
				 * NOTE:
				 *	SSL_add_file_cert_subjects_to_stack()
				 *	has no explanation of return value.
				 */
				tls_runtime_flush_error();
				(void)SSL_add_file_cert_subjects_to_stack(
					stack, fpath);
				if (likely(tls_has_runtime_error() == false)) {
					nadd++;
				} else {
					gflog_tls_error(GFARM_MSG_UNFIXED,
						"Failed to add a cert"
						"file to X509_NAME "
						"stack: %s", fpath);
					free(fpath);
					ret = GFARM_ERR_INTERNAL_ERROR;
					break;
				}
			} else {
				if (ret != GFARM_ERR_NO_ERROR) {
					gflog_tls_warning(GFARM_MSG_UNFIXED,
						"Skip adding %s as a valid "
						"cert.", cert_file);
					continue;
				}
			}
		} else {
			if (errno == 0) {
				ret = GFARM_ERR_NO_ERROR;
			} else {
				ret = gfarm_errno_to_error(errno);
				gflog_tls_error(GFARM_MSG_UNFIXED,
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

	return (ret);
}

static inline gfarm_error_t
tls_get_x509_name_stack_from_dir(const char *dir,
	STACK_OF(X509_NAME) *stack, int *nptr)
{
	return scan_dir_for_x509_name(dir, stack, nptr);
}

static inline gfarm_error_t
tls_set_ca_path(SSL_CTX *ssl_ctx, tls_role_t role,
	const char *ca_path, const char* acceptable_ca_path,
	STACK_OF(X509_NAME) **trust_ca_list)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;

	/*
	 * NOTE: What Apache 2.4 does for this are:
	 *
	 *	SSL_CTX_load_verify_locations(ctx,
	 *		tls_ca_certificate_path);
	 *	if (tls_client_ca_certificate_path) {
	 *		dir = tls_client_ca_certificate_path;
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
		tls_runtime_flush_error();
		if (unlikely(SSL_CTX_load_verify_locations(ssl_ctx,
				NULL, ca_path) == 0)) {
			gflog_tls_error(GFARM_MSG_UNFIXED,
				"Failed to set CA path to a SSL_CTX.");
			goto done;
		}
		if (role == TLS_ROLE_SERVER) {
			int ncerts = 0;
			const char *dir =
				(is_valid_string(acceptable_ca_path) == true)
				? acceptable_ca_path : ca_path;
			STACK_OF(X509_NAME) *ca_list = sk_X509_NAME_new_null();
			if (likely(ca_list != NULL &&
				(ret = tls_get_x509_name_stack_from_dir(
					dir, ca_list, &ncerts)) ==
				GFARM_ERR_NO_ERROR && ncerts > 0)) {
				tls_runtime_flush_error();
				SSL_CTX_set_client_CA_list(ssl_ctx, ca_list);
				/*
				 * NOTE:
				 *	To release ca_list, check runtime 
				 *	error.
				 */
				if (unlikely(tls_has_runtime_error() ==
					true)) {
					gflog_tls_error(GFARM_MSG_UNFIXED,
							"Can't add valid "
							"clients CA certs in "
							"%s for server.", dir);
					ret = GFARM_ERR_TLS_RUNTIME_ERROR;
					goto free_ca_list;
				}
			} else {
				if (ca_list == NULL) {
					gflog_tls_error(GFARM_MSG_UNFIXED,
						"Can't allocate "
						"STACK_OF(X509_NAME).");
					ret = GFARM_ERR_NO_MEMORY;
					goto done;
				} else if (ret == GFARM_ERR_NO_ERROR &&
						ncerts == 0) {
					gflog_tls_warning(GFARM_MSG_UNFIXED,
						"No cert file is "
						"added as a valid cert under "
						"%s directory.", dir);
				}
			free_ca_list:
				sk_X509_NAME_pop_free(ca_list,
						X509_NAME_free);
				
			}
		} else {
			ret = GFARM_ERR_NO_ERROR;
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
		if (likely(X509_STORE_load_path(ch, ca_path) == 1)) {
			tls_runtime_flush_error();
			if (likely(SSL_CTX_set0_chain_cert_store(
					ssl_ctx, ch) == 1)) {
				ret = GFARM_ERR_NO_ERROR;
			} else {
				gflog_tls_error(GFARM_MSG_UNFIXED,
					"Failed to set a CA chain path to a "
					"SSL_CTX");
				ret = GFARM_ERR_TLS_RUNTIME_ERROR;
				goto done;
			}
		} else {
			gflog_tls_error(GFARM_MSG_UNFIXED,
				"Failed to load a CA cnain path");
			ret = GFARM_ERR_TLS_RUNTIME_ERROR;
			goto done;
		}
	} else {
		if (ch == NULL) {
			ret = GFARM_ERR_NO_MEMORY;
			gflog_tls_error(GFARM_MSG_UNFIXED,
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
		if (likely(X509_STORE_load_path(ve, ve_path) == 1)) {
			tls_runtime_flush_error();
			if (likely(SSL_CTX_set0_verify_cert_store(
					ssl_ctx, ve) == 1)) {
				ret = GFARM_ERR_NO_ERROR;
			} else {
				gflog_tls_error(GFARM_MSG_UNFIXED,
					"Failed to set a CA verify path to a "
					"SSL_CTX");
				ret = GFARM_ERR_TLS_RUNTIME_ERROR;
				goto done;
			}
		} else {
			gflog_tls_error(GFARM_MSG_UNFIXED,
				"Failed to load a CA verify path");
			ret = GFARM_ERR_TLS_RUNTIME_ERROR;
			goto done;
		}
	} else {
		if (ch == NULL) {
			ret = GFARM_ERR_NO_MEMORY;
			gflog_tls_error(GFARM_MSG_UNFIXED,
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
 * Set revocation path
 */
static inline gfarm_error_t
tls_set_revoke_path(SSL_CTX *ssl_ctx, const char *revoke_path)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;
	X509_STORE *store = NULL;
	int nent = -1;

	tls_runtime_flush_error();
	if (likely(ssl_ctx != NULL &&
		(ret = scan_dir_for_x509_name(revoke_path, NULL, &nent)) ==
		GFARM_ERR_NO_ERROR && nent > 0 &&
		(store = SSL_CTX_get_cert_store(ssl_ctx)) != NULL &&
		is_valid_string(revoke_path) == true)) {
		int st;

		tls_runtime_flush_error();
		st = X509_STORE_load_locations(store, NULL, revoke_path);
		if (likely(st == 1)) {
			tls_runtime_flush_error();
			st = X509_STORE_set_flags(store,
				X509_V_FLAG_CRL_CHECK |
				X509_V_FLAG_CRL_CHECK_ALL);
			if (likely(st == 1)) {
				ret = GFARM_ERR_NO_ERROR;
			} else {
				gflog_tls_error(GFARM_MSG_UNFIXED,
					"Failed to set CRL flags "
					"to an X509_STORE.");
				ret = GFARM_ERR_TLS_RUNTIME_ERROR;
			}
		} else {
			gflog_tls_error(GFARM_MSG_UNFIXED,
				"Failed to set CRL path to an SSL_CTX.");
			ret = GFARM_ERR_TLS_RUNTIME_ERROR;
		}
	} else if (ret != GFARM_ERR_NO_ERROR) {
		if (tls_has_runtime_error() == true) {
			gflog_tls_error(GFARM_MSG_UNFIXED,
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
 * Internal TLS context constructor/destructor
 */
static inline gfarm_error_t
tls_session_shutdown(tls_session_ctx_t ctx);

static inline gfarm_error_t
tls_session_clear_handle(tls_session_ctx_t ctx, bool do_free)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;
	SSL *ssl = NULL;

	if (likely((ssl = ctx->ssl_) != NULL)) {
		ret = tls_session_shutdown(ctx);
		if (likely(ret == GFARM_ERR_NO_ERROR)) {
			(void)SSL_clear(ssl);
			if (do_free == true) {
				SSL_free(ssl);
				ctx->ssl_ = NULL;
			}
			ctx->is_verified_ = false;
			ctx->is_handshake_tried_ = false;
			ctx->got_fatal_ssl_error_ = false;
			ctx->last_ssl_error_ = SSL_ERROR_SSL;
			ctx->last_gfarm_error_ = GFARM_ERR_UNKNOWN;
			ctx->io_total_ = 0;
			ctx->io_key_update_ = 0;
			free(ctx->peer_dn_oneline_);
			ctx->peer_dn_oneline_ = NULL;
			free(ctx->peer_dn_rfc2253_);
			ctx->peer_dn_rfc2253_ = NULL;
			free(ctx->peer_dn_gsi_);
			ctx->peer_dn_gsi_ = NULL;
		}
	} else {
		ret = GFARM_ERR_INVALID_ARGUMENT;
	}

	return (ret);
}

static inline gfarm_error_t
tls_session_setup_handle(tls_session_ctx_t ctx)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;
	SSL *ssl = NULL;
	SSL_CTX *ssl_ctx = NULL;
	tls_role_t role = TLS_ROLE_UNKNOWN;
	
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
			(osst = SSL_get_max_proto_version(
			    ssl)) != TLS1_3_VERSION ) {

			tls_runtime_flush_error();
			if (unlikely((osst = SSL_set_min_proto_version(ssl,
						TLS1_3_VERSION)) != 1 ||
					(osst = SSL_set_max_proto_version(ssl,
						TLS1_3_VERSION)) != 1)) {
				gflog_tls_error(GFARM_MSG_UNFIXED,
					"Failed to set an SSL "
					"only using TLSv1.3.");
				ret = GFARM_ERR_TLS_RUNTIME_ERROR;
				goto done;
			} else {
				if ((osst = SSL_get_min_proto_version(ssl)) !=
					TLS1_3_VERSION ||
					(osst = SSL_get_max_proto_version(
						ssl)) != TLS1_3_VERSION ) {
					gflog_tls_error(GFARM_MSG_UNFIXED,
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
				gflog_tls_error(GFARM_MSG_UNFIXED,
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
				gflog_tls_error(GFARM_MSG_UNFIXED,
					"Failed to set a "
					"server SSL to use "
					"post-handshake.");
				ret = GFARM_ERR_TLS_RUNTIME_ERROR;
			}
		}
#endif

		if (ret == GFARM_ERR_NO_ERROR) {
			ctx->ssl_ = ssl;
			ctx->is_verified_ = false;
			ctx->is_handshake_tried_ = false;
			ctx->last_ssl_error_ = SSL_ERROR_NONE;
			ctx->last_gfarm_error_ = GFARM_ERR_NO_ERROR;
			ctx->io_total_ = 0;
			ctx->io_key_update_ = 0;
			free(ctx->peer_dn_oneline_);
			ctx->peer_dn_oneline_ = NULL;
			free(ctx->peer_dn_rfc2253_);
			ctx->peer_dn_rfc2253_ = NULL;
			free(ctx->peer_dn_gsi_);
			ctx->peer_dn_gsi_ = NULL;
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
 * Certificate verification callback
 */

static inline gfarm_error_t
get_peer_subjectdn(X509_NAME *pn, int mode, char **nameptr, int maxlen)
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
				gflog_tls_error(GFARM_MSG_UNFIXED,
					"Can't allocate a %d bytes buffer for "
					"a peer SubjectDN.", len);
			} else if (len <= 0) {
				ret = GFARM_ERR_INTERNAL_ERROR;
				gflog_tls_error(GFARM_MSG_UNFIXED,
					"Failed to acquire a length of peer "
					"SubjectDN.");
			}
		}
	} else {
		if (bio == NULL) {
			ret = GFARM_ERR_NO_MEMORY;
			gflog_tls_error(GFARM_MSG_UNFIXED,
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
get_peer_subjectdn_gsi_ish(X509_NAME *pn, char **nameptr, int maxlen)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;

	if (likely(pn != NULL && nameptr != NULL)) {
		char buf[4096];
		char *dn = buf;
		char *cnp = NULL;

		if (*nameptr == NULL) {
			*nameptr = NULL;
		}
#define DN_FORMAT_GLOBUS						\
		(XN_FLAG_RFC2253 & ~(ASN1_STRFLGS_ESC_MSB|XN_FLAG_DN_REV))
		ret = get_peer_subjectdn(pn, DN_FORMAT_GLOBUS,
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
					gflog_tls_error(GFARM_MSG_UNFIXED,
						"Can't allocate a buffer for "
						"a GSI-compat SubjectDN.");
				}
			}
		} else {
			if (unlikely(ret == GFARM_ERR_NO_ERROR &&
					cnp == NULL)) {
				ret = GFARM_ERR_INVALID_CREDENTIAL;
				gflog_tls_error(GFARM_MSG_UNFIXED,
					"A SubjectDN \"%s\" has no CN.", buf);
			}
		}
	} else {
		ret = GFARM_ERR_INVALID_ARGUMENT;
	}

	return (ret);
}
	
static inline int
tls_verify_callback_body(int ok, X509_STORE_CTX *sctx)
{
	int ret = ok;
	SSL *ssl = X509_STORE_CTX_get_ex_data(sctx,
			SSL_get_ex_data_X509_STORE_CTX_idx());
	tls_session_ctx_t ctx = (ssl != NULL) ?
		(tls_session_ctx_t)SSL_get_app_data(ssl) : NULL;
	int verr = X509_STORE_CTX_get_error(sctx);
	int vdepth = X509_STORE_CTX_get_error_depth(sctx);
	const char *verrstr = NULL;

	if (ctx->is_allow_no_crls_ == true &&
		ok != 1 && verr == X509_V_ERR_UNABLE_TO_GET_CRL) {
		X509_STORE_CTX_set_error(sctx, X509_V_OK);
		verr = X509_V_OK;
		ok = ret = 1;
	}

	verrstr = X509_verify_cert_error_string(verr);

	if (gflog_get_priority_level() >= LOG_DEBUG) {
		char dnbuf[4096];
		char *dn = dnbuf;
		X509 *p = X509_STORE_CTX_get_current_cert(sctx);
		X509_NAME *pn = (p != NULL) ? X509_get_subject_name(p) : NULL;

		if (pn != NULL &&
			get_peer_subjectdn_gsi_ish(pn, &dn, sizeof(dnbuf)) ==
			GFARM_ERR_NO_ERROR) {
			dn = dnbuf;
		} else {
			dn = NULL;
		}
		gflog_tls_debug(GFARM_MSG_UNFIXED, "depth %d: ok %d: cert "
			"\"%s\": error %d: error string '%s'",
			vdepth, ok, dn, verr, verrstr);
	}

	ctx->cert_verify_callback_error_ = verr;
	
	return (ret);
}

/*
 * Constructor
 */
static inline gfarm_error_t
tls_session_create_ctx(tls_session_ctx_t *ctxptr,
		       tls_role_t role, bool do_mutual_auth)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;
	tls_session_ctx_t ctxret = NULL;

	/*
	 * Following strings must be copied to *ctxret
	 */
	char *cert_file = NULL;		/* required for server/mutual */
	char *cert_chain_file = NULL;	/* required for server/mutual */
	char *prvkey_file = NULL;	/* required for server/mutual */
	char *ca_path = NULL;		/* required for server/mutual */
	char *acceptable_ca_path = NULL;
	char *revoke_path = NULL;
	char *ciphersuites = NULL;
	char *tmp = NULL;

	bool is_build_chain = false;
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

#define str_or_NULL(x)					\
	((is_valid_string((x)) == true) ? (x) : NULL)

	/*
	 * ca_path is mandatory always.
	 */
	/* CA certs path */
	tmp = str_or_NULL(gfarm_ctxp->tls_ca_certificate_path);
	if ((is_valid_string(tmp) == true) &&
		((ret = is_valid_cert_store_dir(tmp))
		== GFARM_ERR_NO_ERROR)) {
		ca_path = strdup(tmp);
		if (unlikely(ca_path == NULL)) {
			ret = GFARM_ERR_NO_MEMORY;
			gflog_tls_error(GFARM_MSG_UNFIXED,
				"Can't dulicate a CA certs directory "
				" name: %s", gfarm_error_string(ret));
			goto bailout;
		}
	} else {
		if (tmp == NULL) {
			gflog_tls_error(GFARM_MSG_UNFIXED,
				"A CA cert path is not specified.");
			ret = GFARM_ERR_INVALID_ARGUMENT;
		} else {
			gflog_tls_error(GFARM_MSG_UNFIXED,
				"Failed to check a CA certs directory %s: %s",
				tmp, gfarm_error_string(ret));
		}
		goto bailout;
	}

	/* Revocation path (optional) */
	tmp = str_or_NULL(gfarm_ctxp->tls_ca_revocation_path);
	if ((is_valid_string(tmp) == true) &&
		((ret = is_valid_cert_store_dir(tmp)) ==
		GFARM_ERR_NO_ERROR)) {
		revoke_path = strdup(tmp);
		if (unlikely(revoke_path == NULL)) {
			ret = GFARM_ERR_NO_MEMORY;
			gflog_tls_error(GFARM_MSG_UNFIXED,
				"Can't dulicate a revoked CA certs "
				"directory name: %s",
				gfarm_error_string(ret));
			goto bailout;
		}
	} else {
		if (tmp != NULL) {
			gflog_tls_warning(GFARM_MSG_UNFIXED,
				"Failed to check revoked certs directory "
				"%s: %s", tmp, gfarm_error_string(ret));
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
		char *tmp_acceptable_ca_path =
			str_or_NULL(
				gfarm_ctxp->tls_client_ca_certificate_path);

		/* cert/cert chain file */
		if ((is_valid_string(tmp_cert_chain_file) == true) &&
			((ret = is_file_readable(-1, tmp_cert_chain_file))
			== GFARM_ERR_NO_ERROR)) {
			cert_chain_file = strdup(tmp_cert_chain_file);
			if (likely(cert_chain_file != NULL)) {
				has_cert_chain_file = true;
			} else {
				ret = GFARM_ERR_NO_MEMORY;
				gflog_tls_warning(GFARM_MSG_UNFIXED,
					"can't dulicate a cert chain "
					"filename: %s",
					gfarm_error_string(ret));
			}
		}
		if ((is_valid_string(tmp_cert_file) == true) &&
			((ret = is_file_readable(-1, tmp_cert_file))
			== GFARM_ERR_NO_ERROR)) {
			cert_file = strdup(tmp_cert_file);
			if (likely(cert_file != NULL)) {
				has_cert_file = true;
			} else {
				ret = GFARM_ERR_NO_MEMORY;
				gflog_tls_warning(GFARM_MSG_UNFIXED,
					"Can't dulicate a cert filename: %s",
					gfarm_error_string(ret));
			}
		}
		if (has_cert_chain_file == true && has_cert_file == true) {
			need_cert_merge = true;
		} else if (has_cert_chain_file == true &&
				has_cert_file == false) {
			cert_to_use = cert_chain_file;
			free(cert_file);
			cert_file = NULL;
		} else if (has_cert_chain_file == false &&
				has_cert_file == true) {
			cert_to_use = cert_file;
			free(cert_chain_file);
			cert_chain_file = NULL;
		} else {
			gflog_tls_error(GFARM_MSG_UNFIXED,
				"Neither a cert file nor a cert chain "
				"file is specified.");
			if (ret == GFARM_ERR_UNKNOWN ||
				ret == GFARM_ERR_NO_ERROR) {
				ret = GFARM_ERR_INVALID_ARGUMENT;
			}
			goto bailout;
		}

		/* Private key */
		if (likely(is_valid_string(tmp_prvkey_file) == true)) {
			prvkey_file = strdup(tmp_prvkey_file);
			if (unlikely(prvkey_file == NULL)) {
				ret = GFARM_ERR_NO_MEMORY;
				gflog_tls_error(GFARM_MSG_UNFIXED,
					"Can't dulicate a private key "
					"filename: %s",
					gfarm_error_string(ret));
				goto bailout;
			}
		} else {
			gflog_error(GFARM_MSG_UNFIXED,
				"A private key file is not specified.");
			ret = GFARM_ERR_INVALID_ARGUMENT;
			goto bailout;
		}

		/* Acceptable CA cert path (server only & optional) */
		if (role == TLS_ROLE_SERVER &&
			(is_valid_string(tmp_acceptable_ca_path) == true) &&
			((ret = is_valid_cert_store_dir(
					tmp_acceptable_ca_path)) ==
			 GFARM_ERR_NO_ERROR)) {
			acceptable_ca_path = strdup(tmp_acceptable_ca_path);
			if (unlikely(acceptable_ca_path == NULL)) {
				ret = GFARM_ERR_NO_MEMORY;
				gflog_tls_error(GFARM_MSG_UNFIXED,
					"Can't dulicate an acceptable CA "
					"certs directory nmae: %s",
					gfarm_error_string(ret));
				goto bailout;
			}
		} else if (role == TLS_ROLE_SERVER) {
			if (tmp_acceptable_ca_path != NULL) {
				gflog_tls_warning(GFARM_MSG_UNFIXED,
					"Failed to check server acceptable "
					"certs directory %s: %s",
					tmp_acceptable_ca_path,
					gfarm_error_string(ret));
			}
		}
	}

	/*
	 * Ciphersuites check
	 * Set only TLSv1.3 allowed ciphersuites
	 */
	if (is_valid_string(gfarm_ctxp->tls_cipher_suite) == true) {
		if ((ret = is_ciphersuites_ok(gfarm_ctxp->tls_cipher_suite)
			== GFARM_ERR_NO_ERROR)) {
			tmp = gfarm_ctxp->tls_cipher_suite;
		} else {
			goto bailout;
		}
	} else {
		tmp = TLS13_DEFAULT_CIPHERSUITES;
	}
	if (tmp != NULL) {
		ciphersuites = strdup(tmp);
		if (unlikely(ciphersuites == NULL)) {
			ret = GFARM_ERR_NO_MEMORY;
			gflog_tls_error(GFARM_MSG_UNFIXED,
				"Can't dulicate a CA cert store name: %s",
				gfarm_error_string(ret));
			goto bailout;
		}
	}

	/*
	 * Final parameter check
	 */
	if (role == TLS_ROLE_SERVER) {
		if (unlikely(is_valid_string(ca_path) != true ||
			(is_valid_string(cert_file) != true &&
			is_valid_string(cert_chain_file) != true) ||
			is_valid_string(prvkey_file) != true)) {
			gflog_tls_error(GFARM_MSG_UNFIXED,
				"As a TLS server, at least a CA ptth, a cert "
				"file/cert chain file and a private key file "
				"must be presented.");
			goto bailout;
		}
	} else {
		if (do_mutual_auth == true) {
			if (unlikely(is_valid_string(ca_path) != true ||
				(is_valid_string(cert_file) != true &&
				is_valid_string(cert_chain_file) != true) ||
				is_valid_string(prvkey_file) != true)) {
				gflog_tls_error(GFARM_MSG_UNFIXED,
					"For TLS client auth, at least "
					"a CA ptth, a cert file/cert chain "
					"file and a private key file "
					"must be presented.");
				goto bailout;
			}
		} else {
			if (unlikely(is_valid_string(ca_path) != true)) {
				gflog_tls_error(GFARM_MSG_UNFIXED,
					"At least a CA path must be "
					"specified.");
				goto bailout;
			}
		}
	}
	
	/*
	 * TLS runtime initialize
	 */
	if (unlikely((ret = tls_session_runtime_initialize())
		!= GFARM_ERR_NO_ERROR)) {
		gflog_error(GFARM_MSG_UNFIXED,
			"TLS runtime library initialization failed.");
		goto bailout;
	}

	if (need_self_cert == true) {
		/*
		 * Load a private key
		 */
		ret = tls_load_prvkey(prvkey_file, &prvkey);
		if (unlikely(ret != GFARM_ERR_NO_ERROR || prvkey == NULL)) {
			gflog_error(GFARM_MSG_UNFIXED,
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
			gflog_tls_error(GFARM_MSG_UNFIXED,
					"Failed to set an SSL_CTX "
					"only using TLSv1.3.");
			ret = GFARM_ERR_TLS_RUNTIME_ERROR;
			goto bailout;
		} else {
			if ((osst = SSL_CTX_get_min_proto_version(ssl_ctx)) !=
				TLS1_3_VERSION ||
				(osst = SSL_CTX_get_max_proto_version(
						ssl_ctx)) != TLS1_3_VERSION ) {
				gflog_tls_error(GFARM_MSG_UNFIXED,
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

		/*
		 * Set ciphersuites
		 */
		tls_runtime_flush_error();
		if (unlikely(SSL_CTX_set_ciphersuites(ssl_ctx,
					ciphersuites) != 1)) {
			gflog_error(GFARM_MSG_UNFIXED,
				"Failed to set ciphersuites "
				"\"%s\" to the SSL_CTX.",
				ciphersuites);
			/* ?? GFARM_ERRMSG_TLS_INVALID_CIPHER ?? */
			ret = GFARM_ERR_INTERNAL_ERROR;
			goto bailout;
		} else {
			/*
			 * XXX FIXME:
			 *	How one can check the ciphers are
			 *	successfully set?
			 */
		}

		/*
		 * Set CA path
		 */
		ret = tls_set_ca_path(ssl_ctx, role,
			ca_path, acceptable_ca_path, NULL);
		if (unlikely(ret != GFARM_ERR_NO_ERROR)) {
			goto bailout;
		}

		if (need_self_cert == true) {
			/*
			 * Load a cert into the SSL_CTX
			 */
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
			if (unlikely(osst != 1)) {
				gflog_tls_error(GFARM_MSG_UNFIXED,
					"Can't load a certificate "
					"file \"%s\" into a SSL_CTX.",
					cert_to_use);
				/* XXX ret code */
				ret = GFARM_ERR_TLS_RUNTIME_ERROR;
				goto bailout;
			}

			/*
			 * Set a private key into the SSL_CTX
			 */
			tls_runtime_flush_error();
			osst = SSL_CTX_use_PrivateKey(ssl_ctx, prvkey);
			if (unlikely(osst != 1)) {
				gflog_tls_error(GFARM_MSG_UNFIXED,
					"Can't set a private key to a "
					"SSL_CTX.");
				/* ?? GFARM_ERRMSG_TLS_IBVALID_KEY ?? */
				ret = GFARM_ERR_TLS_RUNTIME_ERROR;
				goto bailout;
			}

			/*
			 * XXX FIXME:
			 *	Check prvkey in SSL_CTX by 
			 *	SSL_CTX_check_private_key
			 */
			tls_runtime_flush_error();
			osst = SSL_CTX_check_private_key(ssl_ctx);
			if (unlikely(osst != 1)) {
				gflog_tls_error(GFARM_MSG_UNFIXED,
					"Wrong private key file for the "
					"current certificate.");
				ret = GFARM_ERR_TLS_RUNTIME_ERROR;
				goto bailout;
			}

			is_build_chain =
				(gfarm_ctxp->tls_build_chain_local == 1)
				? true : false;
			if (is_build_chain == true) {
				/*
				 * Build a complete cert chain locally.
				 */
				tls_runtime_flush_error();
				osst = SSL_CTX_build_cert_chain(ssl_ctx, 0);
				if (unlikely(osst != 1)) {
					gflog_tls_error(GFARM_MSG_UNFIXED,
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

		if (false) {
			/*
			 * NOTE: 
			 * Seems revoked certs in
			 * tls_ca_certificate_path should be
			 * rejected. openssl s_{client|server} does
			 * following for this.
			 */
			X509_VERIFY_PARAM *vpm =
				SSL_CTX_get0_param(ssl_ctx);
			if (likely(vpm != NULL)) {
				tls_runtime_flush_error();
				osst = X509_VERIFY_PARAM_set_flags(vpm,
					X509_V_FLAG_CRL_CHECK |
					X509_V_FLAG_CRL_CHECK_ALL);
				if (unlikely(osst != 1)) {
					gflog_tls_error(GFARM_MSG_UNFIXED,
						"Failed to set CRL check bits "
						"to a X509_VERIFY_PARAM");
					ret = GFARM_ERR_TLS_RUNTIME_ERROR;
					X509_VERIFY_PARAM_free(vpm);
					goto bailout;
				}
			}
		}

	} else {
		gflog_tls_error(GFARM_MSG_UNFIXED,
			"Failed to create a SSL_CTX.");
		ret = GFARM_ERR_TLS_RUNTIME_ERROR;
		goto bailout;
	}

	/*
	 * Create a new tls_session_ctx_t
	 */
	ctxret = (tls_session_ctx_t)malloc(
		sizeof(struct tls_session_ctx_struct));
	if (likely(ctxret != NULL)) {
		tls_runtime_flush_error();

		(void)memset(ctxret, 0,
			sizeof(struct tls_session_ctx_struct));
		ctxret->role_ = role;
		ctxret->do_mutual_auth_ = do_mutual_auth;
		if (gfarm_ctxp->tls_key_update > 0) {
#ifndef TLS_TEST
#define TLS_KEY_UPDATE_THRESH	512 * 1024 * 1024;
			ctxret->keyupd_thresh_ = TLS_KEY_UPDATE_THRESH;
#undef TLS_KEY_UPDATE_THRESH
#else
			ctxret->keyupd_thresh_ = gfarm_ctxp->tls_key_update;
#endif /* ! TLS_TEST */
		} else {
			ctxret->keyupd_thresh_ = 0;
		}
		ctxret->prvkey_ = prvkey;
		ctxret->ssl_ctx_ = ssl_ctx;
		ctxret->is_build_chain_ = is_build_chain;
		ctxret->is_allow_no_crls_ =
			(gfarm_ctxp->tls_allow_crl_absence == 1) ?
			true : false;
		ctxret->cert_file_ = cert_file;
		ctxret->cert_chain_file_ = cert_chain_file;
		ctxret->prvkey_file_ = prvkey_file;
		ctxret->ciphersuites_ = ciphersuites;
		ctxret->ca_path_ = ca_path;
		ctxret->acceptable_ca_path_ = acceptable_ca_path;
		ctxret->revoke_path_ = revoke_path;

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
	free(cert_file);
	free(cert_chain_file);
	free(prvkey_file);
	free(ciphersuites);
	free(ca_path);
	free(acceptable_ca_path);
	free(revoke_path);

	if (prvkey != NULL) {
		EVP_PKEY_free(prvkey);
	}
	if (ssl_ctx != NULL) {
		(void)SSL_CTX_clear_chain_certs(ssl_ctx);
		SSL_CTX_free(ssl_ctx);
	}
	free(ctxret);

ok:
	return (ret);

#undef str_or_NULL
}

/*
 * Destructor
 */
static inline void
tls_session_destroy_ctx(tls_session_ctx_t x)
{
	if (x != NULL) {
		free(x->peer_dn_oneline_);
		free(x->peer_dn_rfc2253_);
		free(x->peer_dn_gsi_);
		if (x->ssl_ != NULL) {
			SSL_free(x->ssl_);
		}
		if (x->prvkey_ != NULL) {
			EVP_PKEY_free(x->prvkey_);
		}
		if (x->ssl_ctx_ != NULL) {
			(void)SSL_CTX_clear_chain_certs(x->ssl_ctx_);
			SSL_CTX_free(x->ssl_ctx_);
		}
		free(x->cert_file_);
		free(x->cert_chain_file_);
		free(x->prvkey_file_);
		free(x->ciphersuites_);
		free(x->ca_path_);
		free(x->acceptable_ca_path_);
		free(x->revoke_path_);

		free(x);
	}
}

static inline gfarm_error_t
tls_session_clear_ctx(tls_session_ctx_t x)
{
	return tls_session_clear_handle(x, true);
}



/*
 * TLS I/O operations
 */

/*
 * SSL_ERROR_* handler
 */
static inline bool
tls_session_io_continuable(int sslerr, tls_session_ctx_t ctx,
	bool in_handshake)
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
			gflog_error(GFARM_MSG_UNFIXED,
				    "The TLS handshake must be done before "
				    "begining data I/O in Gfarm.");
			ctx->last_gfarm_error_ = GFARM_ERR_INTERNAL_ERROR;
		} else {
			ctx->last_gfarm_error_ = GFARM_ERR_NO_ERROR;
			ret = true;
		}
		break;

	default:
		gflog_error(GFARM_MSG_UNFIXED,
			"All the TLS I/O error must be handled, but got "
			"TLS I/O error %d.", sslerr);
		ctx->last_gfarm_error_ = GFARM_ERR_INTERNAL_ERROR;
		break;
	}

	return (ret);
}

/*
 * TLS session read timeout checker
 */
static inline gfarm_error_t
tls_session_wait_readable(tls_session_ctx_t ctx, int fd, int tous)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;
	char *method = NULL;
	
	gflog_tls_debug(GFARM_MSG_UNFIXED, "%s(): wait enter.",
		__func__);

	if (SSL_has_pending(ctx->ssl_) == 1) {
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
			fds[0].events = POLLIN;
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

			st = select(fd + 1, &fds, NULL, NULL, tvp);
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

	gflog_tls_debug(GFARM_MSG_UNFIXED, "%s(): wait (%s) end : %s",
		__func__, method, gfarm_error_string(ret));

	return (ret);
}

static inline gfarm_error_t
tls_session_get_pending_read_bytes_n(tls_session_ctx_t ctx, int *nptr)
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
tls_session_verify(tls_session_ctx_t ctx, bool *is_verified)
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
		tls_runtime_flush_error();
		if (likely(((p = SSL_get_peer_certificate(ssl)) != NULL) &&
			((pn = X509_get_subject_name(p)) != NULL))) {
			char *dn_oneline = NULL;
			char *dn_rfc2253 = NULL;
			char *dn_gsi = NULL;
			bool v = false;
			int vres = -INT_MAX;

#define DN_FORMAT_ONELINE	(XN_FLAG_ONELINE & ~ASN1_STRFLGS_ESC_MSB)
#define DN_FORMAT_RFC2253	(XN_FLAG_RFC2253 & ~ASN1_STRFLGS_ESC_MSB)
			if (likely((ret = get_peer_subjectdn(pn,
						DN_FORMAT_ONELINE,
						&dn_oneline, 0)) ==
				GFARM_ERR_NO_ERROR)) {
				ctx->peer_dn_oneline_ = dn_oneline;
			}
			if (likely((ret = get_peer_subjectdn(pn,
						DN_FORMAT_RFC2253,
						&dn_rfc2253, 0)) ==
				GFARM_ERR_NO_ERROR)) {
				ctx->peer_dn_rfc2253_ = dn_rfc2253;
			}
			if (likely((ret = get_peer_subjectdn_gsi_ish(pn,
						&dn_gsi, 0)) ==
				GFARM_ERR_NO_ERROR)) {
				ctx->peer_dn_gsi_ = dn_gsi;
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
				gflog_tls_error(GFARM_MSG_UNFIXED,
					"Certificate verification failed: %s",
					X509_verify_cert_error_string(vres));
				ret = ctx->last_gfarm_error_ = 
					GFARM_ERRMSG_TLS_CERT_VERIFIY_FAILURE;
			}
			ctx->cert_verify_result_error_ = vres;
			ctx->is_verified_ = v;
		} else {
			ret = GFARM_ERR_TLS_RUNTIME_ERROR;
			gflog_tls_error(GFARM_MSG_UNFIXED,
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

static inline char *
tls_session_peer_cn(tls_session_ctx_t ctx);

static inline gfarm_error_t
tls_session_establish(tls_session_ctx_t ctx, int fd)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;
	struct sockaddr sa;
	socklen_t salen = sizeof(sa);
	int pst = -1;
	typedef int (*tls_handshake_proc_t)(SSL *ssl);
	tls_handshake_proc_t p = NULL;
	SSL *ssl = NULL;

	errno = 0;
	if (likely(fd >= 0 &&
		(pst = getpeername(fd, &sa, &salen)) == 0 &&
		ctx != NULL)) {

		/*
		 * Create an SSL
		 */
		ret = tls_session_setup_handle(ctx);
		if (unlikely(ret != GFARM_ERR_NO_ERROR || ctx->ssl_ == NULL)) {
			goto bailout;
		}
		ssl = ctx->ssl_;
		
		tls_runtime_flush_error();
		if (likely(SSL_set_fd(ssl, fd) == 1)) {
			int st;
			int ssl_err;
			bool do_cont = false;

			ctx->is_handshake_tried_ = true;
			p = (ctx->role_ == TLS_ROLE_SERVER) ?
				SSL_accept : SSL_connect;

		retry:
			errno = 0;
			tls_runtime_flush_error();
			st = p(ssl);
			ssl_err = SSL_get_error(ssl, st);
			do_cont = tls_session_io_continuable(
					ssl_err, ctx, false);
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
				gflog_tls_error(GFARM_MSG_UNFIXED,
					"SSL handshake failed: %s",
					gfarm_error_string(ret));
			}
		} else {
			gflog_tls_error(GFARM_MSG_UNFIXED,
				"Failed to set a file "
				"descriptor %d to an SSL.", fd);
			ret = GFARM_ERR_TLS_RUNTIME_ERROR;
		}
	} else {
		if (pst != 0 && errno != 0) {
			ret = gfarm_errno_to_error(errno);
			if (errno == ENOTCONN) {
				gflog_tls_error(GFARM_MSG_UNFIXED,
					"The file descriptor %d is not yet "
					"connected: %s",
					fd, gfarm_error_string(ret));
			} else if (errno == ENOTSOCK) {
				gflog_tls_error(GFARM_MSG_UNFIXED,
					"The file descriptor %d is not a "
					"socket: %s",
					fd, gfarm_error_string(ret));
			} else {
				gflog_tls_error(GFARM_MSG_UNFIXED,
					"Failed to check connection status of "
					"the file descriptor %d: %s",
					fd, gfarm_error_string(ret));
			}
		} else {
			ret = GFARM_ERR_INVALID_ARGUMENT;
			gflog_tls_error(GFARM_MSG_UNFIXED,
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
				gflog_tls_error(GFARM_MSG_UNFIXED,
					"Authentication failed between peer: "
					"'%s'", ctx->peer_dn_oneline_);
			} else {
				gflog_tls_error(GFARM_MSG_UNFIXED,
					"Authentication failed "
					"(no cert acquired.)");
			}
		}

		if (is_valid_string(ctx->peer_dn_oneline_) == true) {
			gflog_tls_notice(GFARM_MSG_UNFIXED,
				"Authentication between \"%s\" %s and a "
				"TLS session %s.",
				ctx->peer_dn_gsi_,
				(is_verified == true) ?
					 "verified" : "not verified",
				(is_verified == true) ?
					 "established" : "not established");
			gflog_tls_debug(GFARM_MSG_UNFIXED,
				"peer CN \"%s\"", tls_session_peer_cn(ctx));
		}
	}

bailout:
	return (ret);
}

/*
 * TLS 1.3 key update
 */
static inline gfarm_error_t
tls_session_update_key(tls_session_ctx_t ctx, int delta)
{
	/*
	 * Only clients initiate KeyUpdate.
	 */
	gfarm_error_t ret = GFARM_ERR_NO_ERROR;
	SSL *ssl;

	if (likely(ctx != NULL && (ssl = ctx->ssl_) != NULL &&
		ctx->role_ == TLS_ROLE_CLIENT &&
		ctx->keyupd_thresh_ > 0 &&
		ctx->got_fatal_ssl_error_ == false &&
		((ctx->io_key_update_ += delta) >= ctx->keyupd_thresh_))) {
		if (likely(SSL_key_update(ssl,
				SSL_KEY_UPDATE_REQUESTED) == 1)) {
			ret = ctx->last_gfarm_error_ = GFARM_ERR_NO_ERROR;
			gflog_tls_debug(GFARM_MSG_UNFIXED,
				"TLS shared key updatted after after %zu "
				"bytes I/O.", ctx->io_key_update_);
		} else {
			/*
			 * XXX FIXME:
			 *	OpenSSL 1.1.1 manual doesn't refer
			 *	what to do when SSL_key_update()
			 *	failure.
			 */
			gflog_warning(GFARM_MSG_UNFIXED,
				"SSL_update_key() failed but we don't know "
				"how to deal with it.");
			ret = ctx->last_gfarm_error_ =
				GFARM_ERR_INTERNAL_ERROR;
		}
		ctx->io_key_update_ = 0;
	} else {
		ret = ctx->last_gfarm_error_;
	}

	return (ret);
}
	
/*
 * TLS session read(2)'ish
 */
static inline gfarm_error_t
tls_session_read(tls_session_ctx_t ctx, void *buf, int len,
	int *actual_io_bytes)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;
	SSL *ssl = NULL;

	if (likely(ctx != NULL && (ssl = ctx->ssl_) != NULL && buf != NULL &&
			len > 0 && actual_io_bytes != NULL &&
			ctx->is_verified_ == true &&
			ctx->got_fatal_ssl_error_ == false)) {
		int n = 0;
		int ssl_err;
		bool continuable;

		gflog_tls_debug(GFARM_MSG_UNFIXED,
			"%s(%s): about to read %d (remains %d)", __func__,
			ctx->peer_dn_gsi_, len, SSL_pending(ssl));

		if (unlikely(len == 0)) {
			ret = ctx->last_gfarm_error_ = GFARM_ERR_NO_ERROR;
			goto done;
		}

		*actual_io_bytes = 0;

	retry:
		gflog_tls_debug(GFARM_MSG_UNFIXED,
			"%s(%s): read %d/%d", __func__,
			ctx->peer_dn_gsi_, n, len);

		errno = 0;
		n = SSL_read(ssl, buf, len);
		/*
		 * NOTE:
		 *	To avoid sending key update request on broken
		 *	TLS stream, check SSL_ERROR_ for the session
		 *	continuity.
		 */
		ssl_err = SSL_get_error(ssl, n);
		continuable = tls_session_io_continuable(ssl_err, ctx, false);
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

		gflog_tls_debug(GFARM_MSG_UNFIXED,
			"%s(%s): read done %d (remains %d) : %s", __func__,
			ctx->peer_dn_gsi_, n, SSL_pending(ssl),
			gfarm_error_string(ret));

	} else {
		ret = ctx->last_gfarm_error_ = GFARM_ERR_INVALID_ARGUMENT;
	}

done:
	return (ret);
}

/*
 * TLS session write(2)'ish
 */
static inline gfarm_error_t
tls_session_write(tls_session_ctx_t ctx, const void *buf, int len,
	int *actual_io_bytes)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;
	SSL *ssl = NULL;

	if (likely(ctx != NULL && (ssl = ctx->ssl_) != NULL && buf != NULL &&
			len > 0 && actual_io_bytes != NULL &&
			ctx->is_verified_ == true &&
			ctx->got_fatal_ssl_error_ == false)) {
		int n = 0;
		int ssl_err;
		bool continuable;

		gflog_tls_debug(GFARM_MSG_UNFIXED,
			"%s(%s): about to write %d", __func__,
			ctx->peer_dn_gsi_, len);

		if (unlikely(len == 0)) {
			ret = ctx->last_gfarm_error_ = GFARM_ERR_NO_ERROR;
			goto done;
		}

		*actual_io_bytes = 0;
	retry:
		gflog_tls_debug(GFARM_MSG_UNFIXED,
			"%s(%s): write %d/%d", __func__,
			ctx->peer_dn_gsi_, n, len);

		errno = 0;
		n = SSL_write(ssl, buf, len);
		/*
		 * NOTE:
		 *	To avoid sending key update request on broken
		 *	TLS stream, check SSL_ERROR_ for the session
		 *	continuity.
		 */
		ssl_err = SSL_get_error(ssl, n);
		continuable = tls_session_io_continuable(ssl_err, ctx, false);
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

		gflog_tls_debug(GFARM_MSG_UNFIXED,
			"%s(%s): write done %d : %s", __func__,
			ctx->peer_dn_gsi_, n, gfarm_error_string(ret));

	} else {
		ret = ctx->last_gfarm_error_ = GFARM_ERR_INVALID_ARGUMENT;
	}

done:
	return (ret);
}

/*
 * tls session read with timeout (includes "forever")
 */
static inline gfarm_error_t
tls_session_timeout_read(tls_session_ctx_t ctx, int fd, void *buf, int len,
	int timeout, int *actual_read)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;

	ret = tls_session_wait_readable(ctx, fd, timeout);
	if (likely(ret == GFARM_ERR_NO_ERROR)) {
		ret = tls_session_read(ctx, buf, len, actual_read);
	}

	return (ret);
}

/*
 * TLS session shutdown
 */
static inline gfarm_error_t
tls_session_shutdown(tls_session_ctx_t ctx)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;
	SSL *ssl;
	bool is_shutdown = false;

	if (likely((ctx != NULL) && ((ssl = ctx->ssl_) != NULL))) {
		int st = -1;

		gflog_tls_debug(GFARM_MSG_UNFIXED,
			"%s(%s): about to shutdown SSL.",
			__func__, ctx->peer_dn_gsi_);

		if (ctx->is_handshake_tried_ == false) {
			ret = GFARM_ERR_NO_ERROR;
			goto done;
		} else if (ctx->got_fatal_ssl_error_ == true) {
			st = 1;
		} else {
			st = SSL_shutdown(ssl);

			gflog_tls_debug(GFARM_MSG_UNFIXED,
				"%s(%s): shutdown SSL issued : %s",
				__func__, ctx->peer_dn_gsi_,
				(st == 1) ? "OK" : "NG");

			is_shutdown = true;
		}
		if (st == 1) {
		shutdown_ok:
			ctx->last_ssl_error_ = SSL_ERROR_SSL;
			ctx->got_fatal_ssl_error_ = true;
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

			gflog_tls_debug(GFARM_MSG_UNFIXED,
				"%s(%s): shutdown SSL replies read %d : %s",
				__func__, ctx->peer_dn_gsi_,
				s_n, gfarm_error_string(ret));
			
			if ((ret == GFARM_ERR_NO_ERROR && s_n > 0) ||
				(ret == GFARM_ERR_PROTOCOL)) {
				goto shutdown_ok;
			}
		}
	done:
		ctx->last_gfarm_error_ = ret;
		if (is_shutdown == true) {
			ctx->got_fatal_ssl_error_ = true;
			ctx->is_verified_ = false;
			ctx->io_key_update_ = 0;
			ctx->io_total_ = 0;
		}

		gflog_tls_debug(GFARM_MSG_UNFIXED,
			"%s(%s): shutdown SSL done : %s",
			__func__, ctx->peer_dn_gsi_, gfarm_error_string(ret));

	} else if (ctx == NULL) {
		ret = GFARM_ERR_NO_ERROR;
	}

	return (ret);
}

static inline char *
tls_session_peer_subjectdn_oneline(tls_session_ctx_t ctx)
{
	if (likely(ctx != NULL)) {
		return (ctx->peer_dn_oneline_);
	} else {
		return (NULL);
	}
}
	
static inline char *
tls_session_peer_subjectdn_rfc2253(tls_session_ctx_t ctx)
{
	if (likely(ctx != NULL)) {
		return (ctx->peer_dn_rfc2253_);
	} else {
		return (NULL);
	}
}

static inline char *
tls_session_peer_subjectdn_gsi(tls_session_ctx_t ctx)
{
	if (likely(ctx != NULL)) {
		return (ctx->peer_dn_gsi_);
	} else {
		return (NULL);
	}
}

static inline char *
tls_session_peer_cn(tls_session_ctx_t ctx)
{
	char *ret = NULL;

	if (likely(ctx != NULL &&
		is_valid_string(ctx->peer_dn_gsi_) == true)) {
		char *dn = ctx->peer_dn_gsi_;
		if (likely(dn != NULL)) {
			ret = (char *)memmem(dn, strlen(dn), "CN=", 3);
			if (likely(ret != NULL && *(ret + 3) != '\0')) {
				ret += 3;
			} else {
				ret = NULL;
			}
		}
	}

	return (ret);
}



#else

#error Don not include this header unless you know what you need.

#endif /* HAVE_TLS_1_3 && IN_TLS_CORE */
