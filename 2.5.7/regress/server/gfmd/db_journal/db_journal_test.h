#include <assert.h>
#include <stdio.h>

#define TEST_ASSERT(msg, x) \
	test_assert((msg), (long long)(x), __FILE__, __LINE__)

#define TEST_ASSERT0(x) \
	TEST_ASSERT("", (x))

/* assert for boolean */
#define TEST_ASSERT_B(msg, x) \
	TEST_ASSERT(msg, x)

/* assert for expecting GFARM_ERR_NO_ERROR */
#define TEST_ASSERT_NOERR(msg, e) \
	test_assert_noerr((msg), (e), __FILE__, __LINE__)

/* assert for gfarm_error_t */
#define TEST_ASSERT_E(msg, e1, e2) \
	test_assert_err((msg), (e1), (e2), __FILE__, __LINE__)

/* assert for int */
#define TEST_ASSERT_I(msg, v1, v2) \
	if ((v1) != (v2)) { \
		char assertbuf[BUFSIZ]; \
		sprintf(assertbuf, "%s : expected %d but %d", \
		    (msg), (v1), (v2)); \
		TEST_ASSERT(assertbuf, 0); \
	}

/* assert for gfarm_int64_t */
#define TEST_ASSERT_L(msg, v1, v2) \
	if ((v1) != (v2)) { \
		char assertbuf[BUFSIZ]; \
		sprintf(assertbuf, "%s : expected " \
		    "%" GFARM_PRId64 " but %" GFARM_PRId64, \
		    (msg), (gfarm_int64_t)(v1), (gfarm_int64_t)(v2)); \
		TEST_ASSERT(assertbuf, 0); \
	}

/* assert for string */
#define TEST_ASSERT_S(msg, v1, v2) \
	if (strcmp((v1), (v2)) != 0) { \
		char assertbuf[BUFSIZ]; \
		sprintf(assertbuf, "%s : expected %s but %s", \
		    (msg), (v1), (v2)); \
		TEST_ASSERT(assertbuf, 0); \
	}

/* assert for gfarm_timespec */
#define TEST_ASSERT_T(msg, v1, v2) \
	if ((v1).tv_sec != (v2).tv_sec || (v1).tv_nsec != (v2).tv_nsec) { \
		char assertbuf[BUFSIZ]; \
		sprintf(assertbuf, "%s : expected %lld.%d but %lld.%d", \
		    (msg), (long long)(v1).tv_sec, (v1).tv_nsec, \
			(long long)(v2).tv_sec, (v2).tv_nsec); \
		TEST_ASSERT(assertbuf, 0); \
	}

extern struct db_ops empty_ops;

void
test_assert(const char *msg, long long x, const char *file, int line)
{
	if (x)
		return;
	fprintf(stderr, "error: %s at %s:%d\n", msg, file, line);
	exit(EXIT_FAILURE);
}

void
test_assert_noerr(const char *msg, gfarm_error_t e, const char *file, int line)
{
	if (e == GFARM_ERR_NO_ERROR)
		return;
	fprintf(stderr, "error: %s (%s) at %s:%d\n", msg,
	    gfarm_error_string(e), file, line);
	exit(EXIT_FAILURE);
}

void
test_assert_err(const char *msg, gfarm_error_t e1, gfarm_error_t e2,
	const char *file, int line)
{
	if (e1 == e2)
		return;
	fprintf(stderr, "%s : expected '%s' but '%s' at %s:%d\n", msg,
	    gfarm_error_string(e1), gfarm_error_string(e2), file, line);
	exit(EXIT_FAILURE);
}

/* some externs in server/gfmd/host.h
 * server/gfmd/host.h is hidden by lib/libgfarm/gfarm/host.h.
 */
struct host *host_lookup(const char *);
char *host_name(struct host *);
int host_port(struct host *);
char *host_architecture(struct host *);
int host_ncpu(struct host *);
int host_flags(struct host *);
void host_init(void);
