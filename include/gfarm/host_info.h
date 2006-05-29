/*
 * GFarm host table:
 * (hostname) -> (architecture, ncpu)
 */

struct gfarm_host_info {
	char *hostname;
	int port;

	int nhostaliases;
	char **hostaliases;
	char *architecture;
	int ncpu;
	int flags;
};
#define GFARM_HOST_INFO_NCPU_NOT_SET	(-1)
#define GFARM_HOST_INFO_FLAGS_HAS_DISK	0x00000001

void gfarm_host_info_free(struct gfarm_host_info *);
void gfarm_host_info_free_all(int, struct gfarm_host_info *);
