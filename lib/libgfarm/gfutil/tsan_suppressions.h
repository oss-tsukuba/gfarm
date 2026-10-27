/*
 * ThreadSanitizer suppressions for the false positives in third-party
 * libraries.
 *
 * ThreadSanitizer looks up __tsan_default_suppressions() in the executable,
 * not in shared libraries.  thus this header must be included exactly once
 * in each program, e.g. in the file which defines main().
 * this is effective only when built with ThreadSanitizer, and does not
 * depend on TSAN_OPTIONS, which is set differently by systemd,
 * init scripts, and so on.
 *
 * keep this consistent with docker/dist/tsan.supp.
 */

#if defined(__SANITIZE_THREAD__)
#define GFARM_TSAN_DEFAULT_SUPPRESSIONS
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define GFARM_TSAN_DEFAULT_SUPPRESSIONS
#endif
#endif

#ifdef GFARM_TSAN_DEFAULT_SUPPRESSIONS
const char *__tsan_default_suppressions(void);

const char *
__tsan_default_suppressions(void)
{
	return (
	    /*
	     * getnameinfo(3) in glibc reallocates its internal buffer by
	     * __libc_dynarray_emplace_enlarge(), and ThreadSanitizer
	     * reports a data race between realloc() in different threads,
	     * since it does not see the free() inside glibc.
	     */
	    "race:__libc_dynarray_emplace_enlarge\n"
	);
}
#endif
