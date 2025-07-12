#include "util.h"
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/types.h>

void *xmalloc(size_t size) {
	void *ptr = malloc(size);
	if (!ptr && size != 0) {
		fprintf(stderr,
			"xmalloc: out of memory (allocating %zu bytes)\n",
			size);
		abort();
	}
	return ptr;
}

void *xrealloc(void *ptr, size_t size) {
	void *new_ptr = realloc(ptr, size);
	if (!new_ptr && size != 0) {
		fprintf(stderr,
			"xrealloc: out of memory (allocating %zu bytes)\n",
			size);
		abort();
	}
	return new_ptr;
}

void *xcalloc(size_t nmemb, size_t size) {
	void *ptr = calloc(nmemb, size);
	if (!ptr && nmemb != 0 && size != 0) {
		fprintf(stderr,
			"xcalloc: out of memory (allocating %zu * %zu bytes)\n",
			nmemb, size);
		abort();
	}
	return ptr;
}

char *xstrdup(const char *s) {
	size_t len = strlen(s) + 1;
	char *ptr = xmalloc(len);
	memcpy(ptr, s, len);
	return ptr;
}

ssize_t emsys_getline(char **lineptr, size_t *n, FILE *stream) {
	char *ptr, *eptr;

	if (lineptr == NULL || n == NULL) {
		errno = EINVAL;
		return -1;
	}

	if (ferror(stream))
		return -1;

	if (feof(stream))
		return -1;

	if (*lineptr == NULL || *n == 0) {
		*n = 120;
		*lineptr = malloc(*n);
		if (*lineptr == NULL)
			return -1;
	}

	(*lineptr)[0] = '\0';

	/* Read first chunk */
	if (fgets(*lineptr, *n, stream) == NULL) {
		return -1;
	}

	/* Keep reading until we get a newline or EOF */
	while (1) {
		size_t len = strlen(*lineptr);

		if (len == 0)
			break;

		if ((*lineptr)[len - 1] == '\n')
			return len;

		/* Line doesn't end with newline, need to grow buffer and read more */
		*n *= 2;
		ptr = realloc(*lineptr, *n);
		if (ptr == NULL)
			return -1;
		*lineptr = ptr;

		eptr = *lineptr + len;
		if (fgets(eptr, *n - len, stream) == NULL)
			break;
	}

	return (*lineptr)[0] != '\0' ? (ssize_t)strlen(*lineptr) : -1;
}

size_t emsys_strlcpy(char *dst, const char *src, size_t dsize) {
	const char *osrc = src;
	size_t nleft = dsize;

	/* Copy as many bytes as will fit. */
	if (nleft != 0) {
		while (--nleft != 0) {
			if ((*dst++ = *src++) == '\0')
				break;
		}
	}

	/* Not enough room in dst, add NUL and traverse rest of src. */
	if (nleft == 0) {
		if (dsize != 0)
			*dst = '\0'; /* NUL-terminate dst */
		while (*src++)
			;
	}

	return (src - osrc - 1); /* count does not include NUL */
}

size_t emsys_strlcat(char *dst, const char *src, size_t dsize) {
	const char *odst = dst;
	const char *osrc = src;
	size_t n = dsize;
	size_t dlen;

	/* Find the end of dst and adjust bytes left but don't go past end. */
	while (n-- != 0 && *dst != '\0')
		dst++;
	dlen = dst - odst;
	n = dsize - dlen;

	if (n-- == 0)
		return (dlen + strlen(src));

	while (*src != '\0') {
		if (n != 0) {
			*dst++ = *src;
			n--;
		}
		src++;
	}
	*dst = '\0';

	return (dlen + (src - osrc)); /* count does not include NUL */
}
