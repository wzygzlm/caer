# Compile caer-ctl (CLI control utility)
ADD_EXECUTABLE(caer-ctl
	../../ext/sshs/sshs_helper.c
	../ext/linenoise-ng/ConvertUTF.cpp
	../ext/linenoise-ng/wcwidth.cpp
	../ext/linenoise-ng/linenoise.cpp
	caerctl.cpp)
TARGET_LINK_LIBRARIES(caer-ctl ${CAER_CXX_LIBS})
INSTALL(TARGETS caer-ctl DESTINATION ${CMAKE_INSTALL_BINDIR})
