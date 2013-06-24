/*
 *  idmapd.c
 *
 *  Userland daemon for idmap.
 *
 *  Copyright (c) 2002 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Marius Aamodt Eriksen <marius@umich.edu>
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the University nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 *  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <time.h>
#include <err.h>
#include <errno.h>
#include <event.h> /* libevent, http://libevent.org/ */
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <syslog.h>
#include <pwd.h>
#include <grp.h>
#include <limits.h>
#include <ctype.h>

#include "queue.h"
#include "ug_idmap.h"

#define IDMAP_TYPE_USER  0
#define IDMAP_TYPE_GROUP 1

#define IDMAP_CONV_IDTONAME 0
#define IDMAP_CONV_NAMETOID 1

#define IDMAP_STATUS_INVALIDMSG 0x01
#define IDMAP_STATUS_AGAIN      0x02
#define IDMAP_STATUS_LOOKUPFAIL 0x04
#define IDMAP_STATUS_SUCCESS    0x08

#define IDMAP_MAXMSGSZ   256

struct idmap_msg {
	u_int8_t  im_type;
	u_int8_t  im_conv;
	char      im_name[UG_IDMAP_NAMESZ];
	u_int32_t im_id;
	u_int8_t  im_status;
};

#ifndef IDMAPD_DIR
#define IDMAPD_DIR  "/proc/net/rpc"
#endif

#ifndef IDMAP4NOBODY_USER
#define IDMAP4NOBODY_USER "nobody"
#endif

#ifndef IDMAP4NOBODY_GROUP
#define IDMAP4NOBODY_GROUP "nobody"
#endif

#define IC_IDNAME 0
#define IC_IDNAME_CHAN  IDMAPD_DIR "/" IDMAP_IDTONAME "/channel"
#define IC_IDNAME_FLUSH IDMAPD_DIR "/" IDMAP_IDTONAME "/flush"

#define IC_NAMEID 1
#define IC_NAMEID_CHAN  IDMAPD_DIR "/" IDMAP_NAMETOID "/channel"
#define IC_NAMEID_FLUSH IDMAPD_DIR "/" IDMAP_NAMETOID "/flush"

#define IC_HOSTADDR 2
#define IC_HOSTADDR_CHAN  IDMAPD_DIR "/" IDMAP_HOSTADDR "/channel"
#define IC_HOSTADDR_FLUSH IDMAPD_DIR "/" IDMAP_HOSTADDR "/flush"

#define IC_ENTMAX 3

static void idmapdcb(int, short, void *);
static void hostaddrcb(int, short, void *);
#define MAXBUFLEN	128

struct idmap_client {
	short                      ic_which;
	char                      *ic_id;
	void			  (*ic_cb)(int, short, void *);
	char                       *ic_path;
	int                        ic_fd;
	int                        ic_dirfd;
	int                        ic_scanned;
	struct event               ic_event;
	TAILQ_ENTRY(idmap_client)  ic_next;
};
static struct idmap_client idmapd_ic[3] = {
	{IC_IDNAME, "idname", idmapdcb, IC_IDNAME_CHAN, -1, -1, 0},
	{IC_NAMEID, "nameid", idmapdcb, IC_NAMEID_CHAN, -1, -1, 0},
	{IC_HOSTADDR, "hostaddr",hostaddrcb,  IC_HOSTADDR_CHAN, -1, -1, 0},
};

TAILQ_HEAD(idmap_clientq, idmap_client);

static int  validateascii(char *, u_int32_t);
static int  addfield(char **, ssize_t *, char *);
static int  getfield(char **, char *, size_t);

static void imconv(struct idmap_client *, struct idmap_msg *);
static void idtonameres(struct idmap_msg *);
static void nametoidres(struct idmap_msg *);

static int idmapdopen(void);
static int idmapdopenone(struct idmap_client *);

static ssize_t atomicio(ssize_t (*)(), int, void *, size_t);
static void    mydaemon(int, int);
static void    release_parent(void);
static void closeall(int min);

