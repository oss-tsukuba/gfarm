/*
 * $Id$
 */

#include <sys/types.h>
#include <string.h>
#include <stdlib.h>

#define GFARM_INTERNAL_USE
#include <gfarm/gfarm.h>

#include "inode.h"
#include "user.h"
#include "group.h"

#define REPLACE_MODE_T 1
#define CHECK_VALIDITY 1

static gfarm_mode_t
acl_get_mode(gfarm_acl_entry_t ent)
{
	gfarm_mode_t mode = 0;
	gfarm_acl_permset_t pset;
	int bool;

	gfs_acl_get_permset(ent, &pset);
	gfs_acl_get_perm(pset, GFARM_ACL_READ, &bool);
	if (bool)
		mode |= 0004;
	gfs_acl_get_perm(pset, GFARM_ACL_WRITE, &bool);
	if (bool)
		mode |= 0002;
	gfs_acl_get_perm(pset, GFARM_ACL_EXECUTE, &bool);
	if (bool)
		mode |= 0001;
	return (mode);
}

static gfarm_error_t
acl_convert_for_setxattr_internal(
	struct inode *inode, gfarm_acl_type_t type,
	const void *in_value, size_t in_size,
	void **out_valuep, size_t *out_sizep,
	int replace_mode_t, int check_validity)
{
	gfarm_error_t e;
	gfarm_acl_t acl;
	int acl_check_err;
	gfarm_acl_entry_t ent;
	gfarm_acl_tag_t tag;
	gfarm_mode_t mode = 0;

	if (type == GFARM_ACL_TYPE_DEFAULT && !inode_is_dir(inode))
		return (GFARM_ERR_INVALID_ARGUMENT);

	e = gfs_acl_from_xattr_value(in_value, in_size, &acl);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002854,
			    "gfs_acl_from_xattr_value() failed: %s",
			    gfarm_error_string(e));
		return (e);
	}

	if (check_validity) {
		e = gfs_acl_check(acl, NULL, &acl_check_err);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_notice(GFARM_MSG_1002855,
				    "gfs_acl_check() failed by %s: %s",
				     user_name(inode_get_user(inode)),
				    gfs_acl_error(acl_check_err));
			gfs_acl_free(acl);
			return (e);
		}
	}

	/* check whether user/group exists and save standard ACL */
	e = gfs_acl_get_entry(acl, GFARM_ACL_FIRST_ENTRY, &ent);
	while (e == GFARM_ERR_NO_ERROR) {
		char *qual;
		struct user *u;
		struct group *g;
		gfs_acl_get_tag_type(ent, &tag);
		switch (tag) {
		case GFARM_ACL_USER_OBJ:
			mode |= (acl_get_mode(ent) << 6);
			break;
		case GFARM_ACL_GROUP_OBJ:
			mode |= (acl_get_mode(ent) << 3);
			break;
		case GFARM_ACL_OTHER:
			mode |= acl_get_mode(ent);
			break;
		case GFARM_ACL_USER:
			gfs_acl_get_qualifier(ent, &qual);
			if ((u = user_lookup(qual)) == NULL) {
				gflog_debug(GFARM_MSG_1002856,
					    "unknown user: %s", qual);
				gfs_acl_free(acl);
				return (GFARM_ERR_NO_SUCH_USER);
			}
			/* Length of user name does not require check here */
			break;
		case GFARM_ACL_GROUP:
			gfs_acl_get_qualifier(ent, &qual);
			if ((g = group_lookup(qual)) == NULL) {
				gflog_debug(GFARM_MSG_1002857,
					    "unknown group: %s", qual);
				gfs_acl_free(acl);
				return (GFARM_ERR_NO_SUCH_GROUP);
			}
			break;
		}
		e = gfs_acl_get_entry(acl, GFARM_ACL_NEXT_ENTRY, &ent);
	}
	if (e != GFARM_ERR_NO_SUCH_OBJECT && e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002858,
			    "gfs_acl_get_entry() failed: %s",
			    gfarm_error_string(e));
		gfs_acl_free(acl);
		return (e);
	}

	if (type == GFARM_ACL_TYPE_ACCESS) {
		gfarm_mode_t old_mode = inode_get_mode(inode);
		if (replace_mode_t)  /* chmod by setfacl */
			e = inode_set_mode(
				inode, (old_mode & 07000) | mode);
		else  /* inheritance of Default ACL */
			e = inode_set_mode(
				inode, (old_mode & 07000) | (old_mode & mode));
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1002859,
				    "inode_set_mode() failed: %s",
				    gfarm_error_string(e));
			gfs_acl_free(acl);
			return (e);
		}

		/* GFARM_ACL_TYPE_ACCESS has extended ACL only */
		e = gfs_acl_delete_mode(acl);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1002860,
				    "gfs_acl_delete_mode() failed: %s",
				    gfarm_error_string(e));
			gfs_acl_free(acl);
			return (e);
		}
	}

	e = gfs_acl_to_xattr_value(acl, out_valuep, out_sizep);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002861,
			    "gfs_acl_to_xattr_value() failed: %s",
			    gfarm_error_string(e));
		gfs_acl_free(acl);
		return (e);
	}
	gfs_acl_free(acl);

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
acl_convert_for_setxattr(
	struct inode *inode, gfarm_acl_type_t type,
	void **valuep, size_t *sizep)
{
	gfarm_error_t e;
	void *out_value = NULL;
	size_t out_size;

	e = acl_convert_for_setxattr_internal(
	    inode, type, *valuep, *sizep, &out_value, &out_size,
	    REPLACE_MODE_T,
	    type == GFARM_ACL_TYPE_DEFAULT ? !CHECK_VALIDITY : CHECK_VALIDITY);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002862,
			    "acl_convert_for_setxattr_internal() failed: %s",
			    gfarm_error_string(e));
		return (e);
	}
	if (out_value == NULL)  /* through */
		return (GFARM_ERR_NO_ERROR); /* valuep is not replaced */

	free(*valuep);
	*valuep = out_value;
	*sizep = out_size;

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
acl_convert_for_getxattr(
	struct inode *inode, const char *name, void **valuep, size_t *sizep)
{
	gfarm_error_t e;
	gfarm_acl_t acl;
	gfarm_acl_entry_t entry;
	gfarm_acl_permset_t permset;
	void *acl_value;
	size_t acl_size;
	gfarm_mode_t mode;

