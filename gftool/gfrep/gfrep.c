/*
 * $Id$
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <gfarm/gfarm.h>

#include "gfs_client.h"

#define LINELEN	2048

char *program_name = "gfrep";

/* Do not use gfrepbe_client/gfrepbe_server, but use gfsd internal routine */
#if 0 /* XXX for now */
int bootstrap_method = 0;
#else
int bootstrap_method = 1;
#endif
int verbose = 0;

struct replication_job {
	struct replication_job *next;

	char *file;
	char *section;
	char *src;
	char *dest;
};

struct replication_job_list {
	struct replication_job *head;
	struct replication_job **tail;
	int n;
};

struct replication_pair {
	struct replication_pair *next;

	gfarm_stringlist files, sections;
	struct gfs_client_rep_backend_state *state;
	char *src, *dest;
};

struct replication_pair_list {
	struct replication_pair *head;
	struct replication_pair **tail;
	int max_fragments_per_pair;
};


static void
replication_job_list_init(struct replication_job_list *list)
{
	list->head = NULL;
	list->tail = &list->head;
	list->n = 0;
}

static int
replication_job_list_add(struct replication_job_list *list,
	char *file, char *section, char *src, char *dest)
{
	char *e, *gfarm_file, *dest_canonical;
	struct replication_job *job;

	if (bootstrap_method) {
		if (src != NULL) {
			e = gfarm_url_section_replicate_from_to(
				file, section, src, dest);
			if (e != NULL)
				fprintf(stderr,
				    "%s: replicate %s:%s from %s to %s: %s\n",
				    program_name, file, section, src, dest, e);
		} else {
			e = gfarm_url_section_replicate_to(
				file, section, dest);
			if (e != NULL)
				fprintf(stderr,
				    "%s: replicate %s:%s to %s: %s\n",
				    program_name, file, section, dest, e);
		}
		return (e != NULL ? 1 : 0);
	}

	e = gfarm_url_make_path(file, &gfarm_file);
	if (e != NULL) {
		fprintf(stderr, "%s: parsing pathname %s: %s\n",
		    program_name, file, e);
		return (1);
	}

	e = gfarm_host_get_canonical_name(dest, &dest_canonical);
	if (e != NULL) {
		fprintf(stderr, "%s: host %s isn't a filesystem node: %s\n",
		    program_name, dest, e);
		free(gfarm_file);
		return (1);
	}
	if (gfarm_file_section_copy_info_does_exist(gfarm_file, section,
	    dest_canonical)) { /* already exists, don't have to replicate */
		free(dest_canonical);
		free(gfarm_file);
		return (0);
	}

	job = malloc(sizeof(*job));
	if (job == NULL) {
		fprintf(stderr, "%s: no memory\n", program_name);
		exit(EXIT_FAILURE);
	}
	job->file = gfarm_file;
	job->section = strdup(section);
	if (job->section == NULL) {
		fprintf(stderr, "%s: no memory\n", program_name);
		exit(EXIT_FAILURE);
	}
	job->dest = dest_canonical;
	if (src != NULL) {
		e = gfarm_host_get_canonical_name(src, &job->src);
		if (e != NULL) {
			fprintf(stderr,
			    "%s: checking source host %s for %s:%s: %s",
			    program_name, src, file, section, e);
			free(job);
			free(dest_canonical);
			free(gfarm_file);
			return (1);
		}
	} else {
		e = gfarm_file_section_host_schedule(job->file, section,
		    &job->src);
		if (e != NULL) {
			fprintf(stderr,
			    "%s: searching source host for %s:%s: %s",
			    program_name, file, section, e);
			free(job);
			free(dest_canonical);
			free(gfarm_file);
			return (1);
		}
	}

	job->next = NULL;
	*list->tail = job;
	list->tail = &job->next;
	list->n++;
	return (0);
}

static void
replication_pair_init(struct replication_pair_list *transfers)
{
	transfers->head = NULL;
	transfers->tail = &transfers->head;
	transfers->max_fragments_per_pair = 0;
}

static int
replication_pair_entry(struct replication_pair_list *transfers,
	struct replication_job **list, int n)
{
	char *e;
	int i;
	struct replication_pair *pair = malloc(sizeof(*pair));

	if (pair == NULL) {
		fprintf(stderr, "%s: no memory\n", program_name);
		exit(EXIT_FAILURE);
	}

