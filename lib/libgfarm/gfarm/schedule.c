#include <stdio.h> /* sprintf */
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <gfarm/gfarm.h>
#include "auth.h" /* XXX gfarm_random_initialize() */
#include "gfs_client.h"

char GFARM_ERR_NO_REPLICA[] = "no replica";
char GFARM_ERR_NO_HOST[] = "no filesystem node";

#define IDLE_LOAD_AVERAGE	0.1F
#define SEMI_IDLE_LOAD_AVERAGE	0.5F

struct search_idle_available_host {
	char *hostname;
	float loadavg;
};

struct search_idle_state {
	struct search_idle_available_host *available_hosts;
	int available_hosts_number;
	int idle_hosts_number;
	int semi_idle_hosts_number;
};

struct search_idle_callback_closure {
	struct search_idle_state *state;
	char *hostname;
};

static void
search_idle_callback(void *closure, struct sockaddr *addr,
	struct gfs_client_load *load, char *error)
{
	struct search_idle_callback_closure *c = closure;
	struct search_idle_state *s = c->state;

	if (error == NULL) {
		struct search_idle_available_host *h =
		    &s->available_hosts[s->available_hosts_number++];

		h->hostname = c->hostname;
		h->loadavg = load->loadavg_1min;
		if (h->loadavg <= SEMI_IDLE_LOAD_AVERAGE) {
			s->semi_idle_hosts_number++;
			if (h->loadavg <= IDLE_LOAD_AVERAGE)
				s->idle_hosts_number++;
		}
	}
	free(c);
}

static int
loadavg_compare(const void *a, const void *b)
{
	const struct search_idle_available_host *p = a;
	const struct search_idle_available_host *q = b;
	const float l1 = p->loadavg;
	const float l2 = q->loadavg;

	if (l1 < l2)
		return (-1);
        else if (l1 > l2)
		return (1);
	else
		return (0);
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
	int nihosts, char **ihosts, int *nohostsp, char **ohosts)
{
	char *e;
	int i, desired_number = *nohostsp;
	struct gfs_client_udp_requests *udp_requests;
	struct sockaddr addr;
	struct search_idle_state s;
	struct search_idle_callback_closure *c;

	s.available_hosts_number =
	    s.idle_hosts_number = s.semi_idle_hosts_number = 0;
	s.available_hosts = malloc(nihosts * sizeof(*s.available_hosts));
	if (s.available_hosts == NULL)
		return (GFARM_ERR_NO_MEMORY);
	e = gfarm_client_init_load_requests(concurrency, &udp_requests);
	if (e != NULL) {
		free(s.available_hosts);
		return (e);
	}
	for (i = 0; i < nihosts; i++) {
		e = gfarm_host_address_get(ihosts[i], gfarm_spool_server_port,
		    &addr, NULL);
		if (e != NULL)
			continue;
		c = malloc(sizeof(*c));
		if (c == NULL)
			break;
		c->state = &s;
		c->hostname = ihosts[i];
		e = gfarm_client_add_load_request(udp_requests, &addr,
		    c, search_idle_callback);
		if (s.idle_hosts_number >= desired_number ||
		    s.semi_idle_hosts_number >= enough_number)
			break;
	}
	e = gfarm_client_wait_all_load_results(udp_requests);
	if (s.available_hosts_number == 0) {
		free(s.available_hosts);
		*nohostsp = 0;
		return (GFARM_ERR_NO_HOST);
	}

	/* sort hosts in the order of load average */
	qsort(s.available_hosts, s.available_hosts_number,
	    sizeof(*s.available_hosts), loadavg_compare);

	for (i = 0; i < s.available_hosts_number && i < desired_number; i++)
		ohosts[i] = s.available_hosts[i].hostname;
	*nohostsp = i;
	free(s.available_hosts);

	return (NULL);
}

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
	int nihosts, char **ihosts, int *nohostsp, char **ohosts)
{
	char *e, **shuffled_ihosts;
	int i;

	/* shuffle ihosts[] to avoid unbalanced host selection */
	shuffled_ihosts = malloc(nihosts * sizeof(*shuffled_ihosts));
	if (shuffled_ihosts == NULL)
		return (GFARM_ERR_NO_MEMORY);
	for (i = 0; i < nihosts; i++)
		shuffled_ihosts[i] = ihosts[i];
	shuffle_strings(nihosts, shuffled_ihosts);

	e = search_idle(concurrency, enough_number,
	    nihosts, shuffled_ihosts, nohostsp, ohosts);
	free(shuffled_ihosts);
	return (e);
}

