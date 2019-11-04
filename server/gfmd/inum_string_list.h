/*
 * inum_string_list
 */

struct inum_string_list_entry;

int inum_string_list_add(struct inum_string_list_entry **,
	gfarm_ino_t, char *);
void inum_string_list_free(struct inum_string_list_entry **);
void inum_string_list_foreach(struct inum_string_list_entry *,
	void (*)(void *, gfarm_ino_t, const char *), void *);
