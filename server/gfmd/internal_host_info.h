/*
 * $Id$
 */
/*
 * A host entry for inside the gfsd (extended with replication group).
 */

struct gfarm_internal_host_info {
	struct gfarm_host_info hi;

	char *fsngroupname;
};

extern const struct gfarm_base_generic_info_ops
	gfarm_base_internal_host_info_ops;

void gfarm_internal_host_info_free(struct gfarm_internal_host_info *);
void gfarm_internal_host_info_free_all(int, struct gfarm_internal_host_info *);
void gfarm_internal_host_info_free_except_hostname(
    struct gfarm_internal_host_info *);
