# Init CMake (require at least version 2.6)
CMAKE_MINIMUM_REQUIRED(VERSION 2.6)

# General build settings
IF (NOT CMAKE_BUILD_TYPE)
	SET(CMAKE_BUILD_TYPE "Release" CACHE STRING "Possible build types: None Debug Release RelWithDebInfo MinSizeRel")
ENDIF()

IF (NOT USE_TCMALLOC)
	SET(USE_TCMALLOC 0 CACHE BOOL "Link to and use TCMalloc (Google Perftools) to provide faster memory allocation")
ENDIF()

# Project name and version
PROJECT(cAER C CXX)
SET(PROJECT_VERSION_MAJOR 1)
SET(PROJECT_VERSION_MINOR 1)
SET(PROJECT_VERSION_PATCH 0)
SET(PROJECT_VERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH})

# Define installation paths.
INCLUDE(GNUInstallDirs)

# Set compiler info
SET(CC_CLANG FALSE)
SET(CC_GCC FALSE)
SET(CC_ICC FALSE)
SET(CC_MSVC FALSE)

IF ("${CMAKE_C_COMPILER_ID}" STREQUAL "Clang")
	SET(CC_CLANG TRUE)
ELSEIF ("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU")
	SET(CC_GCC TRUE)
ELSEIF ("${CMAKE_C_COMPILER_ID}" STREQUAL "Intel")
	SET(CC_ICC TRUE)
ELSEIF ("${CMAKE_C_COMPILER_ID}" STREQUAL "MSVC")
	SET(CC_MSVC TRUE)
ENDIF()

# Set operating system info
SET(OS_UNIX FALSE)
SET(OS_LINUX FALSE)
SET(OS_MACOSX FALSE)
SET(OS_WINDOWS FALSE)

IF (UNIX)
	SET(OS_UNIX TRUE)
	ADD_DEFINITIONS(-DOS_UNIX=1)
ENDIF()

IF (UNIX AND "${CMAKE_SYSTEM_NAME}" MATCHES "Linux")
	SET(OS_LINUX TRUE)
	ADD_DEFINITIONS(-DOS_LINUX=1)
ENDIF()

IF (UNIX AND APPLE AND "${CMAKE_SYSTEM_NAME}" MATCHES "Darwin")
	SET(OS_MACOSX TRUE)
	ADD_DEFINITIONS(-DOS_MACOSX=1)
ENDIF()

IF (WIN32 AND "${CMAKE_SYSTEM_NAME}" MATCHES "Windows")
	SET(OS_WINDOWS TRUE)
	ADD_DEFINITIONS(-DOS_WINDOWS=1)
ENDIF()

# Check GCC compiler version, 4.9 is needed at least for atomics support.
IF (CC_GCC)
	IF (${CMAKE_C_COMPILER_VERSION} VERSION_LESS "5.2.0")
		MESSAGE(FATAL_ERROR "GCC version found is ${CMAKE_C_COMPILER_VERSION}. Required >= 5.2.0.")
	ENDIF()
ENDIF()

# Check Clang compiler version, 3.6 is needed at least for atomics support.
IF (CC_CLANG)
	IF (${CMAKE_C_COMPILER_VERSION} VERSION_LESS "3.6.0")
		MESSAGE(FATAL_ERROR "Clang version found is ${CMAKE_C_COMPILER_VERSION}. Required >= 3.6.0.")
	ENDIF()
ENDIF()

# Test if we are on a big-endian architecture
INCLUDE(TestBigEndian)
TEST_BIG_ENDIAN(SYSTEM_BIGENDIAN)

# Check size of various types
INCLUDE(CheckTypeSize)
CHECK_TYPE_SIZE("size_t" SIZEOF_SIZE_T)
CHECK_TYPE_SIZE("void *" SIZEOF_VOID_PTR)

IF (NOT "${SIZEOF_VOID_PTR}" STREQUAL "${SIZEOF_SIZE_T}")
	MESSAGE(FATAL_ERROR "Size of void * and size_t must be the same!")
ENDIF()

# Check threads support (almost nobody implements C11 threads yet!)
FIND_PACKAGE(Threads)
SET(HAVE_PTHREADS FALSE)
SET(HAVE_WIN32_THREADS FALSE)

