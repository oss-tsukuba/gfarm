#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <gfarm/gfarm.h>

#include "liberror.h"

void
msg(gfarm_error_t e)
{
	int eno = gfarm_error_to_errno(e);

	printf("%3d %10d %s\n", e, eno, strerror(eno));
}

int
main(int argc, char **argv)
{
	gfarm_error_t e;

	e = gfarm_initialize(NULL, NULL);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "initialize: %s\n", gfarm_error_string(e));
		return (2);
	}
	for (e = GFARM_ERR_NO_ERROR; e < GFARM_ERR_NUMBER; e++)
		msg(e);

	msg(GFARM_ERR_NUMBER);
	msg(GFARM_ERR_NUMBER + 1);
	msg(GFARM_ERRMSG_BEGIN - 1);

	for (e = GFARM_ERRMSG_BEGIN; e < GFARM_ERRMSG_END; e++)
		msg(e);

	msg(GFARM_ERRMSG_END + 1);

	return (0);
}