static int verbose = 0;
#define DEFAULT_IDMAP_CACHE_EXPIRY (24 * 3600) /* 1 day */
static int cache_entry_expiration = DEFAULT_IDMAP_CACHE_EXPIRY;
static char *nobodyuser, *nobodygroup;
static uid_t nobodyuid;
static gid_t nobodygid;

static int
flush_idmapd_cache(char *path, time_t now)
{
	int fd;
	char stime[20];

	sprintf(stime, "%ld\n", now);
	fd = open(path, O_RDWR);
	if (fd == -1)
		return (-1);
	write(fd, stime, strlen(stime));
	close(fd);
	return (0);
}

static int
flush_idmapd_idmap_cache(void)
{
	time_t now = time(NULL);
	int ret;

	ret = flush_idmapd_cache(IC_IDNAME_FLUSH, now);
	if (ret)
		return (ret);
	ret = flush_idmapd_cache(IC_NAMEID_FLUSH, now);
	if (ret)
		return (ret);
	ret = flush_idmapd_cache(IC_HOSTADDR_FLUSH, now);
	return (ret);
}

static void
idmapd_warnx(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vsyslog(LOG_WARNING, fmt, args);
	va_end(args);
}

static void
idmapd_errx(int eval, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vsyslog(LOG_ERR, fmt, args);
	va_end(args);
	exit(eval);
}
static void
imconv(struct idmap_client *ic, struct idmap_msg *im)
{
	switch (im->im_conv) {
	case IDMAP_CONV_IDTONAME:
		idtonameres(im);
		if (verbose > 1)
			idmapd_warnx("%s: (%s) id \"%d\" -> name \"%s\"",
				ic->ic_id,
				(im->im_type == IDMAP_TYPE_USER ?
					"user" : "group"),
				im->im_id, im->im_name);
		break;
	case IDMAP_CONV_NAMETOID:
		if (validateascii(im->im_name, sizeof(im->im_name)) == -1) {
			im->im_status |= IDMAP_STATUS_INVALIDMSG;
			return;
		}
		nametoidres(im);
		if (verbose > 1)
			idmapd_warnx("%s: (%s) name \"%s\" -> id \"%d\"",
				ic->ic_id,
				(im->im_type == IDMAP_TYPE_USER ?
					"user" : "group"),
				im->im_name, im->im_id);
		break;
	default:
		idmapd_warnx("imconv: Invalid conversion type (%d) in message",
			     im->im_conv);
		im->im_status |= IDMAP_STATUS_INVALIDMSG;
		break;
	}
}

static int
idmapdopen(void)
{
	int i;
	for (i = 0; i < IC_ENTMAX; i++)
		if (idmapdopenone(&idmapd_ic[i]))
			return (-1);
	return (0);
}

static int
idmapdopenone(struct idmap_client *ic)
{
	if ((ic->ic_fd = open(ic->ic_path, O_RDWR, 0)) == -1) {
		if (verbose > 0)
			idmapd_warnx("idmapdopenone: Opening %s failed: "
				"errno %d (%s)",
				ic->ic_path, errno, strerror(errno));
		return (-1);
	}

	event_set(&ic->ic_event, ic->ic_fd, EV_READ, ic->ic_cb, ic);
	event_add(&ic->ic_event, NULL);

	if (verbose > 0)
		idmapd_warnx("Opened %s", ic->ic_path);

	return (0);
}


static void
idtonameres(struct idmap_msg *im)
{
	char buf[MAXBUFLEN];
	int ret = 0;

	switch (im->im_type) {
	case IDMAP_TYPE_USER:
		{
		struct passwd pwd, *pwdp;
		ret = getpwuid_r(im->im_id, &pwd, buf, sizeof(buf), &pwdp);
		if (ret || !pwdp)
			strcpy(im->im_name, nobodyuser);
		else
			strcpy(im->im_name, pwd.pw_name);
		}
		break;
	case IDMAP_TYPE_GROUP:
		{
		struct group grp, *grpp;
		ret = getgrgid_r(im->im_id, &grp, buf, sizeof(buf), &grpp);
		if (ret || !grpp)
			strcpy(im->im_name, nobodygroup);
		else
			strcpy(im->im_name, grp.gr_name);
		}
		break;
	}
	/* XXX Hack? */
	im->im_status = IDMAP_STATUS_SUCCESS;
}

