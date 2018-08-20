INCLUDE_DIRECTORIES ($ENV{HOME}/github_prjs/xfopencv/include)
INCLUDE_DIRECTORIES ($ENV{HOME}/Xilinx/Vivado/2018.2/include)
INCLUDE_DIRECTORIES (./)
INCLUDE_DIRECTORIES (./abmof_accel)

find_package(OpenCV REQUIRED)

set(ABMOF_ACCEL_SRCS ./abmof_accel/abmof_accel.cpp ./abmof_accel/mmult_accel.cpp)
MESSAGE("The variable abmof_accel_sources is ${ABMOF_ACCEL_SRCS}.")
ADD_LIBRARY(my_filter SHARED caer_abmof.cpp ${ABMOF_ACCEL_SRCS})

# TARGET_INCLUDE_DIRECTORIES (my_filter PRIVATE ./abmof_accel)
SET_TARGET_PROPERTIES(my_filter
	PROPERTIES
	PREFIX "libcaer_"
)

MESSAGE("The variable CAER_LIBS is ${CAER_LIBS}")
MESSAGE("The variable OpenCV_LIBS is ${OpenCV_LIBS}")
TARGET_LINK_LIBRARIES(my_filter ${OpenCV_LIBS} ${CAER_LIBS})

INSTALL(TARGETS my_filter DESTINATION ${CAER_MODULES_DIR})
