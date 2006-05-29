/*
 * Copyright (c) 2003-2006 National Institute of Advanced
 * Industrial Science and Technology (AIST).  All rights reserved.
 *
 * Copyright (c) 2006 National Institute of Informatics in Japan,
 * All rights reserved.
 *
 * This file or a portion of this file is licensed under the terms of
 * the NAREGI Public License, found at
 * http://www.naregi.org/download/index.html.
 * If you redistribute this file, with or without modifications, you
 * must include this notice in the file.
 */

struct gfarm_host_sched_info;
gfarm_error_t gfarm_schedule_select_host(int, struct gfarm_host_sched_info *,
	int, char **, int *);

#if 0 /* not yet in gfarm v2 */

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

int gfarm_is_active_fsnode(void);
int gfarm_is_active_fsnode_to_write(void);

#endif
