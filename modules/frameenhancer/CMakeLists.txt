ADD_LIBRARY(frameenhancer SHARED frameenhancer.c)

SET_TARGET_PROPERTIES(frameenhancer
	PROPERTIES
	PREFIX "caer_"
)

TARGET_LINK_LIBRARIES(frameenhancer ${CAER_LIBS})

INSTALL(TARGETS frameenhancer DESTINATION ${CM_SHARE_DIR})
