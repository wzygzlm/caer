IF (NOT IMAGEGENERATOR)
	SET(IMAGEGENERATOR 0 CACHE BOOL "Enable the image generator filter")
ENDIF()

IF (IMAGEGENERATOR)
    ADD_LIBRARY(imagegenerator SHARED imagegenerator.c)

    TARGET_LINK_LIBRARIES(imagegenerator ${CAER_C_LIBS})

	INSTALL(TARGETS imagegenerator DESTINATION ${CM_SHARE_DIR})
ENDIF()

