/*
 * $Id$
 */

#include <pthread.h>
#include <sys/param.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <netdb.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"

#include "liberror.h"
#include "gfpath.h"

static const char *errcode_string[GFARM_ERR_NUMBER] = {
	"success",

	/* classic errno (1..10) */
	"operation not permitted",
	"no such file or directory",
	"no such process",
	"interrupted system call",
	"input/output error",
	"device not configured",
	"argument list too long",
	"exec format error",
	"bad file descriptor",
	"no child process",

	/* non classic, posix (eagain == ewouldblock) */
	"resource temporarily unavailable",

	/* classic errno (12..34) */
	"no memory",
	"permission denied", /* prohibited by access control */
	"bad address",
	"block device required", /* non posix, non x/open */
	"device busy",
	"already exists",
	"cross device link",
	"operation not supported by device",
	"not a directory",
	"is a directory",
	"invalid argument",
	"too many open files in system",
	"too many open files",
	"inappropriate ioctl for device",
	"text file busy", /* non posix */
	"file too large",
	"no space",
	"illegal seek",
	"read only file system",
	"too many links",
	"broken pipe",
	"numerical argument out of domain",
	"result out of range",

	/* ISO-C standard amendment, wide/multibyte-character handling */
	"illegal byte sequence",

	/* POSIX */
	"resource deadlock avoided",
	"file name too long",
	"directory not empty",
	"no locks available",
	"function not implemented",

	/* X/Open */
	"operation now in progress",
	"operation already in progress",
	/* X/Open - ipc/network software -- argument errors */
	"socket operation on non socket",
	"destination address required",
	"message too long",
	"protocol wrong type for socket",
	"protocol not available",
	"protocol not supported",
	"operation not supported",
	"address family not supported by protocol family",
	"address already in use",
	"cannot assign requested address",
	/* X/Open - ipc/network software -- operational errors */
	"network is down",
	"network is unreachable",
	"connection aborted",
	"connection reset by peer",
	"no buffer space available",
	"socket is already connected",
	"socket is not connected",
	"operation timed out",
	"connection refused",
	"no route to host",
	/* X/Open */
	"too many levels of symbolic link",
	"disk quota exceeded",
	"stale file handle",
	/* X/Open - system-v ipc */
	"identifier removed",
	"no message of desired type",
	"value too large to be stored in data type",

	/* ipc/network errors */
	"authentication error",
	"expired",
	"protocol error",
	"unknown host",
	"cannot resolve an IP address into a hostname",

	/* gfarm specific errors */
	"no such object",
	"can't open",
	"unexpected EOF",
	"gfarm URL prefix is missing",
	"too many jobs",
	"file migrated",
	"not a symbolic link",
	"is a symbolic link",
	"unknown error",
	"invalid file replica",
	"no such user",
	"cannot remove the last replica",
	"no such group",
	"username is missing in a Gfarm URL",
	"hostname is missing in a Gfarm URL",
	"port number is missing in a Gfarm URL",
	"port number is invalid in a Gfarm URL",
	"file busy",
	"not a regular file",
	"is a regular file",
	"path is root",
	"internal error",
	"db access should be retried",
	"too many hosts",
	"gfmd was failed over",
	"bad inode number",
	"bad cookie",
	"insufficient number of file replicas",
	"checksum mismatch",
	"conflict detected",
};