	e = gfarm_stringlist_init(&pair->files);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(EXIT_FAILURE);
	}
	e = gfarm_stringlist_init(&pair->sections);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(EXIT_FAILURE);
	}
	for (i = 0; i < n; i++) {
		e = gfarm_stringlist_add(&pair->files, list[i]->file);
		if (e != NULL) {
			fprintf(stderr, "%s: %s\n", program_name, e);
			exit(EXIT_FAILURE);
		}
		e = gfarm_stringlist_add(&pair->sections, list[i]->section);
		if (e != NULL) {
			fprintf(stderr, "%s: %s\n", program_name, e);
			exit(EXIT_FAILURE);
		}
	}
	pair->src = list[0]->src;
	pair->dest = list[0]->dest;
	if (verbose)
		fprintf(stderr, "%s: scheduling %s -> %s\n",
		    program_name, pair->src, pair->dest);
	e = gfarm_file_section_replicate_multiple_request(
	    &pair->files, &pair->sections,
	    pair->src, pair->dest, &pair->state);
	if (e != NULL) {
		fprintf(stderr, "%s: replication from %s to %s: %s\n",
			program_name, pair->src, pair->dest, e);
		gfarm_stringlist_free(&pair->files);
		gfarm_stringlist_free(&pair->sections);
		free(pair);
		return (1);
	}

	pair->next = NULL;
	*transfers->tail = pair;
	transfers->tail = &pair->next;
	if (transfers->max_fragments_per_pair < n)
		transfers->max_fragments_per_pair = n;
	return (0);
}

static int
replication_pair_results(struct replication_pair_list *transfers)
{
	char *e, **results =
	    malloc(sizeof(*results) * transfers->max_fragments_per_pair);
	struct replication_pair *pair;
	int i, n, error_happend = 0;

	if (results == NULL) {
		fprintf(stderr, "%s: cannot allocate memory for %d results\n",
		    program_name, transfers->max_fragments_per_pair);
		exit(EXIT_FAILURE);
	}
	for (pair = transfers->head; pair != NULL; pair = pair->next) {
		e = gfarm_file_section_replicate_multiple_result(pair->state,
		    results);
		if (e != NULL) {
			fprintf(stderr, "%s: replication from %s to %s: %s\n",
			    program_name, pair->src, pair->dest, e);
			error_happend = 1;
			continue;
		}
		n = gfarm_stringlist_length(&pair->files);
		for (i = 0; i < n; i++) {
			if (results[i] == NULL)
				continue;
			fprintf(stderr,
			    "%s: replicate %s:%s from %s to %s: %s\n",
			    program_name,
			    gfarm_stringlist_elem(&pair->files, i),
			    gfarm_stringlist_elem(&pair->sections, i),
			    pair->src, pair->dest,
			    e);
			error_happend = 1;
		}
	}
	return (error_happend);
}

/* sort by (src, dest) */
static int
job_compare(const void *a, const void *b)
{
	struct replication_job *const * jpa = a, *const * jpb = b;
	struct replication_job *ja = *jpa, *jb = *jpb;
	int cmp;

	if ((cmp = strcasecmp(ja->src, jb->src)) < 0)
		return (-1);
	else if (cmp > 0)
		return (1);
	else if ((cmp = strcasecmp(ja->dest, jb->dest)) < 0)
		return (-1);
	else if (cmp > 0)
		return (1);
	else if ((cmp = strcmp(ja->file, jb->file)) < 0)
		return (-1);
	else if (cmp > 0)
		return (1);
	else
		return (strcmp(ja->section, jb->section));
}

static int
replication_job_list_execute(struct replication_job_list *list)
{
	int i, top;
	struct replication_job *job, **jobs;
	int error_happend = 0;
	struct replication_pair_list transfers;

	if (bootstrap_method) {
		/* All jobs are already completed here with bootstrap mode */
		return (0);
	}

	if (list->n == 0) /* do nothing */
		return (0);
	jobs = malloc(sizeof(*jobs) * list->n);
	if (jobs == NULL) {
		fprintf(stderr, "%s: no memory\n", program_name);
		exit(EXIT_FAILURE);
	}
	for (job = list->head, i = 0; job != NULL; job = job->next, i++) {
		if (i >= list->n) { /* sanity */
			fprintf(stderr, "%s: panic: more than %d jobs\n",
			    program_name, list->n);
			exit(EXIT_FAILURE);
		}
		jobs[i] = job;
	}
	qsort(jobs, list->n, sizeof(*jobs), job_compare);

	replication_pair_init(&transfers);
	top = 0;
	for (i = 1; i < list->n; i++) {
		if (strcasecmp(jobs[top]->src, jobs[i]->src) != 0 ||
		    strcasecmp(jobs[top]->dest, jobs[i]->dest) != 0) {
			if (replication_pair_entry(&transfers,
			    &jobs[top], i - top))
				error_happend = 1;
			top = i;
		}
	}
	if (replication_pair_entry(&transfers, &jobs[top], list->n - top))
		error_happend = 1;

	if (replication_pair_results(&transfers))
		error_happend = 1;

	return (error_happend);
}

