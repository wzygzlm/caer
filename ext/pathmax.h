/*
 * Make PATH_MAX available by including the right headers.
 * This makes it easier to work cross-platform, especially
 * on MacOS X where this is in a different file.
 */

#ifndef EXT_PATHMAX_H_
#define EXT_PATHMAX_H_

#include <unistd.h>
#include <limits.h>

#if defined(__APPLE__)
	#include <sys/syslimits.h>
#endif

#endif /* EXT_PATHMAX_H_ */
