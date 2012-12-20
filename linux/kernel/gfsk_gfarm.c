#include <unistd.h>
#include <gfarm/gfarm.h>
#include <gfarm/gfarm_config.h>
#include "context.h"
#include "config.h"
#include "ug_idmap.h"

int
gfsk_gfarm_init(uid_t uid)
{
	gfarm_error_t e;

	e = gfarm_context_init();
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNFIXED,
			"gfarm_context_init failed: %s",
			gfarm_error_string(e));
		goto out;
	}
	e = gfarm_set_local_user_for_this_uid(uid);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNFIXED,
			"gfarm_set_local_user_for_this_uid failed: %s",
			gfarm_error_string(e));
		goto out;
	}
	gflog_initialize();
	e = gfarm_config_read();
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "gfarm_config_read() failed: %s", gfarm_error_string(e));
		goto out;
	}
out:
	return (-gfarm_error_to_errno(e));
}

void
gfsk_gfarm_fini(void)
{
	gfarm_context_term();
	gfarm_ctxp = NULL;
}

char *
gfarm_get_local_username(void)
{
	static char nobody[] = "Nobody";

	if (!gfsk_task_ctxp->gk_uname[0]
		&& (ug_map_uid_to_name(getuid(),
			gfsk_task_ctxp->gk_uname,
			sizeof(gfsk_task_ctxp->gk_uname)) < 0)) {
		return (nobody);
	}

	return (gfsk_task_ctxp->gk_uname);
}
