#include <stdlib.h>

#include <gfarm/gfarm.h>

char *
gfarm_url_fragment_number(const char *gfarm_url, int *np)
{
	char *e, *gfarm_file;
	struct gfarm_path_info pi;

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL)
		return (e);
	e = gfarm_path_info_get(gfarm_file, &pi);
	free(gfarm_file);
	if (e != NULL)
		return (e);
	if (!GFARM_S_IS_FRAGMENTED_FILE(pi.status.st_mode)) {
		gfarm_path_info_free(&pi);
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	}
	*np = pi.status.st_nsections;
	gfarm_path_info_free(&pi);
	return (NULL);
}
