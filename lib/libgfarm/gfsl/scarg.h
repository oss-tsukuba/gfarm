#define COMMON_OPTIONS	"p:H:M:Nn:U:u"

extern int port;

extern int acceptorSpecified;
extern gss_name_t acceptorName;

int HandleCommonOptions(int, char *);
