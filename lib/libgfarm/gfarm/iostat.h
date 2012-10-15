gfarm_error_t gfarm_iostat_mmap(char *path,  struct gfarm_iostat_spec *specp,
		unsigned int nitem, unsigned int row);
void gfarm_iostat_clear_id(gfarm_uint64_t id, unsigned int hint);
void gfarm_iostat_clear_ip(struct gfarm_iostat_items *ip);
struct gfarm_iostat_items *gfarm_iostat_find_space(unsigned int hint);
struct gfarm_iostat_items *gfarm_iostat_get_ip(unsigned int i);
void gfarm_iostat_set_id(struct gfarm_iostat_items *ip, gfarm_uint64_t id);
void gfarm_iostat_set_local_ip(struct gfarm_iostat_items *ip);
void gfarm_iostat_stat_add(struct gfarm_iostat_items *ip,
			unsigned int cat, int val);
void gfarm_iostat_local_add(unsigned int cat, int val);