static void
nametoidres(struct idmap_msg *im)
{
	char buf[MAXBUFLEN];
	int ret = 0;

	switch (im->im_type) {
	case IDMAP_TYPE_USER:
		{
		struct passwd pwd, *pwdp;
		ret = getpwnam_r(im->im_name, &pwd, buf, sizeof(buf), &pwdp);
		if (ret || !pwdp)
			im->im_id = nobodyuid;
		else
			im->im_id = (u_int32_t) pwd.pw_uid;
		}
		break;
	case IDMAP_TYPE_GROUP:
		{
		struct group grp, *grpp;
		ret = getgrnam_r(im->im_name, &grp, buf, sizeof(buf), &grpp);
		if (ret || !grpp)
			im->im_id = nobodygid;
		else
			im->im_id = grp.gr_gid;
		}
		break;
	}
	/* XXX? */
	im->im_status = IDMAP_STATUS_SUCCESS;
}

static int
validateascii(char *string, u_int32_t len)
{
	int i;

	for (i = 0; i < len; i++) {
		if (string[i] == '\0')
			break;

		if (string[i] & 0x80)
			return (-1);
	}

	/* if (string[i] != '\0') */
	if (i == len)
		return (-1);

	return (i + 1);
}

static int
addfield(char **bpp, ssize_t *bsizp, char *fld)
{
	char ch, *bp = *bpp;
	ssize_t bsiz = *bsizp;

	while ((ch = *fld++) != '\0' && bsiz > 0) {
		switch (ch) {
		case ' ':
		case '\t':
		case '\n':
		case '\\':
			if (bsiz >= 4) {
				bp += snprintf(bp, bsiz, "\\%03o", ch);
				bsiz -= 4;
			}
			break;
		default:
			*bp++ = ch;
			bsiz--;
			break;
		}
	}

	if (bsiz < 1 || ch != '\0')
		return (-1);

	*bp++ = ' ';
	bsiz--;

	*bpp = bp;
	*bsizp = bsiz;

	return (0);
}

static int
getfield(char **bpp, char *fld, size_t fldsz)
{
	char *bp, *ofld = fld;
	u_int val, n;

	while ((bp = strsep(bpp, " ")) != NULL && bp[0] == '\0')
		;

	if (bp == NULL || bp[0] == '\0' || bp[0] == '\n')
		return (-1);

	while (*bp != '\0' && fldsz > 1) {
		if (*bp == '\\') {
			if ((n = sscanf(bp, "\\%03o", &val)) != 1)
				return (-1);
			if (val > (char)-1)
				return (-1);
			*fld++ = (char)val;
			bp += 4;
		} else {
			*fld++ = *bp;
			bp++;
		}
		fldsz--;
	}

	if (*bp != '\0')
		return (-1);
	*fld = '\0';

	return (fld - ofld);
}
/*
 * mydaemon creates a pipe between the partent and child
 * process. The parent process will wait until the
 * child dies or writes a '1' on the pipe signaling
 * that it started successfully.
 */
int pipefds[2] = { -1, -1};

