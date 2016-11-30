struct uint64_to_uint64_map;
struct uint64_to_uint64_map *uint64_to_uint64_map_new(void);
void uint64_to_uint64_map_free(struct uint64_to_uint64_map *);
gfarm_uint64_t uint64_to_uint64_map_size(struct uint64_to_uint64_map *);

struct uint64_to_uint64_map_entry;
gfarm_uint64_t uint64_to_uint64_map_entry_key(
	struct uint64_to_uint64_map_entry *);
gfarm_uint64_t uint64_to_uint64_map_entry_value(
	struct uint64_to_uint64_map_entry *);

int uint64_to_uint64_map_inc_value(struct uint64_to_uint64_map *,
	gfarm_uint64_t, gfarm_uint64_t *);
int uint64_to_uint64_map_foreach(struct uint64_to_uint64_map *,
	void *, int (*)(void *, struct uint64_to_uint64_map_entry *));
