SET(CAER_BASE_FILES
	base/config.c
	base/config_server.c
	base/log.c
	base/misc.c
	base/module.c)

SET(CAER_C_SRC_FILES ${CAER_C_SRC_FILES} ${CAER_BASE_FILES} PARENT_SCOPE)
SET(CAER_CXX_SRC_FILES ${CAER_CXX_SRC_FILES} base/mainloop.cpp PARENT_SCOPE)