	if (strcmp(name, GFARM_ACL_EA_ACCESS) != 0)
		return (GFARM_ERR_NO_ERROR); /* through */

	/* GFARM_ACL_EA_ACCESS */
	e = gfs_acl_from_xattr_value(*valuep, *sizep, &acl);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002863,
			    "gfs_acl_from_xattr_value(%s): %s",
			    name, gfarm_error_string(e));
		return (e);
	}

	/* fill standard ACL */
	mode = inode_get_mode(inode);

	e = gfs_acl_create_entry(&acl, &entry);
	if (e != GFARM_ERR_NO_ERROR) {
		gfs_acl_free(acl);
		return (e);
	}
	gfs_acl_get_permset(entry, &permset);
	gfs_acl_add_perm(permset, (mode & 0700) >> 6);
	gfs_acl_set_permset(entry, permset);
	gfs_acl_set_tag_type(entry, GFARM_ACL_USER_OBJ);

	e = gfs_acl_create_entry(&acl, &entry);
	if (e != GFARM_ERR_NO_ERROR) {
		gfs_acl_free(acl);
		return (e);
	}
	gfs_acl_get_permset(entry, &permset);
	gfs_acl_add_perm(permset, (mode & 0070) >> 3);
	gfs_acl_set_permset(entry, permset);
	gfs_acl_set_tag_type(entry, GFARM_ACL_GROUP_OBJ);

	e = gfs_acl_create_entry(&acl, &entry);
	if (e != GFARM_ERR_NO_ERROR) {
		gfs_acl_free(acl);
		return (e);
	}
	gfs_acl_get_permset(entry, &permset);
	gfs_acl_add_perm(permset, mode & 0007);
	gfs_acl_set_permset(entry, permset);
	gfs_acl_set_tag_type(entry, GFARM_ACL_OTHER);

	gfs_acl_sort(acl);

