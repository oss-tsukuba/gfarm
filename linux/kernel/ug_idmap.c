/*
 *  fs/ugidmap/ug_idmap.c
 *
 *  Mapping of UID/GIDs to name and vice versa.
 *
 */

#include <linux/module.h>
#include <linux/init.h>

#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/cache.h>
#include <linux/sunrpc/svcauth.h>
#include <linux/list.h>
#include <linux/time.h>
#include <linux/seq_file.h>
#include <linux/in.h>
#include <gfarm/gflog.h>
#include "ug_idmap.h"
#include "stdlib.h"

/* Common entry handling */

#define ENT_HASHBITS	  8
#define ENT_HASHMAX	   (1 << ENT_HASHBITS)
#define ENT_HASHMASK	  (ENT_HASHMAX - 1)

static void
warn_no_idmapd(struct cache_detail *detail, int has_died)
{
	gflog_error(GFARM_MSG_1005009, "ugidmap: ID mapping failed: "
			"has idmapd %s?", has_died ? "died" : "started");
}

/*
 * Deferred request handling
 */

struct idmap_defer_req {
	struct cache_req		req;
	struct cache_deferred_req deferred_req;
	struct completion completion;
	atomic_t			count;
};

static int
idmap_cache_wait_for_upcall(struct idmap_defer_req *dreq, int to)
{
	if (wait_for_completion_timeout(&dreq->completion, to * HZ) == 0)
			return (-ETIMEDOUT);
	return (0);
}
static inline void
put_mdr(struct idmap_defer_req *mdr)
{
	if (atomic_dec_and_test(&mdr->count))
		kfree(mdr);
}

static inline void
get_mdr(struct idmap_defer_req *mdr)
{
	atomic_inc(&mdr->count);
}

static void
idmap_revisit(struct cache_deferred_req *dreq, int toomany)
{
	struct idmap_defer_req *mdr =
		container_of(dreq, struct idmap_defer_req, deferred_req);

	complete_all(&mdr->completion);
	put_mdr(mdr);
}

static struct cache_deferred_req *
idmap_defer(struct cache_req *req)
{
	struct idmap_defer_req *mdr =
		container_of(req, struct idmap_defer_req, req);

	mdr->deferred_req.revisit = idmap_revisit;
	get_mdr(mdr);
	return (&mdr->deferred_req);
}
static struct idmap_defer_req*
idmap_cache_defer_req_alloc(void)
{
	struct idmap_defer_req *mdr;

	mdr = kzalloc(sizeof(*mdr), GFP_KERNEL);
	if (!mdr)
		return (NULL);
	init_completion(&mdr->completion);
	atomic_set(&mdr->count, 1);
	mdr->req.defer = idmap_defer;
	return (mdr);
}

/*
 * HOSTADDR Cache hentry
 */

static unsigned int hostaddr_timeout_sec = 1;

struct hent {
	struct cache_head h;
	char	      name[UG_IDMAP_NAMESZ];
	char	      *alias[IDMAP_HOSTNAME_MAX + 1];
	struct in_addr addr[IDMAP_HOSTADDR_MAX + 1];
	char		buf[1];
};
#define HOSTADDR_BUFSIZ (PAGE_SIZE - sizeof(struct hent));

static void
hent_alias_copy(struct hent *ent, char **alias, char *buf)
{
	int	i;

	for (i = 0; i < IDMAP_HOSTNAME_MAX && ent->alias[i]; i++, alias++) {
		*alias = buf;
		strcpy(buf, ent->alias[i]);
		buf += strlen(buf) + 1;
	}
	*alias = NULL;
}
static void
hent_init(struct cache_head *cnew, struct cache_head *citm)
{
	struct hent *new = container_of(cnew, struct hent, h);
	struct hent *itm = container_of(citm, struct hent, h);

	strlcpy(new->name, itm->name, sizeof(new->name));
	memcpy(new->addr, itm->addr, sizeof(new->addr));
	hent_alias_copy(itm, new->alias, new->buf);
}

