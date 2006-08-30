/*
 *
 */

struct gfarm_section_xinfo {
	char *file;
	int ncopy;
	char **copy;
	struct gfarm_file_section_info i;
};

void gfarm_section_xinfo_free(struct gfarm_section_xinfo *);
void gfarm_section_xinfo_print(struct gfarm_section_xinfo *);
