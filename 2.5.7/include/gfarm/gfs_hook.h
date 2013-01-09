/*
 * $Id$
 */

void gfs_hook_set_default_view_local();
void gfs_hook_set_default_view_index(int index, int nfrags);
void gfs_hook_set_default_view_global();
void gfs_hook_set_default_view_section(char *);

char *gfs_hook_set_view_local(int fd, int flags);
char *gfs_hook_set_view_index(
	int fd, int nfrags, int index, char *host, int flags);
char *gfs_hook_set_view_global(int fd, int flags);
char *gfs_hook_set_view_section(int fd, char *section, char *host, int flags);
