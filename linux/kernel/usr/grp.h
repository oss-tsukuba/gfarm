#ifndef _GRP_H_
#define _GRP_H_
struct group {
	char   *gr_name;
	char   *gr_passwd;
	gid_t   gr_gid;
	char   **gr_mem;
};
extern int getgrnam_r(const char *name, struct group *grp,
	char *buf, size_t buflen, struct group **result);
extern int getgrgid_r(gid_t gid, struct group *grp,
	char *buf, size_t buflen, struct group **result);
#endif /* _GRP_H_ */

