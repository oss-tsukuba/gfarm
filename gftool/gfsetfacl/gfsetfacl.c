/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <unistd.h>
#include <errno.h>

#include <gfarm/gfarm.h>

char *program_name = "gfsetfacl";

static void
usage(void)
{
	fprintf(stderr,
		"Usage: %s [-bkt] [-m acl_spec] [-M acl_file] path...\n",
		program_name);
	exit(1);
}

static void
print_acl(const char *title, const char *path,
	  const gfarm_acl_t acl_acc, const gfarm_acl_t acl_def)
{
	gfarm_error_t e;
	char *acc_text = NULL, *def_text = NULL;
#if 0
	char separator = ',';
	int options = GFARM_ACL_TEXT_ABBREVIATE;
	char default_prefix[] = "d:";
#else
	char separator = '\n';
	int options = GFARM_ACL_TEXT_SOME_EFFECTIVE;
	char default_prefix[] = "default:";
#endif

	if (acl_acc != NULL) {
		e = gfs_acl_to_any_text(acl_acc, NULL,
					separator, options, &acc_text);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr,
				"%s: gfs_acl_to_any_text() failed: %s\n",
				program_name, gfarm_error_string(e));
			return;
		}
	}
	if (acl_def != NULL) {
		e = gfs_acl_to_any_text(acl_def, default_prefix,
					separator, options, &def_text);
		if (e != GFARM_ERR_NO_ERROR) {
			free(acc_text);
			fprintf(stderr,
				"%s: gfs_acl_to_any_text() failed: %s\n",
				program_name, gfarm_error_string(e));
			return;
		}
	}
	printf("%s\npath=%s:\n%s%c%s\n",
	       title, path,
	       acc_text ? acc_text : "(no access ACL)",
	       separator,
	       def_text ? def_text : "(no default ACL)");
	free(acc_text);
	free(def_text);
}

static gfarm_error_t
find_acl_entry(const gfarm_acl_t acl, gfarm_acl_tag_t tag, const char *qual,
	       gfarm_acl_entry_t *entry_p)
{
	gfarm_error_t e;
	gfarm_acl_entry_t ent;
	gfarm_acl_tag_t tag2;
	char *qual2;

	e = gfs_acl_get_entry(acl, GFARM_ACL_FIRST_ENTRY, &ent);
	while (e == GFARM_ERR_NO_ERROR) {
		gfs_acl_get_tag_type(ent, &tag2);
		if (tag == tag2) {
			if (tag != GFARM_ACL_USER && tag != GFARM_ACL_GROUP) {
				*entry_p = ent;
				return (GFARM_ERR_NO_ERROR);
			}
			/* GFARM_ACL_USER or GFARM_ACL_GROUP */
			gfs_acl_get_qualifier(ent, &qual2);

			if (qual == NULL || strlen(qual) == 0 ||
			    qual2 == NULL || strlen(qual2) == 0) {
				/* unexpected */
				fprintf(stderr,
					"%s: GFARM_ACL_USER or "
					"GFARM_ACL_GROUP must have "
					"a qualifier\n",
					program_name);
				*entry_p = NULL;
				return (GFARM_ERR_INVALID_ARGUMENT);
			}
			if (strcmp(qual, qual2) == 0) {
				*entry_p = ent;
				return (GFARM_ERR_NO_ERROR);
			}
		}
		e = gfs_acl_get_entry(acl, GFARM_ACL_NEXT_ENTRY, &ent);
	}
	*entry_p = NULL;
	return (e); /* not found or error */
}

