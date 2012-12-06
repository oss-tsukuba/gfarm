#ifndef _STDIO_H_
#define _STDIO_H_

struct stdio_file {
	char *io_rptr;
	char *io_buf;
	char *io_end;
};

typedef struct stdio_file FILE;

extern FILE *fopen(const char *name, const char *mode);
extern int fclose(FILE *stream);
extern char *fgets(char *buf, int n, FILE *stream);

#ifndef EOF
# define EOF (-1)
#endif

#endif /* _STDIO_H_ */

