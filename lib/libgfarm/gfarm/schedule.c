#include <stdio.h> /* sprintf */
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <gfarm/gfarm.h>
#include "gfevent.h"
#include "hash.h"
#include "host.h" /* gfarm_host_info_address_get() */
#include "auth.h" /* XXX gfarm_random_initialize() */
#include "gfs_client.h"

char GFARM_ERR_NO_REPLICA[] = "no replica";
char GFARM_ERR_NO_HOST[] = "no filesystem node";

/*
 * data structure which represents information about a host
 */

#define HOSTS_HASHTAB_SIZE	3079	/* prime number */

struct search_idle_host_state {
	struct gfarm_host_info *host_info;
	float loadavg;
	int flags;
#define HOST_STATE_FLAG_RUNNABLE		0x01
#define HOST_STATE_FLAG_LOADAVG_TRIED		0x02
#define HOST_STATE_FLAG_LOADAVG_AVAIL		0x04
#define HOST_STATE_FLAG_AUTH_TRIED		0x08
#define HOST_STATE_FLAG_AUTH_SUCCEED		0x10
};

static void
free_hosts_state(int n_all_hosts, struct gfarm_host_info *all_hosts,
	struct gfarm_hash_table *hosts_state)
{
	gfarm_hash_table_free(hosts_state);
	gfarm_host_info_free_all(n_all_hosts, all_hosts);
}

static char *
alloc_hosts_state(int *n_all_hostsp, struct gfarm_host_info **all_hostsp,
	struct gfarm_hash_table **hosts_statep)
{
	char *e;
	int i, created, n_all_hosts;
	struct gfarm_host_info *all_hosts;
	struct gfarm_hash_table *hosts_state;
	struct gfarm_hash_entry *entry;
	struct search_idle_host_state *h;

	e = gfarm_host_info_get_all(&n_all_hosts, &all_hosts);
	if (e != NULL)
		return (e);
	if (n_all_hosts == 0) {
		gfarm_host_info_free_all(n_all_hosts, all_hosts);
		return (GFARM_ERR_NO_HOST);
	}
	hosts_state = gfarm_hash_table_alloc(HOSTS_HASHTAB_SIZE,
	    gfarm_hash_casefold, gfarm_hash_key_equal_casefold);
	if (hosts_state == NULL) {
		gfarm_host_info_free_all(n_all_hosts, all_hosts);
		return (GFARM_ERR_NO_MEMORY);
	}
	for (i = 0; i < n_all_hosts; i++) {
		entry = gfarm_hash_enter(hosts_state,
		    all_hosts[i].hostname, strlen(all_hosts[i].hostname) + 1,
		    sizeof(struct search_idle_host_state), &created);
		if (entry == NULL) {
			free_hosts_state(n_all_hosts, all_hosts, hosts_state);
			return (GFARM_ERR_NO_MEMORY);
		}
		/* `created' must be always true. */
		h = gfarm_hash_entry_data(entry);
		h->host_info = &all_hosts[i];
		h->flags = 0;
	}
	*n_all_hostsp = n_all_hosts;
	*all_hostsp = all_hosts;
	*hosts_statep = hosts_state;
	return (NULL);			
}

/*
 * data structure which represents architectures which can run a program
 */

#define ARCH_SET_HASHTAB_SIZE 31		/* prime number */

#define IS_IN_ARCH_SET(arch, arch_set) \
	(gfarm_hash_lookup(arch_set, arch, strlen(arch) + 1) != NULL)

#define free_arch_set(arch_set)	gfarm_hash_table_free(arch_set)

/* Create a set of architectures that the program is registered for */
static char *
program_arch_set(char *program, struct gfarm_hash_table **arch_setp)
{
	char *e, *gfarm_file;
	struct gfarm_path_info pi;
	struct gfarm_file_section_info *sections;
	struct gfarm_hash_table *arch_set;
	int i, nsections, created;

	e = gfarm_url_make_path(program, &gfarm_file);
	if (e != NULL)
		return (e);
	e = gfarm_path_info_get(gfarm_file, &pi);
	if (e != NULL) {
		if (e == GFARM_ERR_NO_SUCH_OBJECT)
			e = "such program isn't registered";
		free(gfarm_file);
		return (e);
	}
	if (!GFARM_S_IS_PROGRAM(pi.status.st_mode)) {
		gfarm_path_info_free(&pi);
		free(gfarm_file);
		return ("specified command is not an executable");
	}
	e = gfarm_file_section_info_get_all_by_file(gfarm_file,
	    &nsections, &sections);
	gfarm_path_info_free(&pi);
	free(gfarm_file);
	if (e != NULL)
		return ("no binary is registered as the specified command");

	arch_set = gfarm_hash_table_alloc(ARCH_SET_HASHTAB_SIZE,
	    gfarm_hash_default, gfarm_hash_key_equal_default);
	if (arch_set == NULL) {
		gfarm_file_section_info_free_all(nsections, sections);
		return (GFARM_ERR_NO_MEMORY);
	}
	/* register architectures of the program to `arch_set' */
	for (i = 0; i < nsections; i++) {
		if (gfarm_hash_enter(arch_set,
		    sections[i].section, strlen(sections[i].section) + 1,
		    sizeof(int), &created) == NULL) {
			free_arch_set(arch_set);
			gfarm_file_section_info_free_all(nsections, sections);
			return (GFARM_ERR_NO_MEMORY);
		}
	}
	gfarm_file_section_info_free_all(nsections, sections);
	*arch_setp = arch_set;
	return (NULL);
}

