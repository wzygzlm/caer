#include "caer-sdk/cross/portable_io.h"
#include "caer-sdk/cross/portable_time.h"
#include "caer-sdk/cross/portable_threads.h"

#include <cstring>

#if defined(OS_UNIX)
	#include <unistd.h>
	#include <pwd.h>
	#include <sys/types.h>
	#include <pthread.h>
	#include <sys/time.h>

	#if defined(OS_LINUX)
		#include <sys/prctl.h>
		#include <sys/resource.h>
	#endif
#elif defined(OS_WINDOWS)
	#define WIN32_LEAN_AND_MEAN
	#include <windows.h>
	#include <errno.h>
	#include <io.h>
#endif

char *portable_realpath(const char *path) {
#if defined(OS_UNIX)
	return (realpath(path, nullptr));
#elif defined(OS_WINDOWS)
	return (_fullpath(nullptr, path, _MAX_PATH));
#else
	#error "No portable realpath() found."
#endif
}

int portable_fsync(int fd) {
#if defined(OS_UNIX)
	return (fsync(fd));
#elif defined(OS_WINDOWS)
	return (_commit(fd));
#else
	#error "No portable fsync() found."
#endif
}

// Remember to free strings returned by this.
char *portable_userHomeDirectory(void) {
	char *homeDir = nullptr;

#if defined(OS_UNIX)
	// Unix: First check the environment for $HOME.
	char *homeVar = getenv("HOME");

	if (homeVar != nullptr) {
		homeDir = strdup(homeVar);
	}

	// Else try to get it from the user data storage.
	if (homeDir == nullptr) {
		struct passwd userPasswd;
		struct passwd *userPasswdPtr;
		char userPasswdBuf[2048];

		if (getpwuid_r(getuid(), &userPasswd, userPasswdBuf, sizeof(userPasswdBuf), &userPasswdPtr) == 0) {
			homeDir = strdup(userPasswd.pw_dir);
		}
	}

	if (homeDir == nullptr) {
		// Else just return /tmp as a place to write to.
		homeDir = strdup("/tmp");
	}
#elif defined(OS_WINDOWS)
	// Windows:
#endif

	// Check if anything worked.
	if (homeDir == nullptr) {
		return (nullptr);
	}

	char *realHomeDir = portable_realpath(homeDir);
	if (realHomeDir == nullptr) {
		free(homeDir);

		return (nullptr);
	}

	free(homeDir);

	return (realHomeDir);
}

#if defined(OS_MACOSX)
	#include <mach/mach.h>
	#include <mach/mach_time.h>
	#include <mach/clock.h>
	#include <mach/clock_types.h>
	#include <mach/mach_host.h>
	#include <mach/mach_port.h>

	bool portable_clock_gettime_monotonic(struct timespec *monoTime) {
		kern_return_t kRet;
		clock_serv_t clockRef;
		mach_timespec_t machTime;

		mach_port_t host = mach_host_self();

		kRet = host_get_clock_service(host, SYSTEM_CLOCK, &clockRef);
		mach_port_deallocate(mach_task_self(), host);

		if (kRet != KERN_SUCCESS) {
			errno = EINVAL;
			return (false);
		}

		kRet = clock_get_time(clockRef, &machTime);
		mach_port_deallocate(mach_task_self(), clockRef);

		if (kRet != KERN_SUCCESS) {
			errno = EINVAL;
			return (false);
		}

		monoTime->tv_sec  = machTime.tv_sec;
		monoTime->tv_nsec = machTime.tv_nsec;

		return (true);
	}

	bool portable_clock_gettime_realtime(struct timespec *realTime) {
		kern_return_t kRet;
		clock_serv_t clockRef;
		mach_timespec_t machTime;

		mach_port_t host = mach_host_self();

		kRet = host_get_clock_service(host, CALENDAR_CLOCK, &clockRef);
		mach_port_deallocate(mach_task_self(), host);

		if (kRet != KERN_SUCCESS) {
			errno = EINVAL;
			return (false);
		}

		kRet = clock_get_time(clockRef, &machTime);
		mach_port_deallocate(mach_task_self(), clockRef);

		if (kRet != KERN_SUCCESS) {
			errno = EINVAL;
			return (false);
		}

		realTime->tv_sec  = machTime.tv_sec;
		realTime->tv_nsec = machTime.tv_nsec;

		return (true);
	}
#elif ((defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200112L) || (defined(_XOPEN_SOURCE) && _XOPEN_SOURCE >= 600) || (defined(_WIN32) && defined(__MINGW32__)))
	bool portable_clock_gettime_monotonic(struct timespec *monoTime) {
		return (clock_gettime(CLOCK_MONOTONIC, monoTime) == 0);
	}

	bool portable_clock_gettime_realtime(struct timespec *realTime) {
		return (clock_gettime(CLOCK_REALTIME, realTime) == 0);
	}
#else
	#error "No portable way of getting absolute monotonic time found."
#endif

bool portable_thread_set_name(const char *name) {
#if defined(OS_LINUX)
	if (prctl(PR_SET_NAME, name) != 0) {
		return (false);
	}

	return (true);
#elif defined(OS_MACOSX)
	if (pthread_setname_np(name) != 0) {
		return (false);
	}

	return (true);
#else
	(void)(name); // UNUSED.

	return (false);
#endif
}

bool portable_thread_set_priority(int priority) {
#if defined(OS_LINUX)
	if (setpriority(PRIO_PROCESS, 0, priority) != 0) {
		return (false);
	}

	return (true);
#else
	(void)(priority); // UNUSED.

	return (false);
#endif
}
