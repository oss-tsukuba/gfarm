#pragma once

#if defined(HAVE_TLS_1_3) && defined(IN_TLS_CORE)

/*
 * Here declare and define static objects needed in tls_funcs.h
 */

/*
 * Static passwd buffer
 */
static char the_privkey_passwd[4096] = { 0 };

/*
 * tty control
 */
static bool is_tty_saved = false;
static struct termios saved_tty = { 0 };

/*
 * MT safeness guarantee, for in case.
 */
static pthread_mutex_t pwd_cb_lock = PTHREAD_MUTEX_INITIALIZER;

static pthread_once_t tls_init_once = PTHREAD_ONCE_INIT;
static bool is_tls_runtime_initd = false;

static inline int
tty_passwd_callback_body(char *buf, int maxlen, int rwflag, void *u);
static int
tty_passwd_callback(char *buf, int maxlen, int rwflag, void *u)
{
	return (tty_passwd_callback_body(buf, maxlen, rwflag, u));
}

static inline int
tls_verify_callback_body(int ok, X509_STORE_CTX *sctx);
static int
tls_verify_callback(int ok, X509_STORE_CTX *sctx)
{
	return (tls_verify_callback_body(ok, sctx));
}

static inline void
tls_runtime_init_once_body(void);
static void
tls_runtime_init_once(void)
{
	tls_runtime_init_once_body();
}

static inline gfarm_error_t
accumulate_x509_names_from_file(const char *file,
	STACK_OF(X509_NAME) (*stack), int *n_added);

/* PREREQUISITE: gfarm_privilege_lock */
static gfarm_error_t
iterate_file_for_x509_name(const char *file, void *arg, int *n_added)
{
	return (accumulate_x509_names_from_file(file,
			(STACK_OF(X509_NAME) (*))arg, n_added));
}

static int
tls_add_cert_to_SSL_CTX_chain(SSL_CTX *sctx, X509 *x)
{
	return (SSL_CTX_add_extra_chain_cert(sctx, x));
}

static struct cert_add_method_struct const methods[] = {
	{ SSL_CTX_use_certificate, "use" },
	{ tls_add_cert_to_SSL_CTX_chain, "add" }
};

static int
x509_name_compare(const X509_NAME * const *a, const X509_NAME * const *b)
{
	return (X509_NAME_cmp(*a, *b));
}

#else

#error Do not include this header unless you know what you need.

#endif /* HAVE_TLS_1_3 && IN_TLS_CORE */
