#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <limits.h>

#include "gssapi.h"

#include "gfsl_config.h"
#include "gfarm_gsi.h"
#include "gfarm_auth.h"
#include "gfarm_secure_session.h"
#include "tcputil.h"


static int
getInt(char *str, int *val)
{
    char *ePtr = NULL;
    int ret = -1;
    int t = 1;
    char *buf = NULL;
    int tmp;
    int base = 10;
    int len;
    int neg = 1;

    switch ((int)str[0]) {
	case '-': {
	    neg = -1;
	    str++;
	    break;
	}
	case '+': {
	    str++;
	    break;
	}
    }
    if (strncmp(str, "0x", 2) == 0) {
	base = 16;
	str += 2;
    }

    buf = strdup(str);
    if (buf == NULL) {
	return -1;
    }
    len = strlen(buf);
    if (len == 0) {
	return -1;
    }

    if (base == 10) {
	int lC = len - 1;
	switch ((int)(buf[lC])) {
	    case 'k': case 'K': {
		t = 1024;
		buf[lC] = '\0';
		break;
	    }
	    case 'm': case 'M': {
		t = 1024 * 1024;
		buf[lC] = '\0';
		break;
	    }
	}
    }

    tmp = (int)strtol(buf, &ePtr, base);
    if (ePtr == (buf + len)) {
	ret = 1;
	*val = tmp * t * neg;
    }

    (void)free(buf);
    return ret;
}


static int port = 0;

static int
ParseArgs(argc, argv)
     int argc;
     char *argv[];
{
    while (*argv != NULL) {
	if (strcmp(*argv, "-p") == 0) {
	    if (argv[1] != NULL &&
		*argv[1] != '\0') {
		int tmp;
		argv++;
		if (getInt(*argv, &tmp) < 0) {
		    fprintf(stderr, "illegal port number.\n");
		    return -1;
		}
		if (tmp <= 0) {
		    fprintf(stderr, "port number must be > 0.\n");
		    return -1;
		} else if (tmp > 65535) {
		    fprintf(stderr, "port number must be < 65536.\n");
		    return -1;
		}
		port = tmp;
	    } else {
		fprintf(stderr, "a port number must be specified.\n");
		return -1;
	    }
	}

	argv++;
    }
    
    return 0;
}


extern void	doServer(int fd, char *host, int port);
extern void	doClient(char *host, int port, gss_cred_id_t sCtx, int deleCheck);


int
main(argc, argv)
     int argc;
     char *argv[];
{
    int bindFd = -1;
    struct sockaddr_in remote;
    int remLen = sizeof(struct sockaddr_in);
    int fd = -1;
    OM_uint32 majStat;
    unsigned long int rAddr, myAddr;
    char *rHost;
    char myHostname[4096];

    if (ParseArgs(argc - 1, argv + 1) != 0) {
	return 1;
    }

    if (gethostname(myHostname, 4096) < 0) {
	return 1;
    }
    myAddr = GetIPAddressOfHost(myHostname);

    if (gfarmSecSessionInitializeBoth(NULL, NULL, NULL, &majStat) <= 0) {
	fprintf(stderr, "can't initialize as both role because of:\n");
	gfarmGssPrintStatus(stderr, majStat);
	gfarmSecSessionFinalizeBoth();
	return 1;
    }

    /*
     * Create a channel.
     */
    bindFd = BindPort(port);
    if (bindFd < 0) {
	gfarmSecSessionFinalizeBoth();
	return 1;
    }
    (void)GetNameOfSocket(bindFd, &port);
    fprintf(stderr, "Accepting port: %d\n", port);

    /*
     * Accept-fork loop.
     */
    while (1) {
	pid_t pid;
	fd = accept(bindFd, (struct sockaddr *)&remote, &remLen);
	if (fd < 0) {
	    perror("accept");
	    (void)close(bindFd);
	    return 1;
	}
	rAddr = ntohl(remote.sin_addr.s_addr);
	rHost = GetHostOfIPAddress(rAddr);
	fprintf(stderr, "Connected from %s\n", rHost);
	(void)free(rHost);
	pid = fork();
	if (pid < 0) {
	    (void)close(fd);
	    (void)close(bindFd);
	    perror("fork");
	    return 1;
	} else if (pid == 0) {
	    (void)close(bindFd);
	    doServer(fd, myHostname, port);
	    (void)close(fd);
	    exit(0);
	} else {
	    (void)close(fd);
	}
    }
#if 0 /* never reach here */
    gfarmSecSessionFinalizeBoth();
    return 0;
#endif
}
