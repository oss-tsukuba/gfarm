#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <math.h> /* file_offset_floor() may be floor() */
#include <time.h>
#include <gfarm/gfarm.h>

#define TABLE_SIZE_INITIAL 128
#define TABLE_SIZE_DELTA 128

/*
 * configuration file format:
 *	fragment_size fragment_hostnmae
 *		:
 */

char *
gfarm_import_fragment_config_read(char *config,
	int *np, char ***hosttabp, file_offset_t **sizetabp,
	int *error_linep)
{
	char *e, **host_table, line[1024];
	int i, table_size = TABLE_SIZE_INITIAL;
	file_offset_t *size_table;
	file_offset_t *stab;
	gfarm_stringlist host_list;
	FILE *fp;

	*error_linep = -1;
	GFARM_MALLOC_ARRAY(size_table, table_size);
	if (size_table == NULL)
		return (GFARM_ERR_NO_MEMORY);
	e = gfarm_stringlist_init(&host_list);
	if (e != NULL) {
		free(size_table);
		return (e);
	}
	if (strcmp(config, "-") == 0) {
		fp = stdin;
	} else if ((fp = fopen(config, "r")) == NULL) {
		gfarm_stringlist_free(&host_list);
		free(size_table);
		return (GFARM_ERR_NO_SUCH_OBJECT);
	}
	for (i = 0; fgets(line, sizeof(line), fp) != NULL; i++) {
		int l = strlen(line);
		char *s, *t, *host;
		file_offset_t size;

		if (l > 0 && line[l - 1] == '\n')
			line[--l] = '\0';
		size = string_to_file_offset(line, &s);
		if (s == line) {
			e = "fragment size expected";
			*error_linep = i + 1;
			goto error;
		}
		while (isspace(*(unsigned char *)s))
			s++;
		if (*s == '\0') {
			e = "fragment hostname expected";
			*error_linep = i + 1;
			goto error;
		}
		for (t = s; *t != '\0' && !isspace(*(unsigned char *)t); t++)
			;
		*t = '\0';
		host = strdup(s);
		if (host == NULL) {
			e = GFARM_ERR_NO_MEMORY;
			*error_linep = i + 1;
			goto error;
		}
		e = gfarm_stringlist_add(&host_list, host);
		if (e != NULL) {
			*error_linep = i + 1;
			goto error;
		}
		if (i >= table_size) {
			table_size += TABLE_SIZE_DELTA;
			GFARM_REALLOC_ARRAY(stab, size_table, table_size);
			if (stab == NULL) {
				e = GFARM_ERR_NO_MEMORY;
				*error_linep = i + 1;
				goto error;
			}
			size_table = stab;
		}
		size_table[i] = size;
	}
	if (i == 0) {
		e = "empty file";
		goto error;
	}
	host_table = gfarm_strings_alloc_from_stringlist(&host_list);
	if (host_table == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto error;
	}
	if (i < table_size) {
		GFARM_REALLOC_ARRAY(stab, size_table, i);
		if (stab == NULL) {
			e = GFARM_ERR_NO_MEMORY;
			goto error;
		}
		memcpy(stab, size_table, sizeof(size_table[0]) * i);
		size_table = stab;
	}

	/*
	 * do not call gfarm_stringlist_free_deeply() here,
	 * because the strings are passed to *host_table.
	 */
	gfarm_stringlist_free(&host_list);
	/* no limit on last fragment */
	size_table[i - 1] = FILE_OFFSET_T_MAX;

	*np = i;
	*hosttabp = host_table;
	*sizetabp = size_table;
	if (strcmp(config, "-") != 0)
		fclose(fp);
	return (NULL);

error:
	if (strcmp(config, "-") != 0)
		fclose(fp);
	gfarm_stringlist_free_deeply(&host_list);
	free(size_table);
	return (e);
}

file_offset_t *
gfarm_import_fragment_size_alloc(file_offset_t total_size, int n)
{
	file_offset_t *sizetab;
	file_offset_t fragment_size;
	int i;

	GFARM_MALLOC_ARRAY(sizetab, n);
	if (sizetab == NULL)
		return (NULL);

	fragment_size = file_offset_floor((total_size + n - 1) / n);
	--n;
	for (i = 0; i < n; i++)
		sizetab[i] = fragment_size;

	/* no limit on last fragment */
	sizetab[n] = FILE_OFFSET_T_MAX;
	return (sizetab);
}

/*
 * NOTE:
 *	returned (*linetabp) should be freed by gfarm_strings_free_deeply().
 *
 * configuration file format:
 *	hostname1
 *	hostname2
 *	   :
 */

char *
gfarm_hostlist_read(char *filename,
	int *np, char ***host_table_p, int *error_linep)
{
	gfarm_stringlist host_list;
	FILE *fp;
	int i;
	char *e, line[1024];

	*error_linep = -1;
	e = gfarm_stringlist_init(&host_list);
	if (e != NULL)
		return (e);
	if (strcmp(filename, "-") == 0) {
		fp = stdin;
	} else if ((fp = fopen(filename, "r")) == NULL) {
		gfarm_stringlist_free(&host_list);
		return (GFARM_ERR_NO_SUCH_OBJECT);
	}
	for (i = 0; fgets(line, sizeof(line), fp) != NULL; i++) {
		int l = strlen(line);
		char *s, *t, *host;

		if (l > 0 && line[l - 1] == '\n')
			line[--l] = '\0';
		for (s = line; isspace(*(unsigned char *)s); s++)
			;
		if (*s == '\0') {
			e = "hostname expected";
			*error_linep = i + 1;
			goto error;
		}
		for (t = s; *t != '\0' && !isspace(*(unsigned char *)t); t++)
			;
		*t = '\0';
		host = strdup(s);
		if (host == NULL) {
			e = GFARM_ERR_NO_MEMORY;
			*error_linep = i + 1;
			goto error;
		}
		e = gfarm_stringlist_add(&host_list, host);
		if (e != NULL) {
			free(host);
			*error_linep = i + 1;
			goto error;
		}
	}
	if (i == 0) {
		e = "empty file";
		goto error;
	}
	*np = gfarm_stringlist_length(&host_list);
	*host_table_p = gfarm_strings_alloc_from_stringlist(&host_list);
	if (e != NULL)
		goto error;
	/*
	 * do not call gfarm_stringlist_free_deeply() here,
	 * because the strings are passed to *host_table.
	 */
	gfarm_stringlist_free(&host_list);
	if (strcmp(filename, "-") != 0)
		fclose(fp);
	return (NULL);

error:
	if (strcmp(filename, "-") != 0)
		fclose(fp);
	gfarm_stringlist_free_deeply(&host_list);
	return (e);
}
