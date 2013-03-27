#ifndef _STRING_H_
#define _STRING_H_
#include <linux/gfp.h>
#include <linux/string.h>

#define strdup(s)	kstrdup(s, GFP_KERNEL)
#define strerror(eno)	(char *)gfarm_error_string(gfarm_errno_to_error(eno))

#endif /* _STRING_H_ */