static void
hent_put(struct kref *ref)
{
	struct hent *map = container_of(ref, struct hent, h.ref);
	kfree(map);
}

static struct cache_head *
hent_alloc(void)
{
	struct hent *e = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (e) {
		memset(e, 0, sizeof(*e));
		return (&e->h);
	} else
		return (NULL);
}

/*-------------------------------------------------------------------*/
/*
 * HOST -> ADDR cache
 */

static struct cache_head *hostaddr_table[ENT_HASHMAX];

static inline int
hostaddr_hash(struct hent *ent)
{
	return (hash_str(ent->name, ENT_HASHBITS));
}

static void
hostaddr_request(struct cache_detail *cd, struct cache_head *ch, char **bpp,
	int *blen)
{
	struct hent *ent = container_of(ch, struct hent, h);

	qword_add(bpp, blen, ent->name);

	(*bpp)[-1] = '\n';
}

static int
hostaddr_upcall(struct cache_detail *cd, struct cache_head *ch)
{
	return (sunrpc_cache_pipe_upcall(cd, ch, hostaddr_request));
}

static int
hostaddr_match(struct cache_head *ca, struct cache_head *cb)
{
	struct hent *a = container_of(ca, struct hent, h);
	struct hent *b = container_of(cb, struct hent, h);

	return (strcmp(a->name, b->name) == 0);
}

static int
hostaddr_show(struct seq_file *m, struct cache_detail *cd, struct cache_head *h)
{
	struct hent *ent;

	if (h == NULL) {
		seq_puts(m, "#domain type name [id]\n");
		return (0);
	}
	ent = container_of(h, struct hent, h);
	seq_printf(m, "%s", ent->name);
	if (test_bit(CACHE_VALID, &h->flags)) {
		int i;
		for (i = 0; i < IDMAP_HOSTNAME_MAX && ent->alias[i]; i++)
			seq_printf(m, " %s", ent->alias[i]);
		seq_printf(m, " =");
		for (i = 0; i < IDMAP_HOSTADDR_MAX && ent->addr[i].s_addr;
			i++) {
			seq_printf(m, " 0x%x", ent->addr[i].s_addr);
		}
	}
	seq_printf(m, "\n");
	return (0);
}

static struct hent *hostaddr_lookup(struct hent *);
static struct hent *hostaddr_update(struct hent *, struct hent *);
static int hostaddr_parse(struct cache_detail *, char *, int);

static struct cache_detail hostaddr_cache = {
	.owner		= THIS_MODULE,
	.hash_size	= ENT_HASHMAX,
	.hash_table	= hostaddr_table,
	.name		= IDMAP_HOSTADDR,
	.cache_put	= hent_put,
	.cache_upcall	= hostaddr_upcall,
	.cache_parse	= hostaddr_parse,
	.cache_show	= hostaddr_show,
	.warn_no_listener = warn_no_idmapd,
	.match		= hostaddr_match,
	.init		= hent_init,
	.update		= hent_init,
	.alloc		= hent_alloc,
};

static int
hostaddr_parse(struct cache_detail *cd, char *buf, int buflen)
{
	struct hent *ent, *res;
	char *bp, *ep = buf + buflen;
	int error = -EINVAL;
	int i, blen;
	char abuf[128];

	if (buf[buflen - 1] != '\n')
		return (-EINVAL);
	buf[buflen - 1] = '\0';


	ent = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (ent == NULL)
		return (-ENOMEM);

	memset(ent, 0, sizeof(*ent));

	/* Name */
	error = qword_get(&buf, ent->name, sizeof(ent->name));
	if (error <= 0)
		goto out;

	/* expiry */
	if (!(ent->h.expiry_time = get_expiry(&buf))) {
		error = -EINVAL;
		goto out;
	}

	abuf[0] = 0;

	for (i = 0; i < IDMAP_HOSTADDR_MAX && buf < ep; i++) {
		error = qword_get(&buf, abuf, sizeof(abuf));
		if (error < 1)
			goto neg;
		if (error == 1 && abuf[0] == '=')
			break;
		if (!in4_pton(abuf, error, (u8 *)&ent->addr[i].s_addr,
			'\0', NULL)) {
			ent->addr[i].s_addr = 0;
			goto neg;
		}
	}
	if (abuf[0] != '=')
		goto neg;

	/* alias */
	bp = ent->buf;
	blen = HOSTADDR_BUFSIZ;
	for (i = 0; i < IDMAP_HOSTNAME_MAX && buf < ep; i++) {
		error = qword_get(&buf, bp, blen);
		if (error < 1)
			break;
		ent->alias[i] = bp;
		bp += error + 1;
		blen -= error + 1;
	}
	goto ok;

neg:
	set_bit(CACHE_NEGATIVE, &ent->h.flags);
ok:
	error = -ENOMEM;
	res = hostaddr_lookup(ent);

	if (res == NULL)
		goto out;

	res = hostaddr_update(ent, res);
	if (res == NULL)
		goto out;

	cache_put(&res->h, &hostaddr_cache);
	error = 0;
out:
	kfree(ent);

	return (error);
}


