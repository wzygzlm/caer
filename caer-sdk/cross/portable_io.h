#ifndef CAER_SDK_PORTABLE_IO_H_
#define CAER_SDK_PORTABLE_IO_H_

#ifdef __cplusplus

#include <cstdlib>

#else

#include <stdlib.h>

#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Make PATH_MAX available by including the right headers.
 * This makes it easier to work cross-platform, especially
 * on MacOS X where this is in a different file.
 */
#include <limits.h>

#if defined(__APPLE__)
	#include <sys/syslimits.h>
#endif

#ifndef PATH_MAX
	#error "No PATH_MAX defined."
#endif

/**
 * Fully resolve and clean up a (relative) file path.
 * What can be done depends on OS support.
 * Remember to free() the returned string after use!
 *
 * @param path a (relative) file path.
 * @return the absolute, clean file path.
 */
char *portable_realpath(const char *path);

/**
 * Synchronize a file to storage (flush all changes).
 *
 * @param fd file descroptor.
 * @return zero on success, -1 on error (errno is set).
 */
int portable_fsync(int fd);

#ifdef __cplusplus
}
#endif

#endif /* CAER_SDK_PORTABLE_IO_H_ */
