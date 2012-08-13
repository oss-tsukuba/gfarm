#include <stddef.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfutil.h"

int
main(int argc, char *argv[]) {
	char *string;
	unsigned char *pi, *po;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s STRING\n", argv[0]);
		return (2);
	}

	string = strdup(argv[1]);
	if (string == NULL) {
		fprintf(stderr, "%s: no memory\n", argv[0]);
		return (2);
	}

	pi = (unsigned char *) string;
	po = (unsigned char *) string;
	while (*pi != '\0') {
		if (*pi == '\\') {
			/*
			 * Parse '\xHH' where H is an hexadecimal character.
			 */
			if ((*(pi + 1) == 'x' || *(pi + 1) == 'X') &&
			    isxdigit(*(pi + 2)) && isxdigit(*(pi + 3))) {
				char hexbuf[3];
				long hexval;

				hexbuf[0] = *(pi + 2);
				hexbuf[1] = *(pi + 3);
				hexbuf[2] = '\0';
				hexval = strtol(hexbuf, NULL, 16);
				*po++ = (unsigned char) hexval;
				pi += 4;
			} else {
				return (2);
			}
		} else {
			*po++ = *pi++;
		}
	}
	*po = '\0';

	if (!gfarm_utf8_validate_string(string))
		return (1);

	free(string);
	return (0);
}