static gfarm_error_t
remove_extended_acl(gfarm_acl_t acl)
{
	gfarm_error_t e;
	gfarm_acl_entry_t ent_group = NULL, ent_mask = NULL, ent;
	gfarm_acl_permset_t pset_group, pset_mask;
	gfarm_acl_tag_t tag;

	e = find_acl_entry(acl, GFARM_ACL_GROUP_OBJ, NULL, &ent_group);
	if (e != GFARM_ERR_NO_SUCH_OBJECT && e != GFARM_ERR_NO_ERROR)
		return (e);
	e = find_acl_entry(acl, GFARM_ACL_MASK, NULL, &ent_mask);
	if (e != GFARM_ERR_NO_SUCH_OBJECT && e != GFARM_ERR_NO_ERROR)
		return (e);

	/* mask GFARM_ACL_GROUP_OBJ */
	if (ent_group != NULL && ent_mask != NULL) {
		int bool;
		gfs_acl_get_permset(ent_group, &pset_group);
		gfs_acl_get_permset(ent_mask, &pset_mask);
		gfs_acl_get_perm(pset_mask, GFARM_ACL_READ, &bool);
		if (!bool)
			gfs_acl_delete_perm(pset_group, GFARM_ACL_READ);
		gfs_acl_get_perm(pset_mask, GFARM_ACL_WRITE, &bool);
		if (!bool)
			gfs_acl_delete_perm(pset_group, GFARM_ACL_WRITE);
		gfs_acl_get_perm(pset_mask, GFARM_ACL_EXECUTE, &bool);
		if (!bool)
			gfs_acl_delete_perm(pset_group, GFARM_ACL_EXECUTE);
	}

	e = gfs_acl_get_entry(acl, GFARM_ACL_FIRST_ENTRY, &ent);
	while (e == GFARM_ERR_NO_ERROR) {
		gfs_acl_get_tag_type(ent, &tag);
		switch (tag) {
		case GFARM_ACL_USER:
		case GFARM_ACL_GROUP:
		case GFARM_ACL_MASK:
			gfs_acl_delete_entry(acl, ent);
		}
		e = gfs_acl_get_entry(acl, GFARM_ACL_NEXT_ENTRY, &ent);
	}
	if (e == GFARM_ERR_NO_SUCH_OBJECT)
		e = GFARM_ERR_NO_ERROR;
	return (e);
}

static gfarm_error_t
merge_acl(gfarm_acl_t *acl_to_p, const gfarm_acl_t acl_from)
{
	gfarm_error_t e;
	gfarm_acl_entry_t ent_from, ent_to;
	gfarm_acl_tag_t tag;
	char *qual_from;
	gfarm_acl_permset_t pset_from;

	if (acl_from == NULL)
		return (GFARM_ERR_NO_ERROR); /* do nothing */
	if (*acl_to_p == NULL)
		return (gfs_acl_dup(acl_to_p, acl_from));

	e = gfs_acl_get_entry(acl_from, GFARM_ACL_FIRST_ENTRY, &ent_from);
	while (e == GFARM_ERR_NO_ERROR) {
		gfs_acl_get_tag_type(ent_from, &tag);
		gfs_acl_get_qualifier(ent_from, &qual_from);
		e = find_acl_entry(*acl_to_p, tag, qual_from, &ent_to);
		if (e != GFARM_ERR_NO_SUCH_OBJECT && e != GFARM_ERR_NO_ERROR)
			return (e);
		if (ent_to == NULL) {
			e = gfs_acl_create_entry(acl_to_p, &ent_to);
			if (e != GFARM_ERR_NO_ERROR)
				return (e);
			gfs_acl_set_tag_type(ent_to, tag);
			if (qual_from != NULL) {
				char *qual_to = strdup(qual_from);
				if (qual_to == NULL)
					return (GFARM_ERR_NO_MEMORY);
				gfs_acl_set_qualifier(ent_to, qual_to);
			} else
				gfs_acl_set_qualifier(ent_to, NULL);
		}
		/* ent_to != NULL */
		gfs_acl_get_permset(ent_from, &pset_from);
		gfs_acl_set_permset(ent_to, pset_from);

		e = gfs_acl_get_entry(acl_from, GFARM_ACL_NEXT_ENTRY,
				      &ent_from);
	}
	if (e == GFARM_ERR_NO_SUCH_OBJECT)
		e = GFARM_ERR_NO_ERROR;
	return (e);
}

static gfarm_error_t
set_missing(gfarm_acl_t *acl_def_p, const gfarm_acl_t acl_acc,
	    gfarm_acl_tag_t tag)
{
	gfarm_error_t e;
	gfarm_acl_entry_t ent_def, ent_acc;
	gfarm_acl_tag_t tag_acc;
	gfarm_acl_permset_t pset_acc;
	char *qual_acc;

	e = find_acl_entry(*acl_def_p, tag, NULL, &ent_def);
	if (e != GFARM_ERR_NO_SUCH_OBJECT && e != GFARM_ERR_NO_ERROR)
		return (e);
	if (ent_def == NULL) {
		e = find_acl_entry(acl_acc, tag, NULL, &ent_acc);
		if (e != GFARM_ERR_NO_ERROR) /* don't fail */
			return (e);
		e = gfs_acl_create_entry(acl_def_p, &ent_def);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
		gfs_acl_get_tag_type(ent_acc, &tag_acc);
		gfs_acl_set_tag_type(ent_def, tag_acc);
		gfs_acl_get_qualifier(ent_acc, &qual_acc);
		if (qual_acc != NULL) {
			char *qual_def = strdup(qual_acc);
			if (qual_def == NULL)
				return (GFARM_ERR_NO_MEMORY);
			gfs_acl_set_qualifier(ent_def, qual_def);
		} else
			gfs_acl_set_qualifier(ent_def, NULL);
		gfs_acl_get_permset(ent_acc, &pset_acc);
		gfs_acl_set_permset(ent_def, pset_acc);
	}
	return (e);
}