static const char *errmsg_string[GFARM_ERRMSG_END - GFARM_ERRMSG_BEGIN] = {
	"inconsistent metadata fixed, try again",
	"libgfarm internal error: too many error domain",

	"unknown keyword",
	"unknown authentication method",

	/* refered only from gfarm/hostspec.c */
	"hostname or IP address expected",
	"invalid character in hostname",
	"invalid character in IP address",
	"too big byte value in IP address",
	"IP address expected",
	"IP address less than 4 bytes",
	"too big netmask",
	"invalid name record",
	"reverse-lookup-name isn't resolvable",
	"reverse-lookup-name doesn't match, possibly forged",

	/* refered only from gfarm/param.c */
	"value isn't specified",
	"value is empty",
	"value isn't allowed for boolean",
	"invalid character in value",
	"unknown parameter",

	/* refered only from gfarm/sockopt.c */
	"getprotobyname(3) failed",
	"unknown socket option",

	/* refered only from gfarm/config.c */
	"missing argument",
	"too many arguments",
	"integer expected",
	"floating point number expected",
	"invalid character",
	"\"enable\" or \"disable\" is expected",
	"invalid syslog priority level",
	"local user name redefined",
	"global user name redefined",
	"local group name redefined",
	"global group name redefined",
	"missing second field (local user)",
	"inconsistent configuration, LDAP is specified as metadata backend before",
	"inconsistent configuration, PostgreSQL is specified as metadata backend before",
	"inconsistent configuration, localfs is specified as metadata backend before",
	"unterminated single quote",
	"unterminated double quote",
	"incomplete escape: \\",
	"missing 1st(auth-command) argument",
	"missing 2nd(auth-method) argument",
	"missing 3rd(host-spec) argument",
	"unknown auth subcommand",
	"missing 1st(netparam-option) argument",
	"missing 1st(sockopt-option) argument",
	"missing <address> argument",
	"missing <user map file> argument",
	"missing <group map file> argument",
	"missing 1st(architecture) argument",
	"missing 2nd(host-spec) argument",
	"cannot open gfarm2.conf",

	/* refered only from gfarm/gfp_xdr.c */
	"gfp_xdr_send: invalid format character",
	"gfp_xdr_recv: invalid format character",
	"gfp_xdr_vrpc: missing result in format string",
	"gfp_xdr_vrpc: invalid format character",

	/* refered only from gfarm/auth_common.c */
	"gfarm sharedsecret key - invalid expire field",
	"gfarm sharedsecret key - invalid key field",
	"gfarm sharedsecret key - key file not exist",

	/* refered only from gfarm/auth_client.c */
	"gfarm auth method isn't available for the host",
	"usable auth-method isn't configured",
	"gfarm_auth_request_sharedsecret: implementaton error",
	"gfarm_auth_request: implementation error",
	"gfarm_auth_request_sharedsecret_multiplexed: implementaton error",
	"gfarm_auth_request_multiplexed: implementation error",

	/* refered only from gfarm/auth_server.c */
	"gfarm_auth_sharedsecret_md5_response: key mismatch, continue",

	/* refered only from gfarm/auth_client_gsi.c */
	"GSI credential initialization failed",
	"GSI initialization failed",
	"cannot initiate GSI connection",
	"cannot acquire client-side GSI credential",

	/* refered only from gfarm/auth_config.c */
	"unknown cred_type",

	/* refered only from gfarm/io_gfsl.c */
	"GSI delegated credential doesn't exist",
	"cannot export GSI delegated credential",

	/* refered only from gfarm/gfs_client.c */
	"gfsd aborted",
	"illegal gfsd descriptor",
	"unknown gfsd reply",
	"GFS_PROTO_PREAD: protocol error",
	"GFS_PROTO_WRITE: protocol error",

	/* refered from gfarm/gfs_pio.c and related modules */
	"internal error: end of file with gfs_pio",

	/* refered only from gfarm/glob.c */
	"gfs_glob(): gfarm library isn't properly initialized",

	/* refered only from gfarm/schedule.c */
	"no filesystem node",

	/* refered only from gfarm/auth_common_gsi.c */
	"cred_type is not set, but cred_name is set",
	"cred_type is not set, but cred_service is set",
	"internal error: missing GSS_C_NO_CREDENTIAL check",
	"cred_type is \"no-name\", but cred_name is set",
	"cred_type is \"no-name\", but cred_service is set",
	"cred_type is \"mechanism-specific\", but cred_name is not set",
	"cred_type is \"mechanism-specific\", but cred_service is set",
	"cred_type is \"user\", but cred_service is set",
	"cred_type is \"self\", but cred_name is set",
	"cred_type is \"self\", but cred_service is set",
	"cred_type is \"self\", but not initialized as an initiator",
	"internal error - invalid cred_type",
	"invalid credential configuration",

	/* refered only from gfarm/import_help.c */
	"hostname expected",
	"empty file",
};