static int
replicate_by_fragment_dest_list(char *fragment_dest_list, char *src_default)
{
	int error_happend = 0;
	struct replication_job_list job_list;
	int n, lineno;
	char line[LINELEN];
	char file[LINELEN], section[LINELEN], dest[LINELEN], src[LINELEN];
	FILE *fp;

	fp = fopen(fragment_dest_list, "r");
	if (fp == NULL) {
		fprintf(stderr, "%s: %s: %s\n",
		    program_name, fragment_dest_list, strerror(errno));
		exit(EXIT_FAILURE);
	}
	replication_job_list_init(&job_list);
	for (lineno = 1; fgets(line, sizeof line, fp) != NULL; lineno++) {
		n = sscanf(line, "%s %s %s %s", file, section, dest, src);
		if (n == 0)
			continue;
		if (n < 3) {
			fprintf(stderr, "%s: %s line %d: "
			    "gfarm-URL, fragment-name and dest-node "
			    "are required, skip this line\n",
			    program_name, fragment_dest_list, lineno);
			continue;
		} else if (n > 4) {
			fprintf(stderr, "%s: %s line %d: "
			    "there are more than 4 fields, ignore extras\n",
			    program_name, fragment_dest_list, lineno);
		}
		if (replication_job_list_add(&job_list, file, section,
		    n > 3 ? src : src_default, dest))
			error_happend = 1;
	}
	fclose(fp);
	return (error_happend | replication_job_list_execute(&job_list));
}

static int
jobs_by_pairs(struct replication_job_list *job_list,
	char *gfarm_url, int npairs,
	gfarm_stringlist *src_nodes, gfarm_stringlist *dst_nodes)
{
	int i, j, k, error_happend = 0;
	char *e, *gfarm_file, *src_host, *dst_host;
	int nfragments, ncopies;
	struct gfarm_file_section_copy_info *copies;
	char section[GFARM_INT32STRLEN];

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL) {
		fprintf(stderr, "%s: cannot determine pathname for %s: %s\n",
		    program_name, gfarm_url, e);
		return (error_happend);
	}
	e = gfarm_url_fragment_number(gfarm_url, &nfragments);
	if (e != NULL) {
		fprintf(stderr, "%s: %s: cannot get fragment number: %s\n",
		    program_name, gfarm_url, e);
		return (error_happend);
	}
	for (i = 0; i < nfragments; i++) {
		sprintf(section, "%d", i);
		e = gfarm_file_section_copy_info_get_all_by_section(
			gfarm_file, section, &ncopies, &copies);
		if (e != NULL) {
			fprintf(stderr,
			    "%s: %s:%s: cannot get replica location: %s\n",
			    program_name, gfarm_url, section, e);
			continue;
		}
		for (j = 0; j < npairs; j++) {
			src_host = gfarm_stringlist_elem(src_nodes, j);
			dst_host = gfarm_stringlist_elem(dst_nodes, j);
			for (k = 0; k < ncopies; k++) {
				if(strcasecmp(copies[k].hostname,src_host)==0){
#if 0
					printf("%s %s %s %s\n",
					    gfarm_url, section,
					    dst_host, src_host);
#else
					if (replication_job_list_add(job_list,
					    gfarm_url, section,
					    src_host, dst_host))
						error_happend = 1;
#endif
					goto found;
				}
			}
		}
		fprintf(stderr, "%s: error: %s:%s - no replica is found\n",
		    program_name, gfarm_url, section);
	found:
		gfarm_file_section_copy_info_free_all(ncopies, copies);
	}
	return (error_happend);
}

