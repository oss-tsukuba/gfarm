#define COMMON_OPTIONS	"p:H:M:Nn:U:u"

extern int port;

extern int acceptorSpecified;
extern gss_name_t acceptorName;

extern int HandleCommonOptions(int, char *);
extern char *newStringOfName(const gss_name_t inputName);
extern char *newStringOfCredential(gss_cred_id_t cred);