static struct hent *
hostaddr_lookup(struct hent *item)
{
	struct cache_head *ch = sunrpc_cache_lookup(&hostaddr_cache,
				&item->h, hostaddr_hash(item));
	if (ch)
		return (container_of(ch, struct hent, h));
	else
		return (NULL);
}

static struct hent *
hostaddr_update(struct hent *new, struct hent *old)
{
	struct cache_head *ch = sunrpc_cache_update(&hostaddr_cache,
				&new->h, &old->h, hostaddr_hash(new));
	if (ch)
		return (container_of(ch, struct hent, h));
	else
		return (NULL);
}
static int
hostaddr_cache_lookup(struct hent *key,  struct hent **item,
		struct idmap_defer_req *mdr)
{
	int ret = -ENOMEM;
	*item = hostaddr_lookup(key);
	if (*item) {
		ret = cache_check(&hostaddr_cache, &(*item)->h, &mdr->req);
		if (ret)
			*item = NULL;
	}
	return (ret);
}
static int
hostaddr_lookup_nowait(struct hent *key,
		struct hent **item)
{
	int ret = -ENOMEM;

	*item = hostaddr_lookup(key);
	if (!*item)
		goto out_err;
	ret = -ETIMEDOUT;
	if (!cache_valid(&(*item)->h))
		goto out_put;
	ret = -ENOENT;
	if (test_bit(CACHE_NEGATIVE, &(*item)->h.flags))
		goto out_put;
	return (0);
out_put:

	cache_put(&(*item)->h, &hostaddr_cache);
out_err:
	*item = NULL;
	return (ret);
}
static int
hostaddr_lookup_wait(struct hent *key,  struct hent **item)
{
	struct idmap_defer_req *mdr;
	int ret;

	mdr = idmap_cache_defer_req_alloc();
	if (!mdr)
		return (-ENOMEM);
	ret = hostaddr_cache_lookup(key, item, mdr);
	if (ret == -EAGAIN) {
		ret = idmap_cache_wait_for_upcall(mdr, hostaddr_timeout_sec);
		if (!ret)
			ret = hostaddr_lookup_nowait(key, item);
	}
	put_mdr(mdr);
	return (ret);
}
int
ug_map_hostaddr_get(char *name, int (*cb)(void *, char *,
		struct in_addr *, char **), void *arg)
{
	struct hent *item, key;
	int ret;

	if (strlen(name) >= sizeof(key.name)) {
		return (-EINVAL);
	}

	memset(&key, 0, sizeof(key));
	strcpy(key.name, name);

	ret = hostaddr_lookup_wait(&key, &item);
	if (!ret) {
		ret = cb(arg, name, item->addr, item->alias);
		cache_put(&item->h, &hostaddr_cache);
	}

	return (ret);
}
EXPORT_SYMBOL(ug_map_hostaddr_get);
/*-------------------------------------------------------------------*/
/*
 * Cache entry
 */

/*
 * XXX we know that UG_IDMAP_NAMESZ < PAGE_SIZE, but it's ugly to rely on
 * that.
 */