static void
mydaemon(int nochdir, int noclose)
{
	int pid, status, tempfd;

	if (pipe(pipefds) < 0)
		err(1, "mydaemon: pipe() failed: errno %d", errno);

	if ((pid = fork()) < 0)
		err(1, "mydaemon: fork() failed: errno %d", errno);

	if (pid != 0) {
		/*
		 * Parent. Wait for status from child.
		 */
		close(pipefds[1]);
		if (read(pipefds[0], &status, 1) != 1)
			exit(1);
		exit(0);
	}
	/* Child.	*/
	close(pipefds[0]);
	setsid();
	if (nochdir == 0) {
		if (chdir("/") == -1)
			err(1, "mydaemon: chdir() failed: errno %d", errno);
	}

	while (pipefds[1] <= 2) {
		pipefds[1] = dup(pipefds[1]);
		if (pipefds[1] < 0)
			err(1, "mydaemon: dup() failed: errno %d", errno);
	}

	if (noclose == 0) {
		tempfd = open("/dev/null", O_RDWR);
		if (tempfd < 0)
			tempfd = open("/", O_RDONLY);
		if (tempfd >= 0) {
			dup2(tempfd, 0);
			dup2(tempfd, 1);
			dup2(tempfd, 2);
			closeall(3);
		} else
			closeall(0);
	}

	return;
}
static void
release_parent(void)
{
	int status;

	if (pipefds[1] > 0) {
		write(pipefds[1], &status, 1);
		close(pipefds[1]);
		pipefds[1] = -1;
	}
}
/*
 * ensure all of data on socket comes through. f==read || f==write
 */
static ssize_t
atomicio(f, fd, _s, n)
	ssize_t (*f) ();
	int fd;
	void *_s;
	size_t n;
{
	char *s = _s;
	ssize_t res, pos = 0;

	while (n > pos) {
		res = (f) (fd, s + pos, n - pos);
		switch (res) {
		case -1:
			if (errno == EINTR || errno == EAGAIN)
				continue;
		case 0:
			if (pos != 0)
				return (pos);
			return (res);
		default:
			pos += res;
		}
	}
	return (pos);
}
static void
closeall(int min)
{
	DIR *dir = opendir("/proc/self/fd");
	if (dir != NULL) {
		int dfd = dirfd(dir);
		struct dirent *d;

		while ((d = readdir(dir)) != NULL) {
			char *endp;
			long n = strtol(d->d_name, &endp, 10);
			if (*endp != '\0' && n >= min && n != dfd)
				(void) close(n);
		}
		closedir(dir);
	} else {
		int fd = sysconf(_SC_OPEN_MAX);
		while (--fd >= min)
			(void) close(fd);
	}
}

