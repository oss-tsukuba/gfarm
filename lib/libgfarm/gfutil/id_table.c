#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>

#include <gfarm/error.h>
#include <gfarm/gflog.h>
#include <gfarm/gfarm_misc.h>

#include "id_table.h"

#define ALIGNMENT 16
#define ALIGN_CEIL_BY(p, alignment)	\
	(((unsigned long)(p) + (alignment) - 1) & ~((alignment) - 1))
#define ALIGN_CEIL(p)	ALIGN_CEIL_BY(p, ALIGNMENT)

#define INITIAL_DELTA		10
#define DELTA_SHIFT		1

#define DEFAULT_ID_BASE		1

/* smaller than INT_MAX - (INT_MAX >> DELTA_SHIFT) */	
#define DEFAULT_ID_LIMIT	1000000000

struct gfarm_id_free_data {
	struct gfarm_id_free_data *next;
};

struct gfarm_id_data_chunk {
	struct gfarm_id_data_chunk *next;
};

struct gfarm_id_index {
	void *data;
	gfarm_int32_t id;
};

struct gfarm_id_table {
	struct gfarm_id_table_entry_ops *entry_ops;

	size_t entry_size;

	int id_base, id_next, id_limit;

	int idx_delta;
	int hole_start, hole_end, idxsize;
	int head_free, tail_free;
	struct gfarm_id_index *index;

	struct gfarm_id_free_data *free_data;
	struct gfarm_id_data_chunk *chunks;
};

struct gfarm_id_table *
gfarm_id_table_alloc(struct gfarm_id_table_entry_ops *entry_ops)
{
	struct gfarm_id_table *idtab;

	GFARM_MALLOC(idtab);
	if (idtab == NULL) {
		gflog_debug(GFARM_MSG_1000787,
			"allocation of 'gfarm_id_table' failed");
		return (NULL);
	}

	idtab->entry_ops = entry_ops;

	/* this assumes sizeof(struct gfarm_id_free_data) is power of 2 */
	if ((sizeof(struct gfarm_id_free_data) &
	     (sizeof(struct gfarm_id_free_data) - 1)) != 0) {
		gflog_fatal(GFARM_MSG_1003251,
		    "gfarm_id_table_alloc: unexpected struct size %zd",
		    sizeof(struct gfarm_id_free_data));
	}
	if (entry_ops->entry_size == 0)
		idtab->entry_size = sizeof(struct gfarm_id_free_data);
	else
		idtab->entry_size = ALIGN_CEIL_BY(entry_ops->entry_size,
		    sizeof(struct gfarm_id_free_data));

	idtab->id_next = 1;
	idtab->id_base = DEFAULT_ID_BASE;
	idtab->id_limit = DEFAULT_ID_LIMIT;

	idtab->idx_delta = INITIAL_DELTA;
	idtab->idxsize = idtab->hole_start = idtab->hole_end =
	    idtab->head_free = idtab->tail_free = 0;
	idtab->index = NULL;
	idtab->free_data = NULL;
	idtab->chunks = NULL;

	return (idtab);
}

void
gfarm_id_table_free(struct gfarm_id_table *idtab,
	void (*id_free)(void *, gfarm_int32_t, void *), void *closure)
{
	struct gfarm_id_index *index = idtab->index;
	int i;
	struct gfarm_id_data_chunk *p, *q;

	if (index != NULL) {
		if (id_free != NULL) {
			for (i = 0; i < idtab->hole_start; i++) {
				if (index[i].data != NULL)
					(*id_free)(closure,
					    index[i].id, index[i].data);
			}
			for (i = idtab->hole_end; i < idtab->idxsize; i++) {
				if (index[i].data != NULL)
					(*id_free)(closure,
					    index[i].id, index[i].data);
			}
		}
		free(index);
	}
	for (p = idtab->chunks; p != NULL; p = q) {
		q = p->next;
		free(p);
	}
	free(idtab);
}

void
gfarm_id_table_set_base(struct gfarm_id_table *idtab, gfarm_int32_t base)
{
	idtab->id_base = base;
}

void
gfarm_id_table_set_limit(struct gfarm_id_table *idtab, gfarm_int32_t limit)
{
	idtab->id_limit = limit;
}

void
gfarm_id_table_set_initial_size(struct gfarm_id_table *idtab, gfarm_int32_t sz)
{
	idtab->idx_delta = sz;
}