#define UG_IDMAP_TYPE_USER  0
#define UG_IDMAP_TYPE_GROUP 1

unsigned int ug_timeout_sec = 1;

struct ent {
	struct cache_head h;
	int	type;		/* User / Group */
	uid_t	id;
	char	name[UG_IDMAP_NAMESZ];
};

static void
ent_init(struct cache_head *cnew, struct cache_head *citm)
{
	struct ent *new = container_of(cnew, struct ent, h);
	struct ent *itm = container_of(citm, struct ent, h);

	new->id = itm->id;
	new->type = itm->type;

	strlcpy(new->name, itm->name, sizeof(new->name));
}

static void
ent_put(struct kref *ref)
{
	struct ent *map = container_of(ref, struct ent, h.ref);
	kfree(map);
}

static struct cache_head *
ent_alloc(void)
{
	struct ent *e = kmalloc(sizeof(*e), GFP_KERNEL);
	if (e)
		return (&e->h);
	else
		return (NULL);
}

/*-------------------------------------------------------------------*/
/*
 * ID -> Name cache
 */

static struct cache_head *idtoname_table[ENT_HASHMAX];

static uint32_t
idtoname_hash(struct ent *ent)
{
	uint32_t hash;

	hash = hash_long(ent->id, ENT_HASHBITS);

	/* Flip LSB for user/group */
	if (ent->type == UG_IDMAP_TYPE_GROUP)
		hash ^= 1;

	return (hash);
}

static void
idtoname_request(struct cache_detail *cd, struct cache_head *ch, char **bpp,
	int *blen)
{
	struct ent *ent = container_of(ch, struct ent, h);
	char idstr[11];

	snprintf(idstr, sizeof(idstr), "%u", ent->id);
	qword_add(bpp, blen,
		(ent->type == UG_IDMAP_TYPE_GROUP ? "group" : "user"));
	qword_add(bpp, blen, idstr);

	(*bpp)[-1] = '\n';
}

static int
idtoname_upcall(struct cache_detail *cd, struct cache_head *ch)
{
	return (sunrpc_cache_pipe_upcall(cd, ch, idtoname_request));
}

static int
idtoname_match(struct cache_head *ca, struct cache_head *cb)
{
	struct ent *a = container_of(ca, struct ent, h);
	struct ent *b = container_of(cb, struct ent, h);

	return (a->id == b->id && a->type == b->type);
}

static int
idtoname_show(struct seq_file *m, struct cache_detail *cd, struct cache_head *h)
{
	struct ent *ent;

	if (h == NULL) {
		seq_puts(m, "#domain type id [name]\n");
		return (0);
	}
	ent = container_of(h, struct ent, h);
	seq_printf(m, "%s %u",
			ent->type == UG_IDMAP_TYPE_GROUP ? "group" : "user",
			ent->id);
	if (test_bit(CACHE_VALID, &h->flags))
		seq_printf(m, " %s", ent->name);
	seq_printf(m, "\n");
	return (0);
}


static int	 idtoname_parse(struct cache_detail *, char *, int);
static struct ent *idtoname_lookup(struct ent *);
static struct ent *idtoname_update(struct ent *, struct ent *);

static struct cache_detail idtoname_cache = {
	.owner		= THIS_MODULE,
	.hash_size	= ENT_HASHMAX,
	.hash_table	= idtoname_table,
	.name		= IDMAP_IDTONAME,
	.cache_put	= ent_put,
	.cache_upcall	= idtoname_upcall,
	.cache_parse	= idtoname_parse,
	.cache_show	= idtoname_show,
	.warn_no_listener = warn_no_idmapd,
	.match		= idtoname_match,
	.init		= ent_init,
	.update		= ent_init,
	.alloc		= ent_alloc,
};

