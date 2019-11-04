#include <errno.h>
#include <ctype.h>
#include <stdlib.h>

#include <openssl/evp.h>

#include "msgdigest.h"

int
gfarm_msgdigest_name_verify(const char *gfarm_name)
{
	const unsigned char *s = (const unsigned char *)gfarm_name;
	const EVP_MD *md_type;

	/*
	 * Gfarm uses just same names with OpenSSL for now,
	 * except that only lower-case characters and digits are allowed.
	 */
	for (; *s != '\0'; s++) {
		if (!islower(*s) && !isdigit(*s))
			return 0;
	}

	md_type = EVP_get_digestbyname(
	    gfarm_msgdigest_name_to_openssl(gfarm_name));

	return (md_type != NULL);
}

const char *
gfarm_msgdigest_name_to_openssl(const char *gfarm_name)
{
	return (gfarm_name); /* XXX just same names with OpenSSL for now */
}

EVP_MD_CTX *
gfarm_msgdigest_alloc(const EVP_MD *md_type)
{
	EVP_MD_CTX *md_ctx;

	md_ctx = EVP_MD_CTX_create();
	if (md_ctx == NULL)
		return (NULL); /* no memory */

	EVP_DigestInit_ex(md_ctx, md_type, NULL);
	return (md_ctx);
}

EVP_MD_CTX *
gfarm_msgdigest_alloc_by_name(const char *md_type_name, int *cause_p)
{
	const EVP_MD *md_type;
	EVP_MD_CTX *md_ctx;

	if (md_type_name == NULL || md_type_name[0] == '\0') {
		if (cause_p != NULL)
			*cause_p = 0; /* DO NOT calculate message digest */
		return (NULL);
	}

	md_type = EVP_get_digestbyname(
	    gfarm_msgdigest_name_to_openssl(md_type_name));
	if (md_type == NULL) {
		if (cause_p != NULL)
			*cause_p = EOPNOTSUPP; /* not supported */
		return (NULL);
	}

	md_ctx = gfarm_msgdigest_alloc(md_type);
	if (md_ctx == NULL) {
		if (cause_p != NULL)
			*cause_p = ENOMEM; /* no memory */
		return (NULL);
	}

	return (md_ctx); /* calculate message digest */
}

/*
 * md_string should be declared as:
 * 	char md_string[GFARM_MSGDIGEST_STRSIZE];
 */
size_t
gfarm_msgdigest_to_string(
	char *md_string, unsigned char *md_value, size_t md_len)
{
	size_t i;

	for (i = 0; i < md_len; ++i)
		sprintf(&md_string[i * 2], "%02x", md_value[i]);
	return (md_len * 2);
}

size_t
gfarm_msgdigest_free(EVP_MD_CTX *md_ctx, unsigned char *md_value)
{
	unsigned int md_len;

	EVP_DigestFinal_ex(md_ctx, md_value, &md_len);
	EVP_MD_CTX_destroy(md_ctx);
	return (md_len);
}

size_t
gfarm_msgdigest_to_string_and_free(EVP_MD_CTX *md_ctx, char *md_string)
{
	size_t md_len;
	unsigned char md_value[EVP_MAX_MD_SIZE];

	md_len = gfarm_msgdigest_free(md_ctx, md_value);
	return (gfarm_msgdigest_to_string(md_string, md_value, md_len));
}
