#ifndef GFSD_DEFAULT_PORT
#define GFSD_DEFAULT_PORT	600
#endif
#ifndef XAUTH_COMMAND
#define XAUTH_COMMAND "xauth"
#endif

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
#if 0
	GFS_PROTO_REPLICA_RECV,
#else
	GFS_PROTO_FHOPEN,
#endif

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
