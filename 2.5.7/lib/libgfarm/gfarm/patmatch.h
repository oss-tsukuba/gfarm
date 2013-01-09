#define GFARM_PATTERN_NOESCAPE	0x01
#define GFARM_PATTERN_PATHNAME	0x02

int gfarm_pattern_charset_parse(const char *, int, int *);
int gfarm_pattern_submatch(const char *, int, const char *, int);
int gfarm_pattern_match(const char *, const char *, int);
