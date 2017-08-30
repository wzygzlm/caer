IF (NOT FPGASPIKEGEN)
	SET(FPGASPIKEGEN 0 CACHE BOOL "Enable fpga spike generator module")
ENDIF()

IF (FPGASPIKEGEN)
    ADD_LIBRARY(fpgaspikegen SHARED fpgaspikegen.c)

    TARGET_LINK_LIBRARIES(fpgaspikegen ${CAER_C_LIBS})

	INSTALL(TARGETS fpgaspikegen DESTINATION ${CM_SHARE_DIR})
ENDIF()


