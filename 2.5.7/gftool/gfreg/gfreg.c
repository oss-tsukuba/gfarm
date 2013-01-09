#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#include <limits.h>
#include <string.h>
#include <assert.h>
#include <dirent.h>
#include <gfarm/gfarm.h>

#include "host.h"
#include "schedule.h" /* gfarm_strings_expand_cyclic() */
#include "config.h"

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
    fprintf(stderr, "\t-r files\t\tspecify some directories and files\n");
    exit(EXIT_FAILURE);
}

static int opt_force = 0;
static char *opt_section = NULL;

static char *opt_hostname = NULL;
static char *opt_hostfile = NULL;
static char *opt_domainname = NULL;

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
	if (S_ISREG(*file_mode_p))
		*file_mode_p &= FILE_MODE_MASK;
	else
		*file_mode_p = 0644; /* XXX, but better than *file_mode_p */
	return (1);
}

static file_offset_t
set_minimum_free_disk_space_from_fd(int fd)
{
	struct stat s;
	file_offset_t old_size = gfarm_get_minimum_free_disk_space();

	if (!fstat(fd, &s) && S_ISREG(s.st_mode))
		gfarm_set_minimum_free_disk_space(s.st_size);
	return (old_size);
}

static int
concat_dir_name(const char *gfarm_url, const char *base_name,
	char **target_url_p)
{
	char *target_url;

	GFARM_MALLOC_ARRAY(target_url,
		strlen(gfarm_url) + 1 + strlen(base_name) + 1);
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
register_fragment(int is_dest_dir, char *gfarm_url, int index, int nfragments,
	char *hostname,
	char *filename, int use_file_mode, gfarm_mode_t file_mode)
{
	char *e;
	int fd, fd_needs_close;
	char *target_url;
	GFS_File gf;
	char section[GFARM_INT32STRLEN + 1];
	file_offset_t old_size;

	if (!open_file(filename, &fd, &fd_needs_close))
		return;
	if (!use_file_mode && !get_file_mode(fd, filename, &file_mode))
		goto finish;

	if (!is_dest_dir)
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
			fprintf(stderr,
				"%s: gfs_pio_create: cannot open %s: %s\n",
				program_name, target_url, e);
			error_happened = 1;
		} else {
			/* specify the minimum free disk space */
			old_size = set_minimum_free_disk_space_from_fd(fd);

			if ((e = gfs_pio_set_view_index(gf, nfragments, index,
			    hostname, 0)) != NULL) {
				fprintf(stderr,
					"%s: gfs_pio_set_view_index: "
					"cannot open %s:%d: %s\n",
				    program_name, target_url, index, e);
				error_happened = 1;
			} else {
				copy_file(fd, gf, target_url, section);
			}
			e = gfs_pio_close(gf);
			if (e != NULL) {
				fprintf(stderr, "%s: closing %s:%d: %s\n",
				    program_name, target_url, index, e);
			}
			gfarm_set_minimum_free_disk_space(old_size);
		}
	}
 finish_url:
	if (target_url != gfarm_url)
		free(target_url);
 finish:
	if (fd_needs_close)
		close(fd);
}

