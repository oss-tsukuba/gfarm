/*
 * $Id$
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

char *program_name = "gfcp_hook";

void
usage()
{
	fprintf(stderr, "Usage: %s [option] <input_file> <output_file>\n",
		program_name);
	fprintf(stderr, "option:\n");
	fprintf(stderr, "\tcurrently no option supported\n");
	exit(1);
}

#define BUFSIZE	65536

int
main(int argc, char *argv[])
{
	char *input, *output;
	int ifd, ofd;

	if (argc >= 1)
		program_name = argv[0];
	--argc;
	++argv;

	if (argc == 0) {
		fprintf(stderr, "%s: missing input file name\n",
			program_name);
		exit(1);
	}
	input = argv[0];
	argc--;
	argv++;

	if (argc == 0) {
		fprintf(stderr, "%s: missing output file name\n",
			program_name);
		exit(1);
	}
	output = argv[0];
	argc--;
	argv++;

	if (argc != 0) {
		fprintf(stderr,
			"%s: currently, "
			"only one input file is supported\n",
			program_name);
		exit(1);
	}

	ofd = creat(output, 0666);
	if (ofd == -1) {
		perror(output);
		exit(1);
	}

	ifd = open(input, O_RDONLY);
	if (ifd == -1) {
		perror(input);
		exit(1);
	}

	/* copy this fragment */
	for (;;) {
		int rv, wv;
		char buffer[BUFSIZE];
		rv = read(ifd, buffer, sizeof(buffer));
		if (rv <= 0)
			break;
		wv = write(ofd, buffer, rv);
		if (wv <= 0)
			break;
	}
	if (close(ifd))
		perror(input);
	if (close(ofd))
		perror(output);

	return (0);
}
