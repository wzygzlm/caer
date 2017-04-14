IF (NOT ENABLE_FILE_INPUT)
	SET(ENABLE_FILE_INPUT 0 CACHE BOOL "Enable the file input module")
ENDIF()

IF (NOT ENABLE_NETWORK_INPUT)
	SET(ENABLE_NETWORK_INPUT 0 CACHE BOOL "Enable the network input modules (TCP, UnixSockets)")
ENDIF()

IF (ENABLE_FILE_INPUT)
	ADD_LIBRARY(input_file SHARED input_common.c file.c)

	SET_TARGET_PROPERTIES(input_file
		PROPERTIES
		PREFIX "caer_"
	)

	TARGET_LINK_LIBRARIES(input_file ${CAER_C_LIBS})
ENDIF()

IF (ENABLE_NETWORK_INPUT)
	# NET_TCP_CLIENT
	ADD_LIBRARY(input_net_tcp_client SHARED input_common.c net_tcp.c)

	SET_TARGET_PROPERTIES(input_net_tcp_client
		PROPERTIES
		PREFIX "caer_"
	)

	TARGET_LINK_LIBRARIES(input_net_tcp_client ${CAER_C_LIBS})
	
	# NET_SOCKET_CLIENT
	ADD_LIBRARY(input_net_socket_client SHARED input_common.c unix_socket.c)

	SET_TARGET_PROPERTIES(input_net_socket_client
		PROPERTIES
		PREFIX "caer_"
	)

	TARGET_LINK_LIBRARIES(input_net_socket_client ${CAER_C_LIBS})
ENDIF()
