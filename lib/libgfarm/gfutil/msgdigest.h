#define GFARM_MSGDIGEST_STRSIZE	(EVP_MAX_MD_SIZE * 2 + 1)

int gfarm_msgdigest_name_verify(const char *);
const char *gfarm_msgdigest_name_to_openssl(const char *);

#ifdef GFARM_USE_OPENSSL /* this requires <openssl/evp.h> */
EVP_MD_CTX *gfarm_msgdigest_alloc(const EVP_MD *);
EVP_MD_CTX *gfarm_msgdigest_alloc_by_name(const char *, int *);
size_t gfarm_msgdigest_free(EVP_MD_CTX *, unsigned char *);
size_t gfarm_msgdigest_to_string_and_free(EVP_MD_CTX *, char *);
#endif
size_t gfarm_msgdigest_to_string(char *, unsigned char *, size_t);
