IF (NOT MONITORNEUFILTER)
	SET(MONITORNEUFILTER 0 CACHE BOOL "Enable the monitor neuron filter")
ENDIF()

IF (MONITORNEUFILTER)
    ADD_LIBRARY(monitorneufilter SHARED monitorneufilter.c)

    TARGET_LINK_LIBRARIES(monitorneufilter ${CAER_C_LIBS})

    INSTALL(TARGETS monitorneufilter DESTINATION ${CM_SHARE_DIR})
ENDIF()

