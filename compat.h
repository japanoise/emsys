#ifndef COMPAT_H
#define COMPAT_H

#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#if !HAVE_GETLINE
ssize_t portable_getline(char **lineptr, size_t *n, FILE *stream);
#define getline portable_getline
#endif

#if !HAVE_GLOB_TILDE
int portable_glob_tilde_expand(const char *pattern, char **expanded);
void portable_glob_tilde_free(char *expanded);
#endif

#endif