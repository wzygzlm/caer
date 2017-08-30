IF (NOT MEANRATEFILTER)
	SET(MEANRATEFILTER 0 CACHE BOOL "Enable the mean rate filter module")
ENDIF()

IF (MEANRATEFILTER)
    ADD_LIBRARY(meanratefilter SHARED meanratefilter.c)

    TARGET_LINK_LIBRARIES(meanratefilter ${CAER_C_LIBS})

	INSTALL(TARGETS meanratefilter DESTINATION ${CM_SHARE_DIR})
ENDIF()

