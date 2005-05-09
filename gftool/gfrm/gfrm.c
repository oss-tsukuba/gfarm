/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <libgen.h>
#include <gfarm/gfarm.h>

char *program_name = "gfrm";

void
usage()
{
	fprintf(stderr, "Usage: %s [-frR] [-h <host>] <gfarm_url>...\n",
		program_name);
	fprintf(stderr, "       %s [-f] -I <fragment> -h <host> "
		"<gfarm_url>...\n", program_name);
	exit(1);
}

struct unlink_ops {
	char *(*unlink)(char *, void *);
	char *(*rmdir)(const char *, void *);
};

typedef struct unlink_ops * Unlink_Ops;

static void
remove_cwd_entries(Unlink_Ops ops, void *closure)
{
	char *e;
	char cwdbf[PATH_MAX * 2];
	int i;
	GFS_Dir dir;
	struct gfs_dirent *entry;
	gfarm_stringlist entry_list;

	e = gfs_getcwd(cwdbf, sizeof(cwdbf));
	if (e != NULL) {
		fprintf(stderr, "%s\n", e);
		return;
	}
	e = gfs_opendir(".", &dir);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", cwdbf, e);
		return;
	}
	e = gfarm_stringlist_init(&entry_list);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", cwdbf, e);
		gfs_closedir(dir);
		return;
	}
	while ((e = gfs_readdir(dir, &entry)) == NULL && entry != NULL) {
		char *p;

		if (entry->d_name[0] == '.' && (entry->d_name[1] == '\0' ||
		    (entry->d_name[1] == '.' && entry->d_name[2] == '\0')))
			continue; /* "." or ".." */
		p = strdup(entry->d_name);
		if (p == NULL) {
			fprintf(stderr, "%s\n", GFARM_ERR_NO_MEMORY);
			exit (1);
		}
		e = gfarm_stringlist_add(&entry_list, p);
		if (e != NULL) {
			fprintf(stderr, "%s/%s: %s\n",
					cwdbf, entry->d_name, e);
		}
	}
	if (e != NULL)
		fprintf(stderr, "%s: %s\n", cwdbf, e);
	gfs_closedir(dir);
	for (i = 0; i < gfarm_stringlist_length(&entry_list); i++) {
		struct gfs_stat gs;
		char *path = gfarm_stringlist_elem(&entry_list, i);
		gfarm_mode_t mode = 0;

		e = gfs_stat(path, &gs);
		if (e != GFARM_ERR_NO_FRAGMENT_INFORMATION && e != NULL) {
			fprintf(stderr, "%s/%s: %s\n", cwdbf, path, e);
			continue;
		}
		if (e == NULL) {
			mode = gs.st_mode;
			gfs_stat_free(&gs);
		}
		if (e == GFARM_ERR_NO_FRAGMENT_INFORMATION
		    || GFARM_S_ISREG(mode)) {
			char *url;

			url = gfarm_url_prefix_add(path);
			if (url == NULL) {
				fprintf(stderr, "%s\n", GFARM_ERR_NO_MEMORY);
				exit (1);
			}
			e = ops->unlink(url, closure);
			if (e != GFARM_ERR_NO_REPLICA_ON_HOST && e != NULL)
				fprintf(stderr, "%s/%s: %s\n", cwdbf, path, e);
			free(url);
		} else if (GFARM_S_ISDIR(mode)) {
			e = gfs_chdir(path);
			if (e != NULL) {
				fprintf(stderr, "%s/%s: %s\n", cwdbf, path, e);
				continue;
			}
			remove_cwd_entries(ops, closure);
			e = gfs_chdir("..");
			if (e != NULL) {
				fprintf(stderr, "%s: %s\n", cwdbf, e);
				exit (1);
			}
			e = ops->rmdir(path, closure);
			if (e != NULL)
				fprintf(stderr, "%s/%s: %s\n", cwdbf, path, e);
		}
	}
	gfarm_stringlist_free_deeply(&entry_list);
}

static char *
remove_whole_file_or_dir(char *path,
	Unlink_Ops ops, void *closure, int is_recursive)
{
	struct gfs_stat gs;
	char *e, cwdbuf[PATH_MAX * 2];
	const char *b;
	gfarm_mode_t mode;

	b = gfarm_path_dir_skip(gfarm_url_prefix_skip(path));
	if (b[0] == '.' && (b[1] == '\0' || (b[1] == '.' && b[2] == '\0')))
		return ("cannot remove \'.\' or \'..\'");

	e = gfs_stat(path, &gs);
	if (e == GFARM_ERR_NO_FRAGMENT_INFORMATION)
		return (ops->unlink(path, closure));
	if (e != NULL)
		return (e);

	mode = gs.st_mode;
	gfs_stat_free(&gs);

	if (GFARM_S_ISREG(mode)) {
		e = ops->unlink(path, closure);
	} else if (GFARM_S_ISDIR(mode)) {
		if (!is_recursive)
			return (GFARM_ERR_IS_A_DIRECTORY);
		e = gfs_getcwd(cwdbuf, sizeof(cwdbuf));
		if (e != NULL)
			return (e);
		e = gfs_chdir(path);
		if (e != NULL)
			return (e);
		remove_cwd_entries(ops, closure);
		e = gfs_chdir_canonical(cwdbuf);
		if (e != NULL)
			return (e);
		e = ops->rmdir(path, closure);
	}
	return (e);
}