static void
register_file(char *gfarm_url, char *section, char *hostname, char *filename)
{
	char *e;
	int fd, fd_needs_close;
	GFS_File gf;
	gfarm_mode_t file_mode;
	file_offset_t old_size;

	if (!open_file(filename, &fd, &fd_needs_close))
		return;
	if (!get_file_mode(fd, filename, &file_mode))
		goto finish;

	if ((file_mode & 0111) == 0) {
		register_fragment(0, gfarm_url, 0, 1, hostname, filename,
			0, 0000);
		goto finish;
	}	

	if (opt_force || section_does_not_exists(gfarm_url, section)) {
		e = gfs_pio_create(gfarm_url,
		    GFARM_FILE_WRONLY|GFARM_FILE_TRUNC, file_mode, &gf);
		if (e != NULL) {
			fprintf(stderr,
				"%s: gfs_pio_create: cannot open %s: %s\n",
				program_name, gfarm_url, e);
			error_happened = 1;
		} else {
			/* specify the minimum free disk space */
			old_size = set_minimum_free_disk_space_from_fd(fd);

			if (section == NULL) {
				fprintf(stderr, "%s: missing -a option\n",
					program_name);
				exit(EXIT_FAILURE);
			}	
			if ((e = gfs_pio_set_view_section(gf, section,
			    hostname, 0)) != NULL) {
				fprintf(stderr, "%s: cannot open %s:%s: %s\n",
				    program_name, gfarm_url, section, e);
				error_happened = 1;
			} else {
				copy_file(fd, gf, gfarm_url, section);
			}
			e = gfs_pio_close(gf);
			if (e != NULL) {
				fprintf(stderr, "%s: closing %s:%s: %s\n",
				    program_name, gfarm_url, section, e);
			}
			gfarm_set_minimum_free_disk_space(old_size);
		}
	}
 finish:
	if (fd_needs_close)
		close(fd);
}

static char *
add_cwd_to_relative_path(char *cwd, const char *path)
{
	char *p;

	GFARM_MALLOC_ARRAY(p, strlen(cwd) + strlen(path) + 2);
	if (p == NULL) {
		fprintf(stderr, "%s: %s\n",
			    program_name, GFARM_ERR_NO_MEMORY);
		exit(EXIT_FAILURE);
	}
	sprintf(p, strcmp(cwd, "") ? "%s/%s" : "%s%s", cwd, path);
	return (p);
}

static int
traverse_file_tree(char *cwd, char *path,
	gfarm_stringlist *dir_list, gfarm_stringlist *file_list)
{
	char *e;
	struct stat s;
	DIR *dir;
	struct dirent *entry;
	char *dpath;

	dpath = add_cwd_to_relative_path(cwd, path);
	if (stat(path, &s) == -1) {
		fprintf(stderr, "%s: cannot stat %s: %s\n",
		    program_name, dpath, strerror(errno));
		error_happened = 1;
		return (0);
	}
	if (S_ISDIR(s.st_mode)) {
		e = gfarm_stringlist_add(dir_list, dpath);
		if (e != NULL) {
			fprintf(stderr, "%s: traverse_file_tree: %s:\n",
			    program_name, e);
			exit(EXIT_FAILURE);			
		}
		if (chdir(path) == -1) {
			fprintf(stderr, "%s: cannot change directory %s: %s\n",
			    program_name, dpath, strerror(errno));
			error_happened = 1;
			return (0);
		}
		if ((dir = opendir(".")) == NULL) {
			fprintf(stderr,	"%s: cannot open directory %s: %s\n",
			    program_name, dpath, strerror(errno));
			error_happened = 1;
			return (0);
		}
		while ((entry = readdir(dir)) != NULL) {
			if (strcmp(entry->d_name, ".") == 0 ||
			    strcmp(entry->d_name, "..") == 0) { 
				continue;
			}
			if (!traverse_file_tree(dpath, entry->d_name,
			    dir_list, file_list)) {
				return (0);
			}
		}
		if (closedir(dir) == -1) {
			fprintf(stderr,
			    "%s: cannot close directory %s: %s\n",
			    program_name, dpath, strerror(errno));
			error_happened = 1;
			return (0);
		}
		if (chdir("..") == -1) {
			fprintf(stderr,
			    "%s: cannot change directory %s: %s\n",
			    program_name, cwd, strerror(errno));
			error_happened = 1;
			return (0);
		}
	} else {
		e = gfarm_stringlist_add(file_list, dpath);
		if (e != NULL) {
			fprintf(stderr, "%s: traverse_file_tree: %s:\n",
			    program_name, e);
			exit(EXIT_FAILURE);			
		}
	}

	return (1);
}

