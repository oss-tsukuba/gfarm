#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#include <string.h>
#include <assert.h>
#include <gfarm/gfarm.h>
#include "schedule.h" /* gfarm_strings_expand_cyclic() */

/*
 *  Register a local file to Gfarm filesystem
 *
 *  gfreg <local_filename> <gfarm_url>
 *
 *  $Id$
 */

/* Don't permit set[ug]id bit for now */
#define FILE_MODE_MASK		0777

/*
 * This value is only used as a last resort,
 * when the local file argument is "-" only,
 * and if `gfarm_url' is a directory or doesn't exist.
 */
#define DEFAULT_FILE_MODE	0644

char *program_name = "gfreg";

static const char STDIN_FILENAME[] = "-";

void
usage()
{
    fprintf(stderr, "Usage: %s [option] <local_filename> ... <gfarm_url>\n",
	    program_name);
    fprintf(stderr, "Register local file(s) to Gfarm filesystem.\n\n");
    fprintf(stderr, "option:\n");
    fprintf(stderr, "\t-I fragment-index\tspecify a fragment index\n");
    fprintf(stderr, "\t-N number\t\ttotal number of fragments\n");
    fprintf(stderr, "\t-a architecture\t\tspecify an architecture\n");
    fprintf(stderr, "\t-h hostname\t\tspecify a hostname\n");
    fprintf(stderr, "\t-H hostfile\t\tspecify hostnames by a file\n");
    fprintf(stderr, "\t-D domainname\t\tspecify a domainname\n");
    fprintf(stderr, "\t-f \t\t\tforce to register\n");
    exit(EXIT_FAILURE);
}

static int opt_force = 0;
static int error_happened = 0;

static int
open_file(char *filename, int *fdp, int *fd_needs_close_p)
{
	int fd;

	if (strcmp(filename, STDIN_FILENAME) == 0) {
		*fdp = STDIN_FILENO;
		*fd_needs_close_p = 0;
		return (1);
	}
	if ((fd = open(filename, O_RDONLY)) == -1) {
		fprintf(stderr, "%s: cannot open %s: %s\n",
		    program_name, filename, strerror(errno));
		error_happened = 1;
		return (0);
	}
	*fdp = fd;
	*fd_needs_close_p = 1;
	return (1);
}

static int
get_mode(int fd, char *filename, gfarm_mode_t *mode_p)
{
	struct stat s;

	if (fstat(fd, &s) == -1) {
		fprintf(stderr, "%s: cannot stat %s: %s\n",
		    program_name, filename, strerror(errno));
		error_happened = 1;
		return (0);
	}
	*mode_p = s.st_mode;
	return (1);
}

static int
get_file_mode(int fd, char *filename, gfarm_mode_t *file_mode_p)
{
	if (!get_mode(fd, filename, file_mode_p))
		return (0);
	*file_mode_p &= FILE_MODE_MASK;
	return (1);
}

static int
concat_dir_name(const char *gfarm_url, const char *base_name,
	char **target_url_p)
{
	char *target_url =
	    malloc(strlen(gfarm_url) + 1 + strlen(base_name) + 1);

	if (target_url == NULL) {
		fprintf(stderr, "%s: %s\n", program_name, GFARM_ERR_NO_MEMORY);
		error_happened = 1;
		return (0);
	}
	if (*gfarm_path_dir_skip(gfarm_url_prefix_skip(gfarm_url)) != '\0')
		sprintf(target_url, "%s/%s", gfarm_url, base_name);
	else
		sprintf(target_url, "%s%s", gfarm_url, base_name);
	*target_url_p = target_url;
	return (1);
}

static int
section_does_not_exists(char *gfarm_url, char *section)
{
	struct gfs_stat s;

	if (gfs_stat_section(gfarm_url, section, &s) == NULL) {
		gfs_stat_free(&s);
		fprintf(stderr, "%s: %s:%s already exists\n",
		    program_name, gfarm_url, section);
		return (0);
	}
	return (1);
}

