# Support files to compile.
SET(CAER_SSHS_FILES
	ext/sshs/sshs.cpp
	ext/sshs/sshs_helper.cpp
	ext/sshs/sshs_node.cpp)

SET(CAER_CXX_SRC_FILES ${CAER_CXX_SRC_FILES} ${CAER_SSHS_FILES} PARENT_SCOPE)