static void
idmapdcb(int fd, short which, void *data)
{
	struct idmap_client *ic = data;
	struct idmap_msg im;
	u_char buf[IDMAP_MAXMSGSZ + 1];
	size_t len;
	ssize_t bsiz;
	char *bp, typebuf[IDMAP_MAXMSGSZ],
		buf1[IDMAP_MAXMSGSZ], *p;
	unsigned long tmp;

	if (which != EV_READ)
		goto out;

	if ((len = read(ic->ic_fd, buf, sizeof(buf))) <= 0) {
		idmapd_warnx("idmapdcb: read(%s) failed: errno %d (%s)",
			     ic->ic_path, (len ? errno : 0),
			     (len ? strerror(errno) : "End of File"));
		goto out;
	}

	/* Get rid of newline and terminate buffer*/
	buf[len - 1] = '\0';
	bp = (char *)buf;

	memset(&im, 0, sizeof(im));

	if (getfield(&bp, typebuf, sizeof(typebuf)) == -1) {
		idmapd_warnx("idmapdcb: bad type in upcall\n");
		return;
	}
	if (verbose > 0)
		idmapd_warnx("idmapdcb: type=%s",
			     typebuf);

	im.im_type = strcmp(typebuf, "user") == 0 ?
		IDMAP_TYPE_USER : IDMAP_TYPE_GROUP;

	switch (ic->ic_which) {
	case IC_NAMEID:
		im.im_conv = IDMAP_CONV_NAMETOID;
		if (getfield(&bp, im.im_name, sizeof(im.im_name)) == -1) {
			idmapd_warnx("idmapdcb: bad name in upcall\n");
			return;
		}
		break;
	case IC_IDNAME:
		im.im_conv = IDMAP_CONV_IDTONAME;
		if (getfield(&bp, buf1, sizeof(buf1)) == -1) {
			idmapd_warnx("idmapdcb: bad id in upcall\n");
			return;
		}
		tmp = strtoul(buf1, (char **)NULL, 10);
		im.im_id = (u_int32_t)tmp;
		if ((tmp == ULONG_MAX && errno == ERANGE)
				|| (unsigned long)im.im_id != tmp) {
			idmapd_warnx("idmapdcb: id '%s' too big!\n", buf1);
			return;
		}
		break;
	default:
		idmapd_warnx("idmapdcb: Unknown which type %d", ic->ic_which);
		return;
	}

	imconv(ic, &im);

	buf[0] = '\0';
	bp = (char *)buf;
	bsiz = sizeof(buf);

	switch (ic->ic_which) {
	case IC_NAMEID:
		/* Type */
		p = im.im_type == IDMAP_TYPE_USER ? "user" : "group";
		addfield(&bp, &bsiz, p);
		/* Name */
		addfield(&bp, &bsiz, im.im_name);
		/* expiry */
		snprintf(buf1, sizeof(buf1), "%lu",
			 time(NULL) + cache_entry_expiration);
		addfield(&bp, &bsiz, buf1);
		/* ID */
		snprintf(buf1, sizeof(buf1), "%u", im.im_id);
		addfield(&bp, &bsiz, buf1);

		/* if (bsiz == sizeof(buf)) */ /* XXX */

		bp[-1] = '\n';

		break;
	case IC_IDNAME:
		/* Type */
		p = im.im_type == IDMAP_TYPE_USER ? "user" : "group";
		addfield(&bp, &bsiz, p);
		/* ID */
		snprintf(buf1, sizeof(buf1), "%u", im.im_id);
		addfield(&bp, &bsiz, buf1);
		/* expiry */
		snprintf(buf1, sizeof(buf1), "%lu",
			 time(NULL) + cache_entry_expiration);
		addfield(&bp, &bsiz, buf1);
		/* Name */
		addfield(&bp, &bsiz, im.im_name);

		bp[-1] = '\n';

		break;
	default:
		idmapd_warnx("idmapdcb: Unknown which type %d", ic->ic_which);
		return;
	}

	bsiz = sizeof(buf) - bsiz;

	if (atomicio(write, ic->ic_fd, buf, bsiz) != bsiz)
		idmapd_warnx("idmapdcb: write(%s) failed: errno %d (%s)",
			     ic->ic_path, errno, strerror(errno));

out:
	event_add(&ic->ic_event, NULL);
}

static void
hostaddrcb(int fd, short which, void *data)
{
	struct idmap_client *ic = data;
#define UGBUFSIZ 0x1000
	char buf[UGBUFSIZ], buf1[UGBUFSIZ];
	char *bp, name[IDMAP_MAXMSGSZ];
	size_t len;
	ssize_t bsiz;
	struct hostent ent, *entp;
	int i, err, herr, local;

	if (which != EV_READ)
		goto out;

	if ((len = read(ic->ic_fd, buf, sizeof(buf))) <= 0) {
		idmapd_warnx("hostaddr: read(%s) failed: errno %d (%s)",
			     ic->ic_path, (len ? errno : 0),
			     (len ? strerror(errno) : "End of File"));
		goto out;
	}

	/* Get rid of newline and terminate buffer*/
	buf[len - 1] = '\0';
	bp = (char *)buf;

	if ((len = getfield(&bp, name, sizeof(name))) == -1) {
		idmapd_warnx("hostaddr: bad name in upcall\n");
		return;
	}
	local = strcmp(name, "localhost") == 0;
	if (verbose > 0)
		idmapd_warnx("hostaddr: name=%s", name);

	buf1[0] = '\0';
	bp = (char *)buf1;
	bsiz = sizeof(buf1);

	addfield(&bp, &bsiz, name);

	if ((err = gethostbyname_r(name, &ent, buf, sizeof(buf), &entp, &herr))
		|| !entp){
		idmapd_warnx("hostaddr:gethostbyname(%s) %s\n", name,
			strerror(err));
		goto err_out;
	}

	/* expiry */
	snprintf(name, sizeof(name), "%lu",
		 time(NULL) + cache_entry_expiration);
	addfield(&bp, &bsiz, name);

	/* addr */
	for (i = 0; entp->h_addr_list[i] && i < IDMAP_HOSTADDR_MAX; i++) {
		inet_ntop(AF_INET, entp->h_addr_list[i], name, sizeof(name));
		addfield(&bp, &bsiz, name);
	}

	addfield(&bp, &bsiz, "=");

	if (local && !gethostname(name, sizeof(name)))
		addfield(&bp, &bsiz, name);
	else
		addfield(&bp, &bsiz, entp->h_name);

	for (i = 0; entp->h_aliases[i] && i < IDMAP_HOSTNAME_MAX - 1; i++) {
		addfield(&bp, &bsiz, entp->h_aliases[i]);
	}

err_out:
	bp[-1] = '\n';

	bsiz = sizeof(buf1) - bsiz;

	if (atomicio(write, ic->ic_fd, buf1, bsiz) != bsiz)
		idmapd_warnx("hostaddr: write(%s) failed: errno %d (%s)",
			     ic->ic_path, errno, strerror(errno));
out:
	event_add(&ic->ic_event, NULL);
}


