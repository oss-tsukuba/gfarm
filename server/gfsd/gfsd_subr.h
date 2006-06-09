/* need #include <gfarm/gfarm_config.h> to see HAVE_GETLOADAVG */

#ifndef HAVE_GETLOADAVG
int getloadavg(double *, int);
#endif

int gfsd_statfs(char *, gfarm_int32_t *,
	file_offset_t *, file_offset_t *, file_offset_t *,
	file_offset_t *, file_offset_t *, file_offset_t *);
