# Compile caer-ctl (CLI control utility)
ADD_EXECUTABLE(caer-ctl
	../../ext/sshs/sshs_helper.c
	../ext/linenoise-ng/ConvertUTF.cpp
	../ext/linenoise-ng/wcwidth.cpp
	../ext/linenoise-ng/linenoise.cpp
	caerctl.c)
INSTALL(TARGETS caer-ctl DESTINATION ${CMAKE_INSTALL_BINDIR})
