#define COMMON_OPTIONS	"p:H:M:Nn:U:u"

extern int port;

extern int acceptorSpecified;
extern char *acceptorNameString;
extern gss_OID acceptorNameType;

int HandleCommonOptions(int, char *);