/*
 * NOTE: The order of the following table is important.
 *
 * For cases that multiple GFARM_ERRs are mapped into same UNIX errno.
 * first map entry will be used to map UNIX errno to GFARM_ERR,
 * The implementation of gfarm_errno_to_error_initialize() and
 * gfarm_errno_to_error() ensures this.
 *
 * It is ok that different gfarm_error_t's are mapped into same errno
 * (because gfarm_error_t is more detailed than errno),
 * but it isn't ok that different errno's are mapped into same gfarm_error_t
 * (because that means errno is more detailed than gfarm_error_t).
 */

static struct gfarm_errno_error_map {
	int unix_errno;
	gfarm_error_t gfarm_error;
} gfarm_errno_error_map_table[] = {
	{ 0,		GFARM_ERR_NO_ERROR },

	/* classic errno (1..10) */
	{ EPERM,	GFARM_ERR_OPERATION_NOT_PERMITTED },
	{ ENOENT,	GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY },
	{ ESRCH,	GFARM_ERR_NO_SUCH_PROCESS },
	{ EINTR,	GFARM_ERR_INTERRUPTED_SYSTEM_CALL },
	{ EIO,		GFARM_ERR_INPUT_OUTPUT },
	{ ENXIO,	GFARM_ERR_DEVICE_NOT_CONFIGURED },
	{ E2BIG,	GFARM_ERR_ARGUMENT_LIST_TOO_LONG },
	{ ENOEXEC,	GFARM_ERR_EXEC_FORMAT },
	{ EBADF,	GFARM_ERR_BAD_FILE_DESCRIPTOR },
	{ ECHILD,	GFARM_ERR_NO_CHILD_PROCESS },

	/* non classic, POSIX (EAGAIN == EWOULDBLOCK) */
	{ EAGAIN,	GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE },

	/* classic errno (12..34) */
	{ ENOMEM,	GFARM_ERR_NO_MEMORY },
	{ EACCES,	GFARM_ERR_PERMISSION_DENIED },
	{ EFAULT,	GFARM_ERR_BAD_ADDRESS },
	{ ENOTBLK,	GFARM_ERR_BLOCK_DEVICE_REQUIRED },
	{ EBUSY,	GFARM_ERR_DEVICE_BUSY },
	{ EEXIST,	GFARM_ERR_ALREADY_EXISTS },
	{ EXDEV,	GFARM_ERR_CROSS_DEVICE_LINK },
	{ ENODEV,	GFARM_ERR_OPERATION_NOT_SUPPORTED_BY_DEVICE },
	{ ENOTDIR,	GFARM_ERR_NOT_A_DIRECTORY },
	{ EISDIR,	GFARM_ERR_IS_A_DIRECTORY },
	{ EINVAL,	GFARM_ERR_INVALID_ARGUMENT },
	{ ENFILE,	GFARM_ERR_TOO_MANY_OPEN_FILES_IN_SYSTEM },
	{ EMFILE,	GFARM_ERR_TOO_MANY_OPEN_FILES },
	{ ENOTTY,	GFARM_ERR_INAPPROPRIATE_IOCTL_FOR_DEVICE },
	{ ETXTBSY,	GFARM_ERR_TEXT_FILE_BUSY },
	{ EFBIG,	GFARM_ERR_FILE_TOO_LARGE },
	{ ENOSPC,	GFARM_ERR_NO_SPACE },
	{ ESPIPE,	GFARM_ERR_ILLEGAL_SEEK },
	{ EROFS,	GFARM_ERR_READ_ONLY_FILE_SYSTEM },
	{ EMLINK,	GFARM_ERR_TOO_MANY_LINKS },
	{ EPIPE,	GFARM_ERR_BROKEN_PIPE },
	{ EDOM,		GFARM_ERR_NUMERICAL_ARGUMENT_OUT_OF_DOMAIN },
	{ ERANGE,	GFARM_ERR_RESULT_OUT_OF_RANGE },

	/* ISO/IEC 9899 amendment1:1995, wide/multibyte-character handling */
	{ EILSEQ,	GFARM_ERR_ILLEGAL_BYTE_SEQUENCE },

	/* POSIX */
	{ EDEADLK,	GFARM_ERR_RESOURCE_DEADLOCK_AVOIDED },
	{ ENAMETOOLONG,	GFARM_ERR_FILE_NAME_TOO_LONG },
	{ ENOTEMPTY,	GFARM_ERR_DIRECTORY_NOT_EMPTY },
	{ ENOLCK,	GFARM_ERR_NO_LOCKS_AVAILABLE },
	{ ENOSYS,	GFARM_ERR_FUNCTION_NOT_IMPLEMENTED },

	/* X/Open */
	{ EINPROGRESS,	GFARM_ERR_OPERATION_NOW_IN_PROGRESS },
	{ EALREADY,	GFARM_ERR_OPERATION_ALREADY_IN_PROGRESS },
	/* X/Open - ipc/network software -- argument errors */
	{ ENOTSOCK,	GFARM_ERR_SOCKET_OPERATION_ON_NON_SOCKET },
	{ EDESTADDRREQ,	GFARM_ERR_DESTINATION_ADDRESS_REQUIRED },
	{ EMSGSIZE,	GFARM_ERR_MESSAGE_TOO_LONG },
	{ EPROTOTYPE,	GFARM_ERR_PROTOCOL_WRONG_TYPE_FOR_SOCKET },
	{ ENOPROTOOPT,	GFARM_ERR_PROTOCOL_NOT_AVAILABLE },
	{ EPROTONOSUPPORT, GFARM_ERR_PROTOCOL_NOT_SUPPORTED },
	{ EOPNOTSUPP,	GFARM_ERR_OPERATION_NOT_SUPPORTED },
	{ EAFNOSUPPORT,	GFARM_ERR_ADDRESS_FAMILY_NOT_SUPPORTED_BY_PROTOCOL_FAMILY },
	{ EADDRINUSE,	GFARM_ERR_ADDRESS_ALREADY_IN_USE },
	{ EADDRNOTAVAIL, GFARM_ERR_CANNOT_ASSIGN_REQUESTED_ADDRESS },
	/* X/Open - ipc/network software -- operational errors */
	{ ENETDOWN,	GFARM_ERR_NETWORK_IS_DOWN },
	{ ENETUNREACH,	GFARM_ERR_NETWORK_IS_UNREACHABLE },
	{ ECONNABORTED,	GFARM_ERR_CONNECTION_ABORTED },
	{ ECONNRESET,	GFARM_ERR_CONNECTION_RESET_BY_PEER },
	{ ENOBUFS,	GFARM_ERR_NO_BUFFER_SPACE_AVAILABLE },
	{ EISCONN,	GFARM_ERR_SOCKET_IS_ALREADY_CONNECTED },
	{ ENOTCONN,	GFARM_ERR_SOCKET_IS_NOT_CONNECTED },
	{ ETIMEDOUT,	GFARM_ERR_OPERATION_TIMED_OUT },
	{ ECONNREFUSED,	GFARM_ERR_CONNECTION_REFUSED },
	{ EHOSTUNREACH,	GFARM_ERR_NO_ROUTE_TO_HOST },
	/* X/Open */
	{ ELOOP,	GFARM_ERR_TOO_MANY_LEVELS_OF_SYMBOLIC_LINK },
	{ EDQUOT,	GFARM_ERR_DISK_QUOTA_EXCEEDED },
	{ ESTALE,	GFARM_ERR_STALE_FILE_HANDLE },
	/* X/Open - System-V IPC */
	{ EIDRM,	GFARM_ERR_IDENTIFIER_REMOVED },
	{ ENOMSG,	GFARM_ERR_NO_MESSAGE_OF_DESIRED_TYPE },
	{ EOVERFLOW,	GFARM_ERR_VALUE_TOO_LARGE_TO_BE_STORED_IN_DATA_TYPE },

	/* ipc/network errors */
#ifdef EAUTH
	{ EAUTH,	GFARM_ERR_AUTHENTICATION }, /* BSD */
#else
	{ EPERM,	GFARM_ERR_AUTHENTICATION }, /* "<-" only */
#endif
#ifdef ETIME
	{ ETIME,	GFARM_ERR_EXPIRED }, /* Linux */
#endif
#ifdef EPROTO
	{ EPROTO,	GFARM_ERR_PROTOCOL },  /* SVR4, Linux */
#else
	{ EPROTONOSUPPORT, GFARM_ERR_PROTOCOL }, /* "<-" only */
#endif

	/* gfarm specific errors */
	/*		GFARM_ERR_UNKNOWN_HOST */
	/*	      GFARM_ERR_CANNOT_RESOLVE_AN_IP_ADDRESS_INTO_A_HOSTNAME */
	/*		GFARM_ERR_NO_SUCH_OBJECT */
	/*		GFARM_ERR_CANT_OPEN */
#ifdef EPROTO
	{ EPROTO,	GFARM_ERR_UNEXPECTED_EOF },
#else
	{ ECONNABORTED,	GFARM_ERR_UNEXPECTED_EOF },
#endif
	/*		GFARM_ERR_GFARM_URL_PREFIX_IS_MISSING */
	{ EAGAIN,	GFARM_ERR_TOO_MANY_JOBS },
	/*		GFARM_ERR_FILE_MIGRATED */
	{ EOPNOTSUPP,	GFARM_ERR_NOT_A_SYMBOLIC_LINK },
	{ EOPNOTSUPP,	GFARM_ERR_IS_A_SYMBOLIC_LINK },
	/*		GFARM_ERR_UNKNOWN */
	/*		GFARM_ERR_INVALID_FILE_REPLICA */
	/*		GFARM_ERR_NO_SUCH_USER */
	{ EPERM,	GFARM_ERR_CANNOT_REMOVE_LAST_REPLICA },
	/*		GFARM_ERR_NO_SUCH_GROUP */
	/*		GFARM_ERR_GFARM_URL_USER_IS_MISSING */
	/*		GFARM_ERR_GFARM_URL_HOST_IS_MISSING */
	/*		GFARM_ERR_GFARM_URL_PORT_IS_MISSING */
	/*		GFARM_ERR_GFARM_URL_PORT_IS_INVALID */
	{ EBUSY,	GFARM_ERR_FILE_BUSY },
	/*		GFARM_ERR_NOT_A_REGULAR_FILE */
	/*		GFARM_ERR_IS_A_REGULAR_FILE */
	/*		GFARM_ERR_IS_PATH_ROOT */
};

