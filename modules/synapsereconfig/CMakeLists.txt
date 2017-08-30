IF (NOT SYNAPSERECONFIG)
	SET(SYNAPSERECONFIG 0 CACHE BOOL "Enable the monitor neuron filter")
ENDIF()

IF (SYNAPSERECONFIG)
    ADD_LIBRARY(synapsereconfig SHARED synapsereconfig.c)

    TARGET_LINK_LIBRARIES(synapsereconfig ${CAER_C_LIBS})

	INSTALL(TARGETS synapsereconfig DESTINATION ${CM_SHARE_DIR})
ENDIF()