#if 0 /* already validated and sorted ACL */
	{
		int acl_check_err;
		e = gfs_acl_check(acl, NULL, &acl_check_err);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1002864,
				    "gfs_acl_check(%s): %s",
				    name, gfs_acl_error(acl_check_err));
			gfs_acl_free(acl);
			return (e);
		}
	}
#endif

	e = gfs_acl_to_xattr_value(acl, &acl_value, &acl_size);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002865,
			    "gfs_acl_to_xattr_value(%s): %s",
			    name, gfarm_error_string(e));
		gfs_acl_free(acl);
		return (e);
	}
	gfs_acl_free(acl);
	free(*valuep);

	*valuep = acl_value;
	*sizep = acl_size;

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
acl_inherit_default_acl(struct inode *parent, struct inode *child,
			void **acl_def_p, size_t *acl_def_size_p,
			void **acl_acc_p, size_t *acl_acc_size_p)
{
	gfarm_error_t e;

	*acl_def_p = NULL;
	*acl_acc_p = NULL;
	if (inode_is_symlink(child))
		return (GFARM_ERR_NO_ERROR);  /* not inherit */

	e = inode_xattr_get_cache(parent, 0, GFARM_ACL_EA_DEFAULT,
				  acl_def_p, acl_def_size_p);
	if (e == GFARM_ERR_NO_SUCH_OBJECT || *acl_def_p == NULL)
		return (GFARM_ERR_NO_ERROR);  /* not inherit */
	else if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002866,
			    "inode_xattr_get_cache(%s) failed: %s",
			    GFARM_ACL_EA_DEFAULT, gfarm_error_string(e));
		return (e);
	}
	e = acl_convert_for_setxattr_internal(
		child, GFARM_ACL_TYPE_ACCESS,
		*acl_def_p, *acl_def_size_p,
		acl_acc_p, acl_acc_size_p,
		!REPLACE_MODE_T, !CHECK_VALIDITY);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002867,
			    "acl_convert_for_setxattr_internal() failed: %s",
			    gfarm_error_string(e));
		return (e);
	}
	e = inode_xattr_add(child, 0, GFARM_ACL_EA_ACCESS,
			    *acl_acc_p, *acl_acc_size_p);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002868,
			    "inode_xattr_add(%lld): %s",
			    (unsigned long long)inode_get_number(child),
			    gfarm_error_string(e));
		free(*acl_acc_p);
		free(*acl_def_p);
		return (e);
	}
	if (inode_is_dir(child)) {
		e = inode_xattr_add(child, 0, GFARM_ACL_EA_DEFAULT,
				    *acl_def_p, *acl_def_size_p);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1002869,
				    "inode_xattr_add(%lld): %s",
				    (unsigned long long)
				    inode_get_number(child),
				    gfarm_error_string(e));
			free(*acl_acc_p);
			free(*acl_def_p);
			return (e);
		}
	} else {
		free(*acl_def_p);
		*acl_def_p = NULL;
	}

	return (GFARM_ERR_NO_ERROR);
}