static int
get_lists(char *dir_path,
	gfarm_stringlist *dir_list, gfarm_stringlist *file_list)
{
	char cwdbf[PATH_MAX * 2];
	struct dirent *entry;
	DIR *dir;

	if (getcwd(cwdbf, sizeof(cwdbf)) == NULL) {
		fprintf(stderr,
		    "%s: cannot get current working directory: %s\n",
		    program_name, strerror(errno));
		error_happened = 1;
		return (0);
	}
	if (chdir(dir_path) == -1) {
		fprintf(stderr,
		    "%s: cannot change directory %s: %s\n",
		    program_name, dir_path, strerror(errno));
		error_happened = 1;
		return (0);
	}
	if ((dir = opendir(".")) == NULL) {
		fprintf(stderr,
		    "%s: cannot open directory %s: %s\n",
		    program_name, dir_path, strerror(errno));
		error_happened = 1;
		return (0);
	}
	while ((entry = readdir(dir)) != NULL) {
		if (strcmp(entry->d_name, "..") == 0) { 
			continue;
		}
		if (strcmp(entry->d_name, ".") == 0) {
			gfarm_stringlist_add(dir_list, strdup(""));
			continue;
		}
		if (!traverse_file_tree("", entry->d_name,
					dir_list, file_list)) {
			closedir(dir);
			return (0);
		}
	}
	if (closedir(dir) == -1) {
		fprintf(stderr,
		    "%s: cannot close directory %s: %s\n",
		    program_name, dir_path, strerror(errno));
		error_happened = 1;
		return (0);
	}
	if (chdir(cwdbf) == -1) {
		fprintf(stderr,
		    "%s: cannot change directory %s: %s\n",
		    program_name, cwdbf, strerror(errno));
		error_happened = 1;
		return (0);
	}
	return (1);
}

static void
get_section(char *hostname, char **section, int *section_alloced) {
	char *s, *e;
	char *canonical;

	*section_alloced = 0;
	if (hostname == NULL) {	
		e = gfarm_host_get_self_architecture(&s);
		if (e == NULL)
			*section = s;
		goto finish;		
	}	
	e = gfarm_host_get_canonical_name(hostname, &canonical);
	if (e != NULL) {
		if (e == GFARM_ERR_NO_SUCH_OBJECT)
			e = "not a filesystem node";
		goto finish;
	}
	s = gfarm_host_info_get_architecture_by_host(canonical);
	free(canonical);
	if (s == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto finish;
	}	
	*section = s;
	*section_alloced = 1;
 finish:	
	if (e != NULL) {
		fprintf(stderr, "%s: host %s: %s\n",
			program_name, hostname, e);
		exit(EXIT_FAILURE);
	}	
}

enum register_mode {
	UNDECIDED,
	PROGRAM,
	AUTO_INDEX,
	FRAGMENT,
	RECURSIVE
};

void foreach_arg(int argc, char *argv[], 
	void (*f)(char *, int, gfarm_mode_t, void *), void *f_a)
{
	int i;

	for (i = 0; i < argc; i++) {
		int fd, fd_needs_close;
		gfarm_mode_t m;

		if (!open_file(argv[i], &fd, &fd_needs_close))
			exit(EXIT_FAILURE);
		if (!get_mode(fd, argv[i], &m))
			exit(EXIT_FAILURE);

		(*f)(argv[i], fd, m, f_a);

		if (fd_needs_close) {
			close(fd);
		}
	}    
}

struct check_mode_args {
	char **ref_m_arg_p;
	gfarm_mode_t *ref_m_p;
};

static void
check_modes_are_mixed(char *c_arg, int fd, gfarm_mode_t m, void *f_args)
{
	struct check_mode_args *a = f_args;

	if (!S_ISREG(m))
		return;

	if (*a->ref_m_arg_p == NULL) {
		*a->ref_m_arg_p = c_arg;
		*a->ref_m_p = m & FILE_MODE_MASK;
	}
	if (((m & 0111) != 0) != ((*a->ref_m_p & 0111) != 0)) {
		fprintf(stderr,
			"%s: program and non-program are mixed in %s and %s\n",
			program_name, *a->ref_m_arg_p, c_arg);
		exit(EXIT_FAILURE);
	}
}