/*
 * Record whether the host can run the program or cannot
 */

static char *
hosts_for_program(char *program,
	int *n_all_hostsp, struct gfarm_host_info **all_hostsp,
	struct gfarm_hash_table **hosts_statep, int *n_runnable_hostsp)
{
	char *e;
	int n_all_hosts, n_runnable_hosts;
	struct gfarm_host_info *all_hosts;
	struct gfarm_hash_table *hosts_state;
	struct gfarm_hash_table *arch_set;
	struct gfarm_hash_iterator it;
	struct gfarm_hash_entry *entry;
	struct search_idle_host_state *h;

	e = alloc_hosts_state(&n_all_hosts, &all_hosts, &hosts_state);
	if (e == NULL) {
		e = program_arch_set(program, &arch_set);
		if (e == NULL) {
			n_runnable_hosts = 0;
			for (gfarm_hash_iterator_begin(hosts_state, &it);
			    !gfarm_hash_iterator_is_end(&it);
			    gfarm_hash_iterator_next(&it)) {
				entry = gfarm_hash_iterator_access(&it);
				h = gfarm_hash_entry_data(entry);
				if (IS_IN_ARCH_SET(h->host_info->architecture,
				    arch_set)) {
					h->flags |= HOST_STATE_FLAG_RUNNABLE;
					++n_runnable_hosts;
				}
			}
			free_arch_set(arch_set);
			if (n_runnable_hosts > 0) {
				*n_all_hostsp = n_all_hosts;
				*all_hostsp = all_hosts;
				*hosts_statep = hosts_state;
				*n_runnable_hostsp = n_runnable_hosts;
				return (NULL);
			}
			e = "there is no host which can run the program";
		}
		free_hosts_state(n_all_hosts, all_hosts, hosts_state);
	}
	return (e);
}

/*
 * search_idle() callback routines to get next hostname
 */

/* abstract super class (a.k.a. interface class) to get next hostname */

struct get_next_iterator {
	char *(*get_next)(struct get_next_iterator *);
};

/* for "char *" */

struct string_array_iterator {
	struct get_next_iterator super;
	char **current;
};

static char *
get_next_from_string_array(struct get_next_iterator *self)
{
	struct string_array_iterator *iterator =
	    (struct string_array_iterator *)self;

	return (*iterator->current++);
}

static struct get_next_iterator *
init_string_array_iterator(struct string_array_iterator *iterator,
	char **strings)
{
	iterator->super.get_next = get_next_from_string_array;
	iterator->current = strings;
	return (&iterator->super);
}

/* for struct gfarm_host_info */

struct host_info_array_iterator {
	struct get_next_iterator super;
	struct gfarm_host_info *current;
};

static char *
get_next_from_host_info_array(struct get_next_iterator *self)
{
	struct host_info_array_iterator *iterator =
	    (struct host_info_array_iterator *)self;

	return ((iterator->current++)->hostname);
}

static struct get_next_iterator *
init_host_info_array_iterator(struct host_info_array_iterator *iterator,
	struct gfarm_host_info *hosts)
{
	iterator->super.get_next = get_next_from_host_info_array;
	iterator->current = hosts;
	return (&iterator->super);
}

/* for struct gfarm_file_section_copy_info */

struct section_copy_info_array_iterator {
	struct get_next_iterator super;
	struct gfarm_file_section_copy_info *current;
};

static char *
get_next_from_section_copy_info_array(struct get_next_iterator *self)
{
	struct section_copy_info_array_iterator *iterator =
	    (struct section_copy_info_array_iterator *)self;

	return ((iterator->current++)->hostname);
}

static struct get_next_iterator *
init_section_copy_info_array_iterator(
	struct section_copy_info_array_iterator *iterator,
	struct gfarm_file_section_copy_info *copies)
{
	iterator->super.get_next = get_next_from_section_copy_info_array;
	iterator->current = copies;
	return (&iterator->super);
}

/* abstract super class (a.k.a. interface class) to restrict host */

struct string_filter {
	int (*suitable)(struct string_filter *self, const char *);
};

/* to not restrict anything */

static int
always_suitable(struct string_filter *self, const char *string)
{
	return (1);
}

static struct string_filter null_filter = { always_suitable };

/* to restrict host by domain */

struct domainname_filter {
	struct string_filter super;
	const char *domainname;
};

