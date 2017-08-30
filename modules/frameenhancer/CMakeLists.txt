IF (NOT FRAMEENHANCER)
	SET(FRAMEENHANCER 0 CACHE BOOL "Enable the APS frame enhancing/demosaicing module")
ENDIF()

IF (FRAMEENHANCER)
	ADD_LIBRARY(frameenhancer SHARED frameenhancer.c)

	SET_TARGET_PROPERTIES(frameenhancer
		PROPERTIES
		PREFIX "caer_"
	)

	TARGET_LINK_LIBRARIES(frameenhancer ${CAER_C_LIBS})

	INSTALL(TARGETS frameenhancer DESTINATION ${CM_SHARE_DIR})
ENDIF()
