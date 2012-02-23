#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <gssapi.h>

#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include "gfarm_gsi.h"
#include "misc.h"

#include "scarg.h"

int port = 0;

int acceptorSpecified = 0;
gss_name_t acceptorName = GSS_C_NO_NAME;
char *serviceName = NULL;
char *hostName = NULL;

int
HandleCommonOptions(int option, char *arg)
{
    int tmp;
    OM_uint32 majStat;
    OM_uint32 minStat;

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
        if (arg != NULL && *arg != '\0') {
            hostName = strdup(arg);
        }
	break;
    case 'S':
        if (arg != NULL && *arg != '\0') {
            serviceName = arg;
        }
        break;
    case 'M': /* mechanism specific name */
	if (gfarmGssImportName(&acceptorName,
			       arg, strlen(arg), GSS_C_NO_OID,
			       &majStat, &minStat) < 0) {
	    fprintf(stderr, "gfarmGssImportName(GSS_C_NO_OID) failed.\n");
	    gfarmGssPrintMajorStatus(majStat);
	    gfarmGssPrintMinorStatus(minStat);
	    return -1;
	}
	acceptorSpecified = 1;
	break;
    case 'N':
	acceptorName = GSS_C_NO_NAME;
	acceptorSpecified = 1;
	break;
    case 'n':
	if (gfarmGssImportName(&acceptorName,
			       arg, strlen(arg), GSS_C_NT_USER_NAME,
			       &majStat, &minStat) < 0) {
	    fprintf(stderr, "gfarmGssImportName(GSS_C_NT_USER_NAME)"
			" failed.\n");
	    gfarmGssPrintMajorStatus(majStat);
	    gfarmGssPrintMinorStatus(minStat);
	    return -1;
	}
	acceptorSpecified = 1;
	break;
    case 'U':
	if (gfarmGssImportName(&acceptorName,
			       arg, strlen(arg), GSS_C_NT_STRING_UID_NAME,
			       &majStat, &minStat) < 0) {
	    fprintf(stderr, "gfarmGssImportName(GSS_C_NT_STRING_UID_NAME)"
			" failed.\n");
	    gfarmGssPrintMajorStatus(majStat);
	    gfarmGssPrintMinorStatus(minStat);
	    return -1;
	}
	acceptorSpecified = 1;
	break;
    case 'X': /* This isn't guaranteed to work */
	if (gfarmGssImportName(&acceptorName,
			       arg, strlen(arg), GSS_C_NT_EXPORT_NAME,
			       &majStat, &minStat) < 0) {
	    fprintf(stderr, "gfarmGssImportName(GSS_C_NT_EXPORT_NAME)"
			" failed.\n");
	    gfarmGssPrintMajorStatus(majStat);
	    gfarmGssPrintMinorStatus(minStat);
	    return -1;
	}
	acceptorSpecified = 1;
	break;
    case 'u':
	arg = getenv("USER");
	if (arg == NULL)
	    arg = getenv("LOGNAME");
	if (arg == NULL) {
	    fprintf(stderr, "neither $USER nor $LOGNAME isn't set");
	    return -1;
	}
	if (gfarmGssImportName(&acceptorName,
			       arg, strlen(arg), GSS_C_NT_USER_NAME,
			       &majStat, &minStat) < 0) {
	    fprintf(stderr, "gfarmGssImportName(GSS_C_NT_USER_NAME)"
			" failed.\n");
	    gfarmGssPrintMajorStatus(majStat);
	    gfarmGssPrintMinorStatus(minStat);
	    return -1;
	}
	acceptorSpecified = 1;
	break;
    default:
	fprintf(stderr, "error happens at an option\n");
        return -1;
    }


    if (hostName != NULL || serviceName != NULL) {
        if (hostName == NULL) {
            char buf[2048];
            if (gethostname(buf, sizeof(buf)) != 0) {
                perror("gethostname");
                return -1;
            }
            hostName = strdup(buf);
        }

        if (serviceName == NULL) {
            if (gfarmGssImportNameOfHost(&acceptorName,
                                         hostName, &majStat, &minStat) < 0) {
                fprintf(stderr,
                        "gfarmGssImportNameOfHost() failed with:\n");
                gfarmGssPrintMajorStatus(majStat);
                gfarmGssPrintMinorStatus(minStat);
                return -1;
            }
       } else {
            if (gfarmGssImportNameOfHostBasedService(&acceptorName,
                                                     serviceName, hostName,
                                                     &majStat, &minStat) < 0) {
                fprintf(stderr,
                        "gfarmGssImportNameOfHostBasedService() "
                        "failed with:\n");
                gfarmGssPrintMajorStatus(majStat);
                gfarmGssPrintMinorStatus(minStat);
                return -1;
            }
        }
        acceptorSpecified = 1;
    }

    return 0;
}


char *
newStringOfName(const gss_name_t inputName)
{
    OM_uint32 majStat, minStat;
    char *s = gfarmGssNewDisplayName(inputName, &majStat, &minStat, NULL);

    if (s != NULL) {
	return s;
    }
    fprintf(stderr, "cannot convert gss_name_t to display string:\n");
    gfarmGssPrintMajorStatus(majStat);
    gfarmGssPrintMinorStatus(minStat);
    return strdup("(invalid gss_name_t)");
}


char *
newStringOfCredential(gss_cred_id_t cred)
{
    OM_uint32 majStat, minStat;
    gss_name_t name;
    char *s;

    if (gfarmGssNewCredentialName(&name, cred, &majStat, &minStat) > 0) {
	s = newStringOfName(name);
	gfarmGssDeleteName(&name, NULL, NULL);
	return s;
    }
    fprintf(stderr, "cannot convert credential to gss_name_t:\n");
    gfarmGssPrintMajorStatus(majStat);
    gfarmGssPrintMinorStatus(minStat);
    return strdup("(invalid credential)");
}
