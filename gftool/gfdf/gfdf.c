#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <gfarm/gfarm.h>

#include "config.h"
#include "gfs_client.h"

char *program_name = "gfdf";

char *
print_space(char *host, int print_inode)
{
	char *e, *canonical_hostname;
	struct gfs_connection *gfs_server;
	struct sockaddr peer_addr;
	gfarm_int32_t bsize;
	file_offset_t blocks, bfree, bavail, files, ffree, favail;

	e = gfarm_host_address_get(host, gfarm_spool_server_port,
	    &peer_addr, NULL);
	if (e != NULL) {
		fprintf(stderr, "%s: %s: %s\n", program_name, host, e);
		return (e);
	}
	e = gfarm_host_get_canonical_name(host, &canonical_hostname);
	if (e != NULL)
		canonical_hostname = host;
	e = gfs_client_connect(canonical_hostname, &peer_addr, &gfs_server);
	if (e != NULL) {
		fprintf(stderr, "%s: connect to %s: %s\n", program_name, host,
		    e);
		if (canonical_hostname != host)
			free(canonical_hostname);
		return (e);
	}

	e = gfs_client_statfs(gfs_server, ".", &bsize,
	    &blocks, &bfree, &bavail, &files, &ffree, &favail);
	if (e != NULL) {
		fprintf(stderr, "%s: df on %s: %s\n", program_name, host, e);
	} else if (print_inode) {
		printf("%12.0f%12.0f%12.0f  %3.0f%%  %s\n", 
		    (double)files,
		    (double)files - ffree,
		    (double)favail,
		    (double)(files - ffree)/(files - (ffree - favail))*100.0,
		    host);
	} else {
		printf("%12.0f%12.0f%12.0f   %3.0f%%   %s\n", 
		    (double)(blocks * bsize) / 1024.0,
		    (double)(blocks - bfree) * bsize / 1024.,
		    (double)bavail * bsize / 1024.0,
		    (double)(blocks - bfree)/(blocks - (bfree - bavail))*100.0,
		    host);
	}
	gfs_client_disconnect(gfs_server);
	if (canonical_hostname != host)
		free(canonical_hostname);
	return (e);
}

#define EXIT_INVALID_ARGUMENT	2

void
usage(void)
{
	fprintf(stderr, "Usage:\t%s -h <host>\n", program_name);
	fprintf(stderr, "\t%s -H <hostfile>\n", program_name);
	exit(EXIT_INVALID_ARGUMENT);
}

int
main(int argc, char **argv)
{
	char *e, **hosts = NULL;
	int c, i, lineno, nhosts = 0, opt_print_inode = 0, failed = 0;
	gfarm_stringlist hostlist;

	if ((e = gfarm_initialize(&argc, &argv)) != NULL) {
		fprintf(stderr, "%s: gfarm initialize: %s\n", program_name, e);
		exit(EXIT_FAILURE);
	}

	gfarm_stringlist_init(&hostlist);

	while ((c = getopt(argc, argv, "H:h:i?")) != -1) {
		switch (c) {
		case 'H':
			e = gfarm_hostlist_read(optarg, &nhosts, &hosts,
			    &lineno);
			if (e != NULL) {
				if (lineno != -1) {
					fprintf(stderr,
					    "%s: %s: line %d: %s\n",
					    program_name, optarg, lineno, e);
				} else {
					fprintf(stderr, "%s: %s: %s\n",
					    program_name, optarg, e);
				}
				exit(EXIT_INVALID_ARGUMENT);
			}
			break;
		case 'h':
			gfarm_stringlist_add(&hostlist, optarg);
			break;
		case 'i':
			opt_print_inode = 1;
			break;
		case '?':
			/*FALLTHROUGH*/
		default:
			usage();
		}
	}
	if (optind < argc ||
	    (nhosts == 0 && gfarm_stringlist_length(&hostlist) == 0))
		usage();

	if (opt_print_inode) {
		printf("%12s%12s%12s%7s %s\n",
		    "Inodes", "IUsed", "IAvail", "%IUsed", "Host");
	} else {
		printf("%12s%12s%12s%9s %s\n",
		    "1K-blocks", "Used", "Avail", "Capacity", "Host");
	}
	for (i = 0; i < nhosts; i++) {
		if (print_space(hosts[i], opt_print_inode) != NULL)
			failed = 1;
	}
	for (i = 0; i < gfarm_stringlist_length(&hostlist); i++) {
		if (print_space(gfarm_stringlist_elem(&hostlist, i),
		    opt_print_inode) != NULL)
			failed = 1;
	}
	gfarm_strings_free_deeply(nhosts, hosts);
	gfarm_stringlist_free(&hostlist);

	if ((e = gfarm_terminate()) != NULL) {
		fprintf(stderr, "%s: gfarm terminate: %s\n", program_name, e);
		exit(EXIT_FAILURE);
	}
	return (failed ? EXIT_FAILURE : EXIT_SUCCESS);
}
