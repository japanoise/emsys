#ifndef EMSYS_UTIL_H
#define EMSYS_UTIL_H

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Header-only utility functions with xmalloc wrappers */

static inline void *xmalloc(size_t size) {
	void *ptr = malloc(size);
	if (!ptr && size != 0) {
		fprintf(stderr,
			"xmalloc: out of memory (allocating %zu bytes)\n",
			size);
		abort();
	}
	return ptr;
}

static inline void *xrealloc(void *ptr, size_t size) {
	void *new_ptr = realloc(ptr, size);
	if (!new_ptr && size != 0) {
		fprintf(stderr,
			"xrealloc: out of memory (allocating %zu bytes)\n",
			size);
		abort();
	}
	return new_ptr;
}

static inline char *xstrdup(const char *s) {
	size_t len = strlen(s) + 1;
	char *ptr = xmalloc(len);
	memcpy(ptr, s, len);
	return ptr;
}

static inline char *stringdup(const char *s) {
	if (!s)
		return NULL;
	return xstrdup(s);
}

#endif /* EMSYS_UTIL_H */