static void
pairlist_read(char *pair_list,
	int *npairsp, gfarm_stringlist *src_nodes, gfarm_stringlist *dst_nodes)
{
	char *e, *s;
	int npairs, lineno, n;
	FILE *fp;
	char line[LINELEN], src[LINELEN], dst[LINELEN];

	e = gfarm_stringlist_init(src_nodes);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(EXIT_FAILURE);
	}
	e = gfarm_stringlist_init(dst_nodes);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(EXIT_FAILURE);
	}

	if ((fp = fopen(pair_list, "r")) == NULL) {
		fprintf(stderr, "%s: %s: %s\n",
		    program_name, pair_list, strerror(errno));
		exit(EXIT_FAILURE);
	}
	npairs = 0;
	for (lineno = 1; fgets(line, sizeof line, fp) != NULL; lineno++) {
		/* remove comment */
		s = strchr(line, '#');
		if (s != NULL)
			*s = '\0';

		n = sscanf(line, "%s %s\n", src, dst);
		if (n == 0)
			continue;
		if (n != 2) {
			fprintf(stderr,
			    "%s: %s line %d: field number is not 2\n",
			    program_name, pair_list, lineno);
			exit(EXIT_FAILURE);
		}

		e = gfarm_host_get_canonical_name(src, &s);
		if (e != NULL) {
			fprintf(stderr, "%s: %s isn't a filesystem node: %s\n",
			    program_name, src, e);
			exit(EXIT_FAILURE);
		}
		e = gfarm_stringlist_add(src_nodes, s);
		if (e != NULL) {
			fprintf(stderr, "%s: string %s: %s\n",
			    program_name, src, e);
			exit(EXIT_FAILURE);
		}

		e = gfarm_host_get_canonical_name(dst, &s);
		if (e != NULL) {
			fprintf(stderr, "%s: %s isn't a filesystem node: %s\n",
			    program_name, dst, e);
			exit(EXIT_FAILURE);
		}
		e = gfarm_stringlist_add(dst_nodes, s);
		if (e != NULL) {
			fprintf(stderr, "%s: string %s: %s\n",
			    program_name, dst, e);
			exit(EXIT_FAILURE);
		}

		npairs++;
	}
	fclose(fp);
	*npairsp = npairs;
}

static char *
get_default_section(char *file, char *dest)
{
	char *e;
	struct gfs_stat gst;

	e = gfs_stat(file, &gst);
	if (e != NULL) {
		fprintf(stderr, "%s: %s: %s\n", program_name, file, e);
		return (NULL);
	}
	if (GFARM_S_IS_PROGRAM(gst.st_mode)) {
		/* architecture of dest host (cache) */
		static char *architecture = NULL;

		gfs_stat_free(&gst);
		if (architecture == NULL) {
			char *dest_canonical;

			e = gfarm_host_get_canonical_name(dest,
			    &dest_canonical);
			if (e != NULL) {
				fprintf(stderr, "%s: host %s: %s\n",
				    program_name, dest, e);
				exit(EXIT_FAILURE);
			}
			architecture =
			    gfarm_host_info_get_architecture_by_host(
			    dest_canonical);
			free(dest_canonical);
			if (architecture == NULL) {
				fprintf(stderr, "%s: host %s: %s\n",
				    program_name, dest,
				    "cannot get architecture");
				exit(EXIT_FAILURE);
			}
		}
		return (architecture); /* assume -a <architecture of dest> */
	} else {
		int nfrags = gst.st_nsections;

		gfs_stat_free(&gst);
		/*
		 * Special case for replicating a Gfarm
		 * file having only one fragment
		 */
		if (nfrags != 1) {
			fprintf(stderr,
			    "%s: %s has more than one fragments, skipped\n",
			    program_name, file);
			return (NULL);
		}
		return ("0");  /* assume -I 0 */
	}
}

static char *
add_cwd_to_relative_path(char *cwd, const char *path)
{
	char *p;

	path = gfarm_url_prefix_skip(path);
	if (*path == '/') {
		p = strdup(path);
		if (p == NULL) {
			fprintf(stderr, "%s: %s\n", program_name,
							 GFARM_ERR_NO_MEMORY);
			exit(EXIT_FAILURE);
		}
	} else {
		p = malloc(strlen(cwd) + strlen(path) + 2);
		if (p == NULL) {
			fprintf(stderr, "%s: %s\n", program_name,
							 GFARM_ERR_NO_MEMORY);
			exit(EXIT_FAILURE);
		}
		sprintf(p, "%s/%s", cwd, path);
	}
	return (p);
}

