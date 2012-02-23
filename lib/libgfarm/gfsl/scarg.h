#define COMMON_OPTIONS	"p:S:H:M:Nn:U:u"

extern int port;

extern int acceptorSpecified;
extern gss_name_t acceptorName;

extern char *seviceName;
extern char *hostName;

extern int HandleCommonOptions(int, char *);
extern char *newStringOfName(const gss_name_t inputName);
extern char *newStringOfCredential(gss_cred_id_t cred);
