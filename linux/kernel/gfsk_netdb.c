#include <linux/mm.h>
#include <linux/in.h>
#include <linux/un.h>
#include <netdb.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>
#include <gfarm/gfarm.h>
#include <gfarm/gfarm_config.h>
#include <gfarm/gflog.h>
#include "ug_idmap.h"
#include "stdlib.h"

struct protoent *
getprotobyname(const char *name)
{
	struct protoent *res = NULL;
	static char *tcp_alias[2] = {"TCP", NULL};
	static struct protoent tcp = { "tcp", tcp_alias, IPPROTO_TCP};
	if (!name) {
		gflog_warning(GFARM_MSG_1004969,
			"getprotobyname(NULLs) is called");
	} else if (!strcasecmp(name, "tcp")) {
		res = &tcp;
	} else {
		gflog_error(GFARM_MSG_1004970,
			"getprotobyname(%s) is called", name);
	}
	return (res);
}

struct servent *
getservbyname(const char *name, const char *proto)
{
	gflog_warning(GFARM_MSG_1004971,
		"getservbyname(%s) is called", name ? name : "NULL");
	return (NULL);	/* metadb_server_port only */
}

/* convert a sockaddr structure to a pair of host name and service strings. */
int
getnameinfo(const struct sockaddr *sa, socklen_t salen,
		       char *host, size_t hostlen,
		       char *serv, size_t servlen, int flags)
{
	switch (sa->sa_family) {
	case AF_INET:
		{
		struct sockaddr_in *sin = (struct sockaddr_in *)sa;
		snprintf(host, hostlen, "%pI4", &sin->sin_addr);
		snprintf(serv, servlen, "%d", sin->sin_port);
		}
		return (0);
	case AF_UNIX:
		{
		struct sockaddr_un *sun = (struct sockaddr_un *)sa;
		snprintf(host, hostlen, "%s", sun->sun_path);
		*serv = 0;
		}
		return (0);
	default:
		return (ENOSYS);
	}
}

struct gethostname_struct {
	int len;
	char *buf;
};
static int
gethostname_cb(void *arg, char *name, struct in_addr *addr, char **alias)
{
	struct gethostname_struct *data = arg;

	if (alias && *alias && strlen(*alias) < data->len) {
		strcpy(data->buf, *alias);
		return (0);
	}
	return (-EINVAL);
}
int
gethostname(char *name, size_t len)
{
	struct gethostname_struct data = {len, name};
	return (ug_map_hostaddr_get("localhost", gethostname_cb, &data));
}

/*
 * get a list of IP addresses and port numbers
 * for host hostname and service servname.
 */
static int
getaddrinfo_cb(void *arg, char *name, struct in_addr *addr, char **alias)
{
	struct comb {
		struct addrinfo ai;
		struct sockaddr_in in;
	} *cp, *comb = arg;
	char *bp, *ep;
	int i, n, len;
	int port = htons((short)comb->ai.ai_addrlen);

	bp = (char *)comb;
	ep = (char *)comb + PAGE_SIZE;

	for (n = 0; addr[n].s_addr; n++)
		;
	if (!n) {
		gflog_warning(GFARM_MSG_1004972, "getaddrinfo: no addr");
		return (-EINVAL);
	}
	if (alias[0]) {
		len = strlen(alias[0]);
		ep -= len + 1;
	}
	if (bp + sizeof(*comb) * n > ep)
		n = (ep - bp) / sizeof(*comb);

	memset(comb, 0, sizeof(*comb) * n);
	if (alias[0]) {
		comb->ai.ai_canonname = ep;
		strcpy(ep, alias[0]);
	}
	cp = comb;
	for (i = 0; i < n; i++) {
		cp->ai.ai_family = AF_INET;
		cp->ai.ai_socktype = SOCK_STREAM;
		cp->ai.ai_protocol = IPPROTO_TCP;
		cp->ai.ai_addrlen = sizeof(struct sockaddr_in);
		cp->ai.ai_addr = (struct sockaddr *)&cp->in;
		cp->ai.ai_next = (struct addrinfo *)(cp + 1);
		cp->in.sin_family = AF_INET;
		cp->in.sin_port = port;
		cp->in.sin_addr = addr[i];
	}
	cp->ai.ai_next = NULL;
	return (0);
}
int
getaddrinfo(const char *hostname, const char *servname,
	const struct addrinfo *hints, struct addrinfo **res)
{
	struct addrinfo *info;
	int err, port = 0;
	char *bp;

	info = kmalloc(PAGE_SIZE, GFP_KERNEL);
	*res = NULL;
	if (!info) {
		gflog_warning(GFARM_MSG_1004973, "getaddrinfo: nomem");
		return (ENOMEM);
	}
	memset(info, 0, sizeof(*info));
	if (servname) {
		port = strtoul(servname, &bp, 10);
		if (port < 0 || port >= 0x10000) {
			gflog_warning(GFARM_MSG_1004974, "invalid port: %s",
				servname);
			return (EINVAL);
		}
		info->ai_addrlen = port;
	}

	err = ug_map_hostaddr_get((char *)hostname, getaddrinfo_cb, info);
	if (err)
		kfree(info);
	else {
		*res = info;
	}

	return (-err);
}