/* If this returns GFARM_ERR_NO_SUCH_OBJECT, the inode does not have ACL. */
gfarm_error_t
acl_access(struct inode *inode, struct user *user, int op)
{
	gfarm_error_t e;
	void *value;
	size_t size;
	gfarm_mode_t mask = 0, acl_mask = 0;
	gfarm_mode_t mode = inode_get_mode(inode);
	gfarm_mode_t user_mode = 0, group_mode = 0;
	gfarm_acl_t acl = NULL;
	gfarm_acl_entry_t ent;
	gfarm_acl_tag_t tag;
	char *qual;
	int user_found = 0, group_found = 0;

#if 0  /* already checked in inode_access() */
	if (user_is_root(user))
		return (GFARM_ERR_NO_ERROR);
#endif

	if (op & GFS_X_OK)
		mask |= 0001;
	if (op & GFS_W_OK)
		mask |= 0002;
	if (op & GFS_R_OK)
		mask |= 0004;

#if 0  /* already checked in inode_access() */
	/* GFARM_ACL_USER_OBJ */
	if (inode_get_user(inode) == user) {
		mode = (mode >> 6) & 0007;
		return ((mode & mask) == mask ? GFARM_ERR_NO_ERROR :
			GFARM_ERR_PERMISSION_DENIED);
	}
#endif

	e = inode_xattr_get_cache(inode, 0, GFARM_ACL_EA_ACCESS,
				  &value, &size);
	if (e != GFARM_ERR_NO_ERROR) {
		if (e != GFARM_ERR_NO_SUCH_OBJECT)
			gflog_debug(GFARM_MSG_1002870,
			    "inode_xattr_get_cache(%s) failed: %s",
			    GFARM_ACL_EA_ACCESS, gfarm_error_string(e));
		return (e);
	}
	if (value == NULL)
		return (GFARM_ERR_NO_SUCH_OBJECT); /* no ACL */
	if (size <= 4) {  /* The value has only version number. */
		free(value);
		return (GFARM_ERR_NO_SUCH_OBJECT); /* no ACL */
	}

	e = gfs_acl_from_xattr_value(value, size, &acl);
	free(value);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002871,
			    "gfs_acl_from_xattr_value() failed: %s",
			    gfarm_error_string(e));
		return (e);
	}

	/* search GFARM_ACL_USER, GFARM_ACL_GROUP and GFARM_ACL_MASK */
	e = gfs_acl_get_entry(acl, GFARM_ACL_FIRST_ENTRY, &ent);
	while (e == GFARM_ERR_NO_ERROR) {
		gfs_acl_get_tag_type(ent, &tag);
		gfs_acl_get_qualifier(ent, &qual);
		if (user_found == 0 && tag == GFARM_ACL_USER &&
		    strcmp(user_name(user), qual) == 0) {
			user_mode = acl_get_mode(ent);
			user_found = 1;
		} else if (user_found == 0 && group_found == 0 &&
			   tag == GFARM_ACL_GROUP &&
			   user_in_group(user, group_lookup(qual))) {
			group_mode = acl_get_mode(ent);
			group_found = 1;
		} else if (tag == GFARM_ACL_MASK)
			acl_mask = acl_get_mode(ent);

		e = gfs_acl_get_entry(acl, GFARM_ACL_NEXT_ENTRY, &ent);
	}
	gfs_acl_free(acl);
	if (e != GFARM_ERR_NO_SUCH_OBJECT && e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002872,
			    "gfs_acl_get_entry() failed: %s",
			    gfarm_error_string(e));
		return (e);
	}

	if (user_found == 1)
		/* GFARM_ACL_USER */
		mode = user_mode & acl_mask;
	else if (user_in_group(user, inode_get_group(inode)))
		/* GFARM_ACL_GROUP_OBJ */
		mode = (mode >> 3) & acl_mask & 0007;
	else if (group_found == 1)
		/* GFARM_ACL_GROUP */
		mode = group_mode & acl_mask;
	else
		/* GFARM_ACL_OTHER */
		mode = mode & 0007;

	return ((mode & mask) == mask ? GFARM_ERR_NO_ERROR :
		GFARM_ERR_PERMISSION_DENIED);
}