enum register_mode
decide_reg_mode(char *file_mode_arg, gfarm_mode_t file_mode,
	int argc, char *argv[])
{
	char *ref_m_arg = file_mode_arg;
	gfarm_mode_t ref_m = file_mode;
	struct check_mode_args a;

	a.ref_m_arg_p = &ref_m_arg;
	a.ref_m_p = &ref_m;

	foreach_arg(argc, argv, check_modes_are_mixed, &a);

	if ((ref_m & 0111) != 0)
		return(PROGRAM);
	else
		return(AUTO_INDEX);
}

static void
check_is_argument_only_one(int argc, char *file_type, char *fragment,
	char *section, char *of, char *gfarm_url)
{
	if (argc > 1) {
		fprintf(stderr, "%s: only one %s can be specified to register"
			"%s%s%s the gfarm file `%s'\n",
			program_name, file_type, fragment, section, of,
			gfarm_url);
		exit(EXIT_FAILURE);
	}
}

static void
check_arguments(int argc, char *argv[],
	char *hostfile, enum register_mode reg_mode, int is_dest_dir, 
	char *file_mode_arg)
{
	int i;
	int c = 0; /* count of "-" in the arguments */

	if (hostfile != NULL && strcmp(hostfile, STDIN_FILENAME) == 0)
		c++;
	for (i = 0; i < argc; i++) {
		int fd, fd_needs_close;
		gfarm_mode_t m;

		if (!open_file(argv[i], &fd, &fd_needs_close))
			exit(EXIT_FAILURE);
		if (!get_mode(fd, argv[i], &m))
			exit(EXIT_FAILURE);
		if (fd_needs_close)
			close(fd);
		if (S_ISDIR(m)) {
			if (reg_mode == AUTO_INDEX || reg_mode == FRAGMENT) {
				fprintf(stderr, "%s: %s: is a directory\n",
					program_name, argv[i]);
				exit(EXIT_FAILURE);
			} else if (reg_mode == RECURSIVE && !is_dest_dir
				   && file_mode_arg != NULL) {
				/* gfarm_url is a regular file */
				fprintf(stderr,
					"%s: cannot register "
					"directory %s "
					"as regular file %s\n",
					program_name, argv[i],
					file_mode_arg == NULL ?
						"" : file_mode_arg);
				exit(EXIT_FAILURE);
			}	
		}
		if ((strcmp(argv[i], STDIN_FILENAME)) == 0) {
			if (is_dest_dir) {
				fprintf(stderr, "%s: cannot create file `-'\n",
				program_name);
				exit(EXIT_FAILURE);
			}
			if (++c > 1) {
				fprintf(stderr, "%s: `-' (stdin) is specified "
				"multiple times\n", program_name);
				exit(EXIT_FAILURE);
			}	
		}	
	}
}	

struct lists_arg {
	int is_dest_dir;
	char *gfarm_url;	
	gfarm_stringlist *dir_list, *src_file_list, *target_file_list;
};