static int
idtoname_parse(struct cache_detail *cd, char *buf, int buflen)
{
	struct ent ent, *res;
	char *buf1, *bp;
	int len;
	int error = -EINVAL;

	if (buf[buflen - 1] != '\n')
		return (-EINVAL);
	buf[buflen - 1] = '\0';

	buf1 = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (buf1 == NULL)
		return (-ENOMEM);

	memset(&ent, 0, sizeof(ent));

	/* Type */
	if (qword_get(&buf, buf1, PAGE_SIZE) <= 0)
		goto out;
	ent.type = strcmp(buf1, "user") == 0 ?
		UG_IDMAP_TYPE_USER : UG_IDMAP_TYPE_GROUP;

	/* ID */
	if (qword_get(&buf, buf1, PAGE_SIZE) <= 0)
		goto out;
	ent.id = strtoul(buf1, &bp, 10);
	if (bp == buf1)
		goto out;

	/* expiry */
	if (!(ent.h.expiry_time = get_expiry(&buf)))
		goto out;

	error = -ENOMEM;
	res = idtoname_lookup(&ent);
	if (!res)
		goto out;

	/* Name */
	error = -EINVAL;
	len = qword_get(&buf, buf1, PAGE_SIZE);
	if (len < 0)
		goto out_put;
	if (len == 0)
		set_bit(CACHE_NEGATIVE, &ent.h.flags);
	else if (len >= UG_IDMAP_NAMESZ)
		goto out_put;
	else
		memcpy(ent.name, buf1, sizeof(ent.name));
	error = -ENOMEM;
	res = idtoname_update(&ent, res); /* old res is put */
	if (res == NULL)
		goto out;

	error = 0;
out_put:
	cache_put(&res->h, &idtoname_cache);
out:
	kfree(buf1);

	return (error);
}


static struct ent *
idtoname_lookup(struct ent *item)
{
	struct cache_head *ch = sunrpc_cache_lookup(&idtoname_cache,
						    &item->h,
						    idtoname_hash(item));
	if (ch)
		return (container_of(ch, struct ent, h));
	else
		return (NULL);
}

static struct ent *
idtoname_update(struct ent *new, struct ent *old)
{
	struct cache_head *ch = sunrpc_cache_update(&idtoname_cache,
						    &new->h, &old->h,
						    idtoname_hash(new));
	if (ch)
		return (container_of(ch, struct ent, h));
	else
		return (NULL);
}

/*-------------------------------------------------------------------*/
/*
 * Name -> ID cache
 */

static struct cache_head *nametoid_table[ENT_HASHMAX];

static inline int
nametoid_hash(struct ent *ent)
{
	return (hash_str(ent->name, ENT_HASHBITS));
}

static void
nametoid_request(struct cache_detail *cd, struct cache_head *ch, char **bpp,
    int *blen)
{
	struct ent *ent = container_of(ch, struct ent, h);

	qword_add(bpp, blen,
		(ent->type == UG_IDMAP_TYPE_GROUP ? "group" : "user"));
	qword_add(bpp, blen, ent->name);

	(*bpp)[-1] = '\n';
}

static int
nametoid_upcall(struct cache_detail *cd, struct cache_head *ch)
{
	return (sunrpc_cache_pipe_upcall(cd, ch, nametoid_request));
}

static int
nametoid_match(struct cache_head *ca, struct cache_head *cb)
{
	struct ent *a = container_of(ca, struct ent, h);
	struct ent *b = container_of(cb, struct ent, h);

	return (a->type == b->type && strcmp(a->name, b->name) == 0);
}

static int
nametoid_show(struct seq_file *m, struct cache_detail *cd, struct cache_head *h)
{
	struct ent *ent;

	if (h == NULL) {
		seq_puts(m, "#domain type name [id]\n");
		return (0);
	}
	ent = container_of(h, struct ent, h);
	seq_printf(m, "%s %s",
			ent->type == UG_IDMAP_TYPE_GROUP ? "group" : "user",
			ent->name);
	if (test_bit(CACHE_VALID, &h->flags))
		seq_printf(m, " %u", ent->id);
	seq_printf(m, "\n");
	return (0);
}

static struct ent *nametoid_lookup(struct ent *);
static struct ent *nametoid_update(struct ent *, struct ent *);
static int nametoid_parse(struct cache_detail *, char *, int);

