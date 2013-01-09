#include <stddef.h>
#include <gfarm/gfarm.h>

int
gfs_mode_to_type(gfarm_mode_t mode)
{
	return (GFARM_S_ISDIR(mode) ? GFS_DT_DIR :
		GFARM_S_ISREG(mode) ? GFS_DT_REG :
		GFARM_S_ISLNK(mode) ? GFS_DT_LNK :
		GFS_DT_UNKNOWN);
}
