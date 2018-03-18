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
SET(PROJECT_VERSION_MINOR 2)
SET(PROJECT_VERSION_PATCH 1)
SET(PROJECT_VERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH})

# Define installation paths
INCLUDE(GNUInstallDirs)

# Custom CMake base module
SET(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATAROOTDIR}/caer)

# Basic setup for cAER
INCLUDE(caer-base)
CAER_SETUP(TRUE)

# Search for external libraries with pkg-config.
INCLUDE(FindPkgConfig)

# Basic device and threads support.
PKG_CHECK_MODULES(LIBCAER REQUIRED libcaer>=2.4.0)

SET(CAER_INCDIRS ${LIBCAER_INCLUDE_DIRS})
SET(CAER_LIBDIRS ${LIBCAER_LIBRARY_DIRS})

SET(CAER_LIBS ${CAER_BASE_LIBS} ${LIBCAER_LIBRARIES})

# Boost support for C++
FIND_PACKAGE(Boost 1.50 REQUIRED COMPONENTS system filesystem iostreams program_options)

SET(CAER_INCDIRS ${CAER_INCDIRS} ${Boost_INCLUDE_DIRS})
SET(CAER_LIBDIRS ${CAER_LIBDIRS} ${Boost_LIBRARY_DIRS})
SET(CAER_LIBS ${CAER_LIBS} ${Boost_LIBRARIES})

IF ((NOT (Boost_MAJOR_VERSION EQUAL 1 AND Boost_MINOR_VERSION GREATER_EQUAL 61))
	OR (OS_UNIX OR OS_LINUX OR OS_MACOSX))
	# libdl needed if Boost too old to have Boost.DLL as a replacement,
	# or for Boost.DLL itself on some platforms like Linux and MacOS X.
	SET(CAER_LIBS ${CAER_LIBS} dl)
ENDIF()

# Windows boost.ASIO needs extra libraries
IF (OS_WINDOWS)
	SET(CAER_LIBS ${CAER_LIBS} wsock32 ws2_32)
ENDIF()

# TCMalloc support (fast memory allocation)
IF (USE_TCMALLOC)
	PKG_CHECK_MODULES(LIBTCMALLOC REQUIRED libtcmalloc>=2.2)

	SET(CAER_INCDIRS ${CAER_INCDIRS} ${LIBTCMALLOC_INCLUDE_DIRS})
	SET(CAER_LIBDIRS ${CAER_LIBDIRS} ${LIBTCMALLOC_LIBRARY_DIRS})

	SET(CAER_LIBS ${CAER_LIBS} ${LIBTCMALLOC_LIBRARIES})
ENDIF()

# Add local directory to include and library paths
SET(CAER_INCDIRS ${CAER_INCDIRS} ${CMAKE_SOURCE_DIR}/)
SET(CAER_LIBDIRS ${CAER_LIBDIRS} ${CMAKE_SOURCE_DIR}/)

# Set include/library paths for successive compilation.
INCLUDE_DIRECTORIES(${CAER_INCDIRS})
LINK_DIRECTORIES(${CAER_LIBDIRS})

# Install SDK library.
ADD_SUBDIRECTORY(caer-sdk)

# Compile libcaersdk and caer-bin main binary.
ADD_SUBDIRECTORY(src)

# Compile extra modules and utilities.
#ADD_SUBDIRECTORY(modules)
ADD_SUBDIRECTORY(utils)