#if 0
int
gfarm_id_get_space_from_head(struct gfarm_id_table *idtab)
{
	struct gfarm_id_index *index = idtab->index;
	size_t i = idtab->hole_start;
	size_t avail, avail_len, space_len;

	if (idtab->head_free <= 0)
		return (0); /* no space */
	while (i > 0) {
		--i;
		if (index[i].data != NULL)
			continue;
		/* space found */
		avail = i + 1;
		avail_len = idtab->hole_start - avail;
		while (i > 0) {
			--i;
			if (index[i].data != NULL) {
				++i;
				break;
			}
		}
		memmove(&index[i], &index[avail],
		    avail_len * sizeof(index[0]));
		space_len = avail - i;
		idtab->hole_start -= space_len;
		idtab->head_free -= space_len;
		return (1); /* got space */
	}
	/* assert(0); */
	idtab->head_free = 0;
	return (0);
}
#endif

#if 0
int
gfarm_id_get_space_from_tail(struct gfarm_id_table *idtab)
{
	struct gfarm_id_index *index = idtab->index;
	size_t i = idtab->hole_end;
	size_t space, avail_len, space_len;

	if (idtab->tail_free <= 0)
		return (0); /* no space */
	for (; i < idtab->idxsize; i++) {
		if (index[i].data != NULL)
			continue;
		/* space found */
		space = i;
		avail_len = space - idtab->hole_end;
		for (++i; i < idtab->idxsize; i++) {
			if (index[i].data != NULL)
				break;
		}
		memmove(&index[i - avail_len], &index[idtab->hole_end],
		    avail_len * sizeof(index[0]));
		space_len = i - space;
#if 0
		assert(idtab->hole_end + space_len == i - avail_len);
#endif
		idtab->hole_end += space_len;
		idtab->tail_free -= space_len;
		return (1); /* got space */
	}
	/* assert(0); */
	idtab->tail_free = 0;
	return (0);
}
#endif

int
gfarm_id_compaction_from_head_force(struct gfarm_id_table *idtab)
{
	struct gfarm_id_index *index = idtab->index;
	size_t i, space, avail, avail_len;

	for (i = 0; i < idtab->hole_start; i++) {
		if (index[i].data == NULL)
			break;
	}
	if (i >= idtab->hole_start) {
		idtab->head_free = 0;
		return (0);
	}
	space = i++;
	while (i < idtab->hole_start) {
		for (; i < idtab->hole_start; i++) {	
			if (index[i].data != NULL)
				break;
		}
		avail = i;
		for (; i < idtab->hole_start; i++) {
			if (index[i].data == NULL)
				break;
		}
		avail_len = i - avail;
		memmove(&index[space], &index[avail],
		    avail_len * sizeof(index[0]));
		space += avail_len;
	}
	idtab->hole_start = space;
	idtab->head_free = 0;
	return (1);
}

int
gfarm_id_compaction_from_tail_force(struct gfarm_id_table *idtab)
{
	struct gfarm_id_index *index = idtab->index;
	size_t i, space, room, avail_len;

	/*
	 * note that idtab->hole_end may be 0,
	 * and condition like (i >= idtab->hole_end) always true in that case,
	 * because i (which is size_t) is unsigned.
	 */

	for (i = idtab->idxsize; i > idtab->hole_end; ) {
		--i;
		if (index[i].data == NULL)
			break;
	}
	if (i < idtab->hole_end || index[i].data != NULL) {
		idtab->tail_free = 0;
		return (0);
	}
	space = i;
	while (i > idtab->hole_end) {
		while (i > idtab->hole_end) {
			--i;
			if (index[i].data != NULL) {
				++i;
				break;
			}
		}
		room = i;
		while (i > idtab->hole_end) {
			--i;
			if (index[i].data == NULL) {
				++i;
				break;
			}
		}
		avail_len = room - i;
		space -= avail_len;
		memmove(&index[space + 1], &index[i],
		    avail_len * sizeof(index[0]));
	}
	idtab->hole_end = space + 1;
	idtab->tail_free = 0;
	return (1);
}

int
gfarm_id_compaction_from_head(struct gfarm_id_table *idtab)
{
	if (idtab->head_free <= 0)
		return (0); /* no room for compaction */
	return (gfarm_id_compaction_from_head_force(idtab));
}

int
gfarm_id_compaction_from_tail(struct gfarm_id_table *idtab)
{
	if (idtab->tail_free <= 0)
		return (0); /* no room for compaction */
	return (gfarm_id_compaction_from_tail_force(idtab));
}

void
gfarm_id_shrink_head(struct gfarm_id_table *idtab)
{
	while (idtab->hole_start > 0) {
		if (idtab->index[idtab->hole_start - 1].data != NULL)
			break;
		--idtab->hole_start;
		--idtab->head_free;
	}
}