#endif /* USE_SHUFFLED */

#define CONCURRENCY	10
#define ENOUGH_RATE	4

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
	int i, j, nfound = nohosts;
#ifdef USE_SHUFFLED
	char *e = search_idle_shuffled(CONCURRENCY, nohosts * ENOUGH_RATE,
	    nihosts, ihosts, &nfound, ohosts);
#else
	char *e = search_idle(CONCURRENCY, nohosts * ENOUGH_RATE,
	    nihosts, ihosts, &nfound, ohosts);
#endif

	if (e != NULL)
		return (e);
	if (nfound == 0) {
		/* Oh, my god */
		return (GFARM_ERR_NO_HOST);
	}
	for (i = nfound, j = 0; i < nohosts; i++, j++) {
		if (j >= nfound)
			j = 0;
		ohosts[i] = ohosts[j];
	}
	return (NULL);
}

char *
gfarm_schedule_search_idle_by_all(int nhosts_req, char **hostnames_found)
{
	char *e, **hostnames;
	int i, nhosts;
	struct gfarm_host_info *hosts;

   	e = gfarm_host_info_get_all(&nhosts, &hosts);
	if (e != NULL)
		return (e);

	hostnames = malloc(sizeof(char *) * nhosts);
	if (hostnames == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto finish;
	}
	for (i = 0; i < nhosts; i++)
		hostnames[i] = hosts[i].hostname;
	e = gfarm_schedule_search_idle_hosts(nhosts, hostnames,
	    nhosts_req, hostnames_found);
	if (e != NULL)
		goto free_hostnames;
	for (i = 0; i < nhosts_req; i++) {
		hostnames_found[i] = strdup(hostnames_found[i]);
		if (hostnames_found[i] == NULL) {
			while (--i >= 0)
				free(hostnames_found[i]);
			e = GFARM_ERR_NO_MEMORY;
			goto free_hostnames;
		}
	}
free_hostnames:
	free(hostnames);
finish:
	gfarm_host_info_free_all(nhosts, hosts);
	return (e);
}

static char *
gfarm_schedule_search_idle_by_section_copy_info(
	int ncopies, struct gfarm_file_section_copy_info *copies, char **hostp)
{
	char *e, **hostnames;
	int i;

	hostnames = malloc(sizeof(char *) * ncopies);
	if (hostnames == NULL)
		return (GFARM_ERR_NO_MEMORY);
	for (i = 0; i < ncopies; i++)
		hostnames[i] = copies[i].hostname;
	e = gfarm_schedule_search_idle_hosts(
	    ncopies, hostnames, 1, hostp);
	free(hostnames);
	return (e);
}

char *
gfarm_file_section_host_schedule(char *gfarm_file, char *section, char **hostp)
{
	char *e;
	int ncopies;
	struct gfarm_file_section_copy_info *copies;
	char *host;

	e = gfarm_file_section_copy_info_get_all_by_section(
	    gfarm_file, section, &ncopies, &copies);
	if (e != NULL)
		return (e);
	if (ncopies == 0) {
		gfarm_file_section_copy_info_free_all(ncopies, copies);
		return (GFARM_ERR_NO_REPLICA);
	}
	e = gfarm_schedule_search_idle_by_section_copy_info(
		ncopies, copies, &host);
	*hostp = strdup(host);
	gfarm_file_section_copy_info_free_all(ncopies, copies);
	if (*hostp == NULL)
		return (GFARM_ERR_NO_MEMORY);
	return (NULL);
}

