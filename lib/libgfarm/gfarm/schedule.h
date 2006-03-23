/*
 *
 */

void gfarm_strings_expand_cyclic(int, char **, int, char **);

char *gfarm_schedule_search_idle_hosts_to_write(int, char **, int, char **);
char *gfarm_schedule_search_idle_acyclic_hosts_to_write(
	int, char **, int *, char **);
char *gfarm_schedule_search_idle_by_all_to_write(int, char **);
char *gfarm_schedule_search_idle_by_domainname_to_write(
	const char *, int, char **);
char *gfarm_schedule_search_idle_acyclic_by_domainname_to_write(
	const char *, int *, char **);
char *gfarm_schedule_search_idle_by_program_to_write(char *, int, char **);
char *gfarm_file_section_host_schedule_to_write(char *, char *, char **);
char *gfarm_file_section_host_schedule_by_program_to_write(
	char *, char *, char *, char **);

char *gfarm_file_section_host_schedule_with_priority_to_local_to_write(
	char *, char *, char **);

