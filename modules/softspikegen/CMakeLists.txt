IF (NOT SOFTSPIKEGEN)
	SET(SOFTSPIKEGEN 0 CACHE BOOL "Enable software spike generator module")
ENDIF()

IF (SOFTSPIKEGEN)
    ADD_LIBRARY(softspikegen SHARED softspikegen.c)

    TARGET_LINK_LIBRARIES(softspikegen ${CAER_C_LIBS})

	INSTALL(TARGETS softspikegen DESTINATION ${CM_SHARE_DIR})
ENDIF()