char *
gfarm_file_section_host_schedule_with_priority_to_local(
	char *gfarm_file, char *section, char **hostp)
{
	char *e, *self_name, *host;
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
	if (i < ncopies) /* local */
		host = copies[i].hostname;	
	else {
		e = gfarm_schedule_search_idle_by_section_copy_info(
			ncopies, copies, &host);
		if (e != NULL) {
			gfarm_file_section_copy_info_free_all(ncopies, copies);
			return (e);
		}
	}

	*hostp = strdup(host);
	gfarm_file_section_copy_info_free_all(ncopies, copies);
	if (*hostp == NULL)
		return (GFARM_ERR_NO_MEMORY);
	return (NULL);
}


char *
gfarm_url_hosts_schedule(char *gfarm_url, char *option,
	int *nhostsp, char ***hostsp)
{
	char *e, *gfarm_file, **hosts;
	int i, nfrags;

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
	for (i = 0; i < nfrags; i++) {
		char section_string[GFARM_INT32STRLEN + 1];

		sprintf(section_string, "%d", i);
		e = gfarm_file_section_host_schedule(
		    gfarm_file, section_string, &hosts[i]);
		if (e != NULL) {
			gfarm_strings_free_deeply(i, hosts);
			free(gfarm_file);
			return (e);
		}
	}
	free(gfarm_file);
	*nhostsp = nfrags;
	*hostsp = hosts;
	return (NULL);
}

/*
 * lists host names that contains domainname.
 */

char *
gfarm_schedule_search_idle_by_domainname(const char *domainname,
	int nhosts, char **hosts)
{
	char *e;
	struct gfarm_host_info *hinfos;
	int i, j, domain_nhosts = 0, nhinfos;
	char **domain_hosts;

	e = gfarm_host_info_get_all(&nhinfos, &hinfos);
	if (e != NULL)
		return (e);
	for (i = 0; i < nhinfos; ++i) {
		if (strstr(hinfos[i].hostname, domainname))
			++domain_nhosts;
	}
	if (domain_nhosts == 0) {
		e = GFARM_ERR_NO_HOST;
		goto finish;
	}
	domain_hosts = malloc(sizeof(char *) * domain_nhosts);
	if (domain_hosts == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto finish;
	}
	j = 0;
	for (i = 0; i < nhinfos; ++i) {
		if (strstr(hinfos[i].hostname, domainname)) {
			domain_hosts[j++] = hinfos[i].hostname;
		}
	}
	e = gfarm_schedule_search_idle_hosts(domain_nhosts, domain_hosts,
	    nhosts, hosts);
	if (e != NULL)
		goto free_hostnames;
	for (i = 0; i < nhosts; i++) {
		hosts[i] = strdup(hosts[i]);
		if (hosts[i] == NULL) {
			while (--i >= 0)
				free(hosts[i]);
			e = GFARM_ERR_NO_MEMORY;
			goto free_hostnames;
		}
	}
free_hostnames:
	free(domain_hosts);
finish:
	gfarm_host_info_free_all(nhinfos, hinfos);
	return (e);
}

/*
 * expands a host list up to nhosts.  This function does not need
 * gfarm_strings_free_deeply() for hostsp.
 */

char *
gfarm_hosts_schedule_expand_cyclic(int nhosts, char ***hostsp,
	int nsrchosts, char **srchosts)
{
	int i;
	char **hosts;

	hosts = malloc(sizeof(char *) * nhosts);
	if (hosts == NULL) {
		return (GFARM_ERR_NO_MEMORY);
	}
	for (i = 0; i < nhosts; ++i) {
		hosts[i] = srchosts[i % nsrchosts];
	}
	*hostsp = hosts;
	return (NULL);
}
