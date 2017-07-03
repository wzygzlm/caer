IF (NOT MEDIANTRACKER)
	SET(MEDIANTRACKER 0 CACHE BOOL "Enable the median tracker module")
ENDIF()

IF (MEDIANTRACKER)
	ADD_LIBRARY(mediantracker SHARED mediantracker.c)

	SET_TARGET_PROPERTIES(mediantracker
		PROPERTIES
		PREFIX "caer_"
	)

	TARGET_LINK_LIBRARIES(mediantracker ${CAER_C_LIBS})
ENDIF()