static void
add_dir_file_list(char *c_arg, int fd, gfarm_mode_t m, void *f_args)
{
	struct lists_arg *a = f_args;
	char *e;
	gfarm_stringlist d, f; 
	char *target_base_url, *target_url, *src_file;
	int i;

	if (a->is_dest_dir) {
		if (!concat_dir_name(a->gfarm_url,
			gfarm_path_dir_skip(c_arg), &target_base_url))
			exit(EXIT_FAILURE);
	} else {
		target_base_url = strdup(a->gfarm_url);
	}	

	if (S_ISDIR(m)) {
		e = gfarm_stringlist_init(&d);
		if (e != NULL)
			goto finish;
		e = gfarm_stringlist_init(&f);
		if (e != NULL)
			goto finish;

		if (!get_lists(c_arg, &d, &f))
			exit(EXIT_FAILURE);
		for (i = 0; i < gfarm_stringlist_length(&d); i++) {
			if (!concat_dir_name(target_base_url,
				gfarm_stringlist_elem(&d, i), &target_url)) {
				exit(EXIT_FAILURE);
			}	
			gfarm_stringlist_add(a->dir_list, target_url);
		}
		gfarm_stringlist_free_deeply(&d);
		for (i = 0; i < gfarm_stringlist_length(&f); i++) {
			if (!concat_dir_name(c_arg,
				gfarm_stringlist_elem(&f, i), &src_file)) {
				exit(EXIT_FAILURE);
			}	
			gfarm_stringlist_add(a->src_file_list, src_file);
			if (!concat_dir_name(a->is_dest_dir ? 
					     target_base_url : a->gfarm_url,
				gfarm_stringlist_elem(&f, i), &target_url)) {
				exit(EXIT_FAILURE);
			}	
			gfarm_stringlist_add(a->target_file_list, target_url);
		}
		gfarm_stringlist_free_deeply(&f);
	} else {	
		e = gfarm_stringlist_add(a->src_file_list, c_arg);
		if (a->is_dest_dir) {
			if (!concat_dir_name(a->gfarm_url,
					     gfarm_path_dir_skip(c_arg),
					     &target_url)) {
				exit(EXIT_FAILURE);
			}
		} else {
			target_url = strdup(a->gfarm_url);
		}
		gfarm_stringlist_add(a->target_file_list, target_url);
	}
	free(target_base_url);
 finish:
	if (e != NULL) {
		fprintf(stderr, "%s: add_dir_file_list: %s:\n",
			program_name, e);
		exit(EXIT_FAILURE);
	}	
}

static void
add_file_list(char *c_arg, int fd, gfarm_mode_t m, void *f_args)
{
	struct lists_arg *a = f_args;
	char *e;
	char *target_url;
	
	if (S_ISDIR(m)) {
		fprintf(stderr, "%s: omitting directory `%s'\n",
			program_name, c_arg);
	} else {	
		e = gfarm_stringlist_add(a->src_file_list, c_arg);
		if (e != NULL) {
			fprintf(stderr, "%s: add_file_list: %s:\n",
			    program_name, e);
			exit(EXIT_FAILURE);			
		}	
		if (a->is_dest_dir) {
			if (!concat_dir_name(a->gfarm_url,
					     gfarm_path_dir_skip(c_arg),
					     &target_url)) {
				exit(EXIT_FAILURE);
			}
		} else {
			target_url = a->gfarm_url;
		}
		gfarm_stringlist_add(a->target_file_list, target_url);
	}
}

static void
get_hosts(int *np, char ***host_table_p)
{
	char **hosts = NULL,  *e, **h;
	int nhosts, error_line, nh;

	if (opt_hostname != NULL) {
		GFARM_MALLOC_ARRAY(h, 1);
		if (h == NULL) {
			fprintf(stderr, "%s: %s\n",
				program_name, GFARM_ERR_NO_MEMORY);
			exit(EXIT_FAILURE);
		}
		h[0] = strdup(opt_hostname);
		if (h[0] == NULL) {
			fprintf(stderr, "%s: %s\n",
				program_name, GFARM_ERR_NO_MEMORY);
			exit(EXIT_FAILURE);
		}	
		nh = 1;
	} else if (opt_hostfile != NULL) {	
		e = gfarm_hostlist_read(opt_hostfile, &nh, &h, &error_line);
		if (e != NULL) {
			if (error_line != -1)
				fprintf(stderr, "%s: %s line %d: %s\n",
					program_name,
					opt_hostfile, error_line, e);
			else
				fprintf(stderr, "%s: %s: %s\n",
					program_name, opt_hostfile, e);
			exit(EXIT_FAILURE);
		}
	} else {	
		if (opt_domainname == NULL)
			opt_domainname = "";
		e = gfarm_hosts_in_domain(&nh, &h, opt_domainname);
		if (e != NULL) {
			fprintf(stderr, "%s: %s\n", program_name, e);
			exit(EXIT_FAILURE);
		}
	}	

	GFARM_MALLOC_ARRAY(hosts, nh);
	if (hosts == NULL) {
		fprintf(stderr, "%s: %s\n", program_name, GFARM_ERR_NO_MEMORY);
		exit(EXIT_FAILURE);
        }
	nhosts = nh;
	e = gfarm_schedule_search_idle_acyclic_hosts_to_write(
	    nh, h, &nhosts, hosts);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(EXIT_FAILURE);
	}
	free(h);
	*np = nhosts;
	*host_table_p = hosts;
}

