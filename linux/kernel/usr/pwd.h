#ifndef _PWD_H_
#define _PWD_H_
struct passwd {
	char   *pw_name;
	char   *pw_passwd;
	uid_t   pw_uid;
	gid_t   pw_gid;
	char   *pw_dir;
};
extern int getpwnam_r(const char *name, struct passwd *pwd,
	char *buf, size_t buflen, struct passwd **result);
extern int getpwuid_r(uid_t uid, struct passwd *pwd,
	char *buf, size_t buflen, struct passwd **result);
#endif /* _PWD_H_ */

