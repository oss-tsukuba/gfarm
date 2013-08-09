#ifndef GFSD_DEFAULT_PORT
#define GFSD_DEFAULT_PORT	600
#endif
#ifndef XAUTH_COMMAND
#define XAUTH_COMMAND "xauth"
#endif

/*
 * TCP protocol
 */

/*
 * 1: protocol until gfarm 2.3
 * 2: protocol since gfarm 2.4
 */
#define GFS_PROTOCOL_VERSION_V2_3	1
#define GFS_PROTOCOL_VERSION_V2_4	2
#define GFS_PROTOCOL_VERSION		GFS_PROTOCOL_VERSION_V2_4

enum gfs_proto_command {
	/* from client */

	GFS_PROTO_PROCESS_SET,

	GFS_PROTO_OPEN_LOCAL,
	GFS_PROTO_OPEN,
	GFS_PROTO_CLOSE,
	GFS_PROTO_PREAD,
	GFS_PROTO_PWRITE,
	GFS_PROTO_FTRUNCATE,
	GFS_PROTO_FSYNC,
	GFS_PROTO_FSTAT,
	GFS_PROTO_CKSUM_SET,

	GFS_PROTO_LOCK,
	GFS_PROTO_TRYLOCK,
	GFS_PROTO_UNLOCK,
	GFS_PROTO_LOCK_INFO,

	GFS_PROTO_REPLICA_ADD,			/* for COMPAT_GFARM_2_3 */
	GFS_PROTO_REPLICA_ADD_FROM,		/* for COMPAT_GFARM_2_3 */
	GFS_PROTO_REPLICA_RECV,

	GFS_PROTO_STATFS,
	GFS_PROTO_COMMAND,

	/* from gfmd */

	GFS_PROTO_FHSTAT,
	GFS_PROTO_FHREMOVE,
	GFS_PROTO_STATUS,
	GFS_PROTO_REPLICATION_REQUEST,
	GFS_PROTO_REPLICATION_CANCEL,

	/* from client */
	GFS_PROTO_PROCESS_RESET,
	GFS_PROTO_WRITE,
};

/*
 * For better remote read performance, subtract 8 byte (errno and the
 * size of data of gfs_client_pread) to fill up the iobuffer.
 */
#define GFS_PROTO_MAX_IOSIZE	(1048576 - 8)

/*
 * sub protocols of GFS_PROTO_COMMAND
 */

enum gfs_proto_command_client {
	GFS_PROTO_COMMAND_EXIT_STATUS,
	GFS_PROTO_COMMAND_SEND_SIGNAL,
	GFS_PROTO_COMMAND_FD_INPUT,
};

enum gfs_proto_command_server {
	GFS_PROTO_COMMAND_EXITED,
	GFS_PROTO_COMMAND_STOPPED, /* currently not used */
	GFS_PROTO_COMMAND_FD_OUTPUT,
};

/*
 * sub protocol of GFS_PROTO_FSYNC
 */

enum gfs_proto_fsync_operation {
	GFS_PROTO_FSYNC_WITHOUT_METADATA,
	GFS_PROTO_FSYNC_WITH_METADATA
};

#define GFARM_DEFAULT_COMMAND_IOBUF_SIZE 0x4000

#define GFSD_MAX_PASSING_FD 5

/* used for both gfsd local privilege and global username of sharedsecret */
#define GFSD_USERNAME	"_gfarmfs"

#define GFSD_LOCAL_SOCKET_DIR	"/tmp/.gfarm-gfsd%s-%d"
#define GFSD_LOCAL_SOCKET_NAME	GFSD_LOCAL_SOCKET_DIR "/sock"

#define GFSD_MAX_PASSING_FD 5

#define FDESC_STDIN	0
#define FDESC_STDOUT	1
#define FDESC_STDERR	2
#define NFDESC		3

extern char GFS_SERVICE_TAG[];

/*
 * UDP protocol
 */

#define GFS_UDP_RPC_SIZE_MAX		1472 /* 1500 -20(IP hdr) -8(UDP hdr) */
#define GFS_UDP_RPC_HEADER_SIZE				24
#define GFS_UDP_PROTO_OLD_LOADAV_REQUEST_SIZE		4
#define GFS_UDP_PROTO_OLD_LOADAV_REPLY_SIZE		24
/* the following values must be different from 4 and 24 */
#define GFS_UDP_PROTO_FAILOVER_NOTIFY_REQUEST_MIN_SIZE	32
#define GFS_UDP_PROTO_FAILOVER_NOTIFY_REPLY_SIZE	28

/* this is chosen not to conflict with GFS_UDP_PROTO_LOADAV_REQUEST */
#define GFS_UDP_RPC_MAGIC		0x4766726d	/* "Gfrm" */

#define GFS_UDP_RPC_TYPE_BIT		0xc0000000 /* == XID_TYPE_BIT */
#define GFS_UDP_RPC_TYPE_REQUEST	0x00000000 /* == XID_TYPE_REQUEST */
#define GFS_UDP_RPC_TYPE_REPLY		0x80000000 /* == XID_TYPE_RESULT */

/* this must be >= gfs_client_datagram_ntimeouts */
#define GFS_UDP_RPC_RETRY_COUNT_SANITY	1000

#define GFS_UDP_RPC_XID_SIZE		8

#define GFS_UDP_PROTO_LOADAV_REQUEST	0x00000000 /*compat*/
#define GFS_UDP_PROTO_FAILOVER_NOTIFY	0x00000001

#define GFARM_MAXHOSTNAMELEN		256
