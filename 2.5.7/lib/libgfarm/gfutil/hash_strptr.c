#include <string.h>
#include <ctype.h>

#include "hash.h"

/*
 * hash functions for pointer to (char *),
 */

int
gfarm_hash_strptr(const void *key, int keylen)
{
	const char *const *strptr = key;
	const char *str = *strptr;

	return (gfarm_hash_default(str, strlen(str)));
}

int
gfarm_hash_key_equal_strptr(
	const void *key1, int key1len,
	const void *key2, int key2len)
{
	const char *const *strptr1 = key1, *const *strptr2 = key2;
	const char *str1 = *strptr1, *str2 = *strptr2;
	int len1, len2;

	/* compare first character of the strings, short-cut in most cases. */
	if (*str1 != *str2)
		return (0);
	len1 = strlen(str1);
	len2 = strlen(str2);
	if (len1 != len2)
		return (0);

	return (gfarm_hash_key_equal_default(str1, len1, str2, len2));
}

/*
 * hash functions for pointer to case-folded (char *),
 */

int
gfarm_hash_casefold_strptr(const void *key, int keylen)
{
	const char *const *strptr = key;
	const char *str = *strptr;

	return (gfarm_hash_casefold(str, strlen(str)));
}

int
gfarm_hash_key_equal_casefold_strptr(
	const void *key1, int key1len,
	const void *key2, int key2len)
{
	const char *const *strptr1 = key1, *const *strptr2 = key2;
	const char *str1 = *strptr1, *str2 = *strptr2;
	int len1, len2;

	/* compare first character of the strings, short-cut in most cases. */
	if (tolower(*(unsigned char *)str1) != tolower(*(unsigned char *)str2))
		return (0);
	len1 = strlen(str1);
	len2 = strlen(str2);
	if (len1 != len2)
		return (0);

	return (gfarm_hash_key_equal_casefold(str1, len1, str2, len2));
}

