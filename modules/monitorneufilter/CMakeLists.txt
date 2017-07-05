IF (NOT MONITORNEURONFILTER)
	SET(MONITORNEURONFILTER 0 CACHE BOOL "Enable the monitor neuron filter")
ENDIF()

IF (MONITORNEURONFILTER)
    ADD_LIBRARY(monitoneuronfilter SHARED monitorneufilter.c)

    TARGET_LINK_LIBRARIES(monitoneuronfilter ${CAER_C_LIBS})

ENDIF()

