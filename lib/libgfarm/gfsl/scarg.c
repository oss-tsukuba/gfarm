#include <stdio.h>
#include <unistd.h>

#include <gssapi.h>

#include "misc.h"

#include "scarg.h"

int port = 0;

int acceptorSpecified = 0;
char *acceptorNameString;
gss_OID acceptorNameType;

int
HandleCommonOptions(option, arg)
	int option;
	char *arg;
{
    int tmp;

    switch (option) {
    case 'p':
	if (gfarmGetInt(arg, &tmp) < 0) {
	    fprintf(stderr, "illegal port number.\n");
	    return -1;
	}
	if (tmp <= 0) {
	    fprintf(stderr, "port number must be > 0.\n");
	    return -1;
	} else if (tmp > 65535) {
	    fprintf(stderr, "port number must be < 65536.\n");
	    return -1;
	}
	port = tmp;
	break;
    case 'H':
	acceptorNameString = arg;
	acceptorNameType = GSS_C_NT_HOSTBASED_SERVICE;
	acceptorSpecified = 1;
	break;
    case 'M':
	acceptorNameString = arg; /* mechanism specific name */
	acceptorNameType = GSS_C_NO_OID;
	acceptorSpecified = 1;
	break;
    case 'N':
	acceptorNameString = NULL; /* this feature is GFSL specific */
	acceptorNameType = GSS_C_NO_OID;
	acceptorSpecified = 1;
	break;
    case 'n':
	acceptorNameString = arg;
	acceptorNameType = GSS_C_NT_USER_NAME;
	acceptorSpecified = 1;
	break;
    case 'U':
	acceptorNameString = arg;
	acceptorNameType = GSS_C_NT_STRING_UID_NAME;
	acceptorSpecified = 1;
	break;
    case 'X':
	acceptorNameString = arg;
	acceptorNameType = GSS_C_NT_EXPORT_NAME;
	acceptorSpecified = 1;
	break;
    case 'u':
	acceptorNameString = getenv("USER");
	if (acceptorNameString == NULL)
	    acceptorNameString = getenv("LOGNAME");
	if (acceptorNameString == NULL) {
	    fprintf(stderr, "neither $USER nor $LOGNAME isn't set");
	    return -1;
	}
	acceptorNameType = GSS_C_NT_USER_NAME;
	acceptorSpecified = 1;
	break;
    default:
	fprintf(stderr, "error happens at an option\n");
        return -1;
    }
    return 0;
}