static struct cache_detail nametoid_cache = {
	.owner		= THIS_MODULE,
	.hash_size	= ENT_HASHMAX,
	.hash_table	= nametoid_table,
	.name		= IDMAP_NAMETOID,
	.cache_put	= ent_put,
	.cache_upcall	= nametoid_upcall,
	.cache_parse	= nametoid_parse,
	.cache_show	= nametoid_show,
	.warn_no_listener = warn_no_idmapd,
	.match		= nametoid_match,
	.init		= ent_init,
	.update		= ent_init,
	.alloc		= ent_alloc,
};

static int
nametoid_parse(struct cache_detail *cd, char *buf, int buflen)
{
	struct ent ent, *res;
	char *buf1;
	int error = -EINVAL;

	if (buf[buflen - 1] != '\n')
		return (-EINVAL);
	buf[buflen - 1] = '\0';

	buf1 = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (buf1 == NULL)
		return (-ENOMEM);

	memset(&ent, 0, sizeof(ent));

	/* Type */
	if (qword_get(&buf, buf1, PAGE_SIZE) <= 0)
		goto out;
	ent.type = strcmp(buf1, "user") == 0 ?
		UG_IDMAP_TYPE_USER : UG_IDMAP_TYPE_GROUP;

	/* Name */
	error = qword_get(&buf, buf1, PAGE_SIZE);
	if (error <= 0 || error >= UG_IDMAP_NAMESZ)
		goto out;
	memcpy(ent.name, buf1, sizeof(ent.name));

	/* expiry */
	if (!(ent.h.expiry_time = get_expiry(&buf))) {
		error = -EINVAL;
		goto out;
	}

	/* ID */
	error = get_int(&buf, &ent.id);
	if (error == -EINVAL)
		goto out;
	if (error == -ENOENT)
		set_bit(CACHE_NEGATIVE, &ent.h.flags);

	error = -ENOMEM;
	res = nametoid_lookup(&ent);
	if (res == NULL)
		goto out;
	res = nametoid_update(&ent, res);
	if (res == NULL)
		goto out;

	cache_put(&res->h, &nametoid_cache);
	error = 0;
out:
	kfree(buf1);

	return (error);
}


static struct ent *
nametoid_lookup(struct ent *item)
{
	struct cache_head *ch = sunrpc_cache_lookup(&nametoid_cache,
						    &item->h,
						    nametoid_hash(item));
	if (ch)
		return (container_of(ch, struct ent, h));
	else
		return (NULL);
}

static struct ent *
nametoid_update(struct ent *new, struct ent *old)
{
	struct cache_head *ch = sunrpc_cache_update(&nametoid_cache,
						    &new->h, &old->h,
						    nametoid_hash(new));
	if (ch)
		return (container_of(ch, struct ent, h));
	else
		return (NULL);
}
/*-------------------------------------------------------------------*/

int __init
ug_idmap_init(void)
{
	int rv;

	if ((rv = cache_register(&idtoname_cache)))
		return (rv);
	if ((rv = cache_register(&nametoid_cache))) {
		cache_unregister(&idtoname_cache);
		return (rv);
	}
	if ((rv = cache_register(&hostaddr_cache))) {
		cache_unregister(&idtoname_cache);
		cache_unregister(&nametoid_cache);
		return (rv);
	}
	if ((rv = ug_idmap_proc_init())) {
		cache_unregister(&idtoname_cache);
		cache_unregister(&nametoid_cache);
		cache_unregister(&hostaddr_cache);
	}
	return (rv);
}

void
ug_idmap_exit(void)
{
	cache_unregister(&idtoname_cache);
	cache_unregister(&nametoid_cache);
	cache_unregister(&hostaddr_cache);
	ug_idmap_proc_cleanup();
}

static inline int
do_idmap_lookup(struct ent *(*lookup_fn)(struct ent *), struct ent *key,
		struct cache_detail *detail, struct ent **item,
		struct idmap_defer_req *mdr)
{
	*item = lookup_fn(key);
	if (!*item)
		return (-ENOMEM);
	return (cache_check(detail, &(*item)->h, &mdr->req));
}