struct gfarm_error_domain {
	gfarm_error_t offset;
	int domerror_min, domerror_number;
	gfarm_error_t *map_to_gfarm;
	const char *(*domerror_to_message)(void *, int);
	void *domerror_to_message_cookie;

	/* only used by gfarm_error_range_alloc() case */
	struct gfarm_error_domain *next;
};

#define MAX_ERROR_DOMAINS	10

static int gfarm_error_domain_number = 0;
static struct gfarm_error_domain gfarm_error_domains[MAX_ERROR_DOMAINS];
static struct gfarm_error_domain *gfarm_error_ranges = NULL;

/*
 * allocate an error_domain
 * for a domain-specific error range [domerror_min, domerror_max]
 *
 * the corresponding gfarm error range will be:
 *	[domain->offset, domain->offset + domain->domerror_number - 1]
 */
gfarm_error_t
gfarm_error_domain_alloc(int domerror_min, int domerror_max,
	const char *(*de_to_m)(void *, int), void *cookie,
	struct gfarm_error_domain **domainp)
{
	int next_error;
	struct gfarm_error_domain *last, *new;

	if (gfarm_error_domain_number >= MAX_ERROR_DOMAINS) {
		gflog_debug(GFARM_MSG_1000845,
		    "gfarm_error_domain_alloc: too many error domains");
		return (GFARM_ERRMSG_TOO_MANY_ERROR_DOMAIN);
	}
	if (gfarm_error_domain_number == 0) {
		next_error = GFARM_ERR_FOREIGN_BEGIN;
	} else {
		last = &gfarm_error_domains[gfarm_error_domain_number - 1];
		next_error = last->offset + last->domerror_number;
	}
	if (next_error + domerror_max - domerror_min
	    >= GFARM_ERR_PRIVATE_BEGIN) {
		gflog_debug(GFARM_MSG_1000846,
		    "gfarm_error_domain_alloc: too large domain [%d, %d]",
		    domerror_min, domerror_max);
		return (GFARM_ERR_NUMERICAL_ARGUMENT_OUT_OF_DOMAIN);
	}
	new = &gfarm_error_domains[gfarm_error_domain_number];
	new->offset = next_error;
	new->domerror_min = domerror_min;
	new->domerror_number = domerror_max - domerror_min + 1;
	new->map_to_gfarm = NULL; /* may be allocated on demand */
	new->domerror_to_message = de_to_m;
	new->domerror_to_message_cookie = cookie;
	new->next = NULL;
	*domainp = new;
	gfarm_error_domain_number++;
	return (GFARM_ERR_NO_ERROR);
}