static int
is_host_in_domain(struct string_filter *self, const char *hostname)
{
	struct domainname_filter *filter = (struct domainname_filter *)self;

	return (gfarm_host_is_in_domain(hostname, filter->domainname));
}

static struct string_filter *
init_domainname_filter(struct domainname_filter *filter,
	const char *domainname)
{
	filter->super.suitable = is_host_in_domain;
	filter->domainname = domainname;
	return (&filter->super);
}

/* to restrict string by string_set */

struct host_runnable_filter {
	struct string_filter super;
	struct gfarm_hash_table *hosts_state;
};

static int
is_host_runnable(struct string_filter *self, const char *host)
{
	struct host_runnable_filter *filter =
	    (struct host_runnable_filter *)self;
	struct gfarm_hash_entry *entry = gfarm_hash_lookup(filter->hosts_state,
	    host, strlen(host) + 1);
	struct search_idle_host_state *h;

	if (entry == NULL) /* never happen, if metadata is consistent */
		return (0);
	h = gfarm_hash_entry_data(entry);
	return ((h->flags & HOST_STATE_FLAG_RUNNABLE) != 0);
}

static struct string_filter *
init_host_runnable_filter(struct host_runnable_filter *filter,
	struct gfarm_hash_table *hosts_state)
{
	filter->super.suitable = is_host_runnable;
	filter->hosts_state = hosts_state;
	return (&filter->super);
}

/*
 * gfarm_client_*_load_*() callback routine which is provided by search_idle()
 */

#define VIRTUAL_LOAD_FOR_SCHEDULED_HOST	1.0F

#define IDLE_LOAD_AVERAGE	0.1F
#define SEMI_IDLE_LOAD_AVERAGE	0.5F

struct search_idle_available_host {
	struct search_idle_host_state *host_state;
	char *hostname; /* used to hold return value */
};

struct search_idle_state {
	struct gfarm_eventqueue *q;
	struct search_idle_available_host *available_hosts;
	int available_hosts_number;
	int idle_hosts_number;
	int semi_idle_hosts_number;

	int concurrency;
};

static void
search_idle_record_host(struct search_idle_state *s,
	struct search_idle_host_state *h, char *hostname)
{
	struct search_idle_available_host *ah =
	    &s->available_hosts[s->available_hosts_number++];
	float loadavg = h->loadavg;

	ah->host_state = h;
	ah->hostname = hostname;

	/*
	 * We don't use (loadavg / h->host_info->ncpu) to count
	 * semi_idle_hosts here for now, because it is possible
	 * that there is a process which is consuming 100% of
	 * memory or 100% of I/O bandwidth on the host.
	 */
	if (loadavg <= SEMI_IDLE_LOAD_AVERAGE)
		s->semi_idle_hosts_number++;

	if (loadavg / h->host_info->ncpu <= IDLE_LOAD_AVERAGE)
		s->idle_hosts_number++;
}

struct search_idle_callback_closure {
	struct search_idle_state *state;
	struct sockaddr peer_addr;
	struct search_idle_available_host ah;
	void *protocol_state;
};

static void
search_idle_load_callback(void *closure)
{
	struct search_idle_callback_closure *c = closure;
	char *e;
	struct gfs_client_load load;

	e = gfs_client_get_load_result_multiplexed(c->protocol_state, &load);
	if (e == NULL) {
		c->ah.host_state->flags |= HOST_STATE_FLAG_LOADAVG_AVAIL;
		c->ah.host_state->loadavg = load.loadavg_1min;
		search_idle_record_host(c->state,
		    c->ah.host_state, c->ah.hostname);
	}
	c->state->concurrency--;
	free(c);
}

static void
search_idle_connect_callback(void *closure)
{
	struct search_idle_callback_closure *c = closure;
	char *e;
	struct gfs_connection *gfs_server;

	e = gfs_client_connect_result_multiplexed(c->protocol_state,
	    &gfs_server);
	if (e == NULL) {
		c->ah.host_state->flags |= HOST_STATE_FLAG_AUTH_SUCCEED;
		search_idle_record_host(c->state,
		    c->ah.host_state, c->ah.hostname);
		gfs_client_disconnect(gfs_server);
	}
	c->state->concurrency--;
	free(c);
}

static void
search_idle_load_and_connect_callback(void *closure)
{
	struct search_idle_callback_closure *c = closure;
	char *e;
	struct gfs_client_load load;
	struct gfs_client_connect_state *cs;

	e = gfs_client_get_load_result_multiplexed(c->protocol_state, &load);
	if (e == NULL) {
		c->ah.host_state->flags |=
			HOST_STATE_FLAG_LOADAVG_AVAIL |
			HOST_STATE_FLAG_AUTH_TRIED;
		c->ah.host_state->loadavg = load.loadavg_1min;
		e = gfs_client_connect_request_multiplexed(c->state->q,
		    c->ah.hostname, &c->peer_addr,
		    search_idle_connect_callback, c,
		    &cs);
		if (e == NULL) {
			c->protocol_state = cs;
			return; /* request continues */
		}
	}
	c->state->concurrency--;
	free(c);
}

