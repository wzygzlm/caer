IF (NOT SPIKEFEATURES)
	SET(SPIKEFEATURES 0 CACHE BOOL "Enable the spike features filter")
ENDIF()

IF (SPIKEFEATURES)
    ADD_LIBRARY(spikefeatures SHARED spikefeatures.c)

    TARGET_LINK_LIBRARIES(spikefeatures ${CAER_C_LIBS})

ENDIF()