static int
traverse_file_tree_with_cwd(char *cwd,
			    char *path,
			    int (*file_processor)(char *, char *, void *),
			    void *closure)
{
	char *e;
	struct gfs_stat gs;
	GFS_Dir dir;
	struct gfs_dirent *entry;
	gfarm_mode_t mode;
	int error_happend = 0;

	e = gfs_stat(path, &gs);
	if (e != NULL) {
		fprintf(stderr, "%s: gfs_stat: %s in gfarm:%s: %s\n",
					program_name, path, cwd, e);
		return (1);
	}
	mode = gs.st_mode;
	gfs_stat_free(&gs);
	if (GFARM_S_ISREG(mode)) {
		return ((*file_processor)(cwd, path, closure));
	} else if (GFARM_S_ISDIR(mode)) {
		e = gfs_chdir(path);
		if (e != NULL) {
			fprintf(stderr, "%s: gfs_chdir: %s: %s\n",
							program_name, path, e);
			return (1);
		}
		e = gfs_opendir(".", &dir);
		if (e != NULL) {
			fprintf(stderr, "%s: gfs_opendir: %s: %s\n",
							program_name, path, e);
			return (1);
		}
		while ((e = gfs_readdir(dir, &entry)) == NULL &&
				entry != NULL) {
			char *ncwd, *url; 

			if (strcmp(entry->d_name, ".") == 0 ||
			    strcmp(entry->d_name, "..") == 0) { 
				continue;
			}
			ncwd = add_cwd_to_relative_path(cwd, path);
			url = gfarm_url_prefix_add(entry->d_name);	
			if (url == NULL) {
				fprintf(stderr, "%s: %s\n",
					 program_name, GFARM_ERR_NO_MEMORY);
				exit(EXIT_FAILURE);
			}
			if (traverse_file_tree_with_cwd(ncwd, url,
					   file_processor, closure)) {
				error_happend = 1;
			}
			free(url);
			free(ncwd);
		}
		e = gfs_chdir("..");
		if (e != NULL) {
			fprintf(stderr, "%s: gfs_chdir: %s/..: %s\n",
						program_name, path, e);
			exit(EXIT_FAILURE);
		}
	}
	return (error_happend);
}

static int
traverse_file_tree(char *path,
		   int (*file_processor)(char *, char *, void *),
		   void *closure)
{
	char *e;
	char cwdbf[PATH_MAX * 2];

	e = gfs_getcwd(cwdbf, sizeof(cwdbf));
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		return (1);
	}
	return (traverse_file_tree_with_cwd(cwdbf, path,
						 file_processor, closure));
}

struct replicate_to_hosts_closure {
	char *url;
	int nhosts;
	char **hosttab;
};

static int
replicate_to_hosts_callback(char *cwd, char *url, void *closure)
{
	struct replicate_to_hosts_closure *c = closure;
	char *e;
	int error_happened = 0;

	e = gfarm_url_fragments_replicate(url, c->nhosts, c->hosttab);
	if (e != NULL) {
		fprintf(stderr, "%s: %s: %s\n",	program_name, url, e);
		error_happened = 1;
	}
	return (error_happened);
}

static int
replicate_files_to_hosts(char *url, int nhosts, char **hosttab)
{
	struct replicate_to_hosts_closure c;

	c.nhosts = nhosts;
	c.hosttab = hosttab;

	return traverse_file_tree(url, replicate_to_hosts_callback, &c);
}

static int
collect_file_paths_callback(char *cwd, char *url, void *closure)
{
	gfarm_stringlist *path_list = closure;
	char *e, *p;
	
	p = add_cwd_to_relative_path(cwd, url);
	url = gfarm_url_prefix_add(p);
	free(p);
	if (url == NULL) {
		fprintf(stderr, "%s: %s\n", program_name, GFARM_ERR_NO_MEMORY);
		exit(EXIT_FAILURE);
	}
	e = gfarm_stringlist_add(path_list, url);
	if (e != NULL) {
		fprintf(stderr, "%s: collect_file_path_callback: %s\n",
							program_name, e);
		exit(EXIT_FAILURE);
	}  
	return (0);
}

static char *
get_hosts_have_replica_in_domain(
	char *url, char *section,
	char *domainname,
	int *nrhosts, char ***rhosts)
{
	char *e, *gfarm_file, **hosts;
	int i, ncinfos, nhosts;
	struct gfarm_file_section_copy_info *cinfos;

	e = gfarm_url_make_path(url, &gfarm_file);
	if (e != NULL)
		return(e);
	e = gfarm_file_section_copy_info_get_all_by_section(
				 gfarm_file, section, &ncinfos, &cinfos);
	free(gfarm_file);
	if (e != NULL)
		return(e);
	hosts = malloc(sizeof(*hosts) * ncinfos);
	if (hosts == NULL) {
		fprintf(stderr, "%s: %s\n", program_name, GFARM_ERR_NO_MEMORY);
		exit(EXIT_FAILURE);
	}
	nhosts = 0;
	for (i = 0; i < ncinfos; i++)
		if (gfarm_host_is_in_domain(cinfos[i].hostname, domainname)) {
			char *p = strdup(cinfos[i].hostname);
			if (p == NULL) {
				fprintf(stderr, "%s: %s\n",
					program_name, GFARM_ERR_NO_MEMORY);
				exit(EXIT_FAILURE);
			}
			hosts[nhosts] = p;
			nhosts++;
		}
	gfarm_file_section_copy_info_free_all(ncinfos, cinfos);
	*nrhosts = nhosts;
	*rhosts = hosts;
	return(NULL);
}

#define min(a,b) (((a)<(b))?(a):(b))

