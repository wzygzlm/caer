IF (NOT POISSONSPIKEGEN)
    SET(POISSONSPIKEGEN 0 CACHE BOOL "Enable the poisson spike generator module")
ENDIF()

IF (POISSONSPIKEGEN)
    ADD_LIBRARY(poissonspikegen SHARED poissonspikegen.c)

    TARGET_LINK_LIBRARIES(poissonspikegen ${CAER_C_LIBS})

	INSTALL(TARGETS poissonspikegen DESTINATION ${CM_SHARE_DIR})
ENDIF()
