#include <gfarm/gflog.h>

void
gfarm_assert_fail(const char *file, int line_no, const char *func,
    const char *message)
{
	gflog_set_message_verbose(2);
	gflog_fatal_message(GFARM_MSG_1003250, LOG_ERR, file, line_no, func,
	    "Assertion '%s' failed.", message);
}