void
gfarm_id_shrink_tail(struct gfarm_id_table *idtab)
{
	while (idtab->hole_end < idtab->idxsize) {
		if (idtab->index[idtab->hole_end].data != NULL)
			break;
		++idtab->hole_end;
		--idtab->tail_free;
	}
}

struct gfarm_id_index *
gfarm_id_bsearch(struct gfarm_id_table *idtab,
	int head, int tail, gfarm_int32_t id)
{
	struct gfarm_id_index *index = idtab->index;
	int mid;

	while (head < tail) {
		mid = (head + tail) >> 1;
		if (index[mid].id == id)
			return (&index[mid]);
		if (index[mid].id > id)
			tail = mid;
		else
			head = mid + 1;
			
	}
	return (NULL); /* not found */
}

int
gfarm_id_bsearch_next(struct gfarm_id_table *idtab,
	int head, int tail, gfarm_int32_t id)
{
	struct gfarm_id_index *index = idtab->index;
	int mid, tail_save = tail;

	while (head < tail) {
		mid = (head + tail) >> 1;
		if (index[mid].id == id)
			return (mid);
		if (index[mid].id > id)
			tail = mid;
		else
			head = mid + 1;
			
	}
	/* assert(head == tail); */
	if (head >= tail_save || index[head].id > id)
		return (head);
	return (head + 1);
}

void
gfarm_id_rewind(struct gfarm_id_table *idtab)
{
	int i;
	gfarm_int32_t id = idtab->id_base;

	if (idtab->hole_start > 0 &&
	    idtab->index[idtab->hole_start - 1].id >= id) {
		i = gfarm_id_bsearch_next(idtab, 0, idtab->hole_start, id);
		/* assert(i < idtab->hole_start); */
		for (; i < idtab->hole_start; i++) {
			if (idtab->index[i].data == NULL)
				break;
		}
		if (i < idtab->hole_start) {
			idtab->id_next = idtab->index[i].id;
			i = idtab->hole_start - 1 - i;
			memmove(&idtab->index[idtab->hole_end - i],
			    &idtab->index[idtab->hole_start - i],
			    i * sizeof(struct gfarm_id_index));
			idtab->hole_start -= i + 1;
			idtab->hole_end -= i;
			/* recalculate idtab->head_free and idtab->tail_free */
			gfarm_id_compaction_from_head_force(idtab);
			gfarm_id_compaction_from_tail_force(idtab);
			return;
		}
		id = idtab->index[idtab->hole_start - 1].id + 1;
	}
	if (idtab->hole_end < idtab->idxsize &&
	    idtab->index[idtab->hole_end].id <= id) {
		i = gfarm_id_bsearch_next(idtab, idtab->hole_end,
		    idtab->idxsize, id);
		/* assert(i >= idtab->hole_end); */
		for (; i < idtab->idxsize; i++) {
			if (idtab->index[i].data == NULL)
				break;
		}
		if (i < idtab->idxsize) {
			idtab->id_next = idtab->index[i].id;
			i = i - idtab->hole_end;
			memmove(&idtab->index[idtab->hole_start],
			    &idtab->index[idtab->hole_end],
			    i * sizeof(struct gfarm_id_index));
			idtab->hole_start += i;
			idtab->hole_end += i + 1;
			/* recalculate idtab->head_free and idtab->tail_free */
			gfarm_id_compaction_from_head_force(idtab);
			gfarm_id_compaction_from_tail_force(idtab);
			return;
		}
		/* mark that index table is full */
		id = idtab->id_limit;
	}
	idtab->id_next = id;
	/* no need to move idtab->hole_{start,end} */
}

void
gfarm_id_adjust_next(struct gfarm_id_table *idtab)
{
	gfarm_int32_t id = idtab->id_next;
	int i;

	if (id >= idtab->id_limit) {
		gfarm_id_rewind(idtab);
		return;
	}

	if (idtab->hole_end >= idtab->idxsize ||
	    id < idtab->index[idtab->hole_end].id)
		return; /* idtab->id_next is ok, no need to adjust */

	i = idtab->hole_end;
#if 0
	assert(idtab->index[i].id == id &&
	    idtab->index[i].data != NULL);
#endif
	++id;
	++i;
	while (i < idtab->idxsize) {
		if (idtab->index[i].id > id ||
		    idtab->index[i].data == NULL)
			break;
		++id;
		++i;
	}
	if (id >= idtab->id_limit) {
		gfarm_id_rewind(idtab);
		return;
	}
	idtab->id_next = id;
	i -= idtab->hole_end;
	memmove(&idtab->index[idtab->hole_start],
	    &idtab->index[idtab->hole_end],
	    i * sizeof(struct gfarm_id_index));
	idtab->hole_start += i;
	idtab->hole_end += i;
	gfarm_id_shrink_head(idtab);
	gfarm_id_shrink_tail(idtab);
}

