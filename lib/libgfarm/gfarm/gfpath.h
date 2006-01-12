/* Globus & SSL/TLS related pathnames */

#ifndef GRID_CONF_DIR
#define GRID_CONF_DIR		"/etc/grid-security"
#endif
#ifndef GRID_CACERT_DIR
#define GRID_CACERT_DIR		GRID_CONF_DIR "/certificates"
#endif
#ifndef GRID_MAPFILE
#define GRID_MAPFILE		GRID_CONF_DIR "/grid-mapfile"
#endif

/* Gfarm related pathnames */

#ifndef GFARM_CONFIG
#define GFARM_CONFIG		"/etc/gfarm.conf"
#endif
#ifndef GFARM_CLIENT_RC
#define GFARM_CLIENT_RC		".gfarmrc"
#endif
#ifndef GFARM_SPOOL_ROOT
#define GFARM_SPOOL_ROOT	"/var/spool/gfarm"
#endif
