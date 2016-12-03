struct dirset;

/*
 * there are some functions which never use NULL as struct dirset *,
 * but use TDIRSET_IS_UNKNOWN, TDIRSET_NOT_SET and an actual dirset.
 * those functions are using `tdirset' (three-kinds-of-valued dirset) as
 * there variable names.
 *
 * TDIRSET_IS_UNKNOWN: it's not known whether dirset is set or not
 * TDIRSET_IS_NOT_SET: it's known that dirset is not set
 * otherwise: it's an actual dirset
 */
extern struct dirset dirset_is_not_set;
#define TDIRSET_IS_NOT_SET (&dirset_is_not_set)
#define TDIRSET_IS_UNKNOWN NULL

int dirset_is_valid(struct dirset *);
const char *dirset_get_username(struct dirset *);
const char *dirset_get_dirsetname(struct dirset *);
struct dirquota;
struct dirquota *dirset_get_dirquota(struct dirset *);
struct quota_metadata;
void dirset_set_quota_metadata_in_cache(struct dirset *,
	const struct quota_metadata *);

/* for functions which refer dirset across giant lock */
void dirset_add_ref(struct dirset *);
int dirset_del_ref(struct dirset *);

/* private interface between quota_dir and dirset */
struct quota_dir;
void dirset_add_dir(struct dirset *, struct quota_dir **);
void dirset_remove_dir(struct dirset *);

void dirset_init(void);

int dirset_foreach_interruptible(void *, int (*)(void *, struct dirset *));
int dirset_foreach_quota_dir_interruptible(struct dirset *,
	void *, int (*)(void *, struct quota_dir *));
gfarm_error_t xattr_list_set_by_dirset(struct xattr_list *,
	const char *, struct dirset *, const char *);

struct dirsets;
struct dirsets *dirsets_new(void);
struct user;
gfarm_error_t dirset_enter(struct dirsets *, const char *, struct user *, int,
	struct dirset **);
struct dirset *dirset_lookup(struct dirsets *, const char *);
gfarm_error_t dirset_remove(struct dirsets *, const char *);

gfarm_error_t gfm_server_dirset_info_set(struct peer *, int, int);
gfarm_error_t gfm_server_dirset_info_remove(struct peer *, int, int);
gfarm_error_t gfm_server_dirset_info_list(struct peer *, int, int);
gfarm_error_t quota_dirset_put_reply(struct peer *, struct dirset *,
	const char *);
gfarm_error_t gfm_server_quota_dirset_get(struct peer *, int, int);
gfarm_error_t gfm_server_quota_dirset_set(struct peer *, int, int);
gfarm_error_t gfm_server_dirset_dir_list(struct peer *, int, int);
