#include <pthread.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <netinet/in.h> /* ntoh[ls]()/hton[ls]() on glibc */
#include <errno.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>

#include <openssl/evp.h>

#include <gfarm/gfarm_config.h>
#include <gfarm/gflog.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

#include "gfutil.h"
#include "thrsubr.h"

#define GFARM_USE_OPENSSL
#include "msgdigest.h"

#include "context.h"
#include "config.h"
#include "liberror.h"
#include "auth.h"

#define GFARM_AUTH_EXPIRE_DEFAULT	(24 * 60 * 60) /* 1 day */
#define PATH_URANDOM			"/dev/urandom"

#define staticp	(gfarm_ctxp->auth_common_static)

struct gfarm_auth_common_static {
	/* gfarm_auth_sharedsecret_response_data() */
	pthread_mutex_t openssl_mutex;
};

gfarm_error_t
gfarm_auth_common_static_init(struct gfarm_context *ctxp)
{
	struct gfarm_auth_common_static *s;

	GFARM_MALLOC(s);
	if (s == NULL)
		return (GFARM_ERR_NO_MEMORY);

	gfarm_mutex_init(&s->openssl_mutex,
	    "gfarm_auth_common_static_init", "openssl mutex");

	ctxp->auth_common_static = s;
	return (GFARM_ERR_NO_ERROR);
}

void
gfarm_auth_common_static_term(struct gfarm_context *ctxp)
{
	struct gfarm_auth_common_static *s = ctxp->auth_common_static;

	if (s == NULL)
		return;

	gfarm_mutex_destroy(&s->openssl_mutex,
	    "gfarm_auth_common_static_term", "openssl mutex");
	free(s);
}

static int
skip_space(FILE *fp)
{
	int c;

	while ((c = getc(fp)) != EOF) {
		if (c != ' ' && c != '\t') {
			ungetc(c, fp);
			return (0);
		}
	}
	return (EOF);
}

static int
read_hex(FILE *fp, void *buffer, size_t length)
{
	char *p = buffer;
	size_t i;
	int c1, c2;
	int x1, x2;

	for (i = 0; i < length; i++) {
		c1 = getc(fp);
		if (!isxdigit(c1)) {
			if (c1 != EOF)
				ungetc(c1, fp);
			return (EOF);
		}
		c2 = getc(fp);
		if (!isxdigit(c2)) {
			if (c2 != EOF)
				ungetc(c2, fp);
			return (EOF);
		}
		x1 = isdigit(c1) ? c1 - '0' : tolower(c1) - 'a' + 10;
		x2 = isdigit(c2) ? c2 - '0' : tolower(c2) - 'a' + 10;
		p[i] = x1 * 16 + x2;
	}
	c1 = getc(fp);
	if (c1 != EOF)
		ungetc(c1, fp);
	return (isxdigit(c1) ? EOF : 0);
}

static void
write_hex(FILE *fp, void *buffer, size_t length)
{
	unsigned char *p = buffer;
	size_t i;

	for (i = 0; i < length; i++)
		fprintf(fp, "%02x", p[i]);
}

void
gfarm_auth_random(void *buffer, size_t length)
{
	unsigned char *p = buffer;
	size_t i = 0;
	int fd, rv;

	/*
	 * do not use fopen(3) here,
	 * because it wastes entropy and kernel cpu resource by reading BUFSIZ
	 * rather than reading just the length.
	 */
	if ((fd = open(PATH_URANDOM, O_RDONLY)) != -1) {
		for (; i < length; i += rv) {
			rv = read(fd, p + i, length - i);
			if (rv == -1)
				break;
		}
		close(fd);
		if (i >= length)
			return;
	}

	/* XXX - this makes things too weak */
	for (; i < length; i++) {
#ifdef HAVE_RANDOM
		p[i] = gfarm_random();
#else
		p[i] = gfarm_random() / (RAND_MAX + 1.0) * 256;
#endif
	}
}

/*
 * server only configuration.
 * constant in clients, thus, gfarm_ctxp is not necessary
 */
static int gfarm_auth_root_squash_support = 1; /* always true in clients */

