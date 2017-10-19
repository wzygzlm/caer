# Compile caer-ctl (CLI control utility)

# TODO: temporary SSHS link, switch to SSHS full library later.
# Support files to compile.
SET(CAER_SSHS_FILES
	../../ext/sshs/sshs.cpp
	../../ext/sshs/sshs_helper.cpp
	../../ext/sshs/sshs_node.cpp)

ADD_EXECUTABLE(caer-ctl
	${CAER_SSHS_FILES}
	../ext/linenoise-ng/ConvertUTF.cpp
	../ext/linenoise-ng/wcwidth.cpp
	../ext/linenoise-ng/linenoise.cpp
	caerctl.cpp)
TARGET_LINK_LIBRARIES(caer-ctl ${CAER_CXX_LIBS})
INSTALL(TARGETS caer-ctl DESTINATION ${CMAKE_INSTALL_BINDIR})
