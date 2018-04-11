#ifndef PORTABLE_MISC_H_
#define PORTABLE_MISC_H_

#include <stdlib.h>

#if defined(_BSD_SOURCE) || defined(_XOPEN_SOURCE)
	#include <unistd.h>
#elif defined(_WIN32)
	#define WIN32_LEAN_AND_MEAN
	#include <windows.h>
	#include <errno.h>
	#include <io.h>
#endif

/**
 * Fully resolve and clean up a (relative) file path.
 * What can be done depends on OS support.
 * Remember to free() the returned string after use!
 *
 * @param path a (relative) file path.
 * @return the absolute, clean file path.
 */
static inline char *portable_realpath(const char *path) {
#if defined(_BSD_SOURCE) || (defined(_XOPEN_SOURCE) && _XOPEN_SOURCE >= 500)
	return (realpath(path, NULL));
#elif defined(_WIN32)
	return (_fullpath(NULL, path, _MAX_PATH));
#else
	#error "No portable realpath() found."
#endif
}

/**
 * Synchronize a file to storage (flush all changes).
 *
 * @param fd file descroptor.
 * @return zero on success, -1 on error (errno is set).
 */
static inline int portable_fsync(int fd) {
#if defined(_BSD_SOURCE) || defined(_XOPEN_SOURCE)
	return (fsync(fd));
#elif defined(_WIN32)
	return (_commit(fd));
#else
	#error "No portable fsync() found."
#endif
}

#endif
