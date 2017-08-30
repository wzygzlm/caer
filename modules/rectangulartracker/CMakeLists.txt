IF (NOT RECTANGULARTRACKER)
	SET(RECTANGULARTRACKER 0 CACHE BOOL "Enable the rectangular cluster tracker module")
ENDIF()

IF (RECTANGULARTRACKER)
	ADD_LIBRARY(rectangulartracker SHARED rectangulartracker.c)

	SET_TARGET_PROPERTIES(rectangulartracker
		PROPERTIES
		PREFIX "caer_"
	)

	TARGET_LINK_LIBRARIES(rectangulartracker ${CAER_C_LIBS})

	INSTALL(TARGETS rectangulartracker DESTINATION ${CM_SHARE_DIR})
ENDIF()