static int
loadavg_compare(const void *a, const void *b)
{
	const struct search_idle_available_host *aa = a;
	const struct search_idle_available_host *bb = b;
	const struct search_idle_host_state *p = aa->host_state;
	const struct search_idle_host_state *q = bb->host_state;
	const float l1 = p->loadavg / p->host_info->ncpu;
	const float l2 = q->loadavg / q->host_info->ncpu;

	if (l1 < l2)
		return (-1);
        else if (l1 > l2)
		return (1);
	else
		return (0);
}

/* check authentication success or not? */

static enum gfarm_schedule_search_mode default_search_method =
	GFARM_SCHEDULE_SEARCH_BY_LOADAVG_AND_AUTH;

enum gfarm_schedule_search_mode
gfarm_schedule_search_mode_get(void)
{
	return (default_search_method);
}

void
gfarm_schedule_search_mode_set(enum gfarm_schedule_search_mode mode)
{
	default_search_method = mode;
}

/*
 * The search will be stopped, if `enough_number' of semi-idle hosts
 * are found.
 *
 * `*nohostsp' is an IN/OUT parameter.
 * It means desired number of hosts as an INPUT parameter.
 * It returns available number of hosts as an OUTPUT parameter.
 */

static char *
search_idle(int concurrency, int enough_number,
	struct gfarm_hash_table *hosts_state,
	int nihosts, struct string_filter *ihost_filter,
	struct get_next_iterator *ihost_iterator,
	int *nohostsp, char **ohosts)
{
	char *e, *ihost;
	int i, rv, desired_number = *nohostsp;
	struct search_idle_state s;
	struct gfarm_hash_entry *entry;
	struct search_idle_host_state *h;
	struct search_idle_callback_closure *c;
	struct sockaddr addr;
	struct gfs_client_get_load_state *gls;

	if (nihosts == 0)
		return (GFARM_ERR_NO_HOST);
	s.q = gfarm_eventqueue_alloc();
	if (s.q == NULL)
		return (GFARM_ERR_NO_MEMORY);
	s.available_hosts_number =
	    s.idle_hosts_number = s.semi_idle_hosts_number = 0;
	s.available_hosts = malloc(nihosts * sizeof(*s.available_hosts));
	if (s.available_hosts == NULL) {
		gfarm_eventqueue_free(s.q);
		return (GFARM_ERR_NO_MEMORY);
	}
	s.concurrency = 0;
	for (i = 0; i < nihosts; i++) {
		do {
			ihost = (*ihost_iterator->get_next)(ihost_iterator);
		} while (!(*ihost_filter->suitable)(ihost_filter, ihost));
		entry = gfarm_hash_lookup(hosts_state,
		    ihost, strlen(ihost) + 1);
		if (entry == NULL)
			continue; /* never happen, if metadata is consistent */
		h = gfarm_hash_entry_data(entry);
		if ((h->flags & HOST_STATE_FLAG_LOADAVG_TRIED) != 0) {
			if ((h->flags &
			     (default_search_method ==
			      GFARM_SCHEDULE_SEARCH_BY_LOADAVG ?
			      HOST_STATE_FLAG_LOADAVG_AVAIL :
			      HOST_STATE_FLAG_AUTH_SUCCEED)) != 0)
				search_idle_record_host(&s, h, ihost);
		} else {
			e = gfarm_host_info_address_get(ihost,
			    gfarm_spool_server_port, h->host_info,
			    &addr, NULL);
			if (e != NULL)
				continue;

			/* We limit concurrency here */
			rv = 0;
			while (s.concurrency >= concurrency) {
				rv = gfarm_eventqueue_turn(s.q, NULL);
				/* XXX - how to report this error? */
				if (rv != 0 && rv != EAGAIN && rv != EINTR)
					break;
			}
			if (rv != 0 && rv != EAGAIN && rv != EINTR)
				break;

			c = malloc(sizeof(*c));
			if (c == NULL)
				break;
			c->state = &s;
			c->peer_addr = addr;
			c->ah.host_state = h;
			c->ah.hostname = ihost; /* record return value */
			h->flags |= HOST_STATE_FLAG_LOADAVG_TRIED;
			e = gfs_client_get_load_request_multiplexed(s.q,
			    &c->peer_addr,
			    default_search_method ==
			    GFARM_SCHEDULE_SEARCH_BY_LOADAVG ?
			    search_idle_load_callback :
			    search_idle_load_and_connect_callback,
			    c, &gls);
			if (e != NULL) {
				free(c);
			} else {
				c->protocol_state = gls;
				s.concurrency++;
			}
		}
		if (s.idle_hosts_number >= desired_number ||
		    s.semi_idle_hosts_number >= enough_number)
			break;
	}
	/* XXX - how to report this error? */
	rv = gfarm_eventqueue_loop(s.q, NULL);
	gfarm_eventqueue_free(s.q);
	if (s.available_hosts_number == 0) {
		free(s.available_hosts);
		*nohostsp = 0;
		return (GFARM_ERR_NO_HOST);
	}

	/* sort hosts in the order of load average */
	qsort(s.available_hosts, s.available_hosts_number,
	    sizeof(*s.available_hosts), loadavg_compare);

	for (i = 0; i < s.available_hosts_number && i < desired_number; i++)
		ohosts[i] = s.available_hosts[i].hostname; /* return value */
	*nohostsp = i;
	free(s.available_hosts);

	return (NULL);
}

