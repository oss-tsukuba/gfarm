#include <stdio.h> /* sprintf */
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <gfarm/gfarm.h>
#ifdef HAVE_POLL
#include <poll.h>
#endif
#include "host.h"
#include "gfs_client.h"

char GFARM_ERR_PANIC_NO_REPLICA_REMAINS[] = "PANIC: no replica remains";
char GFARM_ERR_HOSTS_UNAVAILABLE[] =
    "available hosts count is less than requested";

struct replied_host_of_search_idle {
	char *hostname;
	double loadavg;
};

struct state_of_search_idle {
	int nreplied_hosts;
	struct replied_host_of_search_idle *replied_hosts;
};

struct closure_for_search_idle {
	struct state_of_search_idle *state;
	char *hostname;
};

static void
search_idle_callback(void *closure, struct sockaddr *addr,
	struct gfs_client_load *result, char *error)
{
	struct closure_for_search_idle *c = closure;
	struct state_of_search_idle *s = c->state;

	if (error == NULL) {
		struct replied_host_of_search_idle *h =
		    &s->replied_hosts[s->nreplied_hosts++];

		h->hostname = c->hostname;
		h->loadavg = result->loadavg_1min;
	}
	free(c);
}

static int
loadavg_compare(const void *a, const void *b)
{
	const struct replied_host_of_search_idle *p = a;
	const struct replied_host_of_search_idle *q = b;
	const double l1 = p->loadavg;
	const double l2 = q->loadavg;

	if (l1 < l2)
		return (-1);
        else if (l1 > l2)
		return (1);
	else
		return (0);
}

#define QUERY_LIMIT 10	/* ask 10 hosts */

/* 
 * Orders host names risingly by load average.
 * Returns error if the number of active hosts is less than nohosts.
 */
char *
gfarm_schedule_search_idle_hosts(
	int nihosts, char **ihosts, int nohosts, char **ohosts)
{
	char *e, *e_save = NULL;
	int i;
	struct gfs_client_udp_requests *udp_requests;
	struct sockaddr addr;
	struct state_of_search_idle s;
	struct closure_for_search_idle *c;

	if (nihosts < nohosts)
		return (GFARM_ERR_HOSTS_UNAVAILABLE);
	s.nreplied_hosts = 0;
	s.replied_hosts = malloc(
	    nihosts * sizeof(struct replied_host_of_search_idle));
	if (s.replied_hosts == NULL)
		return (GFARM_ERR_NO_MEMORY);
	e = gfarm_client_init_load_requests(QUERY_LIMIT, &udp_requests);
	if (e != NULL) {
		free (s.replied_hosts);
		return (e);
	}
	for (i = 0; i < nihosts; i++) {
		e = gfarm_host_address_get(ihosts[i],
		    gfarm_spool_server_port,
		    &addr, NULL);
		if (e != NULL) {
			if (e_save == NULL)
				e_save = e;
			continue;
		}
		c = malloc(sizeof(*c));
		if (c == NULL) {
			e_save = GFARM_ERR_NO_MEMORY;
			break;
		}
		c->state = &s;
		c->hostname = ihosts[i];
		e = gfarm_client_add_load_request(udp_requests, &addr,
		    c, search_idle_callback);
		if (s.nreplied_hosts >= nohosts)
			break;
	}
	e = gfarm_client_wait_all_load_results(udp_requests);		
	if (s.nreplied_hosts < nohosts) {
		free(s.replied_hosts);
		if (e_save != NULL)
			return (e_save);
		else 
			return (GFARM_ERR_HOSTS_UNAVAILABLE);
	} else if (s.nreplied_hosts > nohosts) {
		/* to select n minimum load average hosts */
		qsort(s.replied_hosts, s.nreplied_hosts,
		    sizeof(*s.replied_hosts), loadavg_compare);
	}
	for (i = 0; i < nohosts; i++)
		ohosts[i] = s.replied_hosts[i].hostname;
	free(s.replied_hosts);
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

	if (nhosts < nhosts_req) {
		e = GFARM_ERR_HOSTS_UNAVAILABLE;
		goto finish;
	}
	if (nhosts == nhosts_req) { /* there is no other choice */
		for (i = 0; i < nhosts; i++) {
			hostnames_found[i] = strdup(hosts[i].hostname);
			if (hostnames_found[i] == NULL) {
				while (--i >= 0)
					free(hostnames_found[i]);
				e = GFARM_ERR_NO_MEMORY;
				goto finish;
			}
		}
		goto finish;
	}
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

	if (ncopies == 1) { /* there is no other choice. */
		*hostp = copies[0].hostname;		
		return (NULL);
	}
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
		return (GFARM_ERR_PANIC_NO_REPLICA_REMAINS);
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
		return (GFARM_ERR_PANIC_NO_REPLICA_REMAINS);
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
		if (e != NULL)
			return (e);
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
gfarm_host_domain_hosts_schedule(const char *domainname,
	int *nhostsp, char ***hostsp)
{
	char *e;
	struct gfarm_host_info *hinfos;
	int i, j, nhosts = 0, nhinfos;
	char **hosts;

	*nhostsp = 0;
	*hostsp = NULL;

	e = gfarm_host_info_get_all(&nhinfos, &hinfos);
	if (e != NULL)
		return (e);
	for (i = 0; i < nhinfos; ++i) {
		if (strstr(hinfos[i].hostname, domainname))
			++nhosts;
	}
	if (nhosts == 0)
		return ("no host");
	hosts = malloc(sizeof(char *) * nhosts);
	if (hosts == NULL)
		return (GFARM_ERR_NO_MEMORY);

	j = 0;
	for (i = 0; i < nhinfos; ++i) {
		if (strstr(hinfos[i].hostname, domainname)) {
			hosts[j] = strdup(hinfos[i].hostname);
			if (hosts[j] == NULL)
				return (GFARM_ERR_NO_MEMORY);
			++j;
		}
	}
	gfarm_host_info_free_all(nhinfos, hinfos);
	*nhostsp = nhosts;
	*hostsp = hosts;
	return (NULL);
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
