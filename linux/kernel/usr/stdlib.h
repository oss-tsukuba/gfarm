#ifndef _STDLIB_H_
#define _STDLIB_H_
#include <linux/kernel.h>
#include <linux/slab.h>

long int strtol(const char *nptr, char **endptr, int base);
unsigned long int strtoul(const char *nptr, char **endptr, int base);

static inline void *
calloc(size_t nmemb, size_t size)
{
	return (kzalloc((nmemb) * (size), GFP_KERNEL));
}
static inline void *
malloc(size_t size)
{
	return (kmalloc(size, GFP_KERNEL));
}
static inline void
free(void *ptr)
{
	kfree(ptr);
}
static inline void *
realloc(void *ptr, size_t size)
{
	return (krealloc(ptr, size, GFP_KERNEL));
}

static inline char *
getenv(const char *name)
{
	return (NULL);
}

static inline void
abort(void)
{
	BUG();
}
#define exit(n)	BUG()	/* conflicting types for built-in function ‘exit */

#include <linux/sched.h>
#include <linux/random.h>
#if 0
#define srand(seed)     srandom32(seed)
#define rand()  random32()
#endif
static inline void srand(unsigned seed)
{
	srandom32(seed);
}
static inline int
rand(void)
{
	return (random32());
}
#endif /* _STDLIB_H_ */

