#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <assert.h>

#include <gfarm/gfarm.h>

#include "gfs_proto.h"
#include "host_address.h"
#include "host.h"
#include "lookup.h"

#define OP_FAILOVER_REQUEST	'f'

const char *program_name = "fo_notify_test";
struct gfm_connection *gfm_server = NULL;

gfarm_uint32_t proto_failover_notify_request = GFS_UDP_PROTO_FAILOVER_NOTIFY;

int
udp_connect_to(const char *hostname, int port)
{
	gfarm_error_t e;
	int sock, addr_count;
	struct gfarm_host_address **addr_array, *peer_addr;

	if ((e = gfm_host_address_get(gfm_server, hostname, port,
	    &addr_count, &addr_array)) != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s:%d: %s",
		    program_name, hostname, port, gfarm_error_string(e));
		exit(1);
	}
	assert(addr_count > 0);
	peer_addr = addr_array[0]; /* XXX */
	sock = socket(peer_addr->sa_family, SOCK_DGRAM, 0);
	if (sock == -1) {
		fprintf(stderr, "%s: socket() for %s:%d: %s",
		    program_name, hostname, port, strerror(errno));
		exit(1);
	}
	if (connect(sock, &peer_addr->sa_addr, peer_addr->sa_addrlen)
	    == -1) {
		fprintf(stderr, "%s: connect(%s:%d): %s",
		    program_name, hostname, port, strerror(errno));
		exit(1);
	}
	return (sock);
}

void
failover_notify_request(int sock,
	gfarm_uint32_t retry_count, gfarm_uint64_t xid,
	const char *new_master_host, int new_master_port)
{
	char buffer[GFS_UDP_RPC_SIZE_MAX], *p = buffer;
	gfarm_uint32_t u32;
	ssize_t namelen, rv;

	u32 = htonl(GFS_UDP_RPC_MAGIC);
	memcpy(p, &u32, sizeof(u32)); p += sizeof(u32);

	u32 = htonl(GFS_UDP_RPC_TYPE_REQUEST);
	memcpy(p, &u32, sizeof(u32)); p += sizeof(u32);

	u32 = htonl(retry_count);
	memcpy(p, &u32, sizeof(u32)); p += sizeof(u32);

	u32 = htonl(xid >> 32);
	memcpy(p, &u32, sizeof(u32)); p += sizeof(u32);
	u32 = htonl(xid & 0xffffffff);
	memcpy(p, &u32, sizeof(u32)); p += sizeof(u32);

	u32 = htonl(proto_failover_notify_request);
	memcpy(p, &u32, sizeof(u32)); p += sizeof(u32);

	namelen = strlen(new_master_host);
	if (namelen > GFARM_MAXHOSTNAMELEN) { /* sanity */
		fprintf(stderr,
		    "%s: metadb_server_name %s: too long name (%zd > %d)",
		    program_name, new_master_host,
		    namelen, GFARM_MAXHOSTNAMELEN);
		exit(1);
	}
	u32 = htonl(namelen);
	memcpy(p, &u32, sizeof(u32)); p += sizeof(u32);

	memcpy(p, new_master_host, namelen); p += namelen;

	u32 = htonl(new_master_port); /* not 16bit, but 32bit */
	memcpy(p, &u32, sizeof(u32)); p += sizeof(u32);

	rv = write(sock, buffer, p - buffer);
	if (rv == -1) {
		fprintf(stderr, "%s: failover_notify_reqeust: %s",
		    program_name, strerror(errno));
		exit(1);
	} else if (rv != p - buffer) {
		fprintf(stderr,
		    "%s: failover_notify_request: short write: %zd != %zd",
		    program_name, rv, p - buffer);
		exit(1);
	}
}

void
usage(void)
{
	fprintf(stderr, "Usage: %s [-c <retry_count>] [-x <xid>] "
	    "-f <dst_host> <dst_port> <new_master_host> <new_master_port>\n",
	    program_name);
	exit(2);
}

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	int c, verbose = 0, op = 0, dst_port, new_master_port;
	char *dst_host, *new_master_host, *opt_path = ".";
	gfarm_uint32_t retry_count = 0;
	gfarm_uint64_t xid = 0x12345678;

	if (argc > 0)
		program_name = basename(argv[0]);

	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfarm_initialize: %s\n",
		    gfarm_error_string(e));
		return (1);
	}

	while ((c = getopt(argc, argv, "P:c:x:r:vf")) != -1) {
		switch (c) {
		case 'P':
			opt_path = optarg;
			break;
		case 'c':
			retry_count = strtoul(optarg, NULL, 0);
			break;
		case 'x':
			xid = strtoull(optarg, NULL, 0);
			break;
		case 'v':
			verbose = 1;
			break;
		case 'r':
			proto_failover_notify_request =
			    strtoul(optarg, NULL, 0);
			/*FALLTHROUGH*/
		case OP_FAILOVER_REQUEST:
			op = c;
			break;
		default:
			fprintf(stderr, "%s: unknown option -%c\n",
			    program_name, c);
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if ((e = gfm_client_connection_and_process_acquire_by_path(opt_path,
	    &gfm_server)) != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: metadata server for \"%s\": %s\n",
		    program_name, opt_path, gfarm_error_string(e));
		return (1);
	}

	switch (op) {
	case 'r':
	case OP_FAILOVER_REQUEST:
		if (argc != 4)
			usage();
		dst_host = argv[0];
		dst_port = strtoul(argv[1], NULL, 0);
		new_master_host = argv[2];
		new_master_port = strtoul(argv[3], NULL, 0);
		if (verbose)
			printf("%s:%d retry_count:%d xid:%08llx - "
			    "failover notify request from %s:%d\n",
			    dst_host, dst_port,
			    retry_count, (unsigned long long)xid,
			    new_master_host, new_master_port);
		failover_notify_request(udp_connect_to(dst_host, dst_port),
		    retry_count, xid, new_master_host, new_master_port);
		break;
	default:
		fprintf(stderr, "%s: -f option is mandatory\n", program_name);
		usage();
	}
	return (0);
}