static inline int
do_idmap_lookup_nowait(struct ent *(*lookup_fn)(struct ent *),
			struct ent *key, struct cache_detail *detail,
			struct ent **item)
{
	int ret = -ENOMEM;

	*item = lookup_fn(key);
	if (!*item)
		goto out_err;
	ret = -ETIMEDOUT;
	if (!cache_valid(&(*item)->h)) {
		printk(KERN_INFO "ug_idmapd dosen't respond in %d sec\n",
			ug_timeout_sec);
		goto out_put;
	}
	ret = -ENOENT;
	if (test_bit(CACHE_NEGATIVE, &(*item)->h.flags))
		goto out_put;
	return (0);
out_put:
	cache_put(&(*item)->h, detail);
out_err:
	*item = NULL;
	return (ret);
}

static int
idmap_lookup(
		struct ent *(*lookup_fn)(struct ent *), struct ent *key,
		struct cache_detail *detail, struct ent **item)
{
	struct idmap_defer_req *mdr;
	int ret;

	mdr = idmap_cache_defer_req_alloc();
	if (!mdr)
		return (-ENOMEM);
	ret = do_idmap_lookup(lookup_fn, key, detail, item, mdr);
	if (ret == -EAGAIN) {
		ret = idmap_cache_wait_for_upcall(mdr,  ug_timeout_sec);
		if (!ret)
			ret = do_idmap_lookup_nowait(
				lookup_fn, key, detail, item);
	}
	put_mdr(mdr);
	return (ret);
}

static int
idmap_name_to_id(int type, const char *name, u32 namelen,
		uid_t *id)
{
	struct ent *item, key = {
		.type = type,
	};
	int ret;

	if (namelen + 1 > sizeof(key.name))
		return (-EINVAL);
	memcpy(key.name, name, namelen);
	key.name[namelen] = '\0';
	ret = idmap_lookup(nametoid_lookup, &key, &nametoid_cache, &item);
	if (ret == -ENOENT)
		ret = -ESRCH;
	if (ret)
		return (ret);
	*id = item->id;
	cache_put(&item->h, &nametoid_cache);
	return (0);
}

static int
idmap_id_to_name(int type, uid_t id, char *name, int namelen)
{
	struct ent *item, key = {
		.id = id,
		.type = type,
	};
	int ret;

	ret = idmap_lookup(idtoname_lookup, &key, &idtoname_cache, &item);
	if (ret == -ENOENT)
		return (sprintf(name, "%u", id));
	if (ret)
		return (ret);
	ret = strlen(item->name);
	BUG_ON(ret > UG_IDMAP_NAMESZ);
	memcpy(name, item->name, ret < namelen ? ret : namelen);
	if (namelen < ret)
		name[ret] = 0;
	cache_put(&item->h, &idtoname_cache);
	return (ret);
}

int
ug_map_name_to_uid(const char *name, size_t namelen,
		__u32 *id)
{
	return (idmap_name_to_id(UG_IDMAP_TYPE_USER, name, namelen, id));
}
EXPORT_SYMBOL(ug_map_name_to_uid);

int
ug_map_name_to_gid(const char *name, size_t namelen,
		__u32 *id)
{
	return (idmap_name_to_id(UG_IDMAP_TYPE_GROUP, name, namelen, id));
}
EXPORT_SYMBOL(ug_map_name_to_gid);

int
ug_map_uid_to_name(__u32 id, char *name, size_t namelen)
{
	return (idmap_id_to_name(UG_IDMAP_TYPE_USER, id, name, namelen));
}
EXPORT_SYMBOL(ug_map_uid_to_name);

int
ug_map_gid_to_name(__u32 id, char *name, size_t namelen)
{
	return (idmap_id_to_name(UG_IDMAP_TYPE_GROUP, id, name, namelen));
}
EXPORT_SYMBOL(ug_map_gid_to_name);

MODULE_LICENSE("GPL");
