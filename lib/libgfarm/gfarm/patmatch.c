#include <stddef.h>
#include <string.h>

#include "patmatch.h"

int
gfarm_pattern_charset_parse(const char *pattern, int index, int *ip)
{
	int i = index;

	if (pattern[i] == '!')
		i++;
	if (pattern[i] != '\0') {
		if (pattern[i + 1] == '-' && pattern[i + 2] != '\0')
			i += 3;
		else
			i++;
	}
	while (pattern[i] != ']') {
		if (pattern[i] == '\0') {
			/* end of charset isn't found */
			if (ip != NULL)
				*ip = index;
			return (0);
		}
		if (pattern[i + 1] == '-' && pattern[i + 2] != '\0')
			i += 3;
		else
			i++;
	}
	if (ip != NULL)
		*ip = i;
	return (1);
}

static int
gfarm_pattern_charset_match(const char *pattern, int pattern_length, int ch)
{
	int i = 0, negate = 0;
	unsigned char c = ch, *p = (unsigned char *)pattern;

	if (p[i] == '!') {
		negate = 1;
		i++;
	}
	while (i < pattern_length) {
		if (p[i + 1] == '-' && p[i + 2] != '\0') {
			if (p[i] <= c && c <= p[i + 2])
				return (!negate);
			i += 3;
		} else {
			if (c == p[i])
				return (!negate);
			i++;
		}
	}
	return (negate);
}

static int
gfarm_name_submatch(const char *pattern, const char *name, int namelen,
	int flags)
{
	int w;

	for (; --namelen >= 0; name++, pattern++) {
		if (*pattern == '?')
			continue;
		if (*pattern == '[' &&
		    gfarm_pattern_charset_parse(pattern, 1, &w)) {
			if (gfarm_pattern_charset_match(pattern + 1, w - 1,
			    *(unsigned char *)name)) {
				pattern += w;
				continue;
			}
			return (0);
		}
		if ((flags & GFARM_PATTERN_NOESCAPE) == 0 && *pattern == '\\') {
			if (pattern[1] != '\0' &&
			    ((flags & GFARM_PATTERN_PATHNAME) == 0 ||
			     pattern[1] != '/')) {
				if (*name == pattern[1]) {
					pattern++;
					continue;
				}
			}
		}
		if (*name != *pattern)
			return (0);
	}
	return (1);
}

static int
gfarm_pattern_prefix_length_to_asterisk(
	const char *pattern, int pattern_length, int flags,
	const char **asterisk)
{
	int i, length = 0;

	for (i = 0; i < pattern_length; length++, i++) {
		if ((flags & GFARM_PATTERN_NOESCAPE) == 0 &&
		    pattern[i] == '\\') {
			if (i + 1 < pattern_length &&
			    ((flags & GFARM_PATTERN_PATHNAME) == 0 ||
			     pattern[i + 1] != '/'))
				i++;
		} else if (pattern[i] == '*') {
			*asterisk = &pattern[i];
			return (length);
		} else if (pattern[i] == '[') {
			gfarm_pattern_charset_parse(pattern, i + 1, &i);
		}
	}
	*asterisk = &pattern[i];
	return (length);
}

int
gfarm_pattern_submatch(const char *pattern, int pattern_length,
	const char *name, int flags)
{
	const char *asterisk;
	int residual = strlen(name);
	int sublen = gfarm_pattern_prefix_length_to_asterisk(
	    pattern, pattern_length, flags, &asterisk);

	if (residual < sublen ||
	    !gfarm_name_submatch(pattern, name, sublen, flags))
		return (0);
	if (*asterisk == '\0')
		return (residual == sublen);
	for (;;) {
		name += sublen; residual -= sublen;
		pattern_length -= asterisk + 1 - pattern;
		pattern = asterisk + 1;
		sublen = gfarm_pattern_prefix_length_to_asterisk(pattern,
		    pattern_length, flags, &asterisk);
		if (*asterisk == '\0')
			break;
		for (;; name++, --residual) {
			if (residual < sublen)
				return (0);
			if (gfarm_name_submatch(pattern, name, sublen, flags))
				break;
		}
	}
	return (residual >= sublen &&
	    gfarm_name_submatch(pattern, name + residual - sublen, sublen,
	    flags));
}

int
gfarm_pattern_match(const char *pattern, const char *name, int flags)
{
	return (gfarm_pattern_submatch(pattern, strlen(pattern), name, flags));
}
