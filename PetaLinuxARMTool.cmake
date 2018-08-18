# For cross compiling 
set( TC_PATH "/home/petalinux/petalinux/tools/linux-i386/gcc-arm-linux-gnueabi/bin/" )
set( ROOTFS_PATH "/home/petalinux/github_prjs/nnp_petalinux_prjs/custom_nnp_2018_2/images/linux/rootfs" )
set( CROSS_LOCAL_PREFIX "/usr" )

set( CMAKE_SYSTEM_NAME Linux )
set( CMAKE_SYSTEM_PROCESSOR arm )

set( CROSS_COMPILE arm-linux-gnueabihf- )

set( CMAKE_C_COMPILER "${TC_PATH}${CROSS_COMPILE}gcc" )
set( CMAKE_CXX_COMPILER "${TC_PATH}${CROSS_COMPILE}g++" )
set( CMAKE_LINKER "${TC_PATH}${CROSS_COMPILE}ld" )
set( CMAKE_AR "${TC_PATH}${CROSS_COMPILE}ar" )
set( CMAKE_OBJCOPY "${TC_PATH}${CROSS_COMPILE}objcopy" )

set(CMAKE_SYSROOT "${ROOTFS_PATH}")

set( CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER )
set( CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY )
set( CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY )
set( CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY )

set( CMAKE_PREFIX_PATH ${ROOTFS_PATH} )
# set( CMAKE_INSTALL_PREFIX "${ROOTFS_PATH}${CROSS_LOCAL_PREFIX}" )
set( CMAKE_FIND_ROOT_PATH ${ROOTFS_PATH} )
set(ENV{PKG_CONFIG_PATH} "${ROOTFS_PATH}/usr/lib/pkgconfig:${ROOTFS_PATH}/usr/share/pkgconfig:${ROOTFS_PATH}/lib/pkgconfig")
# set(ENV{PKG_CONFIG_DIR} "${ROOTFS_PATH}/usr/lib/pkgconfig:${ROOTFS_PATH}/usr/share/pkgconfig:${ROOTFS_PATH}/lib/pkgconfig")
set( ENV{PKG_CONFIG_SYSROOT_DIR} ${ROOTFS_PATH} )

set( CAER_LOCAL_PREFIX ${CROSS_LOCAL_PREFIX} )
set( CAER_LOCAL_INCDIRS "${ROOTFS_PATH}${CROSS_LOCAL_PREFIX}/include/" )
set( CAER_LOCAL_LIBDIRS "${ROOTFS_PATH}/lib/;${ROOTFS_PATH}${CROSS_LOCAL_PREFIX}/lib/" )
