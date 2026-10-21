/*
 * LeakSanitizer suppressions for the leaks in third-party libraries.
 *
 * LeakSanitizer looks up __lsan_default_suppressions() in the executable,
 * not in shared libraries.  thus this header must be included exactly once
 * in each program, e.g. in the file which defines main().
 * this is effective only when built with AddressSanitizer or LeakSanitizer,
 * and does not depend on LSAN_OPTIONS, which is set differently by systemd,
 * init scripts, and so on.
 *
 * keep this consistent with docker/dist/lsan.supp.
 *
 * __lsan_default_options() is also looked up in the executable, and
 * LSAN_OPTIONS overrides it.
 */

#if defined(__SANITIZE_ADDRESS__)
#define GFARM_LSAN_DEFAULT_SUPPRESSIONS
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(leak_sanitizer)
#define GFARM_LSAN_DEFAULT_SUPPRESSIONS
#endif
#endif

#ifdef GFARM_LSAN_DEFAULT_SUPPRESSIONS
const char *__lsan_default_suppressions(void);
const char *__lsan_default_options(void);

const char *
__lsan_default_suppressions(void)
{
	return (
	    /*
	     * Globus allocates the path at its module activation,
	     * and never frees it.
	     */
	    "leak:globus_i_thread_pre_activate\n"
	    /*
	     * libSciTokens, which the SASL XOAUTH2 plugin loads, calls
	     * curl_global_init() at its initialization, and OpenSSL
	     * initialized by it is never freed.
	     */
	    "leak:curl_global_init\n"
	);
}

const char *
__lsan_default_options(void)
{
	/*
	 * the fast unwinder stops at a library built without frame
	 * pointers (e.g. on RHEL 9 derivatives), and the suppressions
	 * above do not match the truncated stack.
	 */
	return ("fast_unwind_on_malloc=0");
}
#endif
