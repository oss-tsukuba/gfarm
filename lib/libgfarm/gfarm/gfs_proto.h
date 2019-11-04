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
 * 3: protocol since gfarm 2.6
 */
#define GFS_PROTOCOL_VERSION_V2_3	1
#define GFS_PROTOCOL_VERSION_V2_4	2
#define GFS_PROTOCOL_VERSION_V2_6	3
#define GFS_PROTOCOL_VERSION		GFS_PROTOCOL_VERSION_V2_6

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
	GFS_PROTO_COMMAND,			/* gfarm-1.x only */

	/* from gfmd (i.e. back channel) */

	GFS_PROTO_FHSTAT,
	GFS_PROTO_FHREMOVE,
	GFS_PROTO_STATUS,
	GFS_PROTO_REPLICATION_REQUEST,		/* since gfarm-2.4.0 */
	GFS_PROTO_REPLICATION_CANCEL,		/* since gfarm-2.4.0 */

	/* from client */

	GFS_PROTO_PROCESS_RESET,		/* since gfarm-2.5.3 */
	GFS_PROTO_WRITE,			/* since gfarm-2.5.8 */

	GFS_PROTO_CKSUM,			/* since gfarm-2.5.8.5 */
	GFS_PROTO_BULKREAD,			/* since gfarm-2.6.0 */
	GFS_PROTO_BULKWRITE,			/* since gfarm-2.6.0 */
	GFS_PROTO_REPLICA_RECV_CKSUM,		/* since gfarm-2.6.0 */

	/* from gfmd (i.e. back channel) */

	GFS_PROTO_REPLICATION_CKSUM_REQUEST,	/* since gfarm-2.6.0 */

	/* from client */

	GFS_PROTO_CLOSE_WRITE,			/* since gfarm-2.6.0 */
};

#define GFS_PROTO_MAX_IOSIZE	(1024 * 1024)

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

#define GFARM_DEFAULT_COMMAND_IOBUF_SIZE 0x4000

/*
 * sub protocol of GFS_PROTO_FSYNC
 */

enum gfs_proto_fsync_operation {
	GFS_PROTO_FSYNC_WITHOUT_METADATA,
	GFS_PROTO_FSYNC_WITH_METADATA
};

/*
 * GFS_PROTO_REPLICATION_REQUEST and GFS_PROTO_REPLICATION_CKSUM_REQUEST
 */
#define GFS_PROTO_REPLICATION_HANDLE_INVALID	((gfarm_int64_t)-1)

/*
 * GFS_PROTO_REPLICATION_CKSUM_REQUEST request flags
 * GFS_PROTO_REPLICA_RECV_CKSUM request flags
 * GFM_PROTO_REPLICA_ADDING_CKSUM result flags (cksum_request_flags) 
 *
 * see GFM_PROTO_CKSUM_GET result flags as well
 */

#define	GFS_PROTO_REPLICATION_CKSUM_REQFLAG_MAYBE_EXPIRED	0x00000001
#define	GFS_PROTO_REPLICATION_CKSUM_REQFLAG_SRC_SUPPORTS	0x80000000
/* the following are not protocol flags, but only used by gfmd internally */
#define	GFS_PROTO_REPLICATION_CKSUM_REQFLAG_INTERNAL_MASK	0x00ff0000
#define	GFS_PROTO_REPLICATION_CKSUM_REQFLAG_INTERNAL_SUM_AVAIL	0x00400000
#define	GFS_PROTO_REPLICATION_CKSUM_REQFLAG_INTERNAL_ENABLED	0x00800000

/*
 * GFS_PROTO_REPLICA_RECV_CKSUM result flags (cksum_result_flags):
 * just same with both GFM_PROTO_REPLICATION_CKSUM_RESULT flags
 * and cksum_result_flags of GFM_PROTO_REPLICA_ADDED_CKSUM,
 * and currently always 0.
 */

/*
 * flags of GFS_PROTO_CLOSE_WRITE
 */
enum gfs_proto_close_flags {
	GFS_PROTO_CLOSE_FLAG_MODIFIED = 0x01,
};

/*
 * connection parameters
 */

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
