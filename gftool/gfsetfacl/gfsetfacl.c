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
#include "gfarm_foreach.h"
#include "gfarm_path.h"

char *program_name = "gfsetfacl";

static void
usage(void)
{
	fprintf(stderr,
		"Usage: %s [-bknrtR] [-m acl_spec] [-M acl_file] path...\n",
		program_name);
	exit(1);
}

struct gfsetfacl_arg {
	int remove_acl_acccess, remove_acl_default, is_test, recalc_mask;
	int recursive;
	char *acl_file_buf, *acl_spec_buf;
};

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
	return (e); /* GFARM_ERR_NO_SUCH_OBJECT or other errors */
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
copy_permset_from_entry(
	gfarm_acl_t *acl_to_p, gfarm_acl_tag_t tag_to, char *qual_to,
	gfarm_acl_entry_t ent_from, int replace)
{
	gfarm_error_t e;
	gfarm_acl_entry_t ent_to;
	char *qual_from;
	gfarm_acl_permset_t pset_from;

	e = find_acl_entry(*acl_to_p, tag_to, qual_to, &ent_to);
	if (e != GFARM_ERR_NO_SUCH_OBJECT && e != GFARM_ERR_NO_ERROR)
		return (e);

	if (ent_to == NULL) {  /* e == GFARM_ERR_NO_SUCH_OBJECT */
		e = gfs_acl_create_entry(acl_to_p, &ent_to);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
		gfs_acl_set_tag_type(ent_to, tag_to);
		gfs_acl_get_qualifier(ent_from, &qual_from);
		if (qual_from != NULL) {
			char *qual_tmp = strdup(qual_from);
			if (qual_tmp == NULL)
				return (GFARM_ERR_NO_MEMORY);
			gfs_acl_set_qualifier(ent_to, qual_tmp);
		} else
			gfs_acl_set_qualifier(ent_to, NULL);
	} else if (!replace)
		return (GFARM_ERR_ALREADY_EXISTS);

	gfs_acl_get_permset(ent_from, &pset_from);
	gfs_acl_set_permset(ent_to, pset_from);

	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
copy_permset(
	gfarm_acl_t *acl_to_p, gfarm_acl_tag_t tag_to, char *qual_to,
	gfarm_acl_t acl_from, gfarm_acl_tag_t tag_from, char *qual_from,
	int replace)
{
	gfarm_error_t e;
	gfarm_acl_entry_t ent_from;

	e = find_acl_entry(acl_from, tag_from, qual_from, &ent_from);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	return (copy_permset_from_entry(
			acl_to_p, tag_to, qual_to, ent_from, replace));
}

static gfarm_error_t
merge_acl(gfarm_acl_t *acl_to_p, const gfarm_acl_t acl_from)
{
	gfarm_error_t e;
	gfarm_acl_entry_t ent_from;
	gfarm_acl_tag_t tag;
	char *qual_from;

	if (acl_from == NULL)
		return (GFARM_ERR_NO_ERROR); /* do nothing */
	if (*acl_to_p == NULL)
		return (gfs_acl_dup(acl_to_p, acl_from));

	e = gfs_acl_get_entry(acl_from, GFARM_ACL_FIRST_ENTRY, &ent_from);
	while (e == GFARM_ERR_NO_ERROR) {
		gfs_acl_get_tag_type(ent_from, &tag);
		gfs_acl_get_qualifier(ent_from, &qual_from);
		e = copy_permset_from_entry(
			acl_to_p, tag, qual_from, ent_from, 1);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
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
	gfarm_error_t e = copy_permset(
		acl_def_p, tag, NULL, acl_acc, tag, NULL, 0);
	if (e == GFARM_ERR_ALREADY_EXISTS)
		e = GFARM_ERR_NO_ERROR;
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
have_acl_mask(gfarm_acl_t acl)
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
merge_acl_buf(gfarm_acl_t *acl_acc_p, gfarm_acl_t *acl_def_p,
	      const char *acl_buf,
	      int *modified_acc_p, int *give_mask_acc_p,
	      int *modified_def_p, int *give_mask_def_p,
	      int is_test, const char *path, const char *msg)
{
	gfarm_error_t e;
	gfarm_acl_t acl_acc2 = NULL, acl_def2 = NULL;

	e = gfs_acl_from_text_with_default(acl_buf,
					   &acl_acc2, &acl_def2);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (acl_acc2 != NULL) {
		e = merge_acl(acl_acc_p, acl_acc2);
		if (e != GFARM_ERR_NO_ERROR) {
			gfs_acl_free(acl_acc2);
			gfs_acl_free(acl_def2);
			return (e);
		}
		*modified_acc_p = 1;
		if (*give_mask_acc_p == 0)
			*give_mask_acc_p = have_acl_mask(acl_acc2);
	}
	if (acl_def2 != NULL) {
		e = merge_acl(acl_def_p, acl_def2);
		if (e != GFARM_ERR_NO_ERROR) {
			gfs_acl_free(acl_acc2);
			gfs_acl_free(acl_def2);
			return (e);
		}
		*modified_def_p = 1;
		if (*give_mask_def_p == 0)
			*give_mask_def_p = have_acl_mask(acl_def2);
	}
	if (is_test)
		print_acl(msg, path, acl_acc2, acl_def2);
	gfs_acl_free(acl_acc2);
	gfs_acl_free(acl_def2);

	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
acl_set(char *path, struct gfs_stat *st, void *arg)
{
	gfarm_error_t e;
	gfarm_acl_t acl_acc = NULL, acl_def = NULL;
	gfarm_acl_t acl_acc_orig = NULL, acl_def_orig = NULL;
	int modified_acc = 0, modified_def = 0;
	int is_not_equiv;
	int give_mask_acc = 0, give_mask_def = 0;
	int acl_check_err;
	struct gfsetfacl_arg *a = arg;

	if (GFARM_S_ISLNK(st->st_mode))  /* ignore */
		return (GFARM_ERR_NO_ERROR);

	/* gfs_getxattr_cached follows symlinks */
	e = gfs_acl_get_file_cached(path, GFARM_ACL_TYPE_ACCESS, &acl_acc);
	if (e == GFARM_ERR_NO_SUCH_OBJECT) {
		gfarm_mode_t st_mode = st->st_mode;
		struct gfs_stat st2;

		if (GFARM_S_ISLNK(st->st_mode)) {
			/* follow symlinks */
			e = gfs_lstat(path, &st2);
			if (e != GFARM_ERR_NO_ERROR)
				goto end;
			st_mode = st2.st_mode;
			gfs_stat_free(&st2);
		}
		e = gfs_acl_from_mode(st_mode, &acl_acc);
	}
	if (e != GFARM_ERR_NO_ERROR)
		goto end;
	if (GFARM_S_ISDIR(st->st_mode)) {
		e = gfs_acl_get_file_cached(path, GFARM_ACL_TYPE_DEFAULT,
					    &acl_def);
		if (e == GFARM_ERR_NO_SUCH_OBJECT)
			acl_def = NULL;
		else if (e != GFARM_ERR_NO_ERROR) {
			goto end;
		} else if (gfs_acl_entries(acl_def) == 0) {
			gfs_acl_free(acl_def);
			acl_def = NULL;
		}
	} else
		acl_def = NULL;
	/* acl_acc != NULL && (acl_def != NULL || acl_def == NULL) */

	/* save original ACL */
	e = gfs_acl_dup(&acl_acc_orig, acl_acc);
	if (e != GFARM_ERR_NO_ERROR)
		goto end;
	if (acl_def != NULL) {
		e = gfs_acl_dup(&acl_def_orig, acl_def);
		if (e != GFARM_ERR_NO_ERROR)
			goto end;
	} else
		acl_def_orig = NULL;

	/* remove ACL entries */
	if (a->remove_acl_acccess) {
		e = remove_extended_acl(acl_acc);
		if (e != GFARM_ERR_NO_ERROR)
			goto end;
	}
	if (a->remove_acl_default && acl_def != NULL) {
		gfs_acl_free(acl_def);
		e = gfs_acl_init(5, &acl_def);
		if (e != GFARM_ERR_NO_ERROR)
			goto end;
	}

	/* set/replace ACL entries from file */
	if (a->acl_file_buf) {
		e = merge_acl_buf(&acl_acc, &acl_def, a->acl_file_buf,
		    &modified_acc, &give_mask_acc,
		    &modified_def, &give_mask_def,
		    a->is_test, path, "--- file input ---");
		if (e != GFARM_ERR_NO_ERROR)
			goto end;
	}
	/* set/replace ACL entries from command line */
	if (a->acl_spec_buf) {
		e = merge_acl_buf(&acl_acc, &acl_def, a->acl_spec_buf,
		    &modified_acc, &give_mask_acc,
		    &modified_def, &give_mask_def,
		    a->is_test, path, "--- command line input ---");
		if (e != GFARM_ERR_NO_ERROR)
			goto end;
	}

	/* add missing ACL entries for default ACL */
	if (acl_def != NULL && gfs_acl_entries(acl_def) > 0) {
		e = fill_missing_acl_default(&acl_def, acl_acc);
		if (e != GFARM_ERR_NO_ERROR)
			goto end;
	}

	/* recalc_mask: need to recalcurate mask entry ? */
	/*  [0] default : set a mask entry if the entry does not exist */
	/*  [1] -r : force to recalculate a mask entry */
	/* [-1] -n : do not recalculate a mask entry */
	if (modified_acc) {
		e = gfs_acl_equiv_mode(acl_acc, NULL, &is_not_equiv);
		if (e != GFARM_ERR_NO_ERROR)
			goto end;
		if (is_not_equiv) {
			if (!give_mask_acc && !have_acl_mask(acl_acc)) {
				e = copy_permset(
					&acl_acc, GFARM_ACL_MASK, NULL,
					acl_acc, GFARM_ACL_GROUP_OBJ, NULL, 0);
				if (e != GFARM_ERR_NO_ERROR)
					goto end;
			}
			if (a->recalc_mask == 1 ||
			    (a->recalc_mask == 0 && !give_mask_acc)) {
				e = gfs_acl_calc_mask(&acl_acc);
				if (e != GFARM_ERR_NO_ERROR)
					goto end;
			}
		}
	}
	if (modified_def && acl_def != NULL && gfs_acl_entries(acl_def) > 0) {
		e = gfs_acl_equiv_mode(acl_def, NULL, &is_not_equiv);
		if (e != GFARM_ERR_NO_ERROR)
			goto end;
		if (is_not_equiv) {
			if (!give_mask_def && !have_acl_mask(acl_def)) {
				e = copy_permset(
					&acl_def, GFARM_ACL_MASK, NULL,
					acl_def, GFARM_ACL_GROUP_OBJ, NULL, 0);
				if (e != GFARM_ERR_NO_ERROR)
					goto end;
			}
			if (a->recalc_mask == 1 ||
			    (a->recalc_mask == 0 && !give_mask_def)) {
				e = gfs_acl_calc_mask(&acl_def);
				if (e != GFARM_ERR_NO_ERROR)
					goto end;
			}
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
	if (a->is_test) {
		print_acl("--- test output ---", path, acl_acc, acl_def);
		goto end;
	}

	/* update Gfarm filesystem */
	if (acl_acc != NULL) {
		e = gfs_acl_set_file(path, GFARM_ACL_TYPE_ACCESS, acl_acc);
		if (e != GFARM_ERR_NO_ERROR)
			goto end;
	}
	if (acl_def != NULL) {
		if (GFARM_S_ISDIR(st->st_mode)) {
			if (gfs_acl_entries(acl_def) > 0)
				e = gfs_acl_set_file(path,
						     GFARM_ACL_TYPE_DEFAULT,
						     acl_def);
			else
				e = gfs_acl_delete_def_file(path);
		} else if (gfs_acl_entries(acl_def) > 0 && !a->recursive) {
			fprintf(stderr,
				"%s: Only directory can have default ACL\n",
				program_name);
			e = GFARM_ERR_INVALID_ARGUMENT;
		}
	}
	gfs_stat_cache_purge(path);
end:
	gfs_acl_free(acl_acc);
	gfs_acl_free(acl_def);
	gfs_acl_free(acl_acc_orig);
	gfs_acl_free(acl_def_orig);

	if (e != GFARM_ERR_NO_ERROR)
		fprintf(stderr, "%s: %s: %s\n",
		    program_name, path, gfarm_error_string(e));
	return (e);
}

int
main(int argc, char **argv)
{
	gfarm_error_t e, e_save = GFARM_ERR_NO_ERROR;
	int c, i, n;
	const char *acl_file = NULL;
	char *si = NULL, *s;
	struct gfsetfacl_arg arg;
	gfarm_stringlist paths;
	gfs_glob_t types;
	struct gfs_stat st;

	if (argc > 0)
		program_name = basename(argv[0]);
	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(1);
	}

	arg.remove_acl_acccess = 0;
	arg.remove_acl_default = 0;
	arg.acl_file_buf = NULL;
	arg.acl_spec_buf = NULL;
	arg.is_test = 0;
	arg.recalc_mask = 0;
	arg.recursive = 0;
	while ((c = getopt(argc, argv, "bkm:M:nrthR?")) != -1) {
		switch (c) {
		case 'b':
			arg.remove_acl_acccess = 1;
			arg.remove_acl_default = 1;
			break;
		case 'k':
			arg.remove_acl_default = 1;
			break;
		case 'm':
			arg.acl_spec_buf = optarg;
			break;
		case 'M':
			/* filename */
			acl_file = optarg;
			break;
		case 'n': /* no mask */
			arg.recalc_mask = -1;
			break;
		case 'r': /* force mask */
			arg.recalc_mask = 1;
			break;
		case 't':
			arg.is_test = 1;
			break;
		case 'R':
			arg.recursive = 1;
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
		GFARM_MALLOC_ARRAY(arg.acl_file_buf, BUFSIZE);
		if (arg.acl_file_buf == NULL) {
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
				GFARM_REALLOC_ARRAY(tmp, arg.acl_file_buf,
						    bufsize);
				if (tmp == NULL) {
					fprintf(stderr, "%s: no memory",
						program_name);
					free(arg.acl_file_buf);
					if (f != stdin)
						fclose(f);
					goto terminate;
				}
				arg.acl_file_buf = tmp;
			}
			reqsize = bufsize - pos;
			retsize = fread(arg.acl_file_buf + pos, 1, reqsize, f);
			if (retsize < reqsize) {
				if (ferror(f)) {
					fprintf(stderr,
						"%s: fread(%s) failed: %s",
						program_name, acl_file,
						strerror(errno));
					free(arg.acl_file_buf);
					if (f != stdin)
						fclose(f);
					goto terminate;
				}
				arg.acl_file_buf[pos + retsize] = '\0';
				break;
			}
			pos += retsize;
		}
		if (f != stdin)
			fclose(f);
	}

	gfarm_xattr_caching_pattern_add(GFARM_ACL_EA_ACCESS);
	gfarm_xattr_caching_pattern_add(GFARM_ACL_EA_DEFAULT);

	if ((e = gfarm_stringlist_init(&paths)) != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}
	if ((e = gfs_glob_init(&types)) != GFARM_ERR_NO_ERROR) {
		gfarm_stringlist_free_deeply(&paths);
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}
	for (i = 0; i < argc; i++)
		gfs_glob(argv[i], &paths, &types);

	n = gfarm_stringlist_length(&paths);
	for (i = 0; i < n; i++) {
		s = gfarm_stringlist_elem(&paths, i);
		e = gfarm_realpath_by_gfarm2fs(s, &si);
		if (e == GFARM_ERR_NO_ERROR)
			s = si;
		if ((e = gfs_lstat(s, &st)) != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "%s: %s\n", s, gfarm_error_string(e));
		} else {
			if (GFARM_S_ISDIR(st.st_mode) && arg.recursive) {
				e = gfarm_foreach_directory_hierarchy(
				    acl_set, acl_set, NULL, s, &arg);
				gfs_stat_free(&st);
			} else if (GFARM_S_ISLNK(st.st_mode)) {
				gfs_stat_free(&st);
				/* follow symlinks */
				e = gfs_stat(s, &st);
				if (e == GFARM_ERR_NO_ERROR) {
					e = acl_set(s, &st, &arg);
					gfs_stat_free(&st);
				} else {
					fprintf(stderr, "%s: %s\n",
					    s, gfarm_error_string(e));
				}
			} else {
				e = acl_set(s, &st, &arg);
				gfs_stat_free(&st);
			}
		}
		if (e_save == GFARM_ERR_NO_ERROR)
			e_save = e;
		free(si);
	}
	gfs_glob_free(&types);
	gfarm_stringlist_free_deeply(&paths);

terminate:
	free(arg.acl_file_buf);
	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}
	return (e_save == GFARM_ERR_NO_ERROR ? EXIT_SUCCESS : EXIT_FAILURE);
}
