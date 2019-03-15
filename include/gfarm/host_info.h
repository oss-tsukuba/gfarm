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

/* GFM_PROTO_HOST_INFO_{SET,GET_*} request and result flags */
#define GFARM_HOST_INFO_FLAG_READONLY	0x00000001 /* since 2.7.13  */

int host_info_flags_is_readonly(int flags);
int host_info_is_readonly(struct gfarm_host_info *);
void gfarm_host_info_free(struct gfarm_host_info *);
void gfarm_host_info_free_all(int, struct gfarm_host_info *);
