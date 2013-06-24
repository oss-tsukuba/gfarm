#ifndef _GFSK_IF_H_
#define _GFSK_IF_H_

#define GFARM_FSNAME	"gfarm"

struct gfsk_strdata {
	int	d_len;
	char	*d_buf;
};

/* file buffer --------------------------------------------------*/
struct gfsk_fbuf {
	struct gfsk_strdata f_name;		/* file name */
	struct gfsk_strdata f_buf;		/* file content */
};
enum {
	GFSK_FBUF_CONFIG,
	GFSK_FBUF_USER_MAP,
	GFSK_FBUF_GROUP_MAP,
	GFSK_FBUF_MAX
};

/* mount data ---------------------------------------------------*/

#define GFSK_BUF_SIZE	4096
#define GFSK_VER1	0x30313031
#define GFSK_VER	GFSK_VER1
#define GFSK_MAX_USERNAME_LEN	64	/* gfm_proto.h:GFARM_LOGIN_NAME_MAX */

struct gfsk_mount_data {
	int	m_version;
	char	m_fsid[8];			/* out: file system id */
	struct gfsk_fbuf m_fbuf[GFSK_FBUF_MAX];
	int	m_dfd;				/* dev fd */
	uid_t	m_uid;
	char	m_uidname[GFSK_MAX_USERNAME_LEN];
	int	m_mfd;				/* meta sever fd */
	char	m_host[MAXHOSTNAMELEN];		/* connected host by m_fd */
	int	m_optlen;
	char	m_opt[1];			/* option string */
};
#define GFSK_OPTLEN_MAX (GFSK_BUF_SIZE - sizeof(struct gfsk_mount_data))

/* request data ---------------------------------------------------*/
struct gfskdev_in_header {
	unsigned int	len;
	unsigned int	opcode;
	unsigned long	unique;
	unsigned long	nodeid;
	unsigned int	uid;
	unsigned int	gid;
	unsigned int	pid;
	unsigned int	padding;
};

struct gfskdev_out_header {
	unsigned int	len;
	int	error;
	unsigned long	unique;
};

enum gfsk_opcode {
	GFSK_OP_CONNECT_GFMD	= 1000,	/* connect to meta server */
	GFSK_OP_CONNECT_GFSD	= 1001,	/* connect to spool server */
	GFSK_OP_TERM		= 1027,	/* end of operation */
};
#define GFSK_MAX_IPLEN	48

struct gfsk_req_connect {
	char	r_hostname[MAXHOSTNAMELEN];
	int	r_port;
	char	r_source_ip[GFSK_MAX_IPLEN];		/* optional */
	unsigned int	r_v4addr;
	uid_t	r_uid;					/* local user id */
	char	r_global[GFSK_MAX_USERNAME_LEN];	/* global user name */
};

struct gfsk_rpl_connect {
	int	r_fd;
	char	r_hostname[MAXHOSTNAMELEN];
};

#endif /* _GFSK_IF_H_ */