static gfarm_error_t
fill_missing_acl_default(gfarm_acl_t *acl_def_p, const gfarm_acl_t acl_acc)
{
	gfarm_error_t e;

	e = set_missing(acl_def_p, acl_acc, GFARM_ACL_USER_OBJ);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	e = set_missing(acl_def_p, acl_acc, GFARM_ACL_GROUP_OBJ);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	e = set_missing(acl_def_p, acl_acc, GFARM_ACL_OTHER);
	return (e);
}

static int
have_mask(gfarm_acl_t acl)
{
	gfarm_error_t e;
	gfarm_acl_entry_t ent;

	e = find_acl_entry(acl, GFARM_ACL_MASK, NULL, &ent);
	if (e != GFARM_ERR_NO_SUCH_OBJECT && e != GFARM_ERR_NO_ERROR)
		return (0);
	if (ent == NULL)
		return (0);
	else
		return (1);
}

static gfarm_error_t
acl_set(const char *path,
	int remove_acl_acccess, int remove_acl_default,
	const char *acl_file_buf, const char *acl_spec, int is_test)
{
	gfarm_error_t e;
	struct gfs_stat sb;
	gfarm_acl_t acl_acc, acl_def;
	gfarm_acl_t acl_acc_orig, acl_def_orig;
#if 0
	int have_mask_acc, have_mask_def;
#endif
	int is_not_equiv;
	int acl_check_err;

	e = gfs_stat_cached(path, &sb);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	e = gfs_acl_get_file(path, GFARM_ACL_TYPE_ACCESS, &acl_acc);
	if (e == GFARM_ERR_NO_SUCH_OBJECT)
		e = gfs_acl_from_mode(sb.st_mode, &acl_acc);
	if (e != GFARM_ERR_NO_ERROR) {
		gfs_stat_free(&sb);
		return (e);
	}
	if (GFARM_S_ISDIR(sb.st_mode)) {
		e = gfs_acl_get_file(path, GFARM_ACL_TYPE_DEFAULT, &acl_def);
		if (e == GFARM_ERR_NO_SUCH_OBJECT)
			acl_def = NULL;
		else if (e != GFARM_ERR_NO_ERROR) {
			gfs_stat_free(&sb);
			gfs_acl_free(acl_acc);
			return (e);
		} else if (gfs_acl_entries(acl_def) == 0) {
			gfs_acl_free(acl_def);
			acl_def = NULL;
		}
	} else
		acl_def = NULL;
	/* acl_acc != NULL / acl_def != NULL or NULL */

	/* save original ACL */
	e = gfs_acl_dup(&acl_acc_orig, acl_acc);
	if (e != GFARM_ERR_NO_ERROR) {
		gfs_stat_free(&sb);
		gfs_acl_free(acl_acc);
		gfs_acl_free(acl_def);
		return (e);
	}
	if (acl_def != NULL) {
		e = gfs_acl_dup(&acl_def_orig, acl_def);
		if (e != GFARM_ERR_NO_ERROR) {
			gfs_stat_free(&sb);
			gfs_acl_free(acl_acc);
			gfs_acl_free(acl_def);
			gfs_acl_free(acl_acc_orig);
			return (e);
		}
	} else
		acl_def_orig = NULL;
	/* prepared: acl_acc, acl_def, acl_acc_orig, acl_def_orig */

	if (remove_acl_acccess) {
		e = remove_extended_acl(acl_acc);
		if (e != GFARM_ERR_NO_ERROR)
			goto end;
	}
	if (remove_acl_default && acl_def != NULL) {
		gfs_acl_free(acl_def);
		e = gfs_acl_init(5, &acl_def);
		if (e != GFARM_ERR_NO_ERROR)
			goto end;
	}
#if 0
	/* Does gfarm_acl_t have a MASK entry ? */
	have_mask_acc = have_mask(acl_acc);
	if (acl_def != NULL)
		have_mask_def = have_mask(acl_def);
	else
		have_mask_def = 0;
#endif

	if (acl_file_buf) {
		gfarm_acl_t acl_acc2 = NULL, acl_def2 = NULL;
		e = gfs_acl_from_text_with_default(acl_file_buf,
						   &acl_acc2, &acl_def2);
		if (e != GFARM_ERR_NO_ERROR)
			goto end;
		if (acl_acc2 != NULL) {
			e = merge_acl(&acl_acc, acl_acc2);
			if (e != GFARM_ERR_NO_ERROR) {
				gfs_acl_free(acl_acc2);
				gfs_acl_free(acl_def2);
				goto end;
			}
		}
		if (acl_def2 != NULL && GFARM_S_ISDIR(sb.st_mode)) {
			e = merge_acl(&acl_def, acl_def2);
			if (e != GFARM_ERR_NO_ERROR) {
				gfs_acl_free(acl_acc2);
				gfs_acl_free(acl_def2);
				goto end;
			}
		}
		if (is_test)
			print_acl("--- file input ---",
				  path, acl_acc2, acl_def2);

		gfs_acl_free(acl_acc2);
		gfs_acl_free(acl_def2);
	}

	if (acl_spec) {
		gfarm_acl_t acl_acc2 = NULL, acl_def2 = NULL;
		e = gfs_acl_from_text_with_default(acl_spec,
						   &acl_acc2, &acl_def2);
		if (e != GFARM_ERR_NO_ERROR)
			goto end;
		if (acl_acc2 != NULL) {
			e = merge_acl(&acl_acc, acl_acc2);
			if (e != GFARM_ERR_NO_ERROR) {
				gfs_acl_free(acl_acc2);
				gfs_acl_free(acl_def2);
				goto end;
			}
		}
		if (acl_def2 != NULL && GFARM_S_ISDIR(sb.st_mode)) {
			e = merge_acl(&acl_def, acl_def2);
			if (e != GFARM_ERR_NO_ERROR) {
				gfs_acl_free(acl_acc2);
				gfs_acl_free(acl_def2);
				goto end;
			}
		}
		if (is_test)
			print_acl("--- command line input ---",
				  path, acl_acc2, acl_def2);

		gfs_acl_free(acl_acc2);
		gfs_acl_free(acl_def2);
	}

	if (acl_def != NULL && gfs_acl_entries(acl_def) > 0) {
		e = fill_missing_acl_default(&acl_def, acl_acc);
		if (e != GFARM_ERR_NO_ERROR)
			goto end;
	}

	/* calculate MASK if a MASK entry does not exist */
	e = gfs_acl_equiv_mode(acl_acc, NULL, &is_not_equiv);
	if (e != GFARM_ERR_NO_ERROR)
		goto end;
	if (is_not_equiv && !have_mask(acl_acc)) {
		e = gfs_acl_calc_mask(&acl_acc);
		if (e != GFARM_ERR_NO_ERROR)
			goto end;
	}
	if (acl_def != NULL && gfs_acl_entries(acl_def) > 0) {
		e = gfs_acl_equiv_mode(acl_def, NULL, &is_not_equiv);
		if (e != GFARM_ERR_NO_ERROR)
			goto end;
		if (is_not_equiv && !have_mask(acl_def)) {
			e = gfs_acl_calc_mask(&acl_def);
			if (e != GFARM_ERR_NO_ERROR)
				goto end;
		}
	}

	/* sort */
	gfs_acl_sort(acl_acc);
	gfs_acl_sort(acl_def);

	/* valid ? */
	e = gfs_acl_check(acl_acc, NULL, &acl_check_err);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr,
			"%s: invalid access ACL: %s\n",
			program_name, gfs_acl_error(acl_check_err));
		goto end;
	}
	if (acl_def != NULL && gfs_acl_entries(acl_def) > 0) {
		e = gfs_acl_check(acl_def, NULL, &acl_check_err);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr,
				"%s: invalid default ACL: %s\n",
				program_name, gfs_acl_error(acl_check_err));
			goto end;
		}
	}

	/* Are acl_acc and acl_def changed ? */
	if (gfs_acl_cmp(acl_acc_orig, acl_acc) == 0) {
		gfs_acl_free(acl_acc);
		acl_acc = NULL;
	}
	if (acl_def != NULL &&
	    ((acl_def_orig != NULL && gfs_acl_cmp(acl_def_orig, acl_def) == 0)
	     || (acl_def_orig == NULL && gfs_acl_entries(acl_def) == 0))) {
		gfs_acl_free(acl_def);
		acl_def = NULL;
	}

	/* print ACL and exit */
	if (is_test) {
		print_acl("--- test ACL results ---",
			  path, acl_acc, acl_def);
		goto end;
	}

	/* update Gfarm filesystem */
	if (acl_acc != NULL) {
		e = gfs_acl_set_file(path, GFARM_ACL_TYPE_ACCESS, acl_acc);
		if (e != GFARM_ERR_NO_ERROR)
			goto end;
	}
	if (acl_def != NULL) {
		if (GFARM_S_ISDIR(sb.st_mode)) {
			if (gfs_acl_entries(acl_def) > 0)
				e = gfs_acl_set_file(path,
						     GFARM_ACL_TYPE_DEFAULT,
						     acl_def);
			else
				e = gfs_acl_delete_def_file(path);
		} else if (gfs_acl_entries(acl_def) > 0) {
			fprintf(stderr,
				"%s: %s: Only directory "
				"can have default ACL\n",
				program_name, path);
			e = GFARM_ERR_INVALID_ARGUMENT;
		}
	}
