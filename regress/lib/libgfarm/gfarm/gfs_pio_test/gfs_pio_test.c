#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <unistd.h>

#include <gfarm/gfarm.h>

/* XXX FIXME: INTERNAL FUNCTION SHOULD NOT BE USED */
#include "gfs_pio.h"	/* for gfs_pio_internal_set_view_section() */

char *program_name = "gfs_pio_test";

#define EXIT_GF_SUCCESS	0
#define EXIT_GF_INIT	1
#define EXIT_GF_OPEN	2
#define EXIT_GF_CLOSE	3
#define EXIT_GF_TERM	4
#define EXIT_GF_SECHOST	5
#define EXIT_SYS_WRITE	10
#define EXIT_SYS_READ	11
#define EXIT_GF_USAGE	250
/* the OP_* codes below are also used as EXIT_* codes */
#define OP_READ		'R'
#define OP_WRITE	'W'
#define OP_SEEK_SET	'S'
#define OP_SEEK_CUR	'C'
#define OP_SEEK_END	'E'
#define OP_TRUNCATE	'T'
#define OP_PAUSE	'P'
#define OP_FLUSH	'F'
#define OP_SYNC		'M'
#define OP_DATASYNC	'D'
#define OP_STAT		'Q'		/* Query Stat */
#define OP_READALL	'I'		/* Input */
#define OP_WRITEALL	'O'		/* Output */
#define OP_RECVFILE	'A'		/* Acquire */
#define OP_SENDFILE	'Y'		/* Yield */
/* unused opcodes: BGHJKLNUVXZ */

struct op {
	unsigned char op;
	/* for OP_READ, OP_WRITE, OP_SEEK_*, OP_TRUNCATE, OP_PAUSE */
	gfarm_off_t off, off2, off3;
};

#define MAX_OPS	1024

/* should be > GFARM_CLIENT_FILE_BUFSIZE_DEFAULT */
#define IO_BUFSIZE (10*1024*1024)
char buffer[IO_BUFSIZE];

struct op ops[MAX_OPS];
int nops = 0;