static int
search_not_have_replica_host(int pos,
			     int ndhosts, char **dhosts,
			     int *nrhosts,  char ***rhosts)
{
	int i, j, exist_in_rhosts;

	for (i = 0; i < ndhosts; i++) {
		exist_in_rhosts = 0;
		for (j = 0; j < *nrhosts; j++) {
			if (strcasecmp(dhosts[pos], (*rhosts)[j]) == 0) {
				exist_in_rhosts = 1;
				break;
			}
		}
		if (exist_in_rhosts == 0) { /* found a host */
			char **p;

			(*nrhosts)++;
			p = realloc(*rhosts, sizeof(**rhosts) * (*nrhosts));
			if (p == NULL) {
				fprintf(stderr, "%s: %s\n",
					program_name, GFARM_ERR_NO_MEMORY);
				exit(EXIT_FAILURE);
			}
			p[*nrhosts - 1] = strdup(dhosts[pos]);
			if (p[*nrhosts - 1] == NULL) {
				fprintf(stderr, "%s: %s\n",
					program_name, GFARM_ERR_NO_MEMORY);
				exit(EXIT_FAILURE);
			}
			*rhosts = p;
			return (pos);
		}
		pos = (pos + 1) % ndhosts;
	}
	return (-1);
}

static int
replicate_files_to_domain(char *path, int min_replicas, char *domainname)
{
	char *e;
	int i, j, k, k2, m;
	gfarm_stringlist path_list;
	int *nfragments, nhosts;
	char **hosts = NULL;
	struct gfarm_file_section_info **sinfos;
	int error_happend = 0;
	
	e = gfarm_stringlist_init(&path_list);
	if (e != NULL) {
		fprintf(stderr, "%s %s: %s\n", program_name, path, e);
		exit(EXIT_FAILURE);
	}

	if (traverse_file_tree(path, collect_file_paths_callback, &path_list))
 		error_happend = 1;

	nfragments = malloc(sizeof(*nfragments) *
			gfarm_stringlist_length(&path_list));
	if (nfragments == NULL) {
		fprintf(stderr, "%s: %s\n", program_name, GFARM_ERR_NO_MEMORY);
		exit(EXIT_FAILURE);
	}
	sinfos = malloc(sizeof(*sinfos) * gfarm_stringlist_length(&path_list));
	if (sinfos == NULL) {
		fprintf(stderr, "%s: %s\n", path, GFARM_ERR_NO_MEMORY);
		exit(EXIT_FAILURE);
	}
	nhosts = 0;
	for (i = 0; i < gfarm_stringlist_length(&path_list); i++) {
		char *e, *url, *gfarm_file;

		url = gfarm_stringlist_elem(&path_list, i);
		e = gfarm_url_make_path(url, &gfarm_file);
		if (e != NULL) {
			fprintf(stderr, "%s: %s: %s\n", program_name, url, e);
			continue;
		}
		e = gfarm_file_section_info_get_all_by_file(
			gfarm_file, &nfragments[i], &sinfos[i]);
		free(gfarm_file);
		if (e != NULL) {
			fprintf(stderr,	"%s: %s: %s\n",	program_name, url, e);
			nfragments[i] = 0;
			sinfos[i] = NULL;
			continue;
		}
		nhosts += nfragments[i];
	}
	nhosts *= min_replicas;
	hosts = malloc(sizeof(*hosts) * nhosts);
	if (hosts == NULL) {
		fprintf(stderr, "%s: %s\n", program_name, GFARM_ERR_NO_MEMORY);
		exit(EXIT_FAILURE);
	}
	e = gfarm_schedule_search_idle_acyclic_by_domainname(
			domainname, &nhosts, hosts);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(EXIT_FAILURE);
	}
	if (nhosts < min_replicas)
		fprintf(stderr,
			"%s: warning: #host in domain(%d) < "
				     "#replica required(%d)\n",
			program_name, nhosts, min_replicas);
	k = 0;
	for (i = 0; i < gfarm_stringlist_length(&path_list); i++) {
		char *file_path, **rhosts;
		int nrhosts;

		file_path = gfarm_stringlist_elem(&path_list, i);
		for (j = 0; j < nfragments[i]; j++) {
			e = get_hosts_have_replica_in_domain(
				file_path, sinfos[i][j].section,
				domainname, &nrhosts, &rhosts);
			if (e != NULL) {
				fprintf(stderr,
					"%s: get_hosts_replica_in_domain:"
					" %s: %s: %s\n",
					program_name, file_path,
					sinfos[i][j].section, e);
				continue;
			}
			m = min(min_replicas, nhosts) - nrhosts;
			if (m <= 0) {
				gfarm_strings_free_deeply(nrhosts, rhosts);
				continue;
			}
			while (m--) {
				k2 = search_not_have_replica_host(
				  k, nhosts, hosts, &nrhosts, &rhosts);
				if (k2 == -1) /* not found */
					break;
				k = k2;
				e = gfarm_url_section_replicate_to(
					file_path,
					sinfos[i][j].section,
					hosts[k]);
				if (e != NULL) {
					fprintf(stderr,
						"%s: gfarm_url_section_"
						"replicate_to %s: %s: %s\n",
						program_name, file_path,
						sinfos[i][j].section, e);
					continue;
				}
				k = (k + 1) % nhosts;
			}
			gfarm_strings_free_deeply(nrhosts, rhosts);
		}
	}
	gfarm_strings_free_deeply(nhosts, hosts);
	for (i = 0; i < gfarm_stringlist_length(&path_list); i++)
		if (sinfos[i] != NULL)
			gfarm_file_section_info_free_all(nfragments[i],
						sinfos[i]);
	free(sinfos);
	free(nfragments);
	gfarm_stringlist_free_deeply(&path_list);
	return (error_happend);
}

