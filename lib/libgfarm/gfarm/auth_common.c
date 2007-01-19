#include <sys/types.h>
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
#include <openssl/evp.h>
#include <gfarm/gfarm_config.h>
#include <gfarm/gfarm_error.h>
#include <gfarm/gfarm_misc.h>
#include "auth.h"

#define GFARM_AUTH_EXPIRE_DEFAULT	(24 * 60 * 60) /* 1 day */
#define PATH_URANDOM			"/dev/urandom"

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
gfarm_random_initialize(void)
{
	static int rand_initialized = 0;
	struct timeval t;

	if (!rand_initialized) {
		rand_initialized = 1;
		gettimeofday(&t, NULL);
#ifdef HAVE_RANDOM
		srandom(t.tv_sec + t.tv_usec);
#else
		srand(t.tv_sec + t.tv_usec);
#endif
	}
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
	gfarm_random_initialize();
	for (; i < length; i++) {
#ifdef HAVE_RANDOM
		p[i] = random();
#else
		p[i] = rand() / (RAND_MAX + 1.0) * 256;
#endif
	}
}

char *
gfarm_auth_shared_key_get(unsigned int *expirep, char *shared_key,
			  char *home, int create, int period)
{
	FILE *fp;
	static char keyfile_basename[] = "/" GFARM_AUTH_SHARED_KEY_BASENAME;
	char *keyfilename;
	unsigned int expire;

	GFARM_MALLOC_ARRAY(keyfilename, 
		strlen(home) + sizeof(keyfile_basename));
	if (keyfilename == NULL)
		return (GFARM_ERR_NO_MEMORY);
	strcpy(keyfilename, home);
	strcat(keyfilename, keyfile_basename);
	if ((fp = fopen(keyfilename, "r+")) != NULL) {
		if (skip_space(fp) || read_hex(fp, &expire, sizeof(expire))) {
			fclose(fp);
			free(keyfilename);
			return (GFARM_AUTH_SHARED_KEY_PRINTNAME
				": invalid expire field");
		}
		expire = ntohl(expire);
		if (skip_space(fp) ||
		    read_hex(fp, shared_key, GFARM_AUTH_SHARED_KEY_LEN)) {
			fclose(fp);
			free(keyfilename);
			return (GFARM_AUTH_SHARED_KEY_PRINTNAME
				": invalid key field");
		}
	}
	if (fp == NULL) {
		if (create == GFARM_AUTH_SHARED_KEY_GET) {
			free(keyfilename);
			return (GFARM_AUTH_SHARED_KEY_PRINTNAME ": not exist");
		}
		fp = fopen(keyfilename, "w+");
		if (fp == NULL) {
			free(keyfilename);
			return (gfarm_errno_to_error(errno));
		}
		if (chmod(keyfilename, 0600) == -1) {
			fclose(fp);
			free(keyfilename);
			return (gfarm_errno_to_error(errno));
		}
		expire = 0; /* force to regenerate key */
	}
	if (create == GFARM_AUTH_SHARED_KEY_CREATE_FORCE ||
	    time(NULL) >= expire) {
		if (create == GFARM_AUTH_SHARED_KEY_GET) {
			fclose(fp);
			free(keyfilename);
			return (GFARM_ERR_EXPIRED);
		}
		if (fseek(fp, 0L, SEEK_SET) == -1) {
			fclose(fp);
			free(keyfilename);
			return (gfarm_errno_to_error(errno));
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
	fclose(fp);
	free(keyfilename);
	*expirep = expire;
	return (NULL);
}

void
gfarm_auth_sharedsecret_response_data(char *shared_key, char *challenge,
				      char *response)
{
	EVP_MD_CTX mdctx;
	unsigned int md_len;

	EVP_DigestInit(&mdctx, EVP_md5());
	EVP_DigestUpdate(&mdctx, challenge, GFARM_AUTH_CHALLENGE_LEN);
	EVP_DigestUpdate(&mdctx, shared_key, GFARM_AUTH_SHARED_KEY_LEN);
	EVP_DigestFinal(&mdctx, (unsigned char *)response, &md_len);

	if (md_len != GFARM_AUTH_RESPONSE_LEN) {
		fprintf(stderr, "gfarm_auth_sharedsecret_response_data:"
			"md5 digest length should be %d, but %d\n",
			GFARM_AUTH_RESPONSE_LEN, md_len);
		abort();
	}
}
