/* 
 * $Id$
 */
#include <nata/nata_logger.h>

#ifdef perror
#undef perror
#endif /* perror */
#define perror(str)	nata_MsgError("%s: %s\n", str, strerror(errno))
