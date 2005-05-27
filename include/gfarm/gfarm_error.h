/*
 * address constants which represents error conditions
 *
 * $Id$
 */

/* classic errno (1..10, 12..34) */
extern char GFARM_ERR_OPERATION_NOT_PERMITTED[]; /* forbidden entirely */
extern char GFARM_ERR_NO_SUCH_OBJECT[];
extern char GFARM_ERR_INTERRUPTED_SYSTEM_CALL[];
extern char GFARM_ERR_INPUT_OUTPUT[];
extern char GFARM_ERR_NO_MEMORY[];
extern char GFARM_ERR_PERMISSION_DENIED[]; /* prohibited by access control */
extern char GFARM_ERR_ALREADY_EXISTS[];
extern char GFARM_ERR_NOT_A_DIRECTORY[];
extern char GFARM_ERR_IS_A_DIRECTORY[];
extern char GFARM_ERR_INVALID_ARGUMENT[];
extern char GFARM_ERR_NO_SPACE[];
extern char GFARM_ERR_READ_ONLY_FILE_SYSTEM[];

/* non classic, non-blocking and interrupt i/o */
extern char GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE[];

/* non classic, filesystem related errors */
extern char GFARM_ERR_DISK_QUOTA_EXCEEDED[];
extern char GFARM_ERR_DIRECTORY_NOT_EMPTY[];

/* non classic, system call */
extern char GFARM_ERR_FUNCTION_NOT_IMPLEMENTED[];

/* math software */
extern char GFARM_ERR_NUMERICAL_RESULT_OUT_OF_RANGE[];

/* ipc/network errors */
extern char GFARM_ERR_OPERATION_NOT_SUPPORTED[];
extern char GFARM_ERR_AUTHENTICATION[];
extern char GFARM_ERR_EXPIRED[];
extern char GFARM_ERR_PROTOCOL_NOT_SUPPORTED[];
extern char GFARM_ERR_PROTOCOL[];
extern char GFARM_ERR_NETWORK_IS_UNREACHABLE[];
extern char GFARM_ERR_NO_ROUTE_TO_HOST[];
extern char GFARM_ERR_CONNECTION_TIMED_OUT[];
extern char GFARM_ERR_CONNECTION_REFUSED[];
extern char GFARM_ERR_CONNECTION_RESET_BY_PEER[];
extern char GFARM_ERR_UNKNOWN_HOST[];

/* gfarm specific errors */
extern char GFARM_ERR_GFARM_URL_PREFIX_IS_MISSING[];
extern char GFARM_ERR_FRAGMENT_INDEX_NOT_AVAILABLE[];
extern char GFARM_ERR_FRAGMENT_NUMBER_DOES_NOT_MATCH[];
extern char GFARM_ERR_AMBIGUOUS_RESULT[];
extern char GFARM_ERR_INCONSISTENT_RECOVERABLE[];
extern char GFARM_ERR_NO_FRAGMENT_INFORMATION[];
extern char GFARM_ERR_NO_REPLICA_ON_HOST[];
extern char GFARM_ERR_UNKNOWN[];

int gfarm_error_to_errno(const char *);
char *gfarm_errno_to_error(int);

void gfarm_error_initialize(void);
