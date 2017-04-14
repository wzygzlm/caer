IF (NOT ENABLE_FILE_OUTPUT)
	SET(ENABLE_FILE_OUTPUT 0 CACHE BOOL "Enable the file output module")
ENDIF()

IF (NOT ENABLE_NETWORK_OUTPUT)
	SET(ENABLE_NETWORK_OUTPUT 0 CACHE BOOL "Enable the network output modules (TCP server, TCP, UDP, UnixSockets)")
ENDIF()

IF (ENABLE_FILE_OUTPUT)
	ADD_LIBRARY(output_file SHARED output_common.c file.c)

	SET_TARGET_PROPERTIES(output_file
		PROPERTIES
		PREFIX "caer_"
	)

	TARGET_LINK_LIBRARIES(output_file ${CAER_C_LIBS})
ENDIF()

IF (ENABLE_NETWORK_OUTPUT)
	# NET_TCP_SERVER
	ADD_LIBRARY(output_net_tcp_server SHARED output_common.c net_tcp_server.c)

	SET_TARGET_PROPERTIES(output_net_tcp_server
		PROPERTIES
		PREFIX "caer_"
	)

	TARGET_LINK_LIBRARIES(output_net_tcp_server ${CAER_C_LIBS})
	
	# NET_TCP_CLIENT
	ADD_LIBRARY(output_net_tcp_client SHARED output_common.c net_tcp.c)

	SET_TARGET_PROPERTIES(output_net_tcp_client
		PROPERTIES
		PREFIX "caer_"
	)

	TARGET_LINK_LIBRARIES(output_net_tcp_client ${CAER_C_LIBS})
	
	# NET_UDP
	ADD_LIBRARY(output_net_udp SHARED output_common.c net_udp.c)

	SET_TARGET_PROPERTIES(output_net_udp
		PROPERTIES
		PREFIX "caer_"
	)

	TARGET_LINK_LIBRARIES(output_net_udp ${CAER_C_LIBS})
	
	# NET_SOCKET_SERVER
	ADD_LIBRARY(output_net_socket_server SHARED output_common.c unix_socket_server.c)

	SET_TARGET_PROPERTIES(output_net_socket_server
		PROPERTIES
		PREFIX "caer_"
	)

	TARGET_LINK_LIBRARIES(output_net_socket_server ${CAER_C_LIBS})
	
	# NET_SOCKET_CLIENT
	ADD_LIBRARY(output_net_socket_client SHARED output_common.c unix_socket.c)

	SET_TARGET_PROPERTIES(output_net_socket_client
		PROPERTIES
		PREFIX "caer_"
	)

	TARGET_LINK_LIBRARIES(output_net_socket_client ${CAER_C_LIBS})
ENDIF()
