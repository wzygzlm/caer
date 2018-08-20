INCLUDE_DIRECTORIES ($ENV{HOME}/github_prjs/xfopencv/include)
INCLUDE_DIRECTORIES ($ENV{HOME}/Xilinx/Vivado/2018.2/include)
INCLUDE_DIRECTORIES (./)
INCLUDE_DIRECTORIES (./abmof_accel)

# FILE(GLOB abmof_accel_sources RELATIVE "./abmof_accel" "*")
set(ABMOF_ACCEL_SRCS ./abmof_accel/abmof_accel.cpp)
MESSAGE("The variable abmof_accel_sources is ${ABMOF_ACCEL_SRCS}.")
ADD_LIBRARY(abmof SHARED caer_abmof.cpp ${ABMOF_ACCEL_SRCS})

# TARGET_INCLUDE_DIRECTORIES (abmof PRIVATE ./abmof_accel)
SET_TARGET_PROPERTIES(abmof
	PROPERTIES
	PREFIX "libcaer_"
)

TARGET_LINK_LIBRARIES(abmof ${CAER_LIBS})

INSTALL(TARGETS abmof DESTINATION ${CAER_MODULES_DIR})