/*
 * shuffle input-hosts before searching to avoid unbalanced search
 */

/* #define USE_SHUFFLED */

#ifdef USE_SHUFFLED

static void
shuffle_strings(int n, char **strings)
{
	int i, j;
	char *tmp;

	gfarm_random_initialize();
	for (i = n - 1; i > 0; --i) {
#ifdef HAVE_RANDOM
		j = random() % (i + 1);
#else
		j = rand()/(RAND_MAX + 1.0) * (i + 1);
#endif
		tmp = strings[i];
		strings[i] = strings[j];
		strings[j] = tmp;
	}
}

static char *
search_idle_shuffled(int concurrency, int enough_number,
	struct gfarm_hash_table *hosts_state,
	int nihosts, struct string_filter *ihost_filter,
	struct get_next_iterator *ihost_iterator,
	int *nohostsp, char **ohosts)
{
	char *e, *ihost, **shuffled_ihosts;
	int i;
	struct string_array_iterator host_iterator;

	if (nihosts == 0)
		return (GFARM_ERR_NO_HOST);
	shuffled_ihosts = malloc(nihosts * sizeof(*shuffled_ihosts));
	if (shuffled_ihosts == NULL)
		return (GFARM_ERR_NO_MEMORY);
	for (i = 0; i < nihosts; i++) {
		do {
			ihost = (*ihost_iterator->get_next)(ihost_iterator);
		} while (!(*ihost_filter->suitable)(ihost_filter, ihost));
		shuffled_ihosts[i++] = ihost;
	}
	shuffle_strings(nihosts, shuffled_ihosts);

	e = search_idle(concurrency, enough_number, hosts_state,
	    nihosts, &null_filter,
	    init_string_array_iterator(&host_iterator, shuffled_ihosts),
	    nohostsp, ohosts);
	free(shuffled_ihosts);
	return (e);
}

#endif /* USE_SHUFFLED */

void
gfarm_strings_expand_cyclic(int nsrchosts, char **srchosts,
	int ndsthosts, char **dsthosts)
{
	int i, j;

	for (i = 0, j = 0; i < ndsthosts; i++, j++) {
		if (j >= nsrchosts)
			j = 0;
		dsthosts[i] = srchosts[j];
	}
}

#define CONCURRENCY	10
#define ENOUGH_RATE	4

static char *
search_idle_cyclic(struct gfarm_hash_table *hosts_state,
	int nihosts, struct string_filter *ihost_filter,
	struct get_next_iterator *ihost_iterator,
	int nohosts, char **ohosts)
{
	char *e;
	int nfound = nohosts;

#ifdef USE_SHUFFLED
	e = search_idle_shuffled(CONCURRENCY,
	    nohosts * ENOUGH_RATE, hosts_state,
	    nihosts, ihost_filter, ihost_iterator,
	    &nfound, ohosts);
#else
	e = search_idle(CONCURRENCY,
	    nohosts * ENOUGH_RATE, hosts_state,
	    nihosts, ihost_filter, ihost_iterator,
	    &nfound, ohosts);
#endif

	if (e != NULL)
		return (e);
	if (nfound == 0) {
		/* Oh, my god */
		return (GFARM_ERR_NO_HOST);
	}
	if (nohosts > nfound)
		gfarm_strings_expand_cyclic(nfound, ohosts,
		    nohosts - nfound, &ohosts[nfound]);
	return (NULL);
}

/* 
 * Select 'nohosts' hosts among 'nihosts' ihosts in the order of
 * load average, and return to 'ohosts'.
 * When enough number of hosts are not available, the available hosts
 * will be listed in the cyclic manner.
 */
char *
gfarm_schedule_search_idle_hosts(
	int nihosts, char **ihosts, int nohosts, char **ohosts)
{
	char *e;
	int n_all_hosts;
	struct gfarm_host_info *all_hosts;
	struct gfarm_hash_table *hosts_state;
	struct string_array_iterator host_iterator;

	e = alloc_hosts_state(&n_all_hosts, &all_hosts, &hosts_state);
	if (e != NULL)
		return (e);
	e = search_idle_cyclic(hosts_state, nihosts, &null_filter,
	    init_string_array_iterator(&host_iterator, ihosts),
	    nohosts, ohosts);
	free_hosts_state(n_all_hosts, all_hosts, hosts_state);
	return (e);
}