end:
	gfs_stat_free(&sb);
	gfs_acl_free(acl_acc);
	gfs_acl_free(acl_def);
	gfs_acl_free(acl_acc_orig);
	gfs_acl_free(acl_def_orig);

	return (e);
}

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	int i, c, status = 0;
	const char *acl_spec = NULL, *acl_file = NULL;
	char *acl_file_buf = NULL;
	int remove_acl_acccess = 0, remove_acl_default = 0, is_test = 0;

	if (argc > 0)
		program_name = basename(argv[0]);
	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(1);
	}

	while ((c = getopt(argc, argv, "bhkm:M:t?")) != -1) {
		switch (c) {
		case 'b':
			remove_acl_acccess = 1;
			remove_acl_default = 1;
			break;
		case 'k':
			remove_acl_default = 1;
			break;
		case 'm':
			acl_spec = optarg;
			break;
		case 'M':
			acl_file = optarg;
			break;
		case 't':
			is_test = 1;
			break;
		case 'h':
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc <= 0)
		usage();

#if 0
	gflog_set_priority_level(gflog_syslog_name_to_priority("debug"));
#endif
	if (acl_file) {
		FILE *f;
		size_t bufsize, reqsize, retsize;
		int pos;
#define BUFSIZE 64
		if (strcmp(acl_file, "-") == 0)
		    f = stdin;
		else {
			f = fopen(acl_file, "r");
			if (f == NULL) {
				fprintf(stderr, "%s: fopen(%s) failed: %s",
					program_name, acl_file,
					strerror(errno));
				goto terminate;
			}
		}
		GFARM_MALLOC_ARRAY(acl_file_buf, BUFSIZE);
		if (acl_file_buf == NULL) {
			fprintf(stderr, "%s: no memory", program_name);
			if (f != stdin)
				fclose(f);
			goto terminate;
		}
		bufsize = BUFSIZE;
		pos = 0;
		for (;;) {
			if (pos >= bufsize) {
				char *tmp;
				bufsize *= 2;
				GFARM_REALLOC_ARRAY(tmp, acl_file_buf,
						    bufsize);
				if (tmp == NULL) {
					fprintf(stderr, "%s: no memory",
						program_name);
					free(acl_file_buf);
					if (f != stdin)
						fclose(f);
					goto terminate;
				}
				acl_file_buf = tmp;
			}
			reqsize = bufsize - pos;
			retsize = fread(acl_file_buf + pos, 1, reqsize, f);
			if (retsize < reqsize) {
				if (ferror(f)) {
					fprintf(stderr,
						"%s: fread(%s) failed: %s",
						program_name, acl_file,
						strerror(errno));
					free(acl_file_buf);
					if (f != stdin)
						fclose(f);
					goto terminate;
				}
				acl_file_buf[pos + retsize] = '\0';
				break;
			}
			pos += retsize;
		}
		if (f != stdin)
			fclose(f);
	}

	for (i = 0; i < argc; i++) {
		e = acl_set(argv[i], remove_acl_acccess, remove_acl_default,
			    acl_file_buf, acl_spec, is_test);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "%s: %s: %s\n",
			    program_name, argv[i], gfarm_error_string(e));
			status = 1;
		}
	}
terminate:
	free(acl_file_buf);
	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		status = 1;
	}
	return (status);
}