struct replicate_to_dest_closure {
	struct replication_job_list *list;
	char *index;
	char *src;
	char *dest;
};

static int
replicate_to_dest_callback(char *cwd, char *url, void *closure)
{
	struct replicate_to_dest_closure *c = closure;
	char *section = c->index;
	char *p;

	if (c->index == NULL) { /* special case */
		section = get_default_section(url, c->dest);
		if (section == NULL) {
			return (1);
		}
	}
	p = add_cwd_to_relative_path(cwd, url);
	url = gfarm_url_prefix_add(p);
	free(p);
	if (url == NULL) {
		fprintf(stderr, "%s: %s\n", program_name, GFARM_ERR_NO_MEMORY);
		exit(EXIT_FAILURE);
	}
	return (replication_job_list_add(c->list, url, section,
							c-> src, c->dest));
}

static int
replicate_to_dest(struct replication_job_list *list,
	char *url, char *index, char *src, char *dest)
{
	struct replicate_to_dest_closure c;

	c.list = list;
	c.index = index;
	c.src = src;
	c.dest = dest;

	return traverse_file_tree(url, replicate_to_dest_callback, &c);
}

static void
usage()
{
	fprintf(stderr, "Usage: %s [option] <gfarm_url>...\n", program_name);
	fprintf(stderr, "option:\n");
	fprintf(stderr, "\t-b\t\t\tuse bootstrap mode\n");
	fprintf(stderr, "\t-v\t\t\tverbose message\n");
	fprintf(stderr, "\t-H <hostfile>\t\treplicate a whole file\n");
	fprintf(stderr, "\t-D <domainname>\t\treplicate a whole file\n");
	fprintf(stderr, "\t-I fragment-index\treplicate a fragment"
		" with -d option\n");
	fprintf(stderr, "\t-s src-node\n");
	fprintf(stderr, "\t-d dest-node\n");
	fprintf(stderr, "\t-l <fragment-dest-list-file>\n");
	fprintf(stderr, "\t-P <host-pair-file>\n");
	fprintf(stderr, "\t-N <number-of-replicas-per-fragment>\n");
	exit(EXIT_FAILURE);
}

static void
conflict_check(int *mode_ch_p, int ch)
{
	if (*mode_ch_p) {
		fprintf(stderr, "%s: -%c option conflicts with -%c\n",
			program_name, ch, *mode_ch_p);
		usage();
	}
	*mode_ch_p = ch;
}

