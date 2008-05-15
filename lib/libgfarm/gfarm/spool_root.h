/*
 * $Id$
 */

char *gfarm_spool_root_set(char *);
void gfarm_spool_root_clear(void);
void gfarm_spool_root_set_default(void);

char *gfarm_spool_root_get_for_compatibility(void);
int gfarm_spool_root_foreach(int (*)(char *, void *), char *, void *);
char *gfarm_spool_root_get_for_write(void);
char *gfarm_spool_root_get_for_read(char *);
void gfarm_spool_root_check(void);
char *gfarm_spool_path(char *, char *);
char *gfarm_spool_path_localize_for_write(char *, char **);
char *gfarm_spool_path_localize(char *, char **);
