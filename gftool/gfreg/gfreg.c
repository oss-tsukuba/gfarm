#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <gfarm/gfarm.h>

/*
 *  Register a local file to Gfarm filesystem
 *
 *  gfreg <local_filename> <gfarm_url>
 *
 *  $Id$
 */

char *program_name = "gfreg";

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
    fprintf(stderr, "\t-D domainname\t\tspecify a domainname\n");
    fprintf(stderr, "\t-f \t\t\tforce to register\n");
    exit(1);
}

#define GFS_FILE_BUFSIZE 65536

static char *
gfarm_url_fragment_register(char *gfarm_url, char *section, int nfrags,
	char *hostname, char *filename)
{
	char *e, *e_save = NULL;
	int fd;
	size_t rv;
	int length; /* XXX - should be size_t */
	GFS_File gf;
	struct stat s;
	char buffer[GFS_FILE_BUFSIZE];

	/*
	 * register the fragment
	 */
	if (strcmp(filename, "-") == 0) {
		fd = 0;			/* stdin */
		s.st_mode = 0664;	/* XXX */
	}
	else {
		if (stat(filename, &s) == -1)
			return "no such file or directory";
		fd = open(filename, O_RDONLY);
		if (fd == -1)
			return "cannot open";
	}
	e = gfs_pio_create(gfarm_url, GFARM_FILE_WRONLY,
		s.st_mode & GFARM_S_ALLPERM, &gf);
	if (e != NULL) {
		close(fd);
		return (e);
	}
	if (nfrags == GFARM_FILE_DONTCARE)
		e = gfs_pio_set_view_section(gf, section, hostname, 0);
	else
		e = gfs_pio_set_view_index(gf, nfrags,
			strtol(section, NULL, 0), hostname, 0);
	if (e != NULL) {
		char *gfarm_file;
		gfs_pio_close(gf);
		close(fd);
		/* try to unlink path info when there is no fragment file */
		if (gfarm_url_make_path(gfarm_url, &gfarm_file) == NULL) {
			(void)gfarm_path_info_remove(gfarm_file);
			free(gfarm_file);
		}
		return (e);
	}
	for (;;) {
		rv = read(fd, buffer, sizeof(buffer));
		if (rv <= 0)
			break;
		/* XXX - partial write case ? */
		e = gfs_pio_write(gf, buffer, rv, &length);
		if (e != NULL)
			break;
	}
	e_save = e;
	e = gfs_pio_close(gf);
	close(fd);

	if (e_save != NULL)
		return (e_save);

	return (e);
}

static int opt_force;

