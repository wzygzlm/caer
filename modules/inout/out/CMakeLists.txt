# FILE
ADD_LIBRARY(output_file SHARED output_common.cpp file.cpp)

SET_TARGET_PROPERTIES(output_file
	PROPERTIES
	PREFIX "caer_"
)

TARGET_LINK_LIBRARIES(output_file ${CAER_LIBS})

INSTALL(TARGETS output_file DESTINATION ${CAER_MODULES_DIR})

# NET_TCP_SERVER
ADD_LIBRARY(output_net_tcp_server SHARED output_common.cpp net_tcp_server.cpp)

SET_TARGET_PROPERTIES(output_net_tcp_server
	PROPERTIES
	PREFIX "caer_"
)

TARGET_LINK_LIBRARIES(output_net_tcp_server ${CAER_LIBS})

INSTALL(TARGETS output_net_tcp_server DESTINATION ${CAER_MODULES_DIR})

# NET_TCP_CLIENT
ADD_LIBRARY(output_net_tcp_client SHARED output_common.cpp net_tcp.cpp)

SET_TARGET_PROPERTIES(output_net_tcp_client
	PROPERTIES
	PREFIX "caer_"
)

TARGET_LINK_LIBRARIES(output_net_tcp_client ${CAER_LIBS})

INSTALL(TARGETS output_net_tcp_client DESTINATION ${CAER_MODULES_DIR})

# NET_UDP
ADD_LIBRARY(output_net_udp SHARED output_common.cpp net_udp.cpp)

SET_TARGET_PROPERTIES(output_net_udp
	PROPERTIES
	PREFIX "caer_"
)

TARGET_LINK_LIBRARIES(output_net_udp ${CAER_LIBS})

INSTALL(TARGETS output_net_udp DESTINATION ${CAER_MODULES_DIR})

# NET_SOCKET_SERVER
ADD_LIBRARY(output_net_socket_server SHARED output_common.cpp unix_socket_server.cpp)

SET_TARGET_PROPERTIES(output_net_socket_server
	PROPERTIES
	PREFIX "caer_"
)

TARGET_LINK_LIBRARIES(output_net_socket_server ${CAER_LIBS})

INSTALL(TARGETS output_net_socket_server DESTINATION ${CAER_MODULES_DIR})

# NET_SOCKET_CLIENT
ADD_LIBRARY(output_net_socket_client SHARED output_common.cpp unix_socket.cpp)

SET_TARGET_PROPERTIES(output_net_socket_client
	PROPERTIES
	PREFIX "caer_"
)

TARGET_LINK_LIBRARIES(output_net_socket_client ${CAER_LIBS})

INSTALL(TARGETS output_net_socket_client DESTINATION ${CAER_MODULES_DIR})