#define GFS_FILE_BUFSIZE 65536

static void
copy_file(int fd, GFS_File gf, char *gfarm_url, char *section)
{
	char *e;
	ssize_t rv;
	int length; /* XXX - should be size_t */
	char buffer[GFS_FILE_BUFSIZE];

	for (;;) {
		rv = read(fd, buffer, sizeof(buffer));
		if (rv <= 0)
			break;
		/* XXX - partial write case ? */
		e = gfs_pio_write(gf, buffer, rv, &length);
		if (e != NULL) {
			fprintf(stderr, "%s: writing to %s:%s: %s\n",
			    program_name, gfarm_url, section, e);
			error_happened = 1;
			break;
		}
	}
}

static int
get_nsections(char *gfarm_url, int *nsectionsp)
{
	char *e, *gfarm_file;
	struct gfarm_path_info pi;

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL) {
		fprintf(stderr, "%s: gfarm_url_make_path(%s): %s\n",
		    program_name, gfarm_url, e);
		error_happened = 1;
		return (0);
	}
	e = gfarm_path_info_get(gfarm_file, &pi);
	free(gfarm_file);
	if (e != NULL) {
		if (e == GFARM_ERR_NO_SUCH_OBJECT)
			fprintf(stderr,
			    "%s: missing -N <total number of fragments>\n",
			    program_name);
		else
			fprintf(stderr, "%s: gfarm_get_path_info(%s): %s\n",
			    program_name, gfarm_url, e);
		error_happened = 1;
		return (0);
	}
	*nsectionsp = pi.status.st_nsections;
	gfarm_path_info_free(&pi);
	return (1);
}

static void
register_program(int is_dir, char *gfarm_url, char *section, char *hostname,
	char *filename, int use_file_mode, gfarm_mode_t file_mode)
{
	char *e;
	int fd, fd_needs_close;
	char *target_url;
	GFS_File gf;

	if (!open_file(filename, &fd, &fd_needs_close))
		return;
	if (!use_file_mode && !get_file_mode(fd, filename, &file_mode))
		goto finish;

	if (!is_dir)
		target_url = gfarm_url;
	else if (!concat_dir_name(gfarm_url, gfarm_path_dir_skip(filename),
	    &target_url))
		goto finish;

	if (opt_force || section_does_not_exists(target_url, section)) {
		e = gfs_pio_create(target_url,
		    GFARM_FILE_WRONLY|GFARM_FILE_TRUNC, file_mode, &gf);
		if (e != NULL) {
			fprintf(stderr, "%s: cannot open %s: %s\n",
			    program_name, target_url, e);
			error_happened = 1;
		} else {
			if ((e = gfs_pio_set_view_section(gf, section,
			    hostname, 0)) != NULL) {
				fprintf(stderr, "%s: cannot open %s:%s: %s\n",
				    program_name, target_url, section, e);
				error_happened = 1;
			} else {
				copy_file(fd, gf, target_url, section);
			}
			gfs_pio_close(gf);
		}
	}
	if (target_url != gfarm_url)
		free(target_url);
 finish:
	if (fd_needs_close)
		close(fd);
}