void
gfarm_auth_root_squash_support_disable(void)
{
	gfarm_auth_root_squash_support = 0;
}

/*
 * We switch the user's privilege to read ~/.gfarm_shared_key.
 *
 * NOTE: reading this file with root privilege may not work,
 *	if home directory is NFS mounted and root access for
 *	the home directory partition is not permitted.
 *
 * Do not leave the user privilege switched here, even in the switch_to case,
 * because it is necessary to switch back to the original user privilege when
 * gfarm_auth_sharedsecret fails.
 */
gfarm_error_t
gfarm_auth_shared_key_get(unsigned int *expirep, char *shared_key,
	char *home, struct passwd *pwd, int create, int period)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	FILE *fp = NULL;
	static char keyfile_basename[] = "/" GFARM_AUTH_SHARED_KEY_BASENAME;
	char *keyfilename, *allocbuf = NULL;
	unsigned int expire;

	uid_t o_uid;
	gid_t o_gid;
	int is_root = 0;
	static const char diag[] = "gfarm_auth_shared_key_get";

#ifdef __GNUC__ /* workaround gcc warning: might be used uninitialized */
	o_uid = o_gid = 0;
#endif
	keyfilename = gfarm_get_shared_key_file();
	if (keyfilename == NULL) {
		GFARM_MALLOC_ARRAY(keyfilename,
		    strlen(home) + sizeof(keyfile_basename));
		if (keyfilename == NULL) {
			gflog_debug(GFARM_MSG_1001023,
			    "allocation of 'keyfilename' failed: %s",
			    gfarm_error_string(GFARM_ERR_NO_MEMORY));
			return (GFARM_ERR_NO_MEMORY);
		}
		strcpy(keyfilename, home);
		strcat(keyfilename, keyfile_basename);
		allocbuf = keyfilename;
	}
	if (pwd != NULL && gfarm_auth_root_squash_support) {
		gfarm_privilege_lock(diag);
		o_gid = getegid();
		o_uid = geteuid();
		if (seteuid(0) == 0) /* recover root privilege */
			is_root = 1;
		if (initgroups(pwd->pw_name, pwd->pw_gid) == -1 && is_root)
			gflog_error_errno(GFARM_MSG_1002339,
			    "inigroups(%s, %d)",
			    pwd->pw_name, (int)pwd->pw_gid);
		if (setegid(pwd->pw_gid) == -1 && is_root)
			gflog_error_errno(GFARM_MSG_1002340,
			    "setegid(%d)", (int)pwd->pw_gid);
		if (seteuid(pwd->pw_uid) == -1 && is_root)
			gflog_error_errno(GFARM_MSG_1002341,
			    "seteuid(%d)", (int)pwd->pw_uid);
	}

	if ((fp = fopen(keyfilename, "r+")) == NULL) {
		e = GFARM_ERRMSG_SHAREDSECRET_KEY_FILE_NOT_EXIST;
		goto create;
	}
	if (skip_space(fp) || read_hex(fp, &expire, sizeof(expire))) {
		fclose(fp);
		fp = NULL;
		e = GFARM_ERRMSG_SHAREDSECRET_INVALID_EXPIRE_FIELD;
		goto create;
	}
	expire = ntohl(expire);
	if (skip_space(fp) ||
	    read_hex(fp, shared_key, GFARM_AUTH_SHARED_KEY_LEN)) {
		fclose(fp);
		fp = NULL;
		e = GFARM_ERRMSG_SHAREDSECRET_INVALID_KEY_FIELD;
		goto create;
	}