static void
warning_option_N_ignored(int nfragments)
{
	if (nfragments != GFARM_FILE_DONTCARE) {
		/*
		 * XXX - call gfarm_url_replicate() to replicate
		 * `nfragments' copies of gfarm_url:section?
		 */
		fprintf(stderr,
			"%s: warning: option -N is currently ignored\n", 
			program_name);
	}
}

static void
register_recursive_mode(int is_dest_dir, int argc, char *argv[],
	char *gfarm_url, int nfragments)
{
	int section_alloced = 0;
	int nhosts;
	char **hosts;
	gfarm_stringlist dir_list, src_file_list, target_file_list;
	struct lists_arg a;
	char *e, *section;
	int i, j;

	if (!is_dest_dir)
		check_is_argument_only_one(argc, "file or directory",
			"", "", "", gfarm_url);

	/*
	 * XXX - need to check all arguments are files if !is_dest_dir
	 */

	warning_option_N_ignored(nfragments);

	section = opt_section;
	if (section == NULL) {
		get_section(opt_hostname, &section, &section_alloced);
	}
	e = gfarm_stringlist_init(&dir_list);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(EXIT_FAILURE);
	}
	e = gfarm_stringlist_init(&src_file_list);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(EXIT_FAILURE);
	}
	e = gfarm_stringlist_init(&target_file_list);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(EXIT_FAILURE);
	}
	a.is_dest_dir = is_dest_dir;
	a.gfarm_url = gfarm_url;	
	a.dir_list = &dir_list;
	a.src_file_list = &src_file_list;
	a.target_file_list = &target_file_list;
	foreach_arg(argc, argv, add_dir_file_list, &a);

	for (i = 0; i < gfarm_stringlist_length(&dir_list); i++) {
		char *d = gfarm_stringlist_elem(&dir_list, i);

		e = gfs_mkdir(d, 0755);
		if (e != NULL) {
			fprintf(stderr, "%s: gfs_mkdir: %s, %s\n",
				program_name, d, e);
			exit(EXIT_FAILURE);
		}
	}

	get_hosts(&nhosts, &hosts);

	j = 0;
	for (i = 0; i < gfarm_stringlist_length(&src_file_list); i++) {
		register_file(
			gfarm_stringlist_elem(&target_file_list, i),
			section,
			hosts[j++],
			gfarm_stringlist_elem(&src_file_list, i));
		if (j >= nhosts)
			j = 0;
	}
	gfarm_strings_free_deeply(nhosts, hosts);
	if (section_alloced)
		free(section);
	gfarm_stringlist_free_deeply(&dir_list);
	gfarm_stringlist_free(&src_file_list);
	gfarm_stringlist_free_deeply(&target_file_list);
}

