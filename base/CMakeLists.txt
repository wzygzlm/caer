SET(CAER_BASE_C_FILES
	base/log.c
	base/misc.c
	base/module.c)

SET(CAER_BASE_CXX_FILES
	base/config.cpp
	base/config_server.cpp
	base/mainloop.cpp)

SET(CAER_C_SRC_FILES ${CAER_C_SRC_FILES} ${CAER_BASE_C_FILES} PARENT_SCOPE)
SET(CAER_CXX_SRC_FILES ${CAER_CXX_SRC_FILES} ${CAER_BASE_CXX_FILES} PARENT_SCOPE)
