struct gfarm_id_table;

struct gfarm_id_table_entry_ops {
	size_t entry_size;
};

struct gfarm_id_table *gfarm_id_table_alloc(struct gfarm_id_table_entry_ops *);
void gfarm_id_table_free(struct gfarm_id_table *,
	void (*)(void *, gfarm_int32_t, void *), void *);

void gfarm_id_table_set_base(struct gfarm_id_table *, gfarm_int32_t);
void gfarm_id_table_set_limit(struct gfarm_id_table *, gfarm_int32_t);
void gfarm_id_table_set_initial_size(struct gfarm_id_table *, gfarm_int32_t);
void gfarm_id_table_foreach(struct gfarm_id_table *, void *,
	void (*)(void *, struct gfarm_id_table *, gfarm_int32_t, void *));

void *gfarm_id_alloc(struct gfarm_id_table *, gfarm_int32_t *);
void *gfarm_id_lookup(struct gfarm_id_table *, gfarm_int32_t);
int gfarm_id_free(struct gfarm_id_table *, gfarm_int32_t);
