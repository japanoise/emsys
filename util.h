#ifndef EMSYS_UTIL_H
#define EMSYS_UTIL_H

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>

/* Memory allocation wrappers that abort on failure */
void *xmalloc(size_t size);
void *xrealloc(void *ptr, size_t size);
void *xcalloc(size_t nmemb, size_t size);
char *xstrdup(const char *s);

/* Portable getline implementation */
ssize_t emsys_getline(char **lineptr, size_t *n, FILE *stream);

/* Safe string functions (BSD-style but portable) */
size_t emsys_strlcpy(char *dst, const char *src, size_t dsize);
size_t emsys_strlcat(char *dst, const char *src, size_t dsize);

#endif /* EMSYS_UTIL_H */