IF (DEFINED "CMAKE_USE_PTHREADS_INIT")
	IF (${CMAKE_USE_PTHREADS_INIT})
		SET(HAVE_PTHREADS TRUE)
		ADD_DEFINITIONS(-DHAVE_PTHREADS=1)
	ENDIF()
ENDIF()

IF (DEFINED "CMAKE_USE_WIN32_THREADS_INIT")
	IF (${CMAKE_USE_WIN32_THREADS_INIT})
		SET(HAVE_WIN32_THREADS TRUE)
		ADD_DEFINITIONS(-DHAVE_WIN32_THREADS=1)
	ENDIF()
ENDIF()

# Add system defines for header features
IF (OS_UNIX AND HAVE_PTHREADS)
	# POSIX system (Unix, Linux, MacOS X)
	ADD_DEFINITIONS(-D_XOPEN_SOURCE=700)
	ADD_DEFINITIONS(-D_DEFAULT_SOURCE=1)

	IF (OS_MACOSX)
		ADD_DEFINITIONS(-D_DARWIN_C_SOURCE=1)
	ENDIF()

	# Support for large files (>2GB) on 32-bit systems
	ADD_DEFINITIONS(-D_FILE_OFFSET_BITS=64)
ENDIF()

IF (OS_WINDOWS AND (CC_GCC OR CC_CLANG))
	ADD_DEFINITIONS(-D__USE_MINGW_ANSI_STDIO=1)
ENDIF()

# Add definitions to recover CMake binary dir and system dirs.
SET(CM_BUILD_DIR ${CMAKE_BINARY_DIR})
SET(CM_SHARE_DIR ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATAROOTDIR}/caer)
ADD_DEFINITIONS(-DCM_BUILD_DIR=${CM_BUILD_DIR} -DCM_SHARE_DIR=${CM_SHARE_DIR})

# C11 standard needed (atomics, threads)
IF (CC_GCC OR CC_CLANG)
	SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c11")
	SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14")
ENDIF()

# Enable all warnings for GCC / Clang
IF (CC_GCC OR CC_CLANG)
	SET(WARN_COMMON_FLAGS "-pedantic -Wall -Wextra")
	SET(WARN_C_FLAGS "")
	SET(WARN_CXX_FLAGS "")

	IF (CC_GCC)
		# Enable all useful warnings in GCC one-by-one.
		SET(WARN_COMMON_FLAGS "${WARN_COMMON_FLAGS} -Wunused -Wundef -Wformat=2 -Winit-self -Wuninitialized")
		SET(WARN_COMMON_FLAGS "${WARN_COMMON_FLAGS} -Wpointer-arith -Wcast-qual -Wcast-align -Wwrite-strings")
		SET(WARN_COMMON_FLAGS "${WARN_COMMON_FLAGS} -Wredundant-decls -Wmissing-declarations -Wstack-protector")
		SET(WARN_COMMON_FLAGS "${WARN_COMMON_FLAGS} -Wshadow -Wfloat-equal -Wconversion -Wstrict-overflow=5")
		SET(WARN_COMMON_FLAGS "${WARN_COMMON_FLAGS} -Wdouble-promotion")

		SET(WARN_C_FLAGS "${WARN_C_FLAGS} -Wstrict-prototypes -Wmissing-prototypes -Wnested-externs")
		SET(WARN_C_FLAGS "${WARN_C_FLAGS} -Wbad-function-cast -Wjump-misses-init -Wunsuffixed-float-constants")
	ENDIF()

	IF (CC_CLANG)
		# Enable all warnings in Clang, then turn off useless ones.
		SET(WARN_COMMON_FLAGS "${WARN_COMMON_FLAGS} -Weverything -Wno-packed -Wno-padded -Wno-unreachable-code-break")
		SET(WARN_COMMON_FLAGS "${WARN_COMMON_FLAGS} -Wno-disabled-macro-expansion -Wno-reserved-id-macro -Wno-vla")
		SET(WARN_COMMON_FLAGS "${WARN_COMMON_FLAGS} -Wno-parentheses-equality -Wno-covered-switch-default")
		SET(WARN_COMMON_FLAGS "${WARN_COMMON_FLAGS} -Wno-used-but-marked-unused -Wno-cast-align")

		SET(WARN_CXX_FLAGS "${WARN_CXX_FLAGS} -Wno-c++98-compat -Wno-c++98-compat-pedantic -Wno-old-style-cast")
		SET(WARN_CXX_FLAGS "${WARN_CXX_FLAGS} -Wno-global-constructors -Wno-exit-time-destructors")
	ENDIF()

	# Apply all flags.
	SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${WARN_COMMON_FLAGS} ${WARN_C_FLAGS}")
	SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${WARN_COMMON_FLAGS} ${WARN_CXX_FLAGS}")