static void
register_fragment(int is_dir, char *gfarm_url, int index, int nfragments,
	char *hostname,
	char *filename, int use_file_mode, gfarm_mode_t file_mode)
{
	char *e;
	int fd, fd_needs_close;
	char *target_url;
	GFS_File gf;
	char section[GFARM_INT32STRLEN + 1];

	if (!open_file(filename, &fd, &fd_needs_close))
		return;
	if (!use_file_mode && !get_file_mode(fd, filename, &file_mode))
		goto finish;

	if (!is_dir)
		target_url = gfarm_url;
	else if (!concat_dir_name(gfarm_url, gfarm_path_dir_skip(filename),
	    &target_url))
		goto finish;

	if (nfragments == GFARM_FILE_DONTCARE &&
	    !get_nsections(target_url, &nfragments))
		goto finish_url;

	sprintf(section, "%d", index);
	if (opt_force || section_does_not_exists(target_url, section)) {
		e = gfs_pio_create(target_url,
		    GFARM_FILE_WRONLY|GFARM_FILE_TRUNC, file_mode, &gf);
		if (e != NULL) {
			fprintf(stderr, "%s: cannot open %s: %s\n",
			    program_name, target_url, e);
			error_happened = 1;
		} else {
			if ((e = gfs_pio_set_view_index(gf, nfragments, index,
			    hostname, 0)) != NULL) {
				fprintf(stderr, "%s: cannot open %s:%d: %s\n",
				    program_name, target_url, index, e);
				error_happened = 1;
			} else {
				copy_file(fd, gf, target_url, section);
			}
			gfs_pio_close(gf);
		}
	}
 finish_url:
	if (target_url != gfarm_url)
		free(target_url);
 finish:
	if (fd_needs_close)
		close(fd);
}

