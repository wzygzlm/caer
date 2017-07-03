IF (NOT DYNAMICRECTANGULARTRACKER)
	SET(DYNAMICRECTANGULARTRACKER 0 CACHE BOOL "Enable the dynamic rectangular cluster tracker module")
ENDIF()

IF (DYNAMICRECTANGULARTRACKER)
	ADD_LIBRARY(dynamic_rectangulartracker SHARED rectangulartracker_dynamic.c)

	SET_TARGET_PROPERTIES(dynamic_rectangulartracker
		PROPERTIES
		PREFIX "caer_"
	)

	TARGET_LINK_LIBRARIES(dynamic_rectangulartracker ${CAER_C_LIBS})
ENDIF()