static int
gfarm_register_file(char *gfarm_url, char *node_index, char *hostname,
	int total_nodes, char *filename, int auto_index)
{
	struct stat s;
	struct gfs_stat gs;
	char *e, *target_url = NULL;
	int executable_file = 0;

	if (stat(filename, &s) == 0) {
		if (!S_ISREG(s.st_mode)) {
			fprintf(stderr, "%s: not a regular file", filename);
			return (-1);
		}
		if ((s.st_mode & (S_IXUSR|S_IXGRP|S_IXOTH)) != 0)
			executable_file = 1;
	}
	else if (strcmp(filename, "-")) {
		perror(filename);
		return (-1);
	}

	e = gfs_stat(gfarm_url, &gs);
	if (e == NULL && GFARM_S_ISDIR(gs.st_mode)) {
		/* target gfarm_url is a directory */
		char *bname;

		gfs_stat_free(&gs);

		if (auto_index && total_nodes > 1 && !executable_file) {
			/*
			 * In the auto index mode, the target Gfarm URL
			 * should be a regular file when two or more local
			 * non-executable files will be registered for
			 * preventing unexpected results.
			 */
			fprintf(stderr, "%s: not a regular file.  "
				"The target Gfarm URL should be a "
				"regular file when registering two or "
				"more local files.\n", gfarm_url);
			return (-1);
		}

		bname = basename(filename);

		target_url = malloc(strlen(gfarm_url) + strlen(bname) + 2);
		if (target_url == NULL) {
			fprintf(stderr, "not enough memory\n");
			return (-1);
		}

		strcpy(target_url, gfarm_url);
		if (*gfarm_path_dir_skip(gfarm_url_prefix_skip(target_url))
		    != '\0')
			strcat(target_url, "/");
		strcat(target_url, bname);
	}
	else if (e == NULL)
		gfs_stat_free(&gs);

	if (target_url == NULL) {
		target_url = strdup(gfarm_url);
		if (target_url == NULL) {
			fprintf(stderr, "not enough memory\n");
			return (-1);
		}
	}

	if (executable_file) {
		/* register a binary executable. */

		/* auto index case is not permitted */
		if (auto_index) {
			fprintf(stderr, "%s: binary file in auto index mode\n",
				filename);
			return (-1);
		}

		if (node_index == NULL) {
			if (hostname == NULL) {
				char *self_name;

				e = gfarm_host_get_canonical_self_name(
					&self_name);
				if (e != NULL) {
					fprintf(stderr, "%s: %s\n",
						gfarm_host_get_self_name(), e);
					return (-1);
				}
				node_index =
				    gfarm_host_info_get_architecture_by_host(
					    self_name);
			}
			else {
				char *c_name;

				e = gfarm_host_get_canonical_name(
					hostname, &c_name);
				if (e != NULL) {
					fprintf(stderr, "%s: %s\n",
						hostname, e);
					return (-1);
				}
				node_index = 
				    gfarm_host_info_get_architecture_by_host(
					    c_name);
				free(c_name);
			}
		}
		if (node_index == NULL) {
			fprintf(stderr,
				"%s: cannot determine the architecture "
				"of %s.\n",
				program_name, gfarm_host_get_self_name());
			return (-1);
		}
		if (hostname == NULL) {
			int nhosts;
			struct gfarm_host_info *hosts;

			/* if hostname is not specified, check architecture */
			e = gfarm_host_info_get_allhost_by_architecture(
				node_index, &nhosts, &hosts);
			if (e == GFARM_ERR_NO_SUCH_OBJECT) {
				fprintf(stderr, "%s: no such architecture\n",
					node_index);
				return (-1);
			}
			if (e != NULL) {
				fprintf(stderr, "%s: %s\n", node_index, e);
				return (-1);
			}
			/* XXX - select nodes, not implemented yet. */

			gfarm_host_info_free_all(nhosts, hosts);
		}
		if (total_nodes <= 0) {
			if (gfs_pio_get_node_size(&total_nodes) != NULL)
				total_nodes = 1;
		}

		if (!opt_force) {
			struct gfs_stat s;

			if (gfs_stat_section(target_url, node_index, &s)
			    == NULL) {
				gfs_stat_free(&s);
				e = "already exist";
				goto finish;
			}
		}

		e = gfarm_url_fragment_register(target_url, node_index,
			GFARM_FILE_DONTCARE, hostname, filename);
		/*
		 * XXX - gfarm_url_replicate() to replicate
		 * 'total_nodes' copies of target_url.
		 */

	} else {
		char index_str[GFARM_INT32STRLEN + 1];

		/* register a file fragment. */
		if (node_index == NULL) {
			int index;

			e = gfs_pio_get_node_rank(&index);
			if (e != NULL) {
				fprintf(stderr,
					"%s: missing -I <Gfarm index>\n",
					program_name);
				return (-1);
			}
			sprintf(index_str, "%d", index);
			node_index = index_str;
		}

		if (total_nodes <= 0) {
			e = gfs_pio_get_node_size(&total_nodes);
			if (e != NULL) {
				fprintf(stderr,
					"%s: missing -N "
					"<total num of fragments>\n",
					program_name);
				return (-1);
			}
		}

		if (!opt_force) {
			struct gfs_stat s;

			if (gfs_stat_section(target_url, node_index, &s)
			    == NULL) {
				gfs_stat_free(&s);
				e = "already exist";
				goto finish;
			}
		}

		e = gfarm_url_fragment_register(target_url, node_index,
			total_nodes, hostname, filename);
	}
 finish:
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", target_url, e);
		return (-1);
	}

	free(target_url);

	return (0);
}

int
main(int argc, char *argv[])
{
	char *gfarm_url, *node_index = NULL;
	char *hostname = NULL, **auto_hosts = NULL, *domainname = NULL, *e;
	int total_nodes = -1, c, auto_index = 0;
	extern char *optarg;
	extern int optind;
	struct gfs_stat gs;

	e = gfarm_initialize(&argc, &argv);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}

	/*  Command options  */

	while ((c = getopt(argc, argv, "a:fh:D:I:N:?")) != -1) {
		switch (c) {
		case 'I':
		case 'a':
			node_index = optarg;
			break;
		case 'N':
			total_nodes = strtol(optarg, NULL, 0);
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

	/*
	 * auto index mode: when target gfarm_url does not exist (a
	 * new Gfarm file), and when node_index is not specified,
	 * node_index will be automatically determined.
	 */
	e = gfs_stat(gfarm_url, &gs);
	if (e == GFARM_ERR_NO_SUCH_OBJECT && node_index == NULL) {
		/* auto index mode */
		auto_index = 1;

		if (total_nodes >= 0 && total_nodes != argc - 1) {
			fprintf(stderr, "%s: -N option is not correctly "
				"specified\n", program_name);
			usage();
		}
		total_nodes = argc - 1;
		/*
		 * if hostname is not specified, select appropreate
		 * nodes
		 */
		if (hostname != NULL)
			auto_hosts = malloc(total_nodes * sizeof(char *));
		if (auto_hosts != NULL) {
			if (domainname != NULL)
				e = gfarm_schedule_search_idle_by_domainname(
					domainname, total_nodes, auto_hosts);
			else
				e = gfarm_schedule_search_idle_by_all(
					total_nodes, auto_hosts);
			if (e != NULL) {
				free(auto_hosts);
				auto_hosts = NULL;
			}
		}
	}
	if (e == NULL)
		gfs_stat_free(&gs);

	while (--argc) {
		char index_str[GFARM_INT32STRLEN + 1];

		/* XXX - need to register in parallel? */

		if (auto_index) {
			sprintf(index_str, "%d", total_nodes - argc);
			node_index = index_str;
			if (auto_hosts != NULL)
				hostname = auto_hosts[total_nodes - argc];
		}
		if (gfarm_register_file(gfarm_url, node_index, hostname,
			total_nodes, *argv++, auto_index))
			break;
	}

	if (auto_hosts != NULL)
		gfarm_strings_free_deeply(total_nodes, auto_hosts);

	e = gfarm_terminate();
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}

	exit(0);
}