/*
 * allocate an error range [domerror_min, domerror_max]
 * within the gfarm error number space
 *
 * the corresponding gfarm error range will be:
 *	[domain->offset, domain->offset + domain->domerror_number - 1]
 *		==
 *	[domerror_min, domerror_max]
 */
gfarm_error_t
gfarm_error_range_alloc(int domerror_min, int domerror_max,
	const char *(*de_to_m)(void *, int), void *cookie,
	struct gfarm_error_domain **domainp)
{
	struct gfarm_error_domain *dom, *new;

	/* sanity checks */
	if (domerror_min < GFARM_ERR_PRIVATE_BEGIN ||
	    domerror_max < domerror_min) {
		gflog_debug(GFARM_MSG_1000847,
		    "gfarm_error_range_alloc: invalid range [%d, %d]",
		    domerror_min, domerror_max);
		return (GFARM_ERR_NUMERICAL_ARGUMENT_OUT_OF_DOMAIN);
	}
	for (dom = gfarm_error_ranges; dom != NULL; dom = dom->next) {
		if (domerror_max >= dom->offset &&
		    domerror_min < dom->offset + dom->domerror_number - 1) {
			gflog_debug(GFARM_MSG_1000848,
			    "gfarm_error_range_alloc: [%d, %d] is "
			    "overlapping with an existing range [%d, %d]",
			    domerror_min, domerror_max, dom->offset,
			    dom->offset + dom->domerror_number - 1);
			return (GFARM_ERR_NUMERICAL_ARGUMENT_OUT_OF_DOMAIN);
		}
	}

