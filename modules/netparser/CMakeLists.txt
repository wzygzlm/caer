IF (NOT NETPARSER)
	SET(NETPARSER 0 CACHE BOOL "Enable the netparser module")
ENDIF()

IF (NETPARSER)
	ADD_LIBRARY(netparser SHARED netparser.cpp Neuron.cpp)

	TARGET_LINK_LIBRARIES(netparser ${CAER_CXX_LIBS})
ENDIF()
