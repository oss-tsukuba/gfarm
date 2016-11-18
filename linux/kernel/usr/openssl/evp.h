/*
typedef void *EVP_MD_CTX;
typedef void *EVP_MD;
*/
#define EVP_MD_CTX void *
#define EVP_MD void *

#define EVP_md5(void) NULL

static inline int
EVP_DigestUpdate(EVP_MD_CTX *ctx, const void *d, size_t cnt)
{
	return (0);
}
static inline int
EVP_DigestInit(EVP_MD_CTX *ctx, const EVP_MD *type)
{
	return (0);
}
static inline int
EVP_DigestFinal(EVP_MD_CTX *ctx, unsigned char *md, unsigned int *s)
{
	return (0);
}
#define EVP_MAX_MD_SIZE  0

#ifndef EOF
# define EOF (-1)
#endif
#include <sys/socket.h>