void
usage(void)
{
	fprintf(stderr, "Usage: %s [-"
#ifdef GFARM_FILE_APPEND
		"a"
#endif
		"c"
#ifdef GFARM_FILE_EXCLUSIVE
		"e"
#endif
		"rtw] [-h <host>] [-m <mode>] "
		"[-A r_off/w_off/len] "
		"[-Y w_off/r_off/len] "
		"<filename>\n",
	    program_name);
	exit(EXIT_GF_USAGE);
}

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	GFS_File gf;
	int do_create = 0, verbose = 0, c, i, rv, done, m;
	int flags = GFARM_FILE_RDWR;
	gfarm_mode_t mode = 0666;
	gfarm_off_t off, off2, off3, roff;
	struct gfs_stat gst;
	char *s, *host = NULL;

	if (argc > 0)
		program_name = basename(argv[0]);

	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfarm_initialize: %s\n",
		    gfarm_error_string(e));
		return (EXIT_GF_INIT);
	}

	while ((c = getopt(argc, argv, "aA:cC:DeE:Fh:Im:MnOP:QrR:S:tT:vwW:Y:"))
	    != -1) {
		off = off2 = off3 = -1;
		switch (c) {
		case OP_RECVFILE:
		case OP_SENDFILE:
			s = strchr(optarg, ',');
			if (s != NULL) {
				off2 = strtol(s + 1, NULL, 0);
				s = strchr(s + 1, ',');
				if (s != NULL)
					off3 = strtol(s + 1, NULL, 0);
			}
			/*FALLTHROUGH*/
		case OP_READ:
		case OP_WRITE:
		case OP_SEEK_SET:
		case OP_SEEK_CUR:
		case OP_SEEK_END:
		case OP_TRUNCATE:
		case OP_PAUSE:
			off = strtol(optarg, NULL, 0);
			/*FALLTHROUGH*/
		case OP_FLUSH:
		case OP_SYNC:
		case OP_DATASYNC:
		case OP_STAT:
		case OP_READALL:
		case OP_WRITEALL:
			if (nops >= MAX_OPS) {
				fprintf(stderr,
				    "%s: number of operations reaches "
				    "its limit (%d)\n", program_name, MAX_OPS);
				usage();
			}
			ops[nops].op = c;
			ops[nops].off = off;
			ops[nops].off2 = off2;
			ops[nops].off3 = off3;
			++nops;
			break;
#ifdef GFARM_FILE_APPEND
		case 'a':
			flags |= GFARM_FILE_APPEND;
			break;
#endif
		case 'c':
			do_create = 1;
			break;
#ifdef GFARM_FILE_EXCLUSIVE
		case 'e':
			flags |= GFARM_FILE_EXCLUSIVE;
			break;
#endif
		case 'h':
			host = optarg;
			break;
		case 'm':
			mode = strtol(optarg, NULL, 0);
			break;
		case 'r':
			flags = (flags & ~GFARM_FILE_ACCMODE)|GFARM_FILE_RDONLY;
			break;
		case 't':
			flags |= GFARM_FILE_TRUNC;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'w':
			flags = (flags & ~GFARM_FILE_ACCMODE)|GFARM_FILE_WRONLY;
			break;
		default:
			fprintf(stderr, "%s: unknown option -%c\n",
			    program_name, c);
			usage();
		}
       }
	argc -= optind;
	argv += optind;
	if (argc != 1) {
		fprintf(stderr, "%s: %s <filename>\n", program_name,
		    argc == 0 ? "missing" : "extra arguments after");
		usage();
	}

	if (do_create) {
		if (verbose)
			fprintf(stderr, "gfs_pio_create(\"%s\", 0x%x, 0%o)\n",
			    argv[0], flags, (int)mode);
		e = gfs_pio_create(argv[0], flags, mode, &gf);
	} else {
		if (verbose)
			fprintf(stderr, "gfs_pio_open(\"%s\", 0x%x)\n",
			    argv[0], flags);
		e = gfs_pio_open(argv[0], flags, &gf);
	}
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n",
		    do_create ? "gfs_pio_create" : "gfs_pio_open",
		    gfarm_error_string(e));
		return (EXIT_GF_OPEN);
	}
	if (host != NULL) {
		/* XXX FIXME: INTERNAL FUNCTION SHOULD NOT BE USED */
		e = gfs_pio_internal_set_view_section(gf, host);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr,
			    "gfs_pio_internal_set_view_section(%s): %s\n",
			    host, gfarm_error_string(e));
			return (EXIT_GF_SECHOST);
		}
	}
	for (i = 0; i < nops; i++) {
		c = ops[i].op;
		off = ops[i].off;
		off2 = ops[i].off2;
		off3 = ops[i].off3;
		switch (c) {
		case OP_READ:
			if (off > sizeof buffer)
				off = sizeof buffer;
			e = gfs_pio_read(gf, buffer, (int)off, &rv);
			if (e != GFARM_ERR_NO_ERROR) {
				fprintf(stderr, "gfs_pio_read: %s\n",
				    gfarm_error_string(e));
				return (c);
			}
			if (verbose)
				fprintf(stderr, "gfs_pio_read(%d): %d\n",
				    (int)off, rv);
			for (done = 0; done < rv; done += off) {
				off = write(1, buffer + done, rv - done);
				if (off == -1) {
					if (errno == EINTR)
						continue;
					perror("write");
					return (EXIT_SYS_WRITE);
				}
				if (verbose)
					fprintf(stderr, "write(%d): %d\n",
					    rv - done, (int)off);
			}
			break;
		case OP_WRITE:
			if (off > sizeof buffer)
				off = sizeof buffer;
			for (done = 0; done < off; done += rv) {
				rv = read(0, buffer + done,
				    (size_t)(off - done));
				if (rv == -1) {
					if (errno == EINTR)
						continue;
					perror("read");
					return (EXIT_SYS_READ);
				}
				if (verbose)
					fprintf(stderr, "read(%d): %d\n",
					    (int)(off - done), rv);
				if (rv == 0)
					break;
			}
			if (done == 0)
				break;
			e = gfs_pio_write(gf, buffer, done, &rv);
			if (e != GFARM_ERR_NO_ERROR) {
				fprintf(stderr, "gfs_pio_write: %s\n",
				    gfarm_error_string(e));
				return (c);
			}
			if (verbose)
				fprintf(stderr, "gfs_pio_write(%d): %d\n",
				    done, rv);
			break;
		case OP_SEEK_SET:
		case OP_SEEK_CUR:
		case OP_SEEK_END:
			switch (c) {
			case OP_SEEK_SET: m = GFARM_SEEK_SET; s = "SET"; break;
			case OP_SEEK_CUR: m = GFARM_SEEK_CUR; s = "CUR"; break;
			case OP_SEEK_END: m = GFARM_SEEK_END; s = "END"; break;
			default:
				m = 0; s = NULL; /* shut up gcc warnings */
				assert(0);
			}
			e = gfs_pio_seek(gf, off, m, &roff);
			if (e != GFARM_ERR_NO_ERROR) {
				fprintf(stderr,
				    "gfs_pio_seek(GFARM_SEEK_%s): %s\n",
				    s, gfarm_error_string(e));
				return (c);
			}
			if (verbose)
				fprintf(stderr,
				    "gfs_pio_seek(%lld, %s%s): %lld\n",
				    (long long)off, "GFARM_SEEK_", s,
				    (long long)roff);
			break;
		case OP_TRUNCATE:
			e = gfs_pio_truncate(gf, off);
			if (e != GFARM_ERR_NO_ERROR) {
				fprintf(stderr,
				    "gfs_pio_truncate: %s\n",
				    gfarm_error_string(e));
				return (c);
			}
			if (verbose)
				fprintf(stderr, "gfs_pio_truncate(%lld)",
				    (long long)off);
			break;
		case OP_PAUSE:
			rv = sleep((unsigned int)off);
			if (verbose)
				fprintf(stderr,
				    "sleep(%d): slept %d seconds\n",
				    (int)off, rv - (int)off);
			break;
		case OP_FLUSH:
			e = gfs_pio_flush(gf);
			if (e != GFARM_ERR_NO_ERROR) {
				fprintf(stderr, "gfs_pio_flush: %s\n",
				    gfarm_error_string(e));
				return (c);
			}
			if (verbose)
				fprintf(stderr, "gfs_pio_flush()\n");
			break;
		case OP_SYNC:
			e = gfs_pio_sync(gf);
			if (e != GFARM_ERR_NO_ERROR) {
				fprintf(stderr, "gfs_pio_sync: %s\n",
				    gfarm_error_string(e));
				return (c);
			}
			if (verbose)
				fprintf(stderr, "gfs_pio_sync()\n");
			break;
		case OP_DATASYNC:
			e = gfs_pio_datasync(gf);
			if (e != GFARM_ERR_NO_ERROR) {
				fprintf(stderr, "gfs_pio_datasync: %s\n",
				    gfarm_error_string(e));
				return (c);
			}
			if (verbose)
				fprintf(stderr, "gfs_pio_datasync()\n");
			break;
		case OP_STAT:
			e = gfs_pio_stat(gf, &gst);
			if (e != GFARM_ERR_NO_ERROR) {
				fprintf(stderr, "gfs_pio_stat: %s\n",
				    gfarm_error_string(e));
				return (c);
			}
			if (verbose)
				fprintf(stderr, "gfs_pio_stat()\n");
			break;
		case OP_READALL:
			roff = 0;
			for (;;) {
				e = gfs_pio_read(gf, buffer, sizeof buffer,
				    &rv);
				if (e != GFARM_ERR_NO_ERROR) {
					fprintf(stderr, "gfs_pio_read: %s\n",
					    gfarm_error_string(e));
					return (c);
				}
				if (rv == 0)
					break;
				roff += (off_t) rv;
				off = write(1, buffer, rv);
				if (off == -1) {
					perror("write");
					return (EXIT_SYS_WRITE);
				}
			}
			if (verbose)
				fprintf(stderr, "write(%lld)\n",
				    (long long)roff);
			break;
		case OP_WRITEALL:
			roff = 0;
			for (;;) {
				rv = read(0, buffer, sizeof buffer);
				if (rv == -1) {
					perror("read");
					return (EXIT_SYS_READ);
				}
				if (rv == 0)
					break;
				off = rv;
				e = gfs_pio_write(gf, buffer, (int)off, &rv);
				if (e != GFARM_ERR_NO_ERROR) {
					fprintf(stderr, "gfs_pio_write: %s\n",
					    gfarm_error_string(e));
					return (c);
				}
				roff += (off_t) rv;
			}
			if (verbose)
				fprintf(stderr, "read(%lld)\n",
				    (long long)roff);
			break;
		case OP_RECVFILE:
			e = gfs_pio_recvfile(gf, off,
			    STDOUT_FILENO, off2, off3, &roff);
			if (e != GFARM_ERR_NO_ERROR) {
				fprintf(stderr, "gfs_pio_recvfile(, "
				    "r_off:%lld, %d, w_off:%lld, len:%lld, ): "
				    "%s\n", (long long)off, STDOUT_FILENO,
				    (long long)off2, (long long)off3,
				    gfarm_error_string(e));
				return (c);
			}
			if (verbose)
				fprintf(stderr, "gfs_pio_recvfile(, "
				    "r_off:%lld, %d, w_off:%lld, len:%lld, ): "
				    "%lld\n", (long long)off, STDOUT_FILENO,
				    (long long)off2, (long long)off3,
				    (long long)roff);
			break;
		case OP_SENDFILE:
			e = gfs_pio_sendfile(gf, off,
			    STDIN_FILENO, off2, off3, &roff);
			if (e != GFARM_ERR_NO_ERROR) {
				fprintf(stderr, "gfs_pio_sendfile(, "
				    "w_off:%lld, %d, r_off:%lld, len:%lld, ): "
				    "%s\n", (long long)off, STDIN_FILENO,
				    (long long)off2, (long long)off3,
				    gfarm_error_string(e));
				return (c);
			}
			if (verbose)
				fprintf(stderr, "gfs_pio_sendfile(, "
				    "w_off:%lld, %d, r_off:%lld, len:%lld, ): "
				    "%lld\n", (long long)off, STDIN_FILENO,
				    (long long)off2, (long long)off3,
				    (long long)roff);
			break;
		default:
			assert(0);
		}
	}
	e = gfs_pio_close(gf);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfs_pio_close: %s\n",
		    gfarm_error_string(e));
		return (EXIT_GF_CLOSE);
	}

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfarm_terminate: %s\n",
		    gfarm_error_string(e));
		return (EXIT_GF_TERM);
	}
	return (EXIT_GF_SUCCESS);
}
