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
#include <gfarm/gfarm.h>

#include "gfs_client.h"

#define LINELEN	2048

char *program_name = "gfrep";

/* Do not use gfrepbe_client/gfrepbe_server, but use gfsd internal routine */
int bootstrap_method = 0;

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


void
replication_job_list_init(struct replication_job_list *list)
{
	list->head = NULL;
	list->tail = &list->head;
	list->n = 0;
}

int
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
		exit(1);
	}
	job->file = gfarm_file;
	job->section = strdup(section);
	if (job->section == NULL) {
		fprintf(stderr, "%s: no memory\n", program_name);
		exit(1);
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

void
replication_pair_init(struct replication_pair_list *transfers)
{
	transfers->head = NULL;
	transfers->tail = &transfers->head;
	transfers->max_fragments_per_pair = 0;
}

int
replication_pair_entry(struct replication_pair_list *transfers,
	struct replication_job **list, int n)
{
	char *e;
	int i;
	struct replication_pair *pair = malloc(sizeof(*pair));

	if (pair == NULL) {
		fprintf(stderr, "%s: no memory\n", program_name);
		exit(1);
	}

	e = gfarm_stringlist_init(&pair->files);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}
	e = gfarm_stringlist_init(&pair->sections);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}
	for (i = 0; i < n; i++) {
		e = gfarm_stringlist_add(&pair->files, list[i]->file);
		if (e != NULL) {
			fprintf(stderr, "%s: %s\n", program_name, e);
			exit(1);
		}
		e = gfarm_stringlist_add(&pair->sections, list[i]->section);
		if (e != NULL) {
			fprintf(stderr, "%s: %s\n", program_name, e);
			exit(1);
		}
	}
	pair->src = list[0]->src;
	pair->dest = list[0]->dest;
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