void *
gfarm_id_alloc(struct gfarm_id_table *idtab, gfarm_int32_t *idp)
{
	struct gfarm_id_index *entry;

	if (idtab->id_next >= idtab->id_limit) {
		/* previous gfarm_id_rewind(idtab) failed, try again */
		gfarm_id_rewind(idtab);
		if (idtab->id_next >= idtab->id_limit) {
			gflog_debug(GFARM_MSG_1002407,
			    "gfarm_id_alloc: no more id space %d/%d",
			    idtab->id_next, idtab->id_limit);
			return (NULL); /* no more id space */
		}
	}
	if (idtab->hole_start >= idtab->hole_end &&
	    !gfarm_id_compaction_from_head(idtab) &&
	    !gfarm_id_compaction_from_tail(idtab)) {
		/* no space left for the new entry */
		struct gfarm_id_index *newidx;
		struct gfarm_id_data_chunk *data;
		char *p;
		int i;

		/* assert(idtab->hole_start == idtab->hole_end); */
		data = malloc(ALIGN_CEIL(sizeof(struct gfarm_id_data_chunk)) +
		    idtab->idx_delta * idtab->entry_size);
		if (data == NULL) {
			gflog_debug(GFARM_MSG_1002408,
			    "gfarm_id_alloc: no memory for %d * %d",
			    idtab->idx_delta, (int)idtab->entry_size);
			return (NULL);
		}
		newidx = realloc(idtab->index,
		    (idtab->idxsize + idtab->idx_delta) *
		    sizeof(struct gfarm_id_index));
		if (newidx == NULL) {
			free(data);
			gflog_debug(GFARM_MSG_1002409,
			    "gfarm_id_alloc: no memory for (%d + %d) * %d",
			    idtab->idxsize, idtab->idx_delta,
			    (int)sizeof(struct gfarm_id_index));
			return (NULL); /* no more memory */
		}

		/* link to idtab->chunk */
		data->next = idtab->chunks;
		idtab->chunks = data;

		/* link to idtab->free_data */
		p = (char *)data +
		    ALIGN_CEIL(sizeof(struct gfarm_id_data_chunk));
		for (i = 1; i < idtab->idx_delta; i++) {
			((struct gfarm_id_free_data *)p)->next =
			    (struct gfarm_id_free_data *)(p +
			    idtab->entry_size);
			p += idtab->entry_size;
		}
		((struct gfarm_id_free_data *)p)->next =
		    idtab->free_data;
		idtab->free_data = (struct gfarm_id_free_data *)((char *)data +
		    ALIGN_CEIL(sizeof(struct gfarm_id_data_chunk)));

		/* reconstruct idtab->index */
		memmove(&newidx[idtab->hole_end + idtab->idx_delta],
		    &newidx[idtab->hole_end],
		    (idtab->idxsize - idtab->hole_end) *
		    sizeof(struct gfarm_id_index));
		idtab->index = newidx;
		idtab->hole_end += idtab->idx_delta;

		/* make idtab->idx_delta big enough */
		idtab->idxsize += idtab->idx_delta;
		if ((idtab->idxsize >> DELTA_SHIFT) > idtab->idx_delta) {
			idtab->idx_delta = idtab->idxsize >> DELTA_SHIFT;
		}
	}

	entry = idtab->index + idtab->hole_start++;
	entry->id = idtab->id_next++;
	entry->data = idtab->free_data;
	idtab->free_data = idtab->free_data->next;

	gfarm_id_adjust_next(idtab);

	*idp = entry->id;
	return (entry->data);
}

void *
gfarm_id_lookup(struct gfarm_id_table *idtab, gfarm_int32_t id)
{
	struct gfarm_id_index *entry;

	entry = gfarm_id_bsearch(idtab, 0, idtab->hole_start, id);
	if (entry != NULL)
		return (entry->data);
	entry = gfarm_id_bsearch(idtab, idtab->hole_end, idtab->idxsize, id);
	if (entry != NULL)
		return (entry->data);
	return (NULL);
}

