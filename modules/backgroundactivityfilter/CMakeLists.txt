IF (NOT ENABLE_BAFILTER)
	SET(ENABLE_BAFILTER 0 CACHE BOOL "Enable the background activity filtering module")
ENDIF()

IF (ENABLE_BAFILTER)
	ADD_LIBRARY(bafilter SHARED backgroundactivityfilter.c)

	SET_TARGET_PROPERTIES(bafilter
		PROPERTIES
		PREFIX "caer_"
	)

	TARGET_LINK_LIBRARIES(bafilter ${CAER_C_LIBS})
ENDIF()