ENDIF()

# Search for external libraries with pkg-config.
INCLUDE(FindPkgConfig)

# Basic device and threads support.
PKG_CHECK_MODULES(LIBCAER REQUIRED libcaer>=2.4.0)

SET(CAER_INCDIRS ${LIBCAER_INCLUDE_DIRS})
SET(CAER_LIBDIRS ${LIBCAER_LIBRARY_DIRS})

SET(CAER_C_LIBS ${LIBCAER_LIBRARIES})
SET(CAER_C_SRC_FILES "")

SET(CAER_CXX_LIBS ${LIBCAER_LIBRARIES})
SET(CAER_CXX_SRC_FILES "")

# Threads support
SET(CAER_C_LIBS ${CAER_C_LIBS} ${CMAKE_THREAD_LIBS_INIT})
SET(CAER_CXX_LIBS ${CAER_CXX_LIBS} ${CMAKE_THREAD_LIBS_INIT})

# Boost support for C++
FIND_PACKAGE(Boost 1.50 REQUIRED COMPONENTS system filesystem iostreams program_options)

SET(CAER_INCDIRS ${CAER_INCDIRS} ${Boost_INCLUDE_DIRS})
SET(CAER_LIBDIRS ${CAER_LIBDIRS} ${Boost_LIBRARY_DIRS})
SET(CAER_CXX_LIBS ${CAER_CXX_LIBS} ${Boost_LIBRARIES})

IF ((NOT (Boost_MAJOR_VERSION EQUAL 1 AND Boost_MINOR_VERSION GREATER_EQUAL 61))
	OR (OS_UNIX OR OS_LINUX OR OS_MACOSX))
	# libdl needed if Boost too old to have Boost.DLL as a replacement,
	# or for Boost.DLL itself on some platforms like Linux and MacOS X.
	SET(CAER_CXX_LIBS ${CAER_CXX_LIBS} dl)
ENDIF()

# Windows boost.ASIO needs extra libraries
IF (OS_WINDOWS)
	SET(CAER_CXX_LIBS ${CAER_CXX_LIBS} wsock32 ws2_32)
ENDIF()

# Set full RPATH
SET(CMAKE_INSTALL_RPATH ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR})
SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
IF (OS_MACOSX)
	SET(CMAKE_MACOSX_RPATH TRUE)
ENDIF()

IF (OS_UNIX AND NOT OS_MACOSX)
	# Add --as-needed to linker flags for executables.
	SET(CMAKE_EXE_LINKER_FLAGS "-Wl,--as-needed")

	# Add --as-needed to linker flags for libraries.
	SET(CMAKE_SHARED_LINKER_FLAGS "-Wl,--as-needed")
ENDIF()

# Add math support
SET(CAER_C_LIBS ${CAER_C_LIBS} m)

# Add realtime support, not needed on MacOS X or Windows.
IF (OS_UNIX AND NOT OS_MACOSX)
	SET(CAER_C_LIBS ${CAER_C_LIBS} rt)
ENDIF()

# TCMalloc support (fast memory allocation)
IF (USE_TCMALLOC)
	PKG_CHECK_MODULES(LIBTCMALLOC REQUIRED libtcmalloc>=2.2)

	SET(CAER_INCDIRS ${CAER_INCDIRS} ${LIBTCMALLOC_INCLUDE_DIRS})
	SET(CAER_LIBDIRS ${CAER_LIBDIRS} ${LIBTCMALLOC_LIBRARY_DIRS})

	SET(CAER_C_LIBS ${CAER_C_LIBS} ${LIBTCMALLOC_LIBRARIES})