static char *
unlink_file(char *path, void *closure)
{
	return (gfs_unlink(path));
}

static char *
rmdir_file(const char *path, void *closure)
{
	return (gfs_rmdir(path));
}

struct unlink_replica_closure { int nhosts; char **hosts; int force; };

static char *
unlink_replica_alloc_closure(int nhosts, char **hosts, int force, void **cp)
{
	struct unlink_replica_closure *c;

	c = malloc(sizeof(struct unlink_replica_closure));
	if (c == NULL)
		return (GFARM_ERR_NO_MEMORY);
	c->nhosts = nhosts;
	c->hosts = hosts;
	c->force = force;
	*cp = c;
	return (NULL);
}

static char *
unlink_replica(char *path, void *closure)
{
	struct unlink_replica_closure *a = closure;
	return (gfs_unlink_replica(path, a->nhosts, a->hosts, a->force));
}

static char *
rmdir_replica(const char *path, void *closure)
{
	struct unlink_replica_closure *a = closure;

	if (a->force)
		return (gfs_rmdir(path));
	/* do nothing */
	return (NULL);
}

static struct unlink_ops file_ops = { unlink_file, rmdir_file };
static struct unlink_ops replica_ops = { unlink_replica, rmdir_replica };

int
main(argc, argv)
	int argc;
	char **argv;
{
	extern char *optarg;
	extern int optind;
	int argc_save = argc;
	char **argv_save = argv;
	char *e, *section = NULL;
	int ch, nhosts = 0;
	char **hosttab;
	gfarm_stringlist host_list;
	int o_force = 0;
	int o_recursive = 0;
	gfarm_stringlist paths;
	gfs_glob_t types;
	int i;

	if (argc >= 1)
		program_name = basename(argv[0]);

	e = gfarm_stringlist_init(&host_list);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}

	while ((ch = getopt(argc, argv, "h:I:fRr?")) != -1) {
		switch (ch) {
		case 'h':
			e = gfarm_stringlist_add(&host_list, optarg);
			if (e != NULL) {
				fprintf(stderr, "%s: %s\n",
					program_name, e);
				exit(1);
			}
			++nhosts;
			break;
		case 'f':
			o_force = 1;
			break;
		case 'I':
			section = optarg;
			break;
		case 'R':
		case 'r':
			o_recursive = 1;
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
	if (argc == 0) {
		fprintf(stderr, "%s: too few arguments\n",
			program_name);
		exit(1);
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
	if (section == NULL) {
		if (nhosts == 0) {
			/* remove a whole file */
			for (i = 0; i < gfarm_stringlist_length(&paths); i++) {
				char *f = gfarm_stringlist_elem(&paths, i);
				e = remove_whole_file_or_dir(
					f, &file_ops, NULL, o_recursive);
				if (e != NULL)
					fprintf(stderr, "%s: %s\n", f, e);
			}
		}
		else {
			/*
			 * remove file replicas of a whole file
			 * on a specified node.
			 */
			void *closure;
			hosttab = gfarm_strings_alloc_from_stringlist(
				&host_list);
			gfarm_stringlist_free(&host_list);
			e = unlink_replica_alloc_closure(
				nhosts, hosttab, o_force, &closure);
			if (e != NULL) {
				fprintf(stderr, "%s: %s\n", program_name, e);
				exit(EXIT_FAILURE);
			}
			for (i = 0; i < gfarm_stringlist_length(&paths); i++) {
				char *f = gfarm_stringlist_elem(&paths, i);
				e = remove_whole_file_or_dir(
					f, &replica_ops, closure, o_recursive);
				if (e != NULL)
					fprintf(stderr, "%s: %s\n", f, e);
			}
			free(closure);
			free(hosttab);
		}
	} else {
		int i;
		/* remove a file fragment */
		if (nhosts == 0) {
			fprintf(stderr, "%s: -h option should be specified\n",
				program_name);
			exit(1);
		}
		/* assert(nhosts == gfarm_stringlist_length(&host_list)); */
		hosttab = gfarm_strings_alloc_from_stringlist(&host_list);
		gfarm_stringlist_free(&host_list);

		for (i = 0; i < gfarm_stringlist_length(&paths); i++) {
			e = gfs_unlink_section_replica(
				gfarm_stringlist_elem(&paths, i), section,
				nhosts, hosttab, o_force);
			if (e != NULL)
				fprintf(stderr, "%s: %s\n",
					gfarm_stringlist_elem(&paths, i), e);
		}
		free(hosttab);
	}
	gfs_glob_free(&types);
	gfarm_stringlist_free_deeply(&paths);
	e = gfarm_terminate();
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}
	return (0);
}
