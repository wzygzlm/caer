IF (NOT INPUT_FILE)
	SET(INPUT_FILE 0 CACHE BOOL "Enable the file input module")
ENDIF()

IF (NOT INPUT_NETWORK)
	SET(INPUT_NETWORK 0 CACHE BOOL "Enable the network input modules (TCP, UnixSockets)")
ENDIF()

IF (INPUT_FILE)
	ADD_LIBRARY(input_file SHARED input_common.c file.c)

	SET_TARGET_PROPERTIES(input_file
		PROPERTIES
		PREFIX "caer_"
	)

	TARGET_LINK_LIBRARIES(input_file ${CAER_C_LIBS})

	INSTALL(TARGETS input_file DESTINATION ${CM_SHARE_DIR})
ENDIF()

IF (INPUT_NETWORK)
	# NET_TCP_CLIENT
	ADD_LIBRARY(input_net_tcp_client SHARED input_common.c net_tcp.c)

	SET_TARGET_PROPERTIES(input_net_tcp_client
		PROPERTIES
		PREFIX "caer_"
	)

	TARGET_LINK_LIBRARIES(input_net_tcp_client ${CAER_C_LIBS})

	INSTALL(TARGETS input_net_tcp_client DESTINATION ${CM_SHARE_DIR})

	# NET_SOCKET_CLIENT
	ADD_LIBRARY(input_net_socket_client SHARED input_common.c unix_socket.c)

	SET_TARGET_PROPERTIES(input_net_socket_client
		PROPERTIES
		PREFIX "caer_"
	)

	TARGET_LINK_LIBRARIES(input_net_socket_client ${CAER_C_LIBS})

	INSTALL(TARGETS input_net_socket_client DESTINATION ${CM_SHARE_DIR})
ENDIF()