static void
register_program_mode(int is_dest_dir, int argc, char *argv[], char *gfarm_url,
	int nfragments)
{
	int section_alloced = 0;
	int nhosts;
	char **hosts;
	gfarm_stringlist src_file_list, target_file_list;
	struct lists_arg a;
	char *e, *section;
	int i, j;

	if (!is_dest_dir)
		check_is_argument_only_one(argc,
			"file", "", "", "", gfarm_url);

	warning_option_N_ignored(nfragments);

	section = opt_section;
	if (section == NULL) {
		get_section(opt_hostname, &section, &section_alloced);
	}
	e = gfarm_stringlist_init(&src_file_list);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(EXIT_FAILURE);
	}
	e = gfarm_stringlist_init(&target_file_list);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(EXIT_FAILURE);
	}
	a.is_dest_dir = is_dest_dir;
	a.gfarm_url = gfarm_url;	
	a.src_file_list = &src_file_list;
	a.target_file_list = &target_file_list;

	foreach_arg(argc, argv, add_file_list, &a);

	get_hosts(&nhosts, &hosts);

	j = 0;
	for (i = 0; i < gfarm_stringlist_length(&src_file_list); i++) {
		register_file(
			gfarm_stringlist_elem(&target_file_list, i),
			section,
			hosts[j++],
			gfarm_stringlist_elem(&src_file_list, i));
		if (j >= nhosts)
			j = 0;
	}	
	gfarm_strings_free_deeply(nhosts, hosts);
	gfarm_stringlist_free(&src_file_list);
	gfarm_stringlist_free(&target_file_list);
	if (section_alloced)
		free(section);
}

static void
register_fragment_mode(int is_dest_dir, int argc, char *argv[],char *gfarm_url,
	int nfragments, char *file_mode_arg, gfarm_mode_t file_mode)
{
	int i, j, nhosts;
	char **hosts;

	if (!is_dest_dir)
		check_is_argument_only_one(argc,
			"file", " fragment ", opt_section, " of", gfarm_url);

	if (nfragments == GFARM_FILE_DONTCARE)
		gfs_pio_get_node_size(&nfragments);

	get_hosts(&nhosts, &hosts);

	j = 0;
	for (i = 0; i < argc; i++) {
		register_fragment(is_dest_dir, gfarm_url,
				  strtol(opt_section, NULL, 0), nfragments,
				  hosts[j++], argv[i],
				  file_mode_arg == gfarm_url, file_mode);
		if (j >= nhosts)
			j = 0;
	}
}

static void
register_auto_index_mode(int is_dest_dir, int argc, char *argv[],
	char *gfarm_url, int nfragments,
	char *file_mode_arg, gfarm_mode_t file_mode) 
{
	int i, j, nhosts;
	char **hosts;

	if (nfragments == GFARM_FILE_DONTCARE)
		nfragments = argc;
	if (nfragments != argc) {
		fprintf(stderr, "%s: local file number %d "
			"doesn't match with -N %d\n",
			program_name, argc, nfragments);
		exit(EXIT_FAILURE);
	}
	if (is_dest_dir && nfragments > 1) {
		fprintf(stderr, "%s: cannot determine the file name "
			"under the directory %s, "
			"because multiple local file names are specifed\n",
			program_name, gfarm_url);
		exit(EXIT_FAILURE);
	}
	if (file_mode_arg == NULL) {
		int fd, fd_needs_close;

		if (!open_file(argv[0], &fd, &fd_needs_close))
			exit(EXIT_FAILURE);
		if (!get_file_mode(fd, argv[0], &file_mode))
			exit(EXIT_FAILURE);
		if (fd_needs_close)
			close(fd);
	}		

	get_hosts(&nhosts, &hosts);

	/* XXX - need to register in parallel? */
	j = 0;
	for (i = 0; i < argc; i++) {
		register_fragment(is_dest_dir, gfarm_url,
				  i, nfragments,
				  hosts[j++],
				  argv[i],
				  /* use_file_mode */ 1, file_mode);
		if (j >=  nhosts)
			j = 0;
	}
	gfarm_strings_free_deeply(nhosts, hosts);
}

