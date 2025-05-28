#include "compat.h"
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/types.h>

#if !HAVE_GETLINE
ssize_t portable_getline(char **lineptr, size_t *n, FILE *stream) {
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

	while (fgets(*lineptr, *n, stream) != NULL) {
		size_t len = strlen(*lineptr);

		if (len == 0)
			break;

		if ((*lineptr)[len - 1] == '\n')
			return len;

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
#endif

#if !HAVE_GLOB_TILDE
int portable_glob_tilde_expand(const char *pattern, char **expanded) {
	const char *home;
	struct passwd *pw;
	size_t home_len, pattern_len;

	*expanded = NULL;

	if (pattern[0] != '~')
		return 0;

	if (pattern[1] == '\0' || pattern[1] == '/') {
		home = getenv("HOME");
		if (home == NULL) {
			pw = getpwuid(getuid());
			if (pw == NULL)
				return -1;
			home = pw->pw_dir;
		}

		home_len = strlen(home);
		pattern_len = strlen(pattern + 1);

		*expanded = malloc(home_len + pattern_len + 1);
		if (*expanded == NULL)
			return -1;

		strcpy(*expanded, home);
		strcat(*expanded, pattern + 1);
		return 1;
	}

	return 0;
}

void portable_glob_tilde_free(char *expanded) {
	free(expanded);
}
#endif