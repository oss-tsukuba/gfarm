#undef assert

#if (!defined(__STDC_VERSION__) || __STDC_VERSION__ < 199901L) && !defined(__func__)
#if __GNUC__ >= 2
#define __func__ __FUNCTION__
#else
#define __func__ "<unknown>"
#endif
#endif

#ifdef NDEBUG
#define assert(e) ((void) 0)
#else
#define assert(e) \
	((e) ? ((void) 0) : \
	    gfarm_assert_fail(__FILE__, __LINE__, __func__, #e))
# endif

void gfarm_assert_fail(const char *, int, const char *, const char *);