char *
gfarm_schedule_search_idle_by_all(int nohosts, char **ohosts)
{
	char *e;
	int n_all_hosts;
	struct gfarm_host_info *all_hosts;
	struct gfarm_hash_table *hosts_state;
	struct host_info_array_iterator host_iterator;

	e = alloc_hosts_state(&n_all_hosts, &all_hosts, &hosts_state);
	if (e != NULL)
		return (e);
	e = search_idle_cyclic(hosts_state, n_all_hosts, &null_filter,
	    init_host_info_array_iterator(&host_iterator, all_hosts),
	    nohosts, ohosts);
	if (e == NULL)
		e = gfarm_fixedstrings_dup(nohosts, ohosts, ohosts);
	free_hosts_state(n_all_hosts, all_hosts, hosts_state);
	return (e);
}

/*
 * lists host names that contains domainname.
 */
char *
gfarm_schedule_search_idle_by_domainname(const char *domainname,
	int nohosts, char **ohosts)
{
	char *e;
	int i, nhosts, n_all_hosts;
	struct gfarm_host_info *all_hosts;
	struct gfarm_hash_table *hosts_state;
	struct host_info_array_iterator host_iterator;
	struct domainname_filter domain_filter;

	e = alloc_hosts_state(&n_all_hosts, &all_hosts, &hosts_state);
	if (e != NULL)
		return (e);

	nhosts = 0;
	for (i = 0; i < n_all_hosts; i++) {
		if (gfarm_host_is_in_domain(all_hosts[i].hostname, domainname))
			++nhosts;
	}

	e = search_idle_cyclic(hosts_state, nhosts,
	    init_domainname_filter(&domain_filter, domainname),
	    init_host_info_array_iterator(&host_iterator, all_hosts),
	    nohosts, ohosts);
	if (e == NULL)
		e = gfarm_fixedstrings_dup(nohosts, ohosts, ohosts);
	free_hosts_state(n_all_hosts, all_hosts, hosts_state);
	return (e);
}

char *
gfarm_schedule_search_idle_by_program(char *program,
	int nohosts, char **ohosts)
{
	char *e;
	int n_all_hosts, n_runnable_hosts;
	struct gfarm_host_info *all_hosts;
	struct gfarm_hash_table *hosts_state;
	struct host_info_array_iterator host_iterator;
	struct host_runnable_filter host_filter;

	if (!gfarm_is_url(program))
		return (gfarm_schedule_search_idle_by_all(nohosts, ohosts));

	e = hosts_for_program(program, &n_all_hosts, &all_hosts, &hosts_state,
	    &n_runnable_hosts);
	if (e != NULL)
		return (e);
	e = search_idle_cyclic(hosts_state, n_runnable_hosts,
	    init_host_runnable_filter(&host_filter, hosts_state),
	    init_host_info_array_iterator(&host_iterator, all_hosts),
	    nohosts, ohosts);
	if (e == NULL)
		e = gfarm_fixedstrings_dup(nohosts, ohosts, ohosts);
	free_hosts_state(n_all_hosts, all_hosts, hosts_state);
	return (e);
}

/*
 * Return GFARM_ERR_NO_HOST, if there is a replica, but there isn't
 * any host which satisfies the hostname_filter.
 */
static char *
search_idle_by_section_copy_info(struct gfarm_hash_table *hosts_state,
	int ncopies, struct gfarm_file_section_copy_info *copies,
	struct string_filter *hostname_filter,
	int nohosts, char **ohosts)
{
	char *e;
	int i, nhosts;
	struct section_copy_info_array_iterator copy_iterator;
	struct gfarm_hash_entry *entry;
	struct search_idle_host_state *h;

	nhosts = 0;
	for (i = 0; i < ncopies; i++) {
		if ((*hostname_filter->suitable)(hostname_filter,
		    copies[i].hostname))
			nhosts++;
	}
	if (nhosts == 0)
		return (GFARM_ERR_NO_HOST);

	e = search_idle_cyclic(hosts_state, nhosts, hostname_filter,
	    init_section_copy_info_array_iterator(&copy_iterator, copies),
	    nohosts, ohosts);
	if (e == NULL) {
		e = gfarm_fixedstrings_dup(nohosts, ohosts, ohosts);
		if (e == NULL) {
			/* increase the load average of scheduled hosts */
			for (i = 0; i < nohosts; i++) {
				entry = gfarm_hash_lookup(hosts_state,
				    ohosts[i], strlen(ohosts[i]) + 1);
				if (entry == NULL)
					continue; /* shouldn't happen */
				h = gfarm_hash_entry_data(entry);
				h->loadavg += VIRTUAL_LOAD_FOR_SCHEDULED_HOST;
			}
		}
	}
	return (e);
}

