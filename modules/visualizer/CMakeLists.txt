IF (NOT VISUALIZER_SFML)
	SET(VISUALIZER_SFML 0 CACHE BOOL "Enable the visualizer module, based on the SFML C++ library.")
ENDIF()

IF (VISUALIZER_SFML)
	# Visualizer support via the SFML C++ library
	PKG_CHECK_MODULES(SFML REQUIRED sfml-graphics>=2.3.0)

	SET(VISUALIZER_SFML_LIBS ${CAER_C_LIBS} ${SFML_LIBRARIES})
	SET(VISUALIZER_SFML_INCDIRS ${CAER_INCDIRS} ${SFML_INCLUDE_DIRS})
	SET(VISUALIZER_SFML_LIBDIRS ${CAER_LIBDIRS} ${SFML_LIBRARY_DIRS})

	INCLUDE_DIRECTORIES(${VISUALIZER_SFML_INCDIRS})
	LINK_DIRECTORIES(${VISUALIZER_SFML_LIBDIRS})

	ADD_LIBRARY(visualizersfml SHARED visualizer.cpp visualizer_handlers.cpp visualizer_renderers.cpp)

	SET_TARGET_PROPERTIES(visualizersfml
		PROPERTIES
		PREFIX "caer_"
	)

	TARGET_LINK_LIBRARIES(visualizersfml ${VISUALIZER_SFML_LIBS})

	# Copy resources to build directory.
	FILE(COPY ${CMAKE_SOURCE_DIR}/ext/resources/LiberationSans-Regular.ttf DESTINATION ${CM_BUILD_DIR}/ext/resources)
	FILE(COPY ${CMAKE_SOURCE_DIR}/ext/resources/LiberationSans-Bold.ttf DESTINATION ${CM_BUILD_DIR}/ext/resources)

	# Install resources to a common system location.
	INSTALL(FILES ${CM_BUILD_DIR}/ext/resources/LiberationSans-Regular.ttf ${CM_BUILD_DIR}/ext/resources/LiberationSans-Bold.ttf DESTINATION ${CM_SHARE_DIR})
ENDIF()
