#include <gfarm/gfarm_config.h>

#include <stdlib.h>
#ifdef HAVE_EXECINFO_H
#include <execinfo.h>
#endif

#include <gfarm/gflog.h>

#define	MAX_BACKTRACE_ADDRESSES	50

void
gfarm_log_backtrace_symbols(void)
{
#ifdef HAVE_BACKTRACE_SYMBOLS
	void *addresses[MAX_BACKTRACE_ADDRESSES];
	char **symbols;
	int n;
	int i;

	n = backtrace(addresses, MAX_BACKTRACE_ADDRESSES);
	symbols = backtrace_symbols(addresses, n);

	for (i = 0; i < n; i++) {
		gflog_info(GFARM_MSG_1003405, "backtrace symbols [%d/%d]: %s",
		    i + 1, n, symbols[i]);
	}

	free(symbols);
#endif
}
