#define GFARM_MSGDIGEST_STRSIZE	(EVP_MAX_MD_SIZE * 2 + 1)

int gfarm_msgdigest_name_verify(const char *);
const char *gfarm_msgdigest_name_to_openssl(const char *);

#ifdef GFARM_USE_OPENSSL /* this requires <openssl/evp.h> */
int gfarm_msgdigest_init(const char *, EVP_MD_CTX *, int *);
size_t gfarm_msgdigest_final_string(char *, EVP_MD_CTX *);
#endif
size_t gfarm_msgdigest_to_string(char *, unsigned char *, unsigned int);