int
main(int argc, char *argv[])
{
	/* options */
	int nfragments = GFARM_FILE_DONTCARE; /* -1, actually */
	enum register_mode reg_mode = UNDECIDED;
	char *e, *gfarm_url, *file_mode_arg;
	gfarm_mode_t file_mode = DEFAULT_FILE_MODE;
	int c, is_dest_dir;
	struct gfs_stat gs;

	e = gfarm_initialize(&argc, &argv);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(EXIT_FAILURE);
	}

	/*  Command options  */

	while ((c = getopt(argc, argv, "a:fh:iprs:D:I:H:N:?")) != -1) {
		switch (c) {
		case 'I':
			opt_section = optarg;
			reg_mode = FRAGMENT;
			break;
		case 'a':
			opt_section = optarg;
			break;
		case 'H':
			opt_hostfile = optarg;
			break;
		case 'N':
			nfragments = strtol(optarg, NULL, 0);
			break;
		case 'h':
			opt_hostname = optarg;
			break;
		case 'D':
			opt_domainname = optarg;
			break;
		case 'f':
			opt_force = 1;
			break;
		case 'p':
			reg_mode = PROGRAM;
			break;
		case 'i':
			reg_mode = AUTO_INDEX;
			break;
		case 'r':
			reg_mode = RECURSIVE;
			break;
		case 's':
			gfarm_set_minimum_free_disk_space(
				strtol(optarg, NULL, 0));
			break;
		case '?':
		default:
			usage();
		}
	}
	c = 0;
	if (opt_hostname != NULL)
		c++;
	if (opt_hostfile != NULL)
		c++;
	if (opt_domainname != NULL)
		c++;
	if (c > 1) {
		fprintf(stderr,
		    "%s: more than one options are specified "
		    "from -h, -H and -D\n",
		    program_name);
		usage();
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

	e = gfs_stat(gfarm_url, &gs);
	if (e == GFARM_ERR_NO_SUCH_OBJECT) {
		is_dest_dir = 0;
		file_mode_arg = NULL;
	} else if (e != NULL) {
		fprintf(stderr, "%s: %s: %s\n", program_name, gfarm_url, e);
		exit(EXIT_FAILURE);
	} else {
		if (GFARM_S_ISREG(gs.st_mode)) {
			is_dest_dir = 0;
			file_mode_arg = gfarm_url;
			file_mode = gs.st_mode;
		} else if (GFARM_S_ISDIR(gs.st_mode)) {
			is_dest_dir = 1;
			file_mode_arg = NULL;
		} else { /* defensive programming. this shouldn't happen. */
			fprintf(stderr, "%s: %s: unknown file type\n",
			    program_name, gfarm_url);
			exit(EXIT_FAILURE);
		}
		gfs_stat_free(&gs);
	}

	/*
	 * distinguish which mode is specified:
	 * 1. program mode:
	 *	gfreg [-p] [-h <hostname>] [-a <architecture>] \
	 *		<local-program>... <gfarm-URL>
	 * 2. auto index mode:
	 *	gfreg [-i] [-h <hostname>] [-H <hostfile>] [-D <domainname>] \
	 *		<local-file>... <gfarm-URL>
	 * 3. fragment mode:
	 *	gfreg -I <index> [-h <hostname>] [-N <nfragments>] \
	 *		<local-file>... <gfarm-URL>
	 * 4. recursive mode:
	 *	gfreg -r [-h <hostname>] [-a <architecture>] \
	 *		<local-directory|local-program|local-file>... \
	 *		<gfarm-URL>
	 */
	if (reg_mode == UNDECIDED)
		reg_mode = decide_reg_mode(file_mode_arg, file_mode,
			argc, argv);
	check_arguments(argc, argv,
		opt_hostfile, reg_mode, is_dest_dir, file_mode_arg);
	/* exits if an error occurs */

	if (reg_mode == RECURSIVE) {
		register_recursive_mode(is_dest_dir, argc, argv, gfarm_url,
			nfragments);
	} else 	if (reg_mode == PROGRAM) {
		register_program_mode(is_dest_dir, argc, argv, gfarm_url,
			nfragments);
	} else if (reg_mode == FRAGMENT) {
		register_fragment_mode(is_dest_dir, argc, argv, gfarm_url,
			nfragments, file_mode_arg, file_mode);
	} else if (reg_mode == AUTO_INDEX) {
		register_auto_index_mode(is_dest_dir, argc, argv, gfarm_url,
			nfragments, file_mode_arg, file_mode);
	}

	e = gfarm_terminate();
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(EXIT_FAILURE);
	}

	exit(error_happened);
}
