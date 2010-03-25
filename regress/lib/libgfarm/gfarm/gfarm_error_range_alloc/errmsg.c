#include <stdio.h>
#include <stdlib.h>

#include <gfarm/gfarm.h>

#include "liberror.h"

#define PRIVATE_ERR_BEGIN	(GFARM_ERR_PRIVATE_BEGIN + 0)

static const char *private_error_messages[] = {
#define PRIVATE_ERR_HOGE	(PRIVATE_ERR_BEGIN + 0)
	"hoge",
#define PRIVATE_ERR_PIYO	(PRIVATE_ERR_BEGIN + 1)
	"piyo",
#define PRIVATE_ERR_CHOME	(PRIVATE_ERR_BEGIN + 2)
	"chome"
};

#define PRIVATE_ERR_END		(PRIVATE_ERR_BEGIN + \
				GFARM_ARRAY_LENGTH(private_error_messages) - 1)

const char *
private_error_string(void *cookie, int e)
{
	if (e < PRIVATE_ERR_BEGIN || e > PRIVATE_ERR_END)
		return ("private_error_string: internal error");
	else
		return (private_error_messages[e - PRIVATE_ERR_BEGIN]);
}

gfarm_error_t
private_initialize(int *argcp, char ***argvp)
{
	gfarm_error_t e;
	struct gfarm_error_domain *private_error_domain;

	e = gfarm_initialize(argcp, argvp);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfarm_initialize: %s\n",
		    gfarm_error_string(e));
		return (e);
	}

	e = gfarm_error_range_alloc(PRIVATE_ERR_BEGIN, PRIVATE_ERR_END,
	    private_error_string, NULL, &private_error_domain);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfarm_error_range_alloc: %s\n",
		    gfarm_error_string(e));
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

int
main(int argc, char **argv)
{
	gfarm_error_t e;

	e = private_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "private_initialize: %s\n",
		    gfarm_error_string(e));
		return (1);
	}

	if (argc != 2) {
		fprintf(stderr, "Usage: errmsg <number>\n");
		return (2);
	}
	printf("%s\n", gfarm_error_string(atoi(argv[1])));

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfarm_terminate: %s\n",
		    gfarm_error_string(e));
		return (2);
	}
	return (0);
}
