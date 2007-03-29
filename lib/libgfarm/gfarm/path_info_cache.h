/*
 * $Id$
 */

/* path_info cache */

void gfarm_cache_path_info_param_set(unsigned int, unsigned int);
char *gfarm_cache_path_info_get(const char *, struct gfarm_path_info *);
char *gfarm_cache_path_info_set(const char *, const struct gfarm_path_info *);
char *gfarm_cache_path_info_replace(const char *,
	const struct gfarm_path_info *);
char *gfarm_cache_path_info_remove(const char *);
char *gfarm_cache_size_set(const char *, file_offset_t);
