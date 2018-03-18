# Set modules directory
SET(CAER_MODULES_DIR ${CM_SHARE_DIR}/modules)

# Set full RPATH, modules are libraries for cAER
SET(CMAKE_INSTALL_RPATH ${CAER_MODULES_DIR})

ADD_SUBDIRECTORY(backgroundactivityfilter)
ADD_SUBDIRECTORY(cameracalibration)
ADD_SUBDIRECTORY(frameenhancer)
ADD_SUBDIRECTORY(framestatistics)
ADD_SUBDIRECTORY(ini)
ADD_SUBDIRECTORY(misc)
ADD_SUBDIRECTORY(statistics)
ADD_SUBDIRECTORY(visualizer)