/*
 * Return GFARM_ERR_NO_HOST, if there is a replica, but there isn't
 * any host which satisfies the hostname_filter.
 */
static char *
schedule_by_file_section(struct gfarm_hash_table *hosts_state,
	char *gfarm_file, char *section, struct string_filter *hostname_filter,
	int nohosts, char **ohosts)
{
	char *e;
	int ncopies;
	struct gfarm_file_section_copy_info *copies;

	e = gfarm_file_section_copy_info_get_all_by_section(
	    gfarm_file, section, &ncopies, &copies);
	if (e != NULL)
		return (e);
	e = ncopies == 0 ? GFARM_ERR_NO_REPLICA :
	    search_idle_by_section_copy_info(hosts_state,
	    ncopies, copies, hostname_filter,
	    nohosts, ohosts);
	gfarm_file_section_copy_info_free_all(ncopies, copies);
	return (e);
}

char *
gfarm_file_section_host_schedule(char *gfarm_file, char *section, char **hostp)
{
	char *e;
	int n_all_hosts;
	struct gfarm_host_info *all_hosts;
	struct gfarm_hash_table *hosts_state;

	e = alloc_hosts_state(&n_all_hosts, &all_hosts, &hosts_state);
	if (e != NULL)
		return (e);
	e = schedule_by_file_section(hosts_state,
	    gfarm_file, section, &null_filter,
	    1, hostp);
	free_hosts_state(n_all_hosts, all_hosts, hosts_state);
	return (e);
}

char *
gfarm_file_section_host_schedule_by_program(char *gfarm_file, char *section,
	char *program, char **hostp)
{
	char *e;
	int n_all_hosts, n_runnable_hosts;
	struct gfarm_host_info *all_hosts;
	struct gfarm_hash_table *hosts_state;
	struct host_runnable_filter host_filter;
	struct host_info_array_iterator host_iterator;

	if (!gfarm_is_url(program))
		return (gfarm_file_section_host_schedule(gfarm_file, section,
		    hostp));

	e = hosts_for_program(program, &n_all_hosts, &all_hosts, &hosts_state,
	    &n_runnable_hosts);
	if (e != NULL)
		return (e);
	e = schedule_by_file_section(hosts_state, gfarm_file, section,
	    init_host_runnable_filter(&host_filter, hosts_state),
	    1, hostp);
	if (e == GFARM_ERR_NO_HOST) {
		e = search_idle_cyclic(hosts_state, n_runnable_hosts,
		    init_host_runnable_filter(&host_filter, hosts_state),
		    init_host_info_array_iterator(&host_iterator, all_hosts),
		    1, hostp);
		if (e == NULL)
			e = gfarm_fixedstrings_dup(1, hostp, hostp);
	}
	free_hosts_state(n_all_hosts, all_hosts, hosts_state);
	return (e);
}

char *
gfarm_file_section_host_schedule_with_priority_to_local(
	char *gfarm_file, char *section, char **hostp)
{
	char *e, *host, *self_name;
	int n_all_hosts;
	struct gfarm_host_info *all_hosts;
	struct gfarm_hash_table *hosts_state;
	int i, ncopies;
	struct gfarm_file_section_copy_info *copies;

	e = gfarm_file_section_copy_info_get_all_by_section(
	    gfarm_file, section, &ncopies, &copies);
	if (e != NULL)
		return (e);
	if (ncopies == 0) {
		gfarm_file_section_copy_info_free_all(ncopies, copies);
		return (GFARM_ERR_NO_REPLICA);
	}

	/* choose/schedule local one, if possible */
	e = gfarm_host_get_canonical_self_name(&self_name);
	if (e != NULL) {
		i = ncopies;
	} else {
		for (i = 0; i < ncopies; i++)
			if (strcasecmp(self_name, copies[i].hostname) == 0)
				break;
	}
	if (i < ncopies) {
		/*
		 * local host is found in a list of 'copies', but it
		 * is necessary to make sure 'gfsd' is running or not.
		 */
		e = gfarm_schedule_search_idle_hosts(
			1, &copies[i].hostname, 1, &host);
		if (e != NULL) {
			/* 'gfsd' is not running. */
			i = ncopies;
		}
	}
	if (i == ncopies) {
		e = alloc_hosts_state(&n_all_hosts, &all_hosts, &hosts_state);
		if (e != NULL) {
			gfarm_file_section_copy_info_free_all(ncopies, copies);
			return (e);
		}
		e = search_idle_by_section_copy_info(hosts_state,
		    ncopies, copies, &null_filter,
		    1, &host);
		free_hosts_state(n_all_hosts, all_hosts, hosts_state);
		if (e != NULL) {
			gfarm_file_section_copy_info_free_all(ncopies, copies);
			return (e);
		}
	}

	gfarm_file_section_copy_info_free_all(ncopies, copies);
	if (host == NULL)
		return (GFARM_ERR_NO_MEMORY);
	*hostp = host;
	return (NULL);
}