int
main(int argc, char *argv[])
{
	/* options */
	char *section = NULL;
	int nfragments = GFARM_FILE_DONTCARE; /* -1, actually */
	char *hostname = NULL;
	char *hostfile = NULL;
	char *domainname = NULL;

	char *e, *gfarm_url, *file_mode_arg;
	gfarm_mode_t file_mode = DEFAULT_FILE_MODE;
	int c, i, is_dir, index;
	struct gfs_stat gs;

	e = gfarm_initialize(&argc, &argv);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(EXIT_FAILURE);
	}

	/*  Command options  */

	while ((c = getopt(argc, argv, "a:fh:D:I:N:?")) != -1) {
		switch (c) {
		case 'I':
		case 'a':
			section = optarg;
			break;
		case 'H':
			hostfile = optarg;
			break;
		case 'N':
			nfragments = strtol(optarg, NULL, 0);
			break;
		case 'h':
			hostname = optarg;
			break;
		case 'D':
			domainname = optarg;
			break;
		case 'f':
			opt_force = 1;
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc == 0) {
		fprintf(stderr, "%s: missing a local filename\n",
			program_name);
		usage();
	}
	if (argc == 1) {
		fprintf(stderr, "%s: missing a Gfarm URL\n",
			program_name);
		usage();
	}
	gfarm_url = argv[argc - 1];
	--argc;

	if (!gfarm_is_url(gfarm_url)) {
		fprintf(stderr, "%s: %s: %s\n",
		    program_name, gfarm_url,
		    GFARM_ERR_GFARM_URL_PREFIX_IS_MISSING);
		exit(EXIT_FAILURE);
	}

	c = 0;
	if (hostname != NULL)
		c++;
	if (hostfile != NULL)
		c++;
	if (domainname != NULL)
		c++;
	if (c > 1) {
		fprintf(stderr,
		    "%s: more than one options are specified "
		    "from -h, -H and -D\n",
		    program_name);
		usage();
	}

	/*
	 * distinguish which mode is specified:
	 * 1. program mode:
	 *	gfreg [-h <hostname>] [-a <architecture>] \
	 *		<local-program>... <gfarm-URL>
	 * 2. auto index mode:
	 *	gfreg [-h <hostname>] [-H <hostfile>] [-D <domainname>] \
	 *		<local-file>... <gfarm-URL>
	 * 3. fragment mode:
	 *	gfreg [-h <hostname>] [-N <nfragments>] -I <index> \
	 *		<local-file>... <gfarm-URL>
	 */

	e = gfs_stat(gfarm_url, &gs);
	if (e == GFARM_ERR_NO_SUCH_OBJECT) {
		is_dir = 0;
		file_mode_arg = NULL;
	} else if (e != NULL) {
		fprintf(stderr, "%s: %s: %s\n", program_name, gfarm_url, e);
		exit(EXIT_FAILURE);
	} else {
		if (GFARM_S_ISREG(gs.st_mode)) {
			is_dir = 0;
			file_mode_arg = gfarm_url;
			file_mode = gs.st_mode;
		} else if (GFARM_S_ISDIR(gs.st_mode)) {
			is_dir = 1;
			file_mode_arg = NULL;
		} else { /* defensive programming. this shouldn't happen. */
			fprintf(stderr, "%s: %s: unknown file type\n",
			    program_name, gfarm_url);
			exit(EXIT_FAILURE);
		}
		gfs_stat_free(&gs);
	}

	c = 0; /* count of "-" in the arguments */
	if (hostfile != NULL && strcmp(hostfile, STDIN_FILENAME) == 0)
		++c;
	for (i = 0; i < argc; i++) {
		int fd, fd_needs_close;
		gfarm_mode_t m;

		if (!open_file(argv[i], &fd, &fd_needs_close))
			exit(EXIT_FAILURE);
		if (!get_mode(fd, argv[i], &m))
			exit(EXIT_FAILURE);
		if (S_ISREG(m)) {
			if (file_mode_arg == NULL) {
				/*
				 * NOTE: this mode may be used for the mode
				 * to create the gfarm file.
				 */
				file_mode_arg = argv[i];
				file_mode = m & FILE_MODE_MASK;
			}
			if (((m & 0111) != 0) != ((file_mode & 0111) != 0)) {
				fprintf(stderr,
				    "%s: program and non-program are mixed in "
				    "%s and %s\n",
				    program_name, file_mode_arg, argv[i]);
				exit(EXIT_FAILURE);
			}
		} else if (fd_needs_close) {
			/* if it's "-", allow non-file (e.g. pipe) */
			fprintf(stderr, "%s: %s: not a regular file\n",
			    program_name, argv[i]);
			exit(EXIT_FAILURE);
		}
		if (fd_needs_close) {
			close(fd);
		} else if (++c > 1) {
			fprintf(stderr, "%s: `-' (stdin) is specified "
			    "multiple times\n", program_name);
			exit(EXIT_FAILURE);
		}
	}

	if ((file_mode & 0111) != 0) {
		/*
		 * program mode
		 */
		int section_alloced = 0;

		if (!is_dir && argc != 1) {
			fprintf(stderr, "%s: only one file can be specified to"
			    " register the gfarm program `%s'\n",
			    program_name, gfarm_url);
			exit(EXIT_FAILURE);
		}
		if (hostfile != NULL || domainname != NULL) {
			fprintf(stderr,
			    "%s: cannot use -%c to register programs\n", 
			    program_name, hostfile != NULL ? 'H' : 'D');
			exit(EXIT_FAILURE);
		}
		if (nfragments != GFARM_FILE_DONTCARE) {
			/*
			 * XXX - call gfarm_url_replicate() to replicate
			 * `nfragments' copies of gfarm_url:section?
			 */
			fprintf(stderr,
			    "%s: warning: option -N is currently ignored\n", 
			    program_name);
		}
		if (section != NULL) {
			;
		} else if (hostname != NULL) {
			char *canonical;

			e = gfarm_host_get_canonical_name(hostname,
			    &canonical);
			if (e != NULL) {
				if (e == GFARM_ERR_NO_SUCH_OBJECT)
					e = "not a filesystem node";
				fprintf(stderr, "%s: host %s: %s\n",
				    program_name, hostname, e);
				exit(EXIT_FAILURE);
			}
			section_alloced = 1;
			section = gfarm_host_info_get_architecture_by_host(
			    canonical);
			free(canonical);
			if (section == NULL) {
				fprintf(stderr, "%s: %s\n",
				    program_name, GFARM_ERR_NO_MEMORY);
				exit(EXIT_FAILURE);
			}
		} else if (gfarm_host_get_self_architecture(&section) != NULL){
			fprintf(stderr, "%s: missing -a option\n",
			    program_name);
			exit(EXIT_FAILURE);
		}
		for (i = 0; i < argc; i++) {
			register_program(is_dir, gfarm_url, section,
			    hostname, argv[i],
			    file_mode_arg == gfarm_url ||
			    file_mode_arg == argv[i],
			    file_mode);
		}
		if (section_alloced)
			free(section);
	} else if (section != NULL || gfs_pio_get_node_rank(&index) == NULL) {
		/*
		 * fragment mode
		 */
		if (section != NULL)
			index = strtol(section, NULL, 0);
		else if (nfragments == GFARM_FILE_DONTCARE)
			gfs_pio_get_node_size(&nfragments);
		if (!is_dir && argc != 1) {
			fprintf(stderr, "%s: only one file can be specified to"
			    " register a fragment %s of the gfarm file `%s'\n",
			    program_name, section, gfarm_url);
			exit(EXIT_FAILURE);
		}
		if (hostfile != NULL || domainname != NULL) {
			fprintf(stderr,
			    "%s: cannot use -%c with -I\n", 
			    program_name, hostfile != NULL ? 'H' : 'D');
			exit(EXIT_FAILURE);
		}
		for (i = 0; i < argc; i++) {
			register_fragment(is_dir, gfarm_url, index, nfragments,
			    hostname, argv[i],
			    file_mode_arg == gfarm_url ||
			    file_mode_arg == argv[i],
			    file_mode);
		}
	} else {
		/*
		 * auto index mode
		 */
		char **hosts = NULL;

		if (nfragments == GFARM_FILE_DONTCARE)
			nfragments = argc;
		if (nfragments != argc) {
			fprintf(stderr, "%s: local file number %d "
			    "doesn't match with -N %d\n",
			    program_name, argc, nfragments);
			exit(EXIT_FAILURE);
		}
		if (is_dir && nfragments > 1) {
			fprintf(stderr, "%s: cannot determine the file name "
			    "under the directory %s, "
			    "because multiple local file names are specifed\n",
			    program_name, gfarm_url);
			exit(EXIT_FAILURE);
		}
		if (hostname != NULL) {
			;
		} else if (hostfile != NULL) {
			int nhosts, error_line;

			e = gfarm_hostlist_read(hostfile,
			    &nhosts, &hosts, &error_line);
			if (e != NULL) {
				if (error_line != -1)
					fprintf(stderr, "%s: %s line %d: %s\n",
					    program_name,
					    hostfile, error_line, e);
				else
					fprintf(stderr, "%s: %s: %s\n",
					    program_name, hostfile, e);
				exit(EXIT_FAILURE);
			}
			if (nhosts < nfragments) {
				hosts = realloc(hosts,
				    sizeof(*hosts) * nfragments);
				if (hosts == NULL) {
					fprintf(stderr, "%s: %s\n",
					    program_name,
					    GFARM_ERR_NO_MEMORY);
					exit(EXIT_FAILURE);
				}
				gfarm_strings_expand_cyclic(nhosts, hosts,
				    nfragments - nhosts, &hosts[nhosts]);
			}
		} else {
			hosts = malloc(sizeof(*hosts) * nfragments);
			if (hosts == NULL) {
				fprintf(stderr, "%s: %s\n", program_name,
				    GFARM_ERR_NO_MEMORY);
				exit(EXIT_FAILURE);
			}
			if (domainname != NULL)
				e = gfarm_schedule_search_idle_by_domainname(
					domainname, nfragments, hosts);
			else
				e = gfarm_schedule_search_idle_by_all(
					nfragments, hosts);
			if (e != NULL) {
				fprintf(stderr,
				    "%s: selecting filesystem nodes: %s\n",
				    program_name, e);
				exit(EXIT_FAILURE);
			}
		}
		/* XXX - need to register in parallel? */
		for (i = 0; i < argc; i++) {
			register_fragment(is_dir, gfarm_url, i, nfragments,
			    hostname != NULL ? hostname : hosts[i], argv[i],
			    1, file_mode);
		}
		if (hostname == NULL)
			gfarm_strings_free_deeply(nfragments, hosts);
	}

	e = gfarm_terminate();
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(EXIT_FAILURE);
	}

	exit(error_happened);
}
