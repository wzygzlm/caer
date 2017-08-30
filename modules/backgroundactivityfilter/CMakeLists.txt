IF (NOT BAFILTER)
	SET(BAFILTER 0 CACHE BOOL "Enable the background activity filtering module")
ENDIF()

IF (BAFILTER)
	ADD_LIBRARY(bafilter SHARED backgroundactivityfilter.c)

	SET_TARGET_PROPERTIES(bafilter
		PROPERTIES
		PREFIX "caer_"
	)

	TARGET_LINK_LIBRARIES(bafilter ${CAER_C_LIBS})

	INSTALL(TARGETS bafilter DESTINATION ${CM_SHARE_DIR})
ENDIF()