int
replication_pair_results(struct replication_pair_list *transfers)
{
	char *e, **results =
	    malloc(sizeof(*results) * transfers->max_fragments_per_pair);
	struct replication_pair *pair;
	int i, n, error_happend = 0;

	if (results == NULL) {
		fprintf(stderr, "%s: cannot allocate memory for %d results\n",
		    program_name, transfers->max_fragments_per_pair);
		exit(1);
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
int
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

int
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
		exit(1);
	}
	for (job = list->head, i = 0; job != NULL; job = job->next, i++) {
		if (i >= list->n) { /* sanity */
			fprintf(stderr, "%s: panic: more than %d jobs\n",
			    program_name, list->n);
			exit(1);
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

int
replicate_by_fragment_list(char *fragment_list, char *src_default, char *dest)
{
	int error_happend = 0;
	struct replication_job_list job_list;
	int n, lineno;
	char line[LINELEN], file[LINELEN], section[LINELEN], src[LINELEN];
	FILE *fp;

	fp = fopen(fragment_list, "r");
	if (fp == NULL) {
		fprintf(stderr, "%s: %s: %s\n",
		    program_name, fragment_list, strerror(errno));
		exit(1);
	}
	replication_job_list_init(&job_list);
	for (lineno = 1; fgets(line, sizeof line, fp) != NULL; lineno++) {
		n = sscanf(line, "%s %s %s", file, section, src);
		if (n == 0)
			continue;
		if (n < 2) {
			fprintf(stderr, "%s: %s line %d: "
			    "gfarm-URL and fragment-name are required, "
			    "skip this line\n",
			    program_name, fragment_list, lineno);
			continue;
		} else if (n > 3) {
			fprintf(stderr, "%s: %s line %d: "
			    "there are more than 3 fields, ignore extras\n",
			    program_name, fragment_list, lineno);
		}
		if (replication_job_list_add(&job_list, file, section,
		    n > 2 ? src : src_default, dest))
			error_happend = 1;
	}
	fclose(fp);
	return (error_happend | replication_job_list_execute(&job_list));
}

int
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
		exit(1);
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

int
replicate_by_fragments(int n, char **files, char *index, char *src, char *dest)
{
	int error_happend = 0;
	char *e;
	int i;
	struct replication_job_list job_list;

	replication_job_list_init(&job_list);

	for (i = 0; i < n; i++) {
		char *section = index;

		if (index == NULL) {
			/*
			 * Special case for replicating a Gfarm file
			 * having only one fragment
			 */
			int nfrags;

			e = gfarm_url_fragment_number(files[i], &nfrags);
			if (e != NULL) {
				fprintf(stderr, "%s: %s: %s\n",
				    program_name, files[i], e);
				continue;
			}
			if (nfrags != 1) {
				fprintf(stderr,
				    "%s: %s has more than "
				    "one fragments, skipped\n",
				    program_name, files[i]);
				continue;
			}
			/* XXX program case */
			section = "0";  /* assume -I 0 */
		}
		if (replication_job_list_add(&job_list, files[i], section,
		    src, dest))
			error_happend = 1;
	}
	return (error_happend | replication_job_list_execute(&job_list));
}

void
usage()
{
	fprintf(stderr, "Usage: %s [option] <gfarm_url>...\n", program_name);
	fprintf(stderr, "option:\n");
	fprintf(stderr, "\t-b\t\t\tuse bootstrap mode\n");
	fprintf(stderr, "\t-H <hostfile>\t\treplicate a whole file\n");
	fprintf(stderr, "\t-D <domainname>\t\treplicate a whole file\n");
	fprintf(stderr, "\t-I fragment-index\treplicate a fragment"
		" with -d option\n");
	fprintf(stderr, "\t-s src-node\n");
	fprintf(stderr, "\t-d dest-node\n");
	fprintf(stderr, "\t-l <fragment-list-file>\n");
	fprintf(stderr, "\t-L <fragment-dest-list-file>\n");
	exit(1);
}

void
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
	extern char *optarg;
	extern int optind;
	int argc_save = argc;
	char **argv_save = argv;
	char *e, *hostfile = NULL;
	int i, ch, nhosts, error_line, mode_ch = 0;
	char **hosttab;
	char *index = NULL, *src = NULL, *dest = NULL, *domainname = NULL;
	char *fragment_list = NULL;
	char *fragment_dest_list = NULL;
	int error_happened = 1;

	if (argc >= 1)
		program_name = basename(argv[0]);

	while ((ch = getopt(argc, argv, "bH:D:I:s:d:l:L:")) != -1) {
		switch (ch) {
		case 'b':
			bootstrap_method = 1;
			break;
		case 'H':
			hostfile = optarg; conflict_check(&mode_ch, ch);
			break;
		case 'D':
			domainname = optarg; conflict_check(&mode_ch, ch);
			break;
		case 'I':
			index = optarg; conflict_check(&mode_ch, ch);
			break;
		case 's':
			src = optarg;
			break;
		case 'd':
			dest = optarg;
			break;
		case 'l':
			fragment_list = optarg; conflict_check(&mode_ch, ch);
			break;
		case 'L':
			fragment_dest_list = optarg;
			conflict_check(&mode_ch, ch);
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	e = gfarm_initialize(&argc_save, &argv_save);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}

	if (bootstrap_method)
		gfarm_replication_set_method(
		    GFARM_REPLICATION_BOOTSTRAP_METHOD);
	if (src != NULL && (hostfile != NULL || domainname != NULL)) {
		fprintf(stderr,
		    "%s: warning: -s src option is ignored with -%c option\n",
		    program_name, hostfile != NULL ? 'H' : 'D');
	}
	if (argc == 0 && fragment_list == NULL && fragment_dest_list == NULL)
		usage();
	if (hostfile != NULL) {
		/* replicate a whole file */
		e = gfarm_hostlist_read(hostfile, &nhosts,
			&hosttab, &error_line);
		if (e != NULL) {
			if (error_line != -1)
				fprintf(stderr, "%s: line %d: %s\n",
					hostfile, error_line, e);
			else
				fprintf(stderr, "%s: %s\n",
					program_name, e);
			exit(1);
		}
		for (i = 0; i < argc; i++) {
			e = gfarm_url_fragments_replicate(argv[i],
				nhosts, hosttab);
			if (e != NULL) {
				fprintf(stderr, "%s: %s: %s\n",
				    program_name, argv[i], e);
				error_happened = 1;
			}
		}
	} else if (domainname != NULL) {
		/* replicate a whole file */
		for (i = 0; i < argc; i++) {
			e= gfarm_url_fragments_replicate_to_domainname(
			    argv[i], domainname);
			if (e != NULL) {
				fprintf(stderr, "%s: %s: %s\n",
				    program_name, argv[i], e);
				error_happened = 1;
			}
		}
	} else if (fragment_list != NULL) {
		/* replicate specified fragments */
		if (dest == NULL) {
			fprintf(stderr,
			    "%s: -d dest-node option is required with -l\n",
			    program_name);
			usage();
			exit(1);
		}
		if (bootstrap_method) {
			fprintf(stderr, "%s: -l option isn't supported "
			    "on bootstrap mode\n",
			    program_name);
			/* It's easy to support this, but probably too slow */
			usage();
			exit(1);
		}
		error_happened =
		    replicate_by_fragment_list(fragment_list, src, dest);
	} else if (fragment_dest_list != NULL) {
		/* replicate specified fragments */
		if (bootstrap_method) {
			fprintf(stderr, "%s: -l option isn't supported "
			    "on bootstrap mode\n",
			    program_name);
			/* It's easy to support this, but probably too slow */
			usage();
			exit(1);
		}
		error_happened =
		    replicate_by_fragment_dest_list(fragment_dest_list, src);
	} else { /* -I may be omitted */
		/* replicate specified fragments */
		if (dest == NULL) {
			fprintf(stderr,
			    "%s: -d dest-node option is required with -l\n",
			    program_name);
			usage();
			exit(1);
		}
		error_happened =
		    replicate_by_fragments(argc, argv, index, src, dest);
	}
	e = gfarm_terminate();
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}
	return (error_happened);
}