void
freeaddrinfo(struct addrinfo *info)
{
	kfree(info);
}

static int
gethostbyname_cb(void *arg, char *name, struct in_addr *addr, char **alias)
{
	struct hostent *ent = arg;
	char *bp, *ep;
	int i, len;

	bp = (char *)(ent + 1);
	ep = (char *)ent + PAGE_SIZE;

	if (name != ent->h_name) {
		gflog_warning(GFARM_MSG_1004975, "gethostbyname: name differ");
		return (-EINVAL);
	}

	for (i = 0; alias[i]; i++)
		;
	if (!i) {
		gflog_warning(GFARM_MSG_1004976, "gethostbyname: no name");
		return (-EINVAL);
	}
	ent->h_aliases = (char **) bp;
	bp += sizeof(char **) * i;

	for (i = 0; addr[i].s_addr; i++)
		;
	if (!i) {
		gflog_warning(GFARM_MSG_1004977, "gethostbyname: no addr");
		return (-EINVAL);
	}
	ent->h_addr_list = (char **) bp;
	bp += sizeof(char **) * i;

	for (i = 0; alias[i]; i++) {
		len = strlen(bp);
		if (bp + len >= ep)
			break;
		strcpy(bp, alias[i]);
		if (!i)
			ent->h_name = bp;
		else
			ent->h_aliases[i-1] = bp;
		bp += len + 1;
	}
	ent->h_aliases[i-1] = NULL;

	ent->h_addrtype = AF_INET;
	ent->h_length = sizeof(struct in_addr);

	for (i = 0; addr[i].s_addr && bp < ep - 3; i++) {
		memcpy(bp, &addr[i], sizeof(struct in_addr));
		ent->h_addr_list[i] = bp;
		bp += sizeof(struct in_addr);
	}
	ent->h_addr_list[i] = NULL;
	return (0);
}
struct hostent *
gethostbyname(const char *name)
{
	int err;
	struct hostent *ent;
	ent = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!ent) {
		gflog_warning(GFARM_MSG_1004978,
			"gethostbyname: nomem");
		errno = ENOMEM;
		return (NULL);
	}
	memset(ent, 0, sizeof(*ent));
	ent->h_name = (char *)name;

	err = ug_map_hostaddr_get((char *)name, gethostbyname_cb, ent);
	if (!err)
		return (ent);

	errno = -err;
	kfree(ent);
	return (NULL);
}
void
free_gethost_buff(void *buf)
{
	kfree(buf);
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
		gflog_warning(GFARM_MSG_1004979,
			"getpwuid_r: buflen %ld is short", buflen);
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
		gflog_warning(GFARM_MSG_1004980,
			"getpwnam_r: buflen %ld is short", buflen);
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
		gflog_warning(GFARM_MSG_1004981,
			"getgrgid_r: buflen %ld is short", buflen);
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
		gflog_warning(GFARM_MSG_1004982,
			"getgrnam_r: buflen %ld is short", buflen);
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
