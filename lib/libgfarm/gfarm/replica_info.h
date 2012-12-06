/*
 * $Id$
 */

#define GFARM_REPLICAINFO_XATTR_TYPE	"replicainfo"
#define GFARM_REPLICAINFO_XATTR_NAME	"gfarm." GFARM_REPLICAINFO_XATTR_TYPE

struct gfarm_replicainfo;
typedef struct gfarm_replicainfo *gfarm_replicainfo_t;

size_t gfarm_replicainfo_parse(const char *, gfarm_replicainfo_t **);
void gfarm_replicainfo_free(gfarm_replicainfo_t);
const char *gfarm_replicainfo_group(gfarm_replicainfo_t);
size_t gfarm_replicainfo_amount(gfarm_replicainfo_t);
char *gfarm_replicainfo_stringify(gfarm_replicainfo_t *, size_t);
size_t gfarm_replicainfo_reduce(const char *, gfarm_replicainfo_t **);

int gfarm_replicainfo_validate(const char *);
