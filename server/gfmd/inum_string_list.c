#include <stdlib.h>

#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

struct inum_string_list_entry {
	struct inum_string_list_entry *next;

	gfarm_ino_t inum;
	char *string;
};

int
inum_string_list_add(struct inum_string_list_entry **listp,
	gfarm_ino_t inum, char *string)
{
	struct inum_string_list_entry *entry;

	GFARM_MALLOC(entry);
	if (entry == NULL)
		return (0);

	entry->inum = inum;
	entry->string = string;

	entry->next = *listp;
	*listp = entry;
	return (1);
}

void
inum_string_list_free(struct inum_string_list_entry **listp)
{
	struct inum_string_list_entry *entry, *next;

	for (entry = *listp; entry != NULL; entry = next) {
		next = entry->next;
		free(entry->string);
		free(entry);
	}
	*listp = NULL;
}

void
inum_string_list_foreach(struct inum_string_list_entry *list,
	void (*op)(void *, gfarm_ino_t, const char *), void *closure)
{
	struct inum_string_list_entry *entry, *next;

	for (entry = list; entry != NULL; entry = next) {
		next = entry->next;
		(*op)(closure, entry->inum, entry->string);
	}
}