int
gfarm_id_alloc_at(struct gfarm_id_table *idtab, gfarm_int32_t id,
	void **entryp)
{
	void *entry = gfarm_id_lookup(idtab, id);
	gfarm_int32_t new_id;

	if (entry != NULL)
		return (EALREADY);
	idtab->id_next = id;
	entry = gfarm_id_alloc(idtab, &new_id);
	if (entry == NULL)
		return (ENOMEM);
	if (new_id != id) {
		gfarm_id_free(idtab, new_id);
		return (EINVAL);
	}

	*entryp = entry;
	return (0);
}

int
gfarm_id_free(struct gfarm_id_table *idtab, gfarm_int32_t id)
{
	struct gfarm_id_index *entry;
	struct gfarm_id_free_data *data;

	entry = gfarm_id_bsearch(idtab, 0, idtab->hole_start, id);
	if (entry != NULL) {
		data = entry->data;
		if (data == NULL)
			return (0); /* the data already freed */
		entry->data = NULL;
		++idtab->head_free;
		data->next = idtab->free_data;
		idtab->free_data = data;
		if (entry < &idtab->index[idtab->hole_start - 1])
			return (1);
		/* end of head, shrink head from the end edge */
		--idtab->hole_start;
		--idtab->head_free;
		gfarm_id_shrink_head(idtab);
		return (1);
	}
	entry = gfarm_id_bsearch(idtab, idtab->hole_end, idtab->idxsize, id);
	if (entry != NULL) {
		data = entry->data;
		if (data == NULL)
			return (0); /* the data already freed */
		entry->data = NULL;
		++idtab->tail_free;
		data->next = idtab->free_data;
		idtab->free_data = data;
		if (entry > &idtab->index[idtab->hole_end])
			return (1);
		/* beginning of tail, shrink tail from the beginning edge */
		++idtab->hole_end;
		--idtab->tail_free;
		gfarm_id_shrink_tail(idtab);
		return (1);
	}
	return (0); /* the data not found */
}

#ifdef TEST
#include <stdio.h>

main()
{
	int len;
	char buffer[1024], command[sizeof(buffer)];
	struct gfarm_id_table *id_table = NULL;
	struct gfarm_id_table_entry_ops ops = { 256 };
	gfarm_int32_t n;
	void *p;

	while (fgets(buffer, sizeof(buffer), stdin) != NULL) {
		len = strlen(buffer);
		if (len > 0 && buffer[len - 1] == '\n')
			buffer[len - 1] = '\0';
		if (sscanf(buffer, "%s", command) != 1)
			continue;
		if (strcmp(command, "table_alloc") == 0) {
			if (id_table != NULL) {
				fprintf(stderr, "table already alloced\n");
			} else {
				id_table = gfarm_id_table_alloc(&ops);
				if (id_table == NULL)
					fprintf(stderr, "table alloc failed\n");
			}
		} else if (strcmp(command, "table_free") == 0) {
			gfarm_id_table_free(id_table);
		} else if (strcmp(command, "base") == 0) {
			if (sscanf(buffer, "%*s %d", &n) != 1) {
				fprintf(stderr, "Usage: base <base>\n");
			} else {
				gfarm_id_table_set_base(id_table, n);
			}
		} else if (strcmp(command, "limit") == 0) {
			if (sscanf(buffer, "%*s %d", &n) != 1) {
				fprintf(stderr, "Usage: limit <limit>\n");
			} else {
				gfarm_id_table_set_limit(id_table, n);
			}
		} else if (strcmp(command, "alloc") == 0) {
			if ((p = gfarm_id_alloc(id_table, &n)) == NULL)
				fprintf(stderr, "alloc failed\n");
			else
				printf("alloced id=%d, p=%p\n", n, p);
		} else if (strcmp(command, "lookup") == 0) {
			if (sscanf(buffer, "%*s %d", &n) != 1) {
				fprintf(stderr, "Usage: lookup <id>\n");
			} else if ((p = gfarm_id_lookup(id_table, n)) == NULL){
				fprintf(stderr, "lookup %d failed\n", n);
			} else {
				printf("found id=%d, p=%p\n", n, p);
			}
		} else if (strcmp(command, "free") == 0) {
			if (sscanf(buffer, "%*s %d", &n) != 1) {
				fprintf(stderr, "Usage: free <id>\n");
			} else if (!gfarm_id_free(id_table, n)){
				fprintf(stderr, "free %d failed\n", n);
			} else {
				printf("freed id=%d\n", n);
			}
		} else {
			fprintf(stderr, "Unknown command %s\n", command);
		}
	}
}
#endif