	GFARM_MALLOC(new);
	if (new == NULL) {
		gflog_debug(GFARM_MSG_1000849,
		    "gfarm_error_range_alloc: no memory");
		return (GFARM_ERR_NO_MEMORY);
	}
	new->offset = domerror_min;
	new->domerror_min = domerror_min;
	new->domerror_number = domerror_max - domerror_min + 1;
	new->map_to_gfarm = NULL;
	new->domerror_to_message = de_to_m;
	new->domerror_to_message_cookie = cookie;
	new->next = gfarm_error_ranges;
	gfarm_error_ranges = new;
	*domainp = new;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfarm_error_domain_add_map(struct gfarm_error_domain *domain,
	int domerror, gfarm_error_t error)
{
	int i;

	if (domerror < domain->domerror_min ||
	    domerror >= domain->domerror_min + domain->domerror_number) {
		gflog_debug(GFARM_MSG_1000850,
		    "gfarm_error_domain_add_map: "
		    "error code %d is out of [%d, %d]",
		    domerror, domain->domerror_min,
		    domain->domerror_min + domain->domerror_number - 1);
		return (GFARM_ERR_NUMERICAL_ARGUMENT_OUT_OF_DOMAIN);
	}
	if (domain->map_to_gfarm == NULL) {
		GFARM_MALLOC_ARRAY(domain->map_to_gfarm,
		    domain->domerror_number);
		if (domain->map_to_gfarm == NULL) {
			gflog_debug(GFARM_MSG_1000851,
			    "gfarm_error_domain_add_map: no memory for %d",
			    domain->domerror_number);
			return (GFARM_ERR_NO_MEMORY);
		}
		for (i = 0; i < domain->domerror_number; i++)
			domain->map_to_gfarm[i] = GFARM_ERR_UNKNOWN;
	}
	domain->map_to_gfarm[domerror - domain->domerror_min] = error;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfarm_error_domain_map(struct gfarm_error_domain *domain, int domerror)
{
	if (domerror < domain->domerror_min ||
	    domerror >= domain->domerror_min + domain->domerror_number)
		return (GFARM_ERR_UNKNOWN);
	domerror -= domain->domerror_min;
	if (domain->map_to_gfarm != NULL &&
	    domain->map_to_gfarm[domerror] != GFARM_ERR_UNKNOWN)
		return (domain->map_to_gfarm[domerror]);
	return (domain->offset + domerror);
}

static struct gfarm_error_domain *
gfarm_error_domain_search(gfarm_error_t error)
{
	int i;
	struct gfarm_error_domain *domain;

	for (i = 0; i < gfarm_error_domain_number; i++) {
		domain = &gfarm_error_domains[i];
		if (error < domain->offset)
			return (NULL);
		if (error < domain->offset + domain->domerror_number)
			return (domain);
	}
	for (domain = gfarm_error_ranges; domain != NULL;
	     domain = domain->next) {
		if (domain->offset <= error &&
		    error < domain->offset + domain->domerror_number)
			return (domain);
	}
	return (NULL);
}

const char *
gfarm_error_string(gfarm_error_t error)
{
	struct gfarm_error_domain *domain;

	if (error < 0) {
		gflog_warning(GFARM_MSG_1003548,
		    "gfarm_error_string: invalid error: %d", error);
		return (errcode_string[GFARM_ERR_UNKNOWN]);
	}
	if (error < GFARM_ERR_NUMBER) {
		if (error >= GFARM_ARRAY_LENGTH(errcode_string)) {
			gflog_warning(GFARM_MSG_1000852,
			    "gfarm_error_string: missing errcode_string: %d",
			    error);
			return (errcode_string[GFARM_ERR_UNKNOWN]);
		}
		return (errcode_string[error]);
	}

	if (error < GFARM_ERRMSG_BEGIN) {
		gflog_warning(GFARM_MSG_1003549,
		    "gfarm_error_string: unknown error: %d", error);
		return (errcode_string[GFARM_ERR_UNKNOWN]);
	}
	if (error < GFARM_ERRMSG_END)
		return (errmsg_string[error - GFARM_ERRMSG_BEGIN]);

	if ((domain = gfarm_error_domain_search(error)) != NULL) {
		error -= domain->offset;
#if 0 /* to make this return original error message instead of mapped one */
		/* this shouldn't happen, usually */
		if (domain->map_to_gfarm != NULL &&
		    domain->map_to_gfarm[error] != GFARM_ERR_UNKNOWN)
			return (errcode_string[domain->map_to_gfarm[error]]);
#endif
		return ((*domain->domerror_to_message)(
		    domain->domerror_to_message_cookie,
		    domain->domerror_min + error));
	}
	gflog_warning(GFARM_MSG_1003550,
	    "gfarm_error_string: unassigned error: %d", error);
	return (errcode_string[GFARM_ERR_UNKNOWN]);
}

#ifdef __KERNEL__	/* HAVE_SYS_NERR :: not defined in kernel */
#undef HAVE_SYS_NERR
#endif /* __KERNEL__ */

#if defined(HAVE_SYS_NERR)
# define ERRNO_NUMBER sys_nerr
#elif defined(ELAST)
# define ERRNO_NUMBER (ELAST + 1)
#else
# define ERRNO_NUMBER 256 /* XXX */
#endif

static struct gfarm_error_domain *gfarm_errno_domain = NULL;

const char *
gfarm_errno_to_string(void *cookie, int eno)
{
	return (strerror(eno));
}

static void
gfarm_errno_to_error_initialize(void)
{
	gfarm_error_t e;
	int i;
	struct gfarm_errno_error_map *map;

	/* Solaris calls this function more than once with non-pthread apps */
	if (gfarm_errno_domain != NULL)
		return;

	e = gfarm_error_domain_alloc(0, ERRNO_NUMBER,
	    gfarm_errno_to_string, NULL,
	    &gfarm_errno_domain);
	if (e != GFARM_ERR_NO_ERROR) /* really fatal problem */
		gflog_fatal(GFARM_MSG_1000007,
		    "libgfarm: cannot allocate error domain for errno");

	for (i = 0; i < GFARM_ARRAY_LENGTH(gfarm_errno_error_map_table); i++) {
		map = &gfarm_errno_error_map_table[i];
		if (gfarm_error_domain_map(gfarm_errno_domain, map->unix_errno)
		    < GFARM_ERR_NUMBER)
			continue; /* a mapping is already registered */
		if ((e = gfarm_error_domain_add_map(gfarm_errno_domain,
		    map->unix_errno, map->gfarm_error)) != GFARM_ERR_NO_ERROR){
			gflog_fatal(GFARM_MSG_1000853,
			    "libgfarm: initializing error map "
			    "(%d -> %d): %s (%d)",
			    map->unix_errno, map->gfarm_error,
			    e == GFARM_ERR_NO_MEMORY ?
			    "no memory" : "unexpected error", e);
		}
	}
}

gfarm_error_t
gfarm_errno_to_error(int eno)
{
	gfarm_error_t e;
	static pthread_once_t gfarm_errno_to_error_initialized =
	    PTHREAD_ONCE_INIT;

	pthread_once(&gfarm_errno_to_error_initialized,
	    gfarm_errno_to_error_initialize);

	e = gfarm_error_domain_map(gfarm_errno_domain, eno);
	if (e == GFARM_ERR_UNKNOWN)
		gflog_notice(GFARM_MSG_1003551,
		    "errno %d:\"%s\" is converted to \"%s\"",
		    eno, strerror(eno), gfarm_error_string(GFARM_ERR_UNKNOWN));
	return (e);
}

static int gfarm_error_to_errno_map[GFARM_ERR_NUMBER];

static void
gfarm_error_to_errno_initialize(void)
{
	int i;
	struct gfarm_errno_error_map *map;

	for (i = 0; i < GFARM_ARRAY_LENGTH(gfarm_errno_error_map_table); i++) {
		map = &gfarm_errno_error_map_table[i];
		gfarm_error_to_errno_map[map->gfarm_error] = map->unix_errno;
	}
	for (i = 1; i < GFARM_ARRAY_LENGTH(gfarm_error_to_errno_map); i++) {
		if (gfarm_error_to_errno_map[i] == 0)
			gfarm_error_to_errno_map[i] = EINVAL;
	}
}

int
gfarm_error_to_errno(gfarm_error_t error)
{
	static pthread_once_t gfarm_error_to_errno_initialized =
	    PTHREAD_ONCE_INIT;

	pthread_once(&gfarm_error_to_errno_initialized,
	    gfarm_error_to_errno_initialize);
	if (error < GFARM_ERR_NUMBER)
		return (gfarm_error_to_errno_map[error]);
	return (EINVAL);
}
