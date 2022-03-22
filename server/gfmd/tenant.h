struct tenant;
struct gfarm_hashtab;

struct tenant *tenant_lookup(const char *);
gfarm_error_t tenant_lookup_or_enter(const char *, struct tenant **);
const char *tenant_name(struct tenant *);
int tenant_needs_chroot(struct tenant *);
struct gfarm_hash_table **tenant_user_hashtab_ref(struct tenant *);
struct gfarm_hash_table **tenant_group_hashtab_ref(struct tenant *);
struct tenant *tenant_default(void);

void tenant_init(void);
