/*
 * $Id$
 */

gfarm_error_t acl_convert_for_setxattr(
	struct inode *, struct tenant *, gfarm_acl_type_t, void **, size_t *,
	gfarm_mode_t *, int *);
gfarm_error_t acl_convert_for_getxattr(
	struct inode *, const char *, void **, size_t *);

gfarm_error_t acl_inherit_default_acl(
	struct inode *, struct inode *, struct tenant *,
	void **, size_t *, void **, size_t *, gfarm_mode_t *, int *);

gfarm_error_t acl_access(struct inode *, struct tenant *, struct user *, int);