int
main(argc, argv)
	int argc;
	char **argv;
{
	char *e, *file;
	int i, ch, mode_ch = 0;
	gfarm_stringlist paths;
	gfs_glob_t types;
	int error_happened = 0, min_replicas = 1;

	char *hostfile = NULL, *domainname = NULL, *index = NULL;
	char *src = NULL, *dest = NULL, *fragment_dest_list = NULL;
	char *pair_list = NULL;

	if (argc >= 1)
		program_name = basename(argv[0]);
	e = gfarm_initialize(&argc, &argv);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(EXIT_FAILURE);
	}

	while ((ch = getopt(argc, argv, "bXvH:D:I:s:d:l:P:N:?")) != -1) {
		switch (ch) {
		case 'b':
			bootstrap_method = 1;
			break;
		case 'X': /* use eXternal command (gfrepbe_*) */
			bootstrap_method = 0;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'H':
			hostfile = optarg; conflict_check(&mode_ch, ch);
			break;
		case 'D':
			domainname = optarg; conflict_check(&mode_ch, ch);
			break;
		case 'I':
			index = optarg;
			break;
		case 's':
			src = optarg;
			break;
		case 'd':
			dest = optarg; conflict_check(&mode_ch, ch);
			break;
		case 'l':
			fragment_dest_list = optarg;
			conflict_check(&mode_ch, ch);
			break;
		case 'P':
			pair_list = optarg; conflict_check(&mode_ch, ch);
			break;
		case 'N':
			min_replicas = strtol(optarg, NULL, 0);
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (bootstrap_method)
		gfarm_replication_set_method(
		    GFARM_REPLICATION_BOOTSTRAP_METHOD);
	if (src != NULL && dest == NULL && fragment_dest_list == NULL) {
		fprintf(stderr,
		    "%s: -s <src> option only works with -d or -l option\n",
		    program_name);
		exit(EXIT_FAILURE);
	}
	if (index != NULL && dest == NULL) {
		fprintf(stderr,
		    "%s: -I <index> option only works with -d option\n",
		    program_name);
		exit(EXIT_FAILURE);
	}
	if (min_replicas > 1 && mode_ch != 0 && mode_ch != 'D') {
		fprintf(stderr,
		    "%s: error: -N num-replica option is unavailable "
		    "with -%c option\n", program_name, mode_ch);
		exit(EXIT_FAILURE); 
	}
	if (argc == 0 && fragment_dest_list == NULL)
		usage();
	if (min_replicas == 1 && mode_ch == 0) {
		fprintf(stderr, "%s: warning: -N 1 (default) is meaningless "
		    "without another option\n", program_name);
		exit(EXIT_SUCCESS);
	}

	e = gfarm_stringlist_init(&paths);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(EXIT_FAILURE);
	}
	e = gfs_glob_init(&types);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(EXIT_FAILURE);
	}
	for (i = 0; i < argc; i++)
		gfs_glob(argv[i], &paths, &types);

	if (hostfile != NULL) {
		char **hosttab;
		int nhosts, error_line;

		/* replicate directories and whole files */
		e = gfarm_hostlist_read(hostfile, &nhosts,
			&hosttab, &error_line);
		if (e != NULL) {
			if (error_line != -1)
				fprintf(stderr, "%s: line %d: %s\n",
					hostfile, error_line, e);
			else
				fprintf(stderr, "%s: %s\n",
					program_name, e);
			exit(EXIT_FAILURE);
		}
		for (i = 0; i < gfarm_stringlist_length(&paths); i++) {
			file = gfarm_stringlist_elem(&paths, i);
			error_happened = replicate_files_to_hosts(
						file, nhosts, hosttab);
		}
	} else if (fragment_dest_list != NULL) {
		/* replicate specified fragments */
		if (bootstrap_method) {
			fprintf(stderr, "%s: -l option isn't supported "
			    "on bootstrap mode\n",
			    program_name);
			/* It's easy to support this, but probably too slow */
			usage();
			exit(EXIT_FAILURE);
		}
		error_happened =
		    replicate_by_fragment_dest_list(fragment_dest_list, src);
	} else if (pair_list != NULL) {
		int npairs;
		gfarm_stringlist src_nodes, dst_nodes;
		struct replication_job_list job_list;

		pairlist_read(pair_list, &npairs, &src_nodes, &dst_nodes);
		replication_job_list_init(&job_list);
		for (i = 0; i < gfarm_stringlist_length(&paths); i++) {
			file = gfarm_stringlist_elem(&paths, i);
			if (verbose)
				fprintf(stderr, "%s: scan fragments for %s\n",
				    program_name, file);
			if (jobs_by_pairs(&job_list, file,
			    npairs, &src_nodes, &dst_nodes))
				error_happened = 1;
		}
		if (replication_job_list_execute(&job_list))
			error_happened = 1;
	} else if (dest != NULL) { /* -I may be omitted */
		struct replication_job_list job_list;

		/* replicate specified fragments */
		replication_job_list_init(&job_list);
		for (i = 0; i < gfarm_stringlist_length(&paths); i++) {
			file = gfarm_stringlist_elem(&paths, i);
			if (replicate_to_dest(&job_list,
			    file, index, src, dest))
				error_happened = 1;
		}
		if (replication_job_list_execute(&job_list))
			error_happened = 1;
	} else {
		/* replicate directories and whole files */
		if (domainname == NULL)
			domainname = "";
		for (i = 0; i < gfarm_stringlist_length(&paths); i++) {
			file = gfarm_stringlist_elem(&paths, i);
			if (replicate_files_to_domain(file, min_replicas,
							domainname))
				error_happened = 1;
		}
	}	
	gfs_glob_free(&types);
	e = gfarm_terminate();
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(EXIT_FAILURE);
	}
	return (error_happened);
}