ENDIF()

# Add local directory to include and library paths
SET(CAER_INCDIRS ${CAER_INCDIRS} ${CMAKE_SOURCE_DIR}/)
SET(CAER_LIBDIRS ${CAER_LIBDIRS} ${CMAKE_SOURCE_DIR}/)

# Windows needs extra linker information for DLL plugins to work.
# Part 1: tell linker to generate special import library when compiling caer-bin.exe.
IF (OS_WINDOWS)
	SET(CMAKE_EXE_LINKER_FLAGS "-Wl,--out-implib,libcaerbinsupport.a")
ENDIF()

# Add all core source files and libraries.
ADD_SUBDIRECTORY(base)
ADD_SUBDIRECTORY(ext)

# Set include/library paths for successive compilation.
INCLUDE_DIRECTORIES(${CAER_INCDIRS})
LINK_DIRECTORIES(${CAER_LIBDIRS})

# Compile main caer executable.
ADD_EXECUTABLE(caer-bin ${CAER_C_SRC_FILES} ${CAER_CXX_SRC_FILES} main.cpp)
TARGET_LINK_LIBRARIES(caer-bin ${CAER_C_LIBS} ${CAER_CXX_LIBS})
INSTALL(TARGETS caer-bin DESTINATION ${CMAKE_INSTALL_BINDIR})

# Windows needs extra linker information for DLL plugins to work.
# Part 2: tell DLL plugins to use this special import library.
IF (OS_WINDOWS)
	SET(CAER_C_LIBS ${CAER_C_LIBS} caerbinsupport)
	SET(CAER_CXX_LIBS ${CAER_CXX_LIBS} caerbinsupport)
ENDIF()

# Mac OS X's linker also needs to be told that undefined
# references in the plugins are fine.
IF (OS_MACOSX)
	SET(CMAKE_SHARED_LINKER_FLAGS "-Wl,-undefined,dynamic_lookup")
ENDIF()

# Compile extra modules and utilities.
ADD_SUBDIRECTORY(modules)
ADD_SUBDIRECTORY(utils)

# Print info summary for debug purposes
MESSAGE(STATUS "Project version is: ${PROJECT_VERSION}")
MESSAGE(STATUS "Compiler is Clang: ${CC_CLANG}")
MESSAGE(STATUS "Compiler is GCC: ${CC_GCC}")
MESSAGE(STATUS "Compiler is IntelCC: ${CC_ICC}")
MESSAGE(STATUS "Compiler is MS VisualC: ${CC_MSVC}")
MESSAGE(STATUS "OS is Unix: ${OS_UNIX}")
MESSAGE(STATUS "OS is Linux: ${OS_LINUX}")
MESSAGE(STATUS "OS is MacOS X: ${OS_MACOSX}")
MESSAGE(STATUS "OS is Windows: ${OS_WINDOWS}")
MESSAGE(STATUS "System is big-endian: ${SYSTEM_BIGENDIAN}")
MESSAGE(STATUS "Thread support is PThreads: ${HAVE_PTHREADS}")
MESSAGE(STATUS "Thread support is Win32 Threads: ${HAVE_WIN32_THREADS}")
MESSAGE(STATUS "C flags are: ${CMAKE_C_FLAGS}")
MESSAGE(STATUS "CXX flags are: ${CMAKE_CXX_FLAGS}")
MESSAGE(STATUS "Include directories are: ${CAER_INCDIRS}")
MESSAGE(STATUS "Library directories are: ${CAER_LIBDIRS}")
MESSAGE(STATUS "Linked C libraries are: ${CAER_C_LIBS}")
MESSAGE(STATUS "Compiled C source files are: ${CAER_C_SRC_FILES}")
MESSAGE(STATUS "Linked C++ libraries are: ${CAER_CXX_LIBS}")
MESSAGE(STATUS "Compiled C++ source files are: ${CAER_CXX_SRC_FILES}")
MESSAGE(STATUS "Final install bindir is: ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}")
