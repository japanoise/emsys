#ifndef PLATFORM_H
#define PLATFORM_H

#ifdef __linux__
#define _GNU_SOURCE
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || \
	defined(__DragonFly__)
#define _BSD_SOURCE
#define __BSD_VISIBLE 1
#ifdef __FreeBSD__
#define __FBSDID(s)
#endif
#elif defined(__APPLE__)
#define _DARWIN_C_SOURCE
#elif defined(__MSYS__) || defined(__MINGW32__) || defined(__MINGW64__) || \
	defined(__CYGWIN__)
#define _GNU_SOURCE
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#else
#define _POSIX_C_SOURCE 200112L
#endif

#ifdef __has_builtin
#define HAS_BUILTIN(x) __has_builtin(x)
#else
#define HAS_BUILTIN(x) 0
#endif

#ifdef __has_include
#define HAS_INCLUDE(x) __has_include(x)
#else
#define HAS_INCLUDE(x) 0
#endif

#if defined(__linux__) && defined(__GLIBC__)
#define HAVE_GETLINE 1
#elif defined(__MSYS__) || defined(__MINGW32__) || defined(__MINGW64__) || \
	defined(__CYGWIN__)
#define HAVE_GETLINE 1
#else
#define HAVE_GETLINE 0
#endif

#if defined(__linux__) || defined(__GLIBC__)
#define HAVE_GLOB_TILDE 1
#elif defined(__MSYS__) || defined(__MINGW32__) || defined(__MINGW64__) || \
	defined(__CYGWIN__)
#define HAVE_GLOB_TILDE 1
#else
#define HAVE_GLOB_TILDE 0
#endif

#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || \
	defined(__DragonFly__)
#define IS_BSD 1
#else
#define IS_BSD 0
#endif

#if defined(__APPLE__)
#define IS_DARWIN 1
#else
#define IS_DARWIN 0
#endif

#if defined(__linux__)
#define IS_LINUX 1
#else
#define IS_LINUX 0
#endif

#if defined(__MSYS__) || defined(__MINGW32__) || defined(__MINGW64__) || \
	defined(__CYGWIN__)
#define IS_WINDOWS 1
#else
#define IS_WINDOWS 0
#endif

#endif