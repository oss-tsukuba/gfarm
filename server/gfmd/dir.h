#define USE_HASH 0

#if USE_HASH

#include "hash.h"

typedef struct gfarm_hash_table *Dir;
typedef struct gfarm_hash_entry *DirEntry;
typedef struct gfarm_hash_iterator DirCursor;

#else /* ! USE_HASH */

/* red-black tree */
typedef struct rbdir *Dir;
typedef struct rbdir_entry *DirEntry;
typedef DirEntry DirCursor;

#endif /* ! USE_HASH */

struct inode;

Dir dir_alloc(void);
void dir_free(Dir);
int dir_is_empty(Dir);
gfarm_off_t dir_get_entry_count(Dir);

DirEntry dir_enter(Dir, const char *, int, int *);
DirEntry dir_lookup(Dir, const char *, int);
int dir_remove_entry(Dir, const char *, int);

void dir_entry_set_inode(DirEntry, struct inode *);
struct inode *dir_entry_get_inode(DirEntry);
char *dir_entry_get_name(DirEntry, int *);

int dir_cursor_lookup(Dir, const char *, int, DirCursor *);
int dir_cursor_next(Dir, DirCursor *);
int dir_cursor_remove_entry(Dir, DirCursor *);
int dir_cursor_set_pos(Dir, gfarm_off_t, DirCursor *);
gfarm_off_t dir_cursor_get_pos(Dir, DirCursor *);
DirEntry dir_cursor_get_entry(Dir, DirCursor *);

/* utility functions */

gfarm_error_t dir_cursor_get_name_and_inode(Dir, DirCursor *,
	char **, struct inode **);

#define DOT_LEN		1
#define DOTDOT_LEN	2
extern const char DOT[];
extern const char DOTDOT[];
int name_is_dot_or_dotdot(const char *, int);
int string_is_dot_or_dotdot(const char *);

/*
 * the following should belong to inode.h, really.
 * the reason why this is put here is to avoid to #include "dir.h"
 * in *.c files which need inode.h, but don't really need dir.h.
 */
Dir inode_get_dir(struct inode *);