int
main(int argc, char **argv)
{
	int opt, fg = 0, idmapdret = -1;
	struct passwd *pw;
	struct group *gr;
	int ret;
	char *progname;
	char *pidfile = NULL;

	nobodyuser = IDMAP4NOBODY_USER;
	nobodygroup = IDMAP4NOBODY_GROUP;

	if ((progname = strrchr(argv[0], '/')))
		progname++;
	else
		progname = argv[0];
	openlog(progname, LOG_PID, LOG_DAEMON);

#define GETOPTSTR "vfd:U:G:P:"
	opterr = 0; /* Turn off error messages */

	while ((opt = getopt(argc, argv, GETOPTSTR)) != -1) {
		switch (opt) {
		case 'v':
			verbose++;
			break;
		case 'f':
			fg = 1;
			break;
		case 'P':
			pidfile = optarg;
			break;
		case 'd':
		case 'U':
		case 'G':
			errx(1, "the -d, -U, and -G options have been removed;"
				" please use the configuration file instead.");
			break;
		default:
			break;
		}

	}
	if (strlen(nobodyuser) >= sizeof(((struct idmap_msg *)0)->im_name))
		nobodyuser = IDMAP4NOBODY_USER;
	if ((pw = getpwnam(nobodyuser)) == NULL)
		errx(1, "Could not find user \"%s\"", nobodyuser);
	nobodyuid = pw->pw_uid;

	if (strlen(nobodygroup) >= sizeof(((struct idmap_msg *)0)->im_name))
		nobodygroup = IDMAP4NOBODY_GROUP;
	if ((gr = getgrnam(nobodygroup)) == NULL)
		errx(1, "Could not find group \"%s\"", nobodygroup);
	nobodygid = gr->gr_gid;

	if (!fg)
		mydaemon(0, 0);

	if (pidfile != NULL) {
		FILE *fp = fopen(pidfile, "w");
		if (fp != NULL) {
			fprintf(fp, "%d\n", getpid());
			fclose(fp);
		} else {
			errx(1, "cannot write PID to \"%s\"", pidfile);
		}
	}
	event_init();

	if (verbose > 0)
		idmapd_warnx("Expiration time is %d seconds.",
			     cache_entry_expiration);
	idmapdret = idmapdopen();
	if (idmapdret == 0) {
		ret = flush_idmapd_idmap_cache();
		if (ret)
			idmapd_errx(1,
				"main: Failed to flush idmapd idmap cache\n");
	}

	if (idmapdret != 0)
		idmapd_errx(1, "main: ugidmap file not found\n");

	release_parent();

	if (event_dispatch() < 0)
		idmapd_errx(1, "main: event_dispatch returns errno %d (%s)",
			    errno, strerror(errno));
	/* NOTREACHED */
	return (1);
}
