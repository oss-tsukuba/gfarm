/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <gfarm/gfarm.h>

struct gfarm_acl_entry {
	gfarm_acl_tag_t tag;
	gfarm_acl_perm_t perm;
	char *qualifier;
};

struct gfarm_acl {
	struct gfarm_acl_entry **entries;
	int nentries;
	int current_idx;  /* for gfs_acl_get_entry() */
	int unused_idx;
};

/* ----- POSIX(1003.1e draft17)-like functions --------------------- */

gfarm_error_t
gfs_acl_init(int count, gfarm_acl_t *acl_p)
{
	gfarm_acl_t acl;

	GFARM_MALLOC(acl);
	if (acl == NULL) {
		*acl_p = NULL;
		gflog_debug(GFARM_MSG_1002672,
			    "allocation of gfarm_acl_t failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	if (count > 0) {
		GFARM_CALLOC_ARRAY(acl->entries, count);
		if (acl->entries == NULL) {
			free(acl);
			gflog_debug(GFARM_MSG_1002673,
				    "allocation of gfarm_acl_t failed");
			return (GFARM_ERR_NO_MEMORY);
		}
	} else
		acl->entries = NULL;
	acl->nentries = count;
	acl->current_idx = 0;
	acl->unused_idx = 0;
	*acl_p = acl;

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_acl_dup(gfarm_acl_t *acl_dst_p, gfarm_acl_t acl_src)
{
	gfarm_error_t e;
	int i;

	if (acl_dst_p == NULL || acl_src == NULL) {
		gflog_debug(GFARM_MSG_1002674,
			    "invalid argument of gfs_acl_dup()");
		return (GFARM_ERR_INVALID_ARGUMENT);
	}
	e = gfs_acl_init(acl_src->nentries, acl_dst_p);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	for (i = 0; i < acl_src->nentries; i++) {
		if (acl_src->entries[i] == NULL) {
			(*acl_dst_p)->entries[i] = NULL;
			continue;
		}
		GFARM_CALLOC_ARRAY((*acl_dst_p)->entries[i], 1);
		if ((*acl_dst_p)->entries[i] == NULL) {
			gflog_debug(GFARM_MSG_1002675,
				    "allocation of gfarm_acl_entry failed");
			gfs_acl_free(*acl_dst_p);
			*acl_dst_p = NULL;
			return (GFARM_ERR_NO_MEMORY);
		}
		(*acl_dst_p)->entries[i]->tag = acl_src->entries[i]->tag;
		(*acl_dst_p)->entries[i]->perm = acl_src->entries[i]->perm;
		if (acl_src->entries[i]->qualifier) {
			(*acl_dst_p)->entries[i]->qualifier
				= strdup(acl_src->entries[i]->qualifier);
			if ((*acl_dst_p)->entries[i]->qualifier == NULL) {
				gflog_debug(GFARM_MSG_1002676,
					"allocation of acl qualifier failed");
				gfs_acl_free(*acl_dst_p);
				*acl_dst_p = NULL;
				return (GFARM_ERR_NO_MEMORY);
			}
		} else
			(*acl_dst_p)->entries[i]->qualifier = NULL;
	}
	(*acl_dst_p)->unused_idx = acl_src->unused_idx;

	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfs_acl_entry_free(struct gfarm_acl_entry *entry_p)
{
	if (entry_p) {
		if (entry_p->qualifier)
			free(entry_p->qualifier);
		free(entry_p);
	}

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_acl_free(gfarm_acl_t acl)
{
	int i;

	if (acl == NULL)
		return (GFARM_ERR_NO_ERROR);
	for (i = 0; i < acl->nentries; i++)
		gfs_acl_entry_free(acl->entries[i]);
	if (acl->nentries > 0)
		free(acl->entries);
	free(acl);

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_acl_create_entry(gfarm_acl_t *acl_p, gfarm_acl_entry_t *entry_p)
{
	gfarm_acl_t acl;
	int i, new_count;
	struct gfarm_acl_entry **new_entries;
	struct gfarm_acl_entry **pp;

	if (acl_p == NULL || entry_p == NULL) {
		if (entry_p != NULL)
			*entry_p = NULL;
		gflog_debug(GFARM_MSG_1002677,
			    "invalid argument of gfs_acl_create_entry()");
		return (GFARM_ERR_INVALID_ARGUMENT);
	}

	acl = *acl_p;
	if (acl->nentries == 0 || acl->entries == NULL) {
		gfs_acl_free(acl);
		gfs_acl_init(5, &acl);
		*acl_p = acl;
	}
	for (i = acl->unused_idx; i < acl->nentries; i++) {
		if (acl->entries[i] == NULL) {
			pp = &acl->entries[i]; /* unused */
			acl->unused_idx++;    /* next */
			goto success;
		}
	}
	/* realloc */
	new_count = acl->nentries * 2;
	GFARM_REALLOC_ARRAY(new_entries, acl->entries, new_count);
	if (new_entries == NULL) {
		gflog_debug(GFARM_MSG_1002678,
			    "reallocation of gfarm_acl_entry array failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	acl->entries = new_entries;
	for (i = acl->nentries; i < new_count; i++)
		acl->entries[i] = NULL;
	pp = &acl->entries[acl->nentries]; /* new */
	acl->unused_idx = acl->nentries + 1; /* next */
	acl->nentries = new_count;
success:
	GFARM_CALLOC_ARRAY(*pp, 1);
	if (*pp == NULL) {
		gflog_debug(GFARM_MSG_1002679,
			    "allocation of gfarm_acl_entry failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	*entry_p = *pp;

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_acl_delete_entry(gfarm_acl_t acl, gfarm_acl_entry_t entry_d)
{
	int i;
	if (acl == NULL || entry_d == NULL) {
		gflog_debug(GFARM_MSG_1002680,
			    "invalid argument of gfs_acl_delete_entry()");
		return (GFARM_ERR_INVALID_ARGUMENT);
	}
	for (i = 0; i < acl->nentries; i++) {
		if (acl->entries[i] == entry_d) {
			gfs_acl_entry_free(entry_d);
			acl->entries[i] = NULL;
			acl->unused_idx = i;
			return (GFARM_ERR_NO_ERROR);
		}
	}

	gflog_debug(GFARM_MSG_1002681,
		    "This gfarm_acl_t do not include the gfarm_acl_entry_t");
	return (GFARM_ERR_INVALID_ARGUMENT);
}

gfarm_error_t
gfs_acl_get_entry(gfarm_acl_t acl, int entry_id, gfarm_acl_entry_t *entry_p)
{
	if (acl == NULL || entry_p == NULL) {
		gflog_debug(GFARM_MSG_1002682,
			    "invalid argument of gfs_acl_get_entry()");
		return (GFARM_ERR_INVALID_ARGUMENT);
	}
	if (entry_id == GFARM_ACL_FIRST_ENTRY)
		acl->current_idx = 0;
	else if (entry_id != GFARM_ACL_NEXT_ENTRY) {
		gflog_debug(GFARM_MSG_1002683,
			    "invalid entry_id of gfs_acl_get_entry()");
		return (GFARM_ERR_INVALID_ARGUMENT);
	}

	do {
		if (acl->current_idx >= acl->nentries) {
			*entry_p = NULL;
			return (GFARM_ERR_NO_SUCH_OBJECT);
		}
		*entry_p = acl->entries[acl->current_idx];
		acl->current_idx++;
	} while (*entry_p == NULL);

	/* *entry_p != NULL : found */
	return (GFARM_ERR_NO_ERROR);
}

#if 0
gfarm_error_t
gfs_acl_copy_entry(gfarm_acl_entry_t dest_d, gfarm_acl_entry_t src_d)
{
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED); /* XXX */
}
#endif

gfarm_error_t
gfs_acl_valid(gfarm_acl_t acl)
{
	return (gfs_acl_check(acl, NULL, NULL));
}

gfarm_error_t
gfs_acl_calc_mask(gfarm_acl_t *acl_p)
{
	gfarm_error_t e;
	int i;
	gfarm_acl_perm_t perm = 0;
	gfarm_acl_entry_t mask_ent = NULL;

	for (i = 0; i < (*acl_p)->nentries; i++) {
		if ((*acl_p)->entries[i] != NULL) {
			switch ((*acl_p)->entries[i]->tag) {
			case GFARM_ACL_USER_OBJ:
			case GFARM_ACL_OTHER:
				break;
			case GFARM_ACL_MASK:
				mask_ent = (*acl_p)->entries[i];
				break;
			case GFARM_ACL_USER:
			case GFARM_ACL_GROUP_OBJ:
			case GFARM_ACL_GROUP:
				perm |= (*acl_p)->entries[i]->perm;
				break;
			default:
				gflog_debug(GFARM_MSG_1002684,
					    "invalid acl tag: %d",
					    (*acl_p)->entries[i]->tag);
				return (GFARM_ERR_INVALID_ARGUMENT);
			}
		}
	}
	if (mask_ent == NULL) {
		e = gfs_acl_create_entry(acl_p, &mask_ent);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1002685,
				    "gfs_acl_create_entry() failed: %s",
				    gfarm_error_string(e));
			return (e);
		}
		mask_ent->tag = GFARM_ACL_MASK;
	}
	mask_ent->perm = perm;

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_acl_get_permset(gfarm_acl_entry_t entry_d,
		      gfarm_acl_permset_t *permset_p)
{
	if (entry_d == NULL || permset_p == NULL) {
		gflog_debug(GFARM_MSG_1002686,
			    "invalid argument of gfs_acl_get_permset()");
		return (GFARM_ERR_INVALID_ARGUMENT);
	}
	*permset_p = &entry_d->perm;

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_acl_set_permset(gfarm_acl_entry_t entry_d, gfarm_acl_permset_t permset_d)
{
	entry_d->perm = *permset_d;

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_acl_add_perm(gfarm_acl_permset_t permset_d, gfarm_acl_perm_t perm)
{
	*permset_d |= perm;

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_acl_clear_perms(gfarm_acl_permset_t permset_d)
{
	*permset_d = 0;

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_acl_delete_perm(gfarm_acl_permset_t permset_d, gfarm_acl_perm_t perm)
{
	*permset_d &= ~perm;

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_acl_get_qualifier(gfarm_acl_entry_t entry_d, char **qualifier_p)
{
	*qualifier_p = entry_d->qualifier;

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_acl_set_qualifier(gfarm_acl_entry_t entry_d, const char *qualifier)
{
	if (entry_d->qualifier != NULL)
		free(entry_d->qualifier);
	entry_d->qualifier = (char *) qualifier;

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_acl_get_tag_type(gfarm_acl_entry_t entry_d, gfarm_acl_tag_t *tag_type_p)
{
	*tag_type_p = entry_d->tag;

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_acl_set_tag_type(gfarm_acl_entry_t entry_d, gfarm_acl_tag_t tag_type)
{
	entry_d->tag = tag_type;

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_acl_delete_def_file(const char *path)
{
	gfarm_error_t e;
	struct gfs_stat sb;

	/* follow symlinks because symlinks do not have ACL */
	e = gfs_stat(path, &sb);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002687,
			    "gfs_stat(%s) failed: %s",
			    path, gfarm_error_string(e));
		return (e);
	}

	if (!GFARM_S_ISDIR(sb.st_mode)) {
		gflog_debug(GFARM_MSG_1002688, "%s is not a directory", path);
		gfs_stat_free(&sb);
		return (GFARM_ERR_NOT_A_DIRECTORY);
	}
	gfs_stat_free(&sb);

	/* follow symlinks because symlinks do not have ACL */
	e = gfs_removexattr(path, GFARM_ACL_EA_DEFAULT);
	if (e == GFARM_ERR_NO_SUCH_OBJECT)
		e = GFARM_ERR_NO_ERROR;

	return (e);
}

/* GFARM_ERR_NO_SUCH_OBJECT : path does not exist or type does not exist */
static gfarm_error_t
acl_get_file_common(
	const char *path, gfarm_acl_type_t type, gfarm_acl_t *acl_p,
	gfarm_error_t (*gfs_getxattr_func)(
		const char *, const char *, void *, size_t *))
{
	gfarm_error_t e;
	size_t size = 0;
	const char *name;
	void *xattr;

	if (type == GFARM_ACL_TYPE_ACCESS)
		name = GFARM_ACL_EA_ACCESS;
	else if (type == GFARM_ACL_TYPE_DEFAULT)
		name = GFARM_ACL_EA_DEFAULT;
	else {
		gflog_debug(GFARM_MSG_1002689,
			    "invalid type of gfs_acl_get_file()");
		return (GFARM_ERR_INVALID_ARGUMENT);
	}

	/* get xattr size */
	/* follow symlinks because symlinks do not have ACL */
	e = gfs_getxattr_func(path, name, NULL, &size);
	if (e != GFARM_ERR_NO_ERROR) {
		if (e != GFARM_ERR_NO_SUCH_OBJECT)
			gflog_debug(GFARM_MSG_1002690,
				    "gfs_getxattr(%s, %s) failed: %s",
				    path, name, gfarm_error_string(e));
		return (e);
	}

	GFARM_MALLOC_ARRAY(xattr, size);
	if (xattr == NULL) {
		gflog_debug(GFARM_MSG_1002691,
			    "allocation of xattr value failed");
		return (GFARM_ERR_NO_MEMORY);
	}

	/* follow symlinks because symlinks do not have ACL */
	e = gfs_getxattr_func(path, name, xattr, &size);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1002692,
			    "gfs_getxattr(%s, %s) failed: %s",
			    path, name, gfarm_error_string(e));
	else {
		e = gfs_acl_from_xattr_value(xattr, size, acl_p);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_debug(GFARM_MSG_1002693,
				    "gfs_acl_from_xattr_value() failed: %s",
				    gfarm_error_string(e));
	}
	free(xattr);

	return (e);
}

gfarm_error_t
gfs_acl_get_file(const char *path, gfarm_acl_type_t type, gfarm_acl_t *acl_p)
{
	return (acl_get_file_common(path, type, acl_p, gfs_getxattr));
}

gfarm_error_t
gfs_acl_get_file_cached(const char *path, gfarm_acl_type_t type,
			gfarm_acl_t *acl_p)
{
	return (acl_get_file_common(path, type, acl_p, gfs_getxattr_cached));
}


#if 0
gfarm_error_t
gfs_acl_get_fh(GFS_File gf, gfarm_acl_type_t type, gfarm_acl_t *acl_p)
{
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED); /* XXX */
}
#endif

gfarm_error_t
gfs_acl_set_file(const char *path, gfarm_acl_type_t type, gfarm_acl_t acl)
{
	gfarm_error_t e;
	void *xattr;
	size_t size;
	const char *name;

	if (type == GFARM_ACL_TYPE_ACCESS)
		name = GFARM_ACL_EA_ACCESS;
	else if (type == GFARM_ACL_TYPE_DEFAULT)
		name = GFARM_ACL_EA_DEFAULT;
	else {
		gflog_debug(GFARM_MSG_1002694,
			    "invalid type of gfs_acl_set_file()");
		return (GFARM_ERR_INVALID_ARGUMENT);
	}

	e = gfs_acl_to_xattr_value(acl, &xattr, &size);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1002695,
			    "gfs_acl_to_xattr_value: %s",
			    gfarm_error_string(e));
		return (e);
	}
	/* follow symlinks because symlinks do not have ACL */
	e = gfs_setxattr(path, name, xattr, size, 0);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1002696,
			    "gfs_setxattr(%s, %s): %s",
			    path, name, gfarm_error_string(e));
	}
	free(xattr);

	return (e);
}

#if 0
gfarm_error_t
gfs_acl_set_fh(GFS_File gf, gfarm_acl_type_t type, gfarm_acl_t acl)
{
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED); /* XXX */
}
#endif

static char *
gfs_acl_tag_to_str(gfarm_acl_tag_t tag, int options)
{
	static char s_user[] = "u:";
	static char s_group[] = "g:";
	static char s_other[] = "o:";
	static char s_mask[] = "m:";
	static char l_user[] = "user:";
	static char l_group[] = "group:";
	static char l_other[] = "other:";
	static char l_mask[] = "mask:";
	int is_abb = options & GFARM_ACL_TEXT_ABBREVIATE;

	switch (tag) {
	case GFARM_ACL_USER_OBJ:
	case GFARM_ACL_USER:
		return (is_abb ? s_user : l_user);
	case GFARM_ACL_GROUP_OBJ:
	case GFARM_ACL_GROUP:
		return (is_abb ? s_group : l_group);
	case GFARM_ACL_MASK:
		return (is_abb ? s_mask : l_mask);
	case GFARM_ACL_OTHER:
		return (is_abb ? s_other : l_other);
	default:
		return (NULL);
	}
}

static char *
gfs_acl_perm_to_str(gfarm_acl_perm_t perm) {
	static char prwx[] = "rwx";
	static char prw_[] = "rw-";
	static char pr_x[] = "r-x";
	static char pr__[] = "r--";
	static char p_wx[] = "-wx";
	static char p_w_[] = "-w-";
	static char p__x[] = "--x";
	static char p___[] = "---";

	if (perm & GFARM_ACL_READ) {
		if (perm & GFARM_ACL_WRITE) {
			if (perm & GFARM_ACL_EXECUTE)
				return (prwx);
			else
				return (prw_);
		} else {
			if (perm & GFARM_ACL_EXECUTE)
				return (pr_x);
			else
				return (pr__);
		}
	} else {
		if (perm & GFARM_ACL_WRITE) {
			if (perm & GFARM_ACL_EXECUTE)
				return (p_wx);
			else
				return (p_w_);
		} else {
			if (perm & GFARM_ACL_EXECUTE)
				return (p__x);
			else
				return (p___);
		}
	}
}

#define ADD_STR(src) \
	do { \
		int slen = strlen(src); \
		while (nowlen + slen + 1 > bufsize) { \
			char *tmp; \
			bufsize *= 2; \
			GFARM_REALLOC_ARRAY(tmp, buf, bufsize); \
			if (tmp == NULL) { \
				free(buf); \
				gflog_debug(GFARM_MSG_1002697, \
				    "reallocation for acl_to_text failed"); \
				return (GFARM_ERR_NO_MEMORY); \
			} \
			buf = tmp; \
		} \
		strcpy(buf + nowlen, src);  /* with '\0' */ \
		nowlen += slen;  /* without '\0' */ \
	} while (0)

static gfarm_error_t
gfs_acl_to_text_common(gfarm_acl_t acl, const char *prefix, char separator,
		       const char *suffix, int options,
		       char **str_p, size_t *len_p)
{
	int i;
	gfarm_acl_perm_t mask_perm = 0;
	int mask_exists = 0;
	char *buf;
	int nowlen = 0, bufsize;
	static char colon[] = ":";
	static char effective_str[] = "#effective:";

	if (acl == NULL || str_p == NULL) {
		gflog_debug(GFARM_MSG_1002698,
			    "invalid argument of gfs_acl_to_text()");
		return (GFARM_ERR_INVALID_ARGUMENT);
	}
	if (options &
	    (GFARM_ACL_TEXT_SOME_EFFECTIVE | GFARM_ACL_TEXT_ALL_EFFECTIVE)) {
		/* get MASK previously */
		for (i = 0; i < acl->nentries; i++) {
			if (acl->entries[i] != NULL &&
			    (acl->entries[i]->tag & GFARM_ACL_MASK)) {
				mask_perm = acl->entries[i]->perm;
				mask_exists = 1;
			}
		}
	}

	bufsize = 64;
	GFARM_MALLOC_ARRAY(buf, bufsize);
	if (buf == NULL) {
		gflog_debug(GFARM_MSG_1002699,
			    "allocation for acl_to_text failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	buf[0] = '\0';
	for (i = 0; i < acl->nentries; i++) {
		char *s, sepstr[2];
		int save_pos = nowlen;
		if (acl->entries[i] == NULL)
			continue;
		if (i > 0) {
			snprintf(sepstr, 2, "%c", separator);
			ADD_STR(sepstr);
		}
		if (prefix)
			ADD_STR(prefix);
		s = gfs_acl_tag_to_str(acl->entries[i]->tag, options);
		if (s)
			ADD_STR(s);
		s = acl->entries[i]->qualifier;
		if (s)
			ADD_STR(s);
		ADD_STR(colon);
		ADD_STR(gfs_acl_perm_to_str(acl->entries[i]->perm));

		if (mask_exists &&
		    (options & (GFARM_ACL_TEXT_SOME_EFFECTIVE |
				GFARM_ACL_TEXT_ALL_EFFECTIVE)) &&
		    (acl->entries[i]->tag &
		     (GFARM_ACL_USER | GFARM_ACL_GROUP_OBJ | GFARM_ACL_GROUP))
			) {
			gfarm_acl_perm_t effective =
				mask_perm & acl->entries[i]->perm;
			if (effective != acl->entries[i]->perm ||
			    options & GFARM_ACL_TEXT_ALL_EFFECTIVE) {
				/* align effective comments to column 40 */
				int i = (options &
					 GFARM_ACL_TEXT_SMART_INDENT) ?
					(5 - ((nowlen - save_pos) / 8)) : 1;
				while (i > 0) {
					ADD_STR("\t");
					i--;
				}
				ADD_STR(effective_str);
				ADD_STR(gfs_acl_perm_to_str(effective));
			}
		}
	}
	if (suffix)
		ADD_STR(suffix);
	*str_p = buf;
	if (len_p)
		*len_p = nowlen; /* without '\0' */

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_acl_to_text(gfarm_acl_t acl, char **str_p, size_t *len_p)
{
	return (gfs_acl_to_text_common(acl, NULL, '\n', "\n",
				       GFARM_ACL_TEXT_SOME_EFFECTIVE,
				       str_p, len_p));
}

/* --- gfs_acl_from_text() --- */

/* skip whitespace */
#define SKIP_WS(c) \
	do { \
		while (isspace(*(unsigned char *)(c))) \
			(c)++; \
		if (*(c) == '#') { \
			while (*(c) != '\n' && *(c) != '\0') \
				(c)++; \
		} \
	} while (0)

static const char *
gfs_acl_skip_tag_name(const char *text, const char *token)
{
	size_t len = strlen(token);
	const char *p = text;

	SKIP_WS(p);
	if (strncmp(p, token, len) == 0) {
		p += len;
		goto delimiter;
	} else if (*p == *token) {
		p++;
		goto delimiter;
	}
	return (NULL);
delimiter:
	SKIP_WS(p);
	if (*p == ':')
		p++;
	return (p);
}

static gfarm_error_t
gfs_acl_next_qualifier(const char *text, char **qual_p,
		       const char **next_text_p)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	const char *p = text;

	*qual_p = NULL;
	SKIP_WS(p);
	while (*p != '\0' && *p != '\r' && *p != '\n' &&
	       *p != ':' && *p != ',')
		p++;
	if (p == text)
		goto next;
	GFARM_MALLOC_ARRAY(*qual_p, p - text + 1);
	if (*qual_p == NULL) {
		gflog_error(GFARM_MSG_1002700,
			    "allocation of acl qualifier failed");
		e = GFARM_ERR_NO_MEMORY;
		goto next;
	}
	memcpy(*qual_p, text, (p - text));
	(*qual_p)[p - text] = '\0';
next:
	if (*p == ':')
		p++;
	*next_text_p = p;

	return (e);
}

static gfarm_error_t
gfs_acl_parse_acl_entry(const char *text, gfarm_acl_t *acl_p,
			const char **next_text_p)
{
	gfarm_error_t e;
	gfarm_acl_entry_t entry_d;
	const char *p = text, *nextp;
	int i;
	gfarm_acl_tag_t tag = 0;
	gfarm_acl_perm_t perm = 0;
	char *qual = NULL;
	gfarm_acl_permset_t permset;

	if (*p == 'd') {
		p = gfs_acl_skip_tag_name(p, "default");
		if (p == NULL)
			goto fail;
	}
	SKIP_WS(p);
	switch (*p) {
	case '\0':
	case '\n':
	case '#':
		/* no entry data */
		*next_text_p = p;
		return (GFARM_ERR_NO_ERROR);
	case 'u':  /* user */
		p = gfs_acl_skip_tag_name(p, "user");
		if (p == NULL)
			goto fail;
		e = gfs_acl_next_qualifier(p, &qual, &nextp);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
		p = nextp;
		if (qual != NULL)
			tag = GFARM_ACL_USER;
		else  /* user:: */
			tag = GFARM_ACL_USER_OBJ;
		break;
	case 'g':  /* group */
		p = gfs_acl_skip_tag_name(p, "group");
		if (p == NULL)
			goto fail;
		e = gfs_acl_next_qualifier(p, &qual, &nextp);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
		p = nextp;
		if (qual != NULL)
			tag = GFARM_ACL_GROUP;
		else  /* group:: */
			tag = GFARM_ACL_GROUP_OBJ;
		break;
	case 'm':  /* mask */
		p = gfs_acl_skip_tag_name(p, "mask");
		if (p == NULL)
			goto fail;
		SKIP_WS(p); /* skip empty qualifier */
		if (*p == ':')
			p++;
		tag = GFARM_ACL_MASK;
		break;
	case 'o':  /* other */
		p = gfs_acl_skip_tag_name(p, "other");
		if (p == NULL)
			goto fail;
		SKIP_WS(p); /* skip empty qualifier */
		if (*p == ':')
			p++;
		tag = GFARM_ACL_OTHER;
		break;
	default:
		goto fail;
	}
	for (i = 0; i < 3; i++, p++) {
		switch (*p) {
		case 'r':
			if (perm & GFARM_ACL_READ)
				goto fail;
			perm |= GFARM_ACL_READ;
			break;
		case 'w':
			if (perm & GFARM_ACL_WRITE)
				goto fail;
			perm |= GFARM_ACL_WRITE;
			break;
		case 'x':
			if (perm & GFARM_ACL_EXECUTE)
				goto fail;
			perm |= GFARM_ACL_EXECUTE;
			break;
		case '-': /* ignore */
			break;
		default: /* next ACL entry ? */
			if (i == 0) /* no rwx */
				goto fail;
			goto end;
		}
	}
end:
	*next_text_p = p;
	e = gfs_acl_create_entry(acl_p, &entry_d);
	if (e != GFARM_ERR_NO_ERROR) {
		free(qual);
		gflog_debug(GFARM_MSG_1002701,
			    "gfs_acl_create_entry() failed: %s",
			    gfarm_error_string(e));
		return (e);
	}
	gfs_acl_get_permset(entry_d, &permset);
	gfs_acl_add_perm(permset, perm);
	gfs_acl_set_permset(entry_d, permset);
	gfs_acl_set_qualifier(entry_d, qual);
	gfs_acl_set_tag_type(entry_d, tag);

	return (GFARM_ERR_NO_ERROR);
fail:
	return (GFARM_ERR_INVALID_ARGUMENT);
}

static gfarm_error_t
gfs_acl_from_text_common(const char *buf_p, gfarm_acl_t *acl_acc_p,
			 gfarm_acl_t *acl_def_p)
{
	gfarm_error_t e;
	const char *p = buf_p, *nextp;

	if (p == NULL || acl_acc_p == NULL) {
		gflog_debug(GFARM_MSG_1002702,
			    "invalid argument of acl_from_text");
		return (GFARM_ERR_INVALID_ARGUMENT);
	}
	*acl_acc_p = NULL;
	e = gfs_acl_init(5, acl_acc_p);
	if (e == GFARM_ERR_NO_ERROR && acl_def_p != NULL) {
		*acl_def_p = NULL;
		e = gfs_acl_init(5, acl_def_p);
	}
	if (e != GFARM_ERR_NO_ERROR) {
		gfs_acl_free(*acl_acc_p);
		gflog_debug(GFARM_MSG_1002703, "gfs_acl_init() failed: %s",
			    gfarm_error_string(e));
		return (e);
	}

	while (*p != '\0') {
		SKIP_WS(p);
		if (*p == 'd') {
			if (acl_def_p != NULL)
				e = gfs_acl_parse_acl_entry(
					p, acl_def_p, &nextp);
			else
				e = GFARM_ERR_INVALID_ARGUMENT;
		} else
			e = gfs_acl_parse_acl_entry(p, acl_acc_p, &nextp);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1002704,
				    "parsing acl text error: %s",
				    gfarm_error_string(e));
			if (acl_def_p != NULL)
				gfs_acl_free(*acl_def_p);
			gfs_acl_free(*acl_acc_p);
			return (e);
		}
		p = nextp;
		SKIP_WS(p);
		if (*p == ',') {
			p++;
			SKIP_WS(p);
		}
	}
	/* end of p : *p == '\0' */

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_acl_from_text(const char *buf_p, gfarm_acl_t *acl_p)
{
	return (gfs_acl_from_text_common(buf_p, acl_p, NULL));
}

/* ----- Linux-ACL-Extension-like functions ---------------------- */

gfarm_error_t
gfs_acl_get_perm(gfarm_acl_permset_t permset_d, gfarm_acl_perm_t perm,
		 int *bool_p)
{
	if (bool_p == NULL || permset_d == NULL ||
	    (perm & !(GFARM_ACL_READ | GFARM_ACL_WRITE | GFARM_ACL_EXECUTE))) {
		gflog_debug(GFARM_MSG_1002705,
			    "invalid argument of gfs_acl_get_perm()");
		return (GFARM_ERR_INVALID_ARGUMENT);
	}
	*bool_p = ((*permset_d & perm) != 0);

	return (GFARM_ERR_NO_ERROR);
}

const char *
gfs_acl_error(int acl_check_err)
{
	static char multi[] = "Multiple entries of same type";
	static char duplicate[] = "Duplicate entries";
	static char miss[] = "Missing or wrong entry";
	static char entry[] = "Invalid entry type";
	static char noerror[] = "No error";

	switch (acl_check_err) {
	case GFARM_ACL_MULTI_ERROR:
		return (multi);
	case GFARM_ACL_DUPLICATE_ERROR:
		return (duplicate);
	case GFARM_ACL_MISS_ERROR:
		return (miss);
	case GFARM_ACL_ENTRY_ERROR:
		return (entry);
	}
	return (noerror);
}

/* check required entries and sorted entries */
gfarm_error_t
gfs_acl_check(gfarm_acl_t acl, int *last_p, int *acl_check_err_p)
{
	int state = GFARM_ACL_USER_OBJ;
	int need_mask = 0;
	char *tmp_qual = NULL;
	int i;
	int acl_e;

	if (acl == NULL) {
		gflog_debug(GFARM_MSG_1002706,
			    "invalid argument of gfs_acl_check()");
		if (acl_check_err_p != NULL)
			*acl_check_err_p = GFARM_ACL_NO_ERROR;
		return (GFARM_ERR_INVALID_ARGUMENT);
	}
	if (last_p != NULL)
		*last_p = 0;
	for (i = 0; i < acl->nentries; i++) {
		char *q;
		if (acl->entries[i] == NULL)
			continue;
		q = acl->entries[i]->qualifier;
		switch (acl->entries[i]->tag) {
		case GFARM_ACL_USER_OBJ:
			if (state == GFARM_ACL_USER_OBJ) {
				state = GFARM_ACL_USER;
				tmp_qual = NULL;
				break;
			}
			acl_e = GFARM_ACL_MULTI_ERROR;
			goto fail;
		case GFARM_ACL_USER:
			if (state != GFARM_ACL_USER) {
				acl_e = GFARM_ACL_MISS_ERROR;
				goto fail;
			}
			if (q == NULL || strlen(q) == 0) {
				acl_e = GFARM_ACL_MISS_ERROR;
				goto fail;
			} else if (tmp_qual != NULL &&
				   strcmp(q, tmp_qual) <= 0) {
				acl_e = GFARM_ACL_DUPLICATE_ERROR;
				goto fail;
			}
			tmp_qual = q;
			need_mask = 1;
			break;
		case GFARM_ACL_GROUP_OBJ:
			if (state == GFARM_ACL_USER) {
				state = GFARM_ACL_GROUP;
				tmp_qual = NULL;
				break;
			}
			if (state >= GFARM_ACL_GROUP)
				acl_e = GFARM_ACL_MULTI_ERROR;
			else
				acl_e = GFARM_ACL_MISS_ERROR;
			goto fail;
		case GFARM_ACL_GROUP:
			if (state != GFARM_ACL_GROUP) {
				acl_e = GFARM_ACL_MISS_ERROR;
				goto fail;
			}
			if (q == NULL || strlen(q) == 0) {
				acl_e = GFARM_ACL_MISS_ERROR;
				goto fail;
			} else if (tmp_qual != NULL &&
			     strcmp(q, tmp_qual) <= 0) {
				acl_e = GFARM_ACL_DUPLICATE_ERROR;
				goto fail;
			}
			tmp_qual = q;
			need_mask = 1;
			break;
		case GFARM_ACL_MASK:
			if (state == GFARM_ACL_GROUP) {
				state = GFARM_ACL_OTHER;
				break;
			}
			if (state >= GFARM_ACL_OTHER)
				acl_e = GFARM_ACL_MULTI_ERROR;
			else
				acl_e = GFARM_ACL_MISS_ERROR;
			goto fail;
		case GFARM_ACL_OTHER:
			if (state == GFARM_ACL_OTHER ||
			    (state == GFARM_ACL_GROUP &&
			     !need_mask)) {
				state = 0;
				break;
			}
			acl_e = GFARM_ACL_MISS_ERROR;
			goto fail;
		default:
			acl_e = GFARM_ACL_ENTRY_ERROR;
			goto fail;
		}
		if (last_p != NULL)
			(*last_p)++;
	}
	if (state != 0) {
		acl_e = GFARM_ACL_MISS_ERROR;
		goto fail;
	}
	if (acl_check_err_p != NULL)
		*acl_check_err_p = GFARM_ACL_NO_ERROR;
	return (GFARM_ERR_NO_ERROR);
fail:
	gflog_debug(GFARM_MSG_1002707, "error in gfs_acl_check(): %s",
		    gfs_acl_error(acl_e));
	if (acl_check_err_p != NULL)
		*acl_check_err_p = acl_e;
	return (GFARM_ERR_INVALID_ARGUMENT);
}

int
gfs_acl_entries(gfarm_acl_t acl)
{
	int i, count = 0;

	if (acl == NULL)
		return (0);

	for (i = 0; i < acl->nentries; i++) {
		if (acl->entries[i] != NULL)
			count++;
	}

	return (count);
}

gfarm_error_t
gfs_acl_equiv_mode(gfarm_acl_t acl, gfarm_mode_t *mode_p, int *is_not_equiv_p)
{
	int i;
	gfarm_mode_t mode = 0, mask = 0;
	int not_equivalent = 0, mask_found = 0;

	if (acl == NULL) {
		gflog_debug(GFARM_MSG_1002708,
			    "invalid argument of gfs_acl_equiv_mode()");
		return (GFARM_ERR_INVALID_ARGUMENT);
	}
	for (i = 0; i < acl->nentries; i++) {
		if (acl->entries[i] == NULL)
			continue;
		switch (acl->entries[i]->tag) {
		case GFARM_ACL_USER_OBJ:
			mode |= (acl->entries[i]->perm &
			 (GFARM_ACL_READ|GFARM_ACL_WRITE|GFARM_ACL_EXECUTE))
				<< 6;
			break;
		case GFARM_ACL_GROUP_OBJ:
			mode |= (acl->entries[i]->perm &
			 (GFARM_ACL_READ|GFARM_ACL_WRITE|GFARM_ACL_EXECUTE))
				<< 3;
			break;
		case GFARM_ACL_OTHER:
			mode |= (acl->entries[i]->perm &
			(GFARM_ACL_READ|GFARM_ACL_WRITE|GFARM_ACL_EXECUTE));
			break;
		case GFARM_ACL_MASK:
			mask |= (acl->entries[i]->perm &
			(GFARM_ACL_READ|GFARM_ACL_WRITE|GFARM_ACL_EXECUTE));
			mask_found = 1;
			/* not break */
		case GFARM_ACL_USER:
		case GFARM_ACL_GROUP:
			not_equivalent = 1;
			break;
		default:
			return (GFARM_ERR_INVALID_ARGUMENT);
		}
	}
	if (mode_p != NULL) {
		/* mask GFARM_ACL_GROUP_OBJ */
		if (mode != 0 && mask_found != 0)
			mode = (mode & ~00070) |
				((mode & 00070) & ((mask & 00007) << 3));
		*mode_p = mode;
	}

	if (is_not_equiv_p != NULL)
		*is_not_equiv_p = not_equivalent;

	return (GFARM_ERR_NO_ERROR);
}

/* must use after gfs_acl_sort() */
int
gfs_acl_cmp(gfarm_acl_t acl1, gfarm_acl_t acl2)
{
	int min_nentries, max_nentries, i;
	struct gfarm_acl_entry **min_entries, **max_entries;

	if (acl1 == NULL || acl2 == NULL) {
		gflog_debug(GFARM_MSG_1002709,
			    "invalid argument of gfs_acl_cmp()");
		return (GFARM_ERR_INVALID_ARGUMENT);
	}

	if (acl1->nentries > acl2->nentries) {
		min_nentries = acl2->nentries;
		min_entries = acl2->entries;
		max_nentries = acl1->nentries;
		max_entries = acl1->entries;
	} else {
		min_nentries = acl1->nentries;
		min_entries = acl1->entries;
		max_nentries = acl2->nentries;
		max_entries = acl2->entries;
	}

	for (i = 0; i < max_nentries; i++) {
		if (i >= min_nentries) {
			if (max_entries[i] != NULL)
				return (1); /* not equal */
			continue;
		}
		/* i < min_nentries */
		if (max_entries[i] == NULL) {
			if (min_entries[i] != NULL)
				return (1); /* not equal */
			continue;
		}
		if (min_entries[i] == NULL)
			return (1); /* not equal */
		if (max_entries[i]->tag != min_entries[i]->tag)
			return (1); /* not equal */
		if (max_entries[i]->perm != min_entries[i]->perm)
			return (1); /* not equal */
		if (max_entries[i]->tag == GFARM_ACL_USER ||
		    max_entries[i]->tag == GFARM_ACL_GROUP) {
			if (max_entries[i]->qualifier == NULL ||
			    min_entries[i]->qualifier == NULL)
				return (1); /* not equal (unexpected) */
			else if (strcmp(max_entries[i]->qualifier,
					min_entries[i]->qualifier) != 0)
				return (1); /* not equal */
		}
		/* continue */
	}
	return (0); /* equal */
}

#if 0
gfarm_error_t
gfs_acl_extended_fh(GFS_file gf)
{
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED); /* XXX */
}

gfarm_error_t
gfs_acl_extended_file(const char *path)
{
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED); /* XXX */
}
#endif

gfarm_error_t
gfs_acl_from_mode(gfarm_mode_t mode, gfarm_acl_t *acl_p)
{
	char text[] = "u::---,g::---,o::---";

	if (mode & 00400)
		text[3] = 'r';
	if (mode & 00200)
		text[4] = 'w';
	if (mode & 00100)
		text[5] = 'x';
	if (mode & 00040)
		text[10] = 'r';
	if (mode & 00020)
		text[11] = 'w';
	if (mode & 00010)
		text[12] = 'x';
	if (mode & 00004)
		text[17] = 'r';
	if (mode & 00002)
		text[18] = 'w';
	if (mode & 00001)
		text[19] = 'x';

	return (gfs_acl_from_text(text, acl_p));
}

gfarm_error_t
gfs_acl_to_any_text(gfarm_acl_t acl, const char *prefix,
		    char separator, int options, char **str_p)
{
	return (gfs_acl_to_text_common(acl, prefix, separator, NULL, options,
				       str_p, NULL));
}

/* ----- non-POSIX-like functions (utilities) ------------------------ */

#define ADD_BUF(src, size) \
	do { \
		while (nowlen + size > bufsize) { \
			char *tmp; \
			bufsize *= 2; \
			GFARM_REALLOC_ARRAY(tmp, buf, bufsize); \
			if (tmp == NULL) { \
				free(buf); \
				gflog_debug(GFARM_MSG_1002710, \
				    "reallocation for acl_to_xattr failed"); \
				return (GFARM_ERR_NO_MEMORY); \
			} \
			buf = tmp; \
		} \
		memcpy(buf + nowlen, src, size); \
		nowlen += size; \
	} while (0)

gfarm_error_t
gfs_acl_to_xattr_value(gfarm_acl_t acl, void **xattr_value_p, size_t *size_p)
{
	int i;
	char *buf;
	int nowlen = 0, bufsize;
	gfarm_uint32_t version;
	gfarm_uint16_t tag, perm;

	if (acl == NULL || xattr_value_p == NULL || size_p == NULL) {
		gflog_debug(GFARM_MSG_1002711,
			    "invalid argument of gfs_acl_to_xattr_value()");
		return (GFARM_ERR_INVALID_ARGUMENT);
	}

	/* assume that all qualifiers are 15 characters */
	bufsize = 4 + acl->nentries * (4 + 16);
	GFARM_MALLOC_ARRAY(buf, bufsize);
	if (buf == NULL) {
		gflog_debug(GFARM_MSG_1002712,
			    "allocation of for acl_to_xattr failed");
		return (GFARM_ERR_NO_MEMORY);
	}

	version = gfarm_htol_32(GFARM_ACL_EA_VERSION);
	ADD_BUF(&version, sizeof(version));
	for (i = 0; i < acl->nentries; i++) {
		static const char zerostr[] = "";
		const char *s;
		if (acl->entries[i] == NULL)
			continue;
		tag = gfarm_htol_16(
			(gfarm_uint16_t)(acl->entries[i]->tag));
		ADD_BUF(&tag, sizeof(tag));
		perm = gfarm_htol_16(
			(gfarm_uint16_t)(acl->entries[i]->perm));
		ADD_BUF(&perm, sizeof(perm));
		s = acl->entries[i]->qualifier;
		if (s == NULL)
			s = zerostr;
		ADD_BUF(s, strlen(s) + 1);  /* with '\0' */
	}
	*xattr_value_p = buf;
	*size_p = nowlen;

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_acl_from_xattr_value(const void *xattr_value, size_t size,
			   gfarm_acl_t *acl_p)
{
	gfarm_error_t e;
	gfarm_acl_t acl;
	gfarm_acl_entry_t ent;
	gfarm_acl_permset_t pset;
	const void *p = xattr_value;
	const void *endp = p + size;
	gfarm_uint32_t version;
	gfarm_uint16_t tag, perm;
	char *qual;
	size_t len;

	memcpy(&version, p, sizeof(version));
	p += sizeof(version);
	if (p > endp) {
		gflog_debug(GFARM_MSG_1002713,
			    "invalid xattr_value size");
		return (GFARM_ERR_INVALID_ARGUMENT);
	}
	if (gfarm_ltoh_32(version) != GFARM_ACL_EA_VERSION) {
		gflog_debug(GFARM_MSG_1002714,
			    "unsupported acl version: %d",
			    gfarm_ltoh_32(version));
		return (GFARM_ERR_INVALID_ARGUMENT);
	}
	e = gfs_acl_init(5, &acl);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002715,
			    "gfs_acl_init() failed: %s",
			    gfarm_error_string(e));
		return (e);
	}
	while (p < endp) {
		memcpy(&tag, p, sizeof(tag));
		p += sizeof(tag);
		memcpy(&perm, p, sizeof(perm));
		p += sizeof(perm);
		len = strlen(p) + 1;  /* with '\0' */
		if (p + len > endp) {
			gfs_acl_free(acl);
			gflog_debug(GFARM_MSG_1002716,
				    "invalid xattr_value size");
			return (GFARM_ERR_INVALID_ARGUMENT);
		}
		qual = strdup(p);
		if (qual == NULL) {
			gfs_acl_free(acl);
			gflog_debug(GFARM_MSG_1002717,
				    "strdup() failed");
			return (GFARM_ERR_NO_MEMORY);
		}
		p += len;
		e = gfs_acl_create_entry(&acl, &ent);
		if (e != GFARM_ERR_NO_ERROR) {
			free(qual);
			gfs_acl_free(acl);
			gflog_debug(GFARM_MSG_1002718,
				    "gfs_acl_create_entry() failed: %s",
				    gfarm_error_string(e));
			return (e);
		}
		gfs_acl_set_tag_type(ent, gfarm_ltoh_16(tag));
		gfs_acl_get_permset(ent, &pset);
		gfs_acl_add_perm(pset, gfarm_ltoh_16(perm));
		gfs_acl_set_permset(ent, pset);
		gfs_acl_set_qualifier(ent, qual);
	}
	*acl_p = acl;

	return (GFARM_ERR_NO_ERROR);
}

static int
gfs_acl_compare_entry(const void *p1, const void *p2)
{
	struct gfarm_acl_entry *e1 = *(struct gfarm_acl_entry **) p1;
	struct gfarm_acl_entry *e2 = *(struct gfarm_acl_entry **) p2;

	/* entry1, entry2, ..., NULL, NULL */
	if (e1 == NULL) {
		if (e2 == NULL)
			return (0);
		else
			return (1);  /* NULL pointer is end */
	}
	/* e1 != NULL */
	if (e2 == NULL)
		return (-1);  /* NULL pointer is end */
	if (e1->tag > e2->tag)
		return (1);
	if (e1->tag < e2->tag)
		return (-1);
	if (e1->qualifier == NULL) {
		if (e2->qualifier == NULL)
			return (0);
		else
			return (-1);
	}
	/* e1->qualifier != NULL */
	if (e2->qualifier == NULL)
		return (1);

	return (strcmp(e1->qualifier, e2->qualifier));
}

gfarm_error_t
gfs_acl_sort(gfarm_acl_t acl)
{
	if (acl == NULL)
		return (GFARM_ERR_NO_ERROR);

	qsort(acl->entries, acl->nentries, sizeof(struct gfs_acl_entry *),
	      gfs_acl_compare_entry);

	return (GFARM_ERR_NO_ERROR);
}

/* remove standard ACL entries */
gfarm_error_t
gfs_acl_delete_mode(gfarm_acl_t acl)
{
	int i;

	if (acl == NULL) {
		gflog_debug(GFARM_MSG_1002719,
			    "invalid argument of gfs_acl_delete_mode()");
		return (GFARM_ERR_INVALID_ARGUMENT);
	}
	for (i = 0; i < acl->nentries; i++) {
		if (acl->entries[i] != NULL) {
			switch (acl->entries[i]->tag) {
			case GFARM_ACL_USER_OBJ:
			case GFARM_ACL_GROUP_OBJ:
			case GFARM_ACL_OTHER:
				gfs_acl_entry_free(acl->entries[i]);
				acl->entries[i] = NULL;
			}
		}
	}

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_acl_from_text_with_default(const char *buf_p, gfarm_acl_t *acl_acc_p,
			       gfarm_acl_t *acl_def_p)
{
	return (gfs_acl_from_text_common(buf_p, acl_acc_p, acl_def_p));
}
