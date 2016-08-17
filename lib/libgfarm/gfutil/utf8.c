/**
 * @file  utf8.c
 * @brief UTF-8 validation utility.
 */

#include <stddef.h>
#include <string.h>
#include <unistd.h>

#include "gfutil.h"

/**
 * Validate an UTF-8 byte sequences.
 *
 * @param sequences  Byte sequences.
 * @param len        Length of 'sequences'.
 * @return           Validation result.
 *
 * Return 1 if 'sequences' consists of valid UTF-8 sequences, 0 otherwise.
 */
int
gfarm_utf8_validate_sequences(const char *sequences, size_t len)
{
	const unsigned char *s = (const unsigned char *)sequences;
	unsigned long codepoint;
	unsigned long min_codepoint;
	unsigned long max_codepoint;
	int nfollows;

	while (len > 0) {
		len--;
		if (*s <= 0x7f) {
			nfollows = 0;
			min_codepoint = 0x00;
			max_codepoint = 0x7f;
			codepoint = *s & 0x7f;
		} else if (*s <= 0xbf) {
			return 0;
		} else if (*s <= 0xdf) {
			nfollows = 1;
			min_codepoint = 0x0080;
			max_codepoint = 0x07ff;
			codepoint = *s & 0x1f;
		} else if (*s <= 0xef) {
			nfollows = 2;
			min_codepoint = 0x0800;
			max_codepoint = 0xffff;
			codepoint = *s & 0x0f;
		} else if (*s <= 0xf7) {
			nfollows = 3;
			min_codepoint = 0x010000;
			max_codepoint = 0x10ffff;
			codepoint = *s & 0x07;
		} else {
			return 0;
		}

		if (len < nfollows)
			return 0;
		len -= nfollows;
		s++;

		while (nfollows > 0) {
			if (0x80 <= *s && *s <= 0xbf)
				codepoint = (codepoint << 6) | (*s & 0x3f);
			else
				return 0;
			s++;
			nfollows--;
		}

		if (codepoint < min_codepoint || codepoint > max_codepoint)
			return 0;

		/*
		 * Surrogates are not allowed.
		 */
		if (0xd800 <= codepoint && codepoint <= 0xdfff)
			return 0;
	}

	return 1;
}

/**
 * Validate an UTF-8 string.
 *
 * @param string     A NUL-terminated string.
 * @return           Validation result.
 *
 * Return 1 if 'string' is a valid UTF-8 string, 0 otherwise.
 */
int
gfarm_utf8_validate_string(const char *string)
{
	return gfarm_utf8_validate_sequences(string, strlen(string));
}