static char *
url_hosts_schedule_filtered(struct gfarm_hash_table *hosts_state,
	char *gfarm_url, char *option,
	int nihosts, struct string_filter *ihost_filter,
	struct get_next_iterator *ihost_iterator,
	int *nhostsp, char ***hostsp)
{
	char *e, *gfarm_file, **hosts, **residual;
	int i, nfrags, shortage;
	char section[GFARM_INT32STRLEN + 1];

	e = gfarm_url_fragment_number(gfarm_url, &nfrags);
	if (e != NULL)
		return (e);
	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL)
		return (e);
	hosts = malloc(sizeof(char *) * nfrags);
	if (hosts == NULL) {
		free(gfarm_file);
		return (GFARM_ERR_NO_MEMORY);
	}
	shortage = 0;
	for (i = 0; i < nfrags; i++) {
		sprintf(section, "%d", i);
		e = schedule_by_file_section(hosts_state,
		    gfarm_file, section, ihost_filter,
		    1, &hosts[i]);
		if (e == GFARM_ERR_NO_HOST) {
			hosts[i] = NULL;
			shortage++;
			continue;
		}
		if (e != NULL) {
			gfarm_strings_free_deeply(i, hosts);
			free(gfarm_file);
			return (e);
		}
	}
	if (shortage > 0) {
		residual = malloc(shortage * sizeof(*residual));
		if (residual == NULL) {
			gfarm_strings_free_deeply(nfrags, hosts);
			free(gfarm_file);
			return (GFARM_ERR_NO_MEMORY);
		}
		e = search_idle_cyclic(hosts_state,
		    nihosts, ihost_filter, ihost_iterator,
		    shortage, residual);
		if (e == NULL)
			e = gfarm_fixedstrings_dup(shortage,residual,residual);
		if (e != NULL) {
			free(residual);
			gfarm_strings_free_deeply(nfrags, hosts);
			free(gfarm_file);
			return (e);
		}
		for (i = 0; i < nfrags; i++) {
			if (hosts[i] == NULL) {
				hosts[i] = residual[--shortage];
				if (shortage == 0)
					break;
			}
		}
		free(residual);
	}
	*nhostsp = nfrags;
	*hostsp = hosts;
	free(gfarm_file);
	return (e);
}

char *
gfarm_url_hosts_schedule(const char *gfarm_url, char *option,
	int *nhostsp, char ***hostsp)
{
	char *e, *gfarm_file, **hosts;
	int i, nfrags, n_all_hosts;
	struct gfarm_host_info *all_hosts;
	struct gfarm_hash_table *hosts_state;
	char section[GFARM_INT32STRLEN + 1];

	e = gfarm_url_fragment_number(gfarm_url, &nfrags);
	if (e != NULL)
		return (e);
	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL)
		return (e);
	hosts = malloc(sizeof(char *) * nfrags);
	if (hosts == NULL) {
		free(gfarm_file);
		return (GFARM_ERR_NO_MEMORY);
	}
	e = alloc_hosts_state(&n_all_hosts, &all_hosts, &hosts_state);
	if (e != NULL) {
		free(hosts);
		free(gfarm_file);
		return (e);
	}
	for (i = 0; i < nfrags; i++) {
		sprintf(section, "%d", i);
		e = schedule_by_file_section(hosts_state,
		    gfarm_file, section, &null_filter,
		    1, &hosts[i]);
		if (e != NULL) {
			free_hosts_state(n_all_hosts, all_hosts, hosts_state);
			gfarm_strings_free_deeply(i, hosts);
			free(gfarm_file);
			return (e);
		}
	}
	free_hosts_state(n_all_hosts, all_hosts, hosts_state);
	free(gfarm_file);
	*nhostsp = nfrags;
	*hostsp = hosts;
	return (NULL);
}

char *
gfarm_url_hosts_schedule_by_program(
	char *gfarm_url, char *program, char *option,
	int *nhostsp, char ***hostsp)
{
	char *e;
	int n_all_hosts, n_runnable_hosts;
	struct gfarm_host_info *all_hosts;
	struct gfarm_hash_table *hosts_state;
	struct host_runnable_filter host_filter;
	struct host_info_array_iterator host_iterator;

	if (!gfarm_is_url(program))
		return (gfarm_url_hosts_schedule(gfarm_url, option,
		    nhostsp, hostsp));

	e = hosts_for_program(program, &n_all_hosts, &all_hosts, &hosts_state,
	    &n_runnable_hosts);
	if (e != NULL)
		return (e);
	e = url_hosts_schedule_filtered(hosts_state, gfarm_url, option,
	    n_runnable_hosts,
	    init_host_runnable_filter(&host_filter, hosts_state),
	    init_host_info_array_iterator(&host_iterator, all_hosts),
	    nhostsp, hostsp);
	free_hosts_state(n_all_hosts, all_hosts, hosts_state);
	return (e);
}
