/*
 * $Id$
 */

struct gfpconcat_option {
	int compare;		/* -c */
	int force;		/* -f */
	char *dst_host; 	/* -h */
	char *input_list; 	/* -i */
	int n_para;		/* -j */
	off_t minimum_size;	/* -m */
	char *out_file; 	/* -o */
	int performance;	/* -p */
	int quiet;		/* -q */
	int verbose;		/* -v */
	int debug;		/* -d */
	int test;		/* -t */

	/* params */
	int argc;
	char **argv;
	char *program_name;
	void (*usage_func)(int, struct gfpconcat_option *);
	GFURL tmp_url;
	GFURL out_url;
	gfarm_ino_t out_ino;
	int out_exist;
	struct gfpconcat_part *part_list;
	int n_part;
	char **parts;
	int orig_mode;
	int mode;
	off_t total_size;
	int gfarm_initialized;
};

void gfpconcat_init(int, char **, char *,
	void gfpconcat_usage(int, struct gfpconcat_option *),
	void (*getopt_func)(int, char **, struct gfpconcat_option *),
	struct gfpconcat_option *);
int gfpconcat_main(struct gfpconcat_option *);

gfarm_error_t gfarm_filelist_read(char *, int *, char ***, int *);
