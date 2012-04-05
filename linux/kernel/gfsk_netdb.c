#include <linux/in.h>
#include <netdb.h>
#include <pwd.h>
#include <grp.h>
#include <gfarm/gfarm.h>
#include <gfarm/gfarm_config.h>
#include <gfarm/gflog.h>
#include "ug_idmap.h"

int
gethostname(char *name, size_t len)
{
	strcpy(name, "xxxx");
	return (0);
}

struct protoent *
getprotobyname(const char *name)
{
	struct protoent *res = NULL;
	static char *tcp_alias[2] = {"TCP", NULL};
	static struct protoent tcp = { "tcp", tcp_alias, IPPROTO_TCP};
	if (!name) {
		gflog_warning(0, "getprotobyname(NULLs) is called");
	} else if (!strcasecmp(name, "tcp")) {
		res = &tcp;
	} else {
		gflog_error(0, "getprotobyname(%s) is called", name);
	}
	return (res);
}

/* convert a sockaddr structure to a pair of host name and service strings. */
int
getnameinfo(const struct sockaddr *sa, socklen_t salen,
		       char *host, size_t hostlen,
		       char *serv, size_t servlen, int flags)
{
	return (EINVAL);
}

/*
 * get a list of IP addresses and port numbers
 * for host hostname and service servname.
 */
int
getaddrinfo(const char *hostname, const char *servname,
	const struct addrinfo *hints, struct addrinfo **res)
{
	return (EINVAL);
}

void
freeaddrinfo(struct addrinfo *ai)
{
}

struct hostent *
gethostbyname(const char *name)
{
	gflog_error(0, "gethostbyname(%s) is called", name ? name : "NULL");
	return (NULL);
}

struct servent *
getservbyname(const char *name, const char *proto)
{
	gflog_warning(0, "getservbyname(%s) is called", name ? name : "NULL");
	return (NULL);
}

int
getpwuid_r(uid_t uid, struct passwd *pwd,
		   char *buf, size_t buflen, struct passwd **result)
{
	int err;
	*result = 0;
	if ((err = ug_map_uid_to_name(uid, buf, buflen)) < 0) {
		return (-err);
	} else if (err >= buflen) {
		gflog_warning(0, "getpwuid_r: buflen %ld is short", buflen);
		return (ERANGE);
	} else {
		memset(pwd, 0, sizeof(*pwd));
		pwd->pw_name = buf;
		pwd->pw_uid = uid;
		pwd->pw_dir = "/tmp"; /* NOTE: dummy setting */
		*result = pwd;
		return (0);
	}
}

int
getpwnam_r(const char *name, struct passwd *pwd,
		   char *buf, size_t buflen, struct passwd **result)
{
	int err;
	uid_t	uid;
	*result = 0;

	if ((err = ug_map_name_to_uid(name, strlen(name), &uid)) < 0) {
		return (-err);
	} else if (strlen(name) >= buflen) {
		gflog_warning(0, "getpwnam_r: buflen %ld is short", buflen);
		return (ERANGE);
	} else {
		memset(pwd, 0, sizeof(*pwd));
		strcpy(buf, name);
		pwd->pw_name = buf;
		pwd->pw_uid = uid;
		*result = pwd;
		return (0);
	}
}

int
getgrgid_r(gid_t gid, struct group *grp,
		   char *buf, size_t buflen, struct group **result)
{
	int err;
	*result = 0;
	if ((err = ug_map_gid_to_name(gid, buf, buflen)) < 0) {
		return (-err);
	} else if (err >= buflen) {
		gflog_warning(0, "getgrgid_r: buflen %ld is short", buflen);
		return (ERANGE);
	} else {
		memset(grp, 0, sizeof(*grp));
		grp->gr_name = buf;
		grp->gr_gid = gid;
		*result = grp;
		return (0);
	}
}

int
getgrnam_r(const char *name, struct group *grp,
	char *buf, size_t buflen, struct group **result)
{
	int err;
	gid_t	gid;
	*result = 0;

	if ((err = ug_map_name_to_gid(name, strlen(name), &gid)) < 0) {
		return (-err);
	} else if (strlen(name) >= buflen) {
		gflog_warning(0, "getgrnam_r: buflen %ld is short", buflen);
		return (ERANGE);
	} else {
		memset(grp, 0, sizeof(*grp));
		strcpy(buf, name);
		grp->gr_name = buf;
		grp->gr_gid = gid;
		*result = grp;
		return (0);
	}
}
