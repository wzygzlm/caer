SET(CAER_SRC_FILES
	log.c
	misc.c
	config.cpp
	config_server.cpp
	module.cpp
	mainloop.cpp
	main.cpp)

SET(LIBCAERSDK_SRC_FILES
	module_sdk.cpp
	mainloop_sdk.cpp
    sshs/sshs.cpp
    sshs/sshs_helper.cpp
    sshs/sshs_node.cpp)

# Compile SDK library.
ADD_LIBRARY(caersdk SHARED ${LIBCAERSDK_SRC_FILES})

SET_TARGET_PROPERTIES(caersdk
    PROPERTIES
    SOVERSION ${PROJECT_VERSION_MAJOR}
    VERSION ${PROJECT_VERSION}
)

TARGET_LINK_LIBRARIES(caersdk ${CAER_LIBS})

IF (OS_WINDOWS)
    INSTALL(TARGETS caersdk RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
ELSE()
    INSTALL(TARGETS caersdk DESTINATION ${CMAKE_INSTALL_LIBDIR})
ENDIF()

# Compile main caer executable.
ADD_EXECUTABLE(caer-bin ${CAER_SRC_FILES})
TARGET_LINK_LIBRARIES(caer-bin ${CAER_LIBS} caersdk)
INSTALL(TARGETS caer-bin DESTINATION ${CMAKE_INSTALL_BINDIR})
