/*
 * $Id$
 */

#include <stdlib.h>
#include <stdio.h>
#include <gfarm/gfarm.h>
#include "gfarm_xinfo.h"

void
gfarm_section_xinfo_free(struct gfarm_section_xinfo *i)
{
	free(i->file);
	gfarm_strings_free_deeply(i->ncopy, i->copy);
	gfarm_file_section_info_free(&i->i);
	free(i);
}

void
gfarm_section_xinfo_print(struct gfarm_section_xinfo *info)
{
	int i;

	printf("%s (%s) [%lld byte]\n",
	       info->file, info->i.section, info->i.filesize);
	printf("copy (%d):", info->ncopy);
	for (i = 0; i < info->ncopy; ++i)
		printf(" %s", info->copy[i]);
	printf("\n");
}
