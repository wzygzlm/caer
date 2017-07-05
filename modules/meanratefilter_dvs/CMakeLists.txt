IF (NOT MEANRATEFILTERDVS)
	SET(MEANRATEFILTERDVS 0 CACHE BOOL "Enable the mean rate filter module for polarity events")
ENDIF()

IF (MEANRATEFILTERDVS)
    ADD_LIBRARY(meanratefilterdvs SHARED meanratefilter_dvs.c)

    TARGET_LINK_LIBRARIES(meanratefilterdvs ${CAER_C_LIBS})

ENDIF()