create:
	if (e != GFARM_ERR_NO_ERROR && create == GFARM_AUTH_SHARED_KEY_GET)
		goto finish;
	if (fp == NULL) {
		if (e != GFARM_ERR_NO_ERROR &&
		    e != GFARM_ERRMSG_SHAREDSECRET_KEY_FILE_NOT_EXIST) {
			gflog_warning(GFARM_MSG_1003703,
			    "%s, create the key again: %s",
			    gfarm_error_string(e), keyfilename);
			e = GFARM_ERR_NO_ERROR;
		}
		fp = fopen(keyfilename, "w+");
		if (fp == NULL) {
			e = gfarm_errno_to_error(errno);
			goto finish;
		}
		if (chmod(keyfilename, 0600) == -1) {
			e = gfarm_errno_to_error(errno);
			fclose(fp);
			goto finish;
		}
		expire = 0; /* force to regenerate key */
	}
	if (create == GFARM_AUTH_SHARED_KEY_CREATE_FORCE ||
	    time(NULL) >= expire) {
		if (create == GFARM_AUTH_SHARED_KEY_GET) {
			fclose(fp);
			e = GFARM_ERR_EXPIRED;
			goto finish;
		}
		if (fseek(fp, 0L, SEEK_SET) == -1) {
			e = gfarm_errno_to_error(errno);
			fclose(fp);
			goto finish;
		}
		gfarm_auth_random(shared_key, GFARM_AUTH_SHARED_KEY_LEN);
		if (period <= 0)
			period = GFARM_AUTH_EXPIRE_DEFAULT;
		expire = time(NULL) + period;
		expire = htonl(expire);
		write_hex(fp, &expire, sizeof(expire));
		expire = ntohl(expire);
		fputc(' ', fp);
		write_hex(fp, shared_key, GFARM_AUTH_SHARED_KEY_LEN);
		fputc('\n', fp);
	}
	if (fclose(fp) != 0) {
		e = gfarm_errno_to_error(errno);
	} else {
		e = GFARM_ERR_NO_ERROR;
		*expirep = expire;
	}
finish:
	free(allocbuf);
	if (pwd != NULL && gfarm_auth_root_squash_support) {
		if (seteuid(0) == -1 && is_root) /* recover root privilege */
			gflog_error_errno(GFARM_MSG_1002342, "seteuid(0)");
		/* abandon group privileges */
		if (setgroups(1, &o_gid) == -1 && is_root)
			gflog_error_errno(GFARM_MSG_1002343,
			    "setgroups(%d)", (int)o_gid);
		if (setegid(o_gid) == -1 && is_root)
			gflog_error_errno(GFARM_MSG_1002344,
			    "setegid(%d)", (int)o_gid);
		/* suppress root privilege, if possible */
		if (seteuid(o_uid) == -1 && is_root)
			gflog_error_errno(GFARM_MSG_1002345,
			    "seteuid(%d)", (int)o_uid);
		gfarm_privilege_unlock(diag);
	}
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001024,
			"getting shared key failed: %s",
			gfarm_error_string(e));
	}
	return (e);
}

gfarm_error_t
gfarm_auth_sharedsecret_response_data(char *shared_key, char *challenge,
				      char *response)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	EVP_MD_CTX *md_ctx;
	int md_len = 0;
	static const char openssl_diag[] = "openssl_mutex";
	static const char diag[] = "gfarm_auth_sharedsecret_response_data";

	/*
	 * according to "valgrind --tool=helgrind",
	 * these OpenSSL functions are not multithread safe,
	 * at least about openssl-0.9.8e-12.el5_4.1.x86_64 on CentOS 5.4
	 */

	gfarm_mutex_lock(&staticp->openssl_mutex, diag, openssl_diag);
	md_ctx = gfarm_msgdigest_alloc(EVP_md5());
	if (md_ctx == NULL) {
		e = GFARM_ERR_NO_MEMORY;
	} else {
		EVP_DigestUpdate(md_ctx,
		    challenge, GFARM_AUTH_CHALLENGE_LEN);
		EVP_DigestUpdate(md_ctx,
		    shared_key, GFARM_AUTH_SHARED_KEY_LEN);
		md_len = gfarm_msgdigest_free(
		    md_ctx, (unsigned char *)response);
	}
	gfarm_mutex_unlock(&staticp->openssl_mutex, diag, openssl_diag);

	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (md_len != GFARM_AUTH_RESPONSE_LEN) {
		gflog_fatal(GFARM_MSG_1003263,
			"gfarm_auth_sharedsecret_response_data:"
			"md5 digest length should be %d, but %d\n",
			GFARM_AUTH_RESPONSE_LEN, md_len);
	}
	return (GFARM_ERR_NO_ERROR);
}
