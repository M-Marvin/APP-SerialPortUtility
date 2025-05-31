cmake_minimum_required(VERSION 3.10)

#configure project
project(SerialPortAccess LANGUAGES CXX)

#configure C++
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

#configure header includes
include_directories("C:/Program Files/Java/jdk-17.0.10+7/include")
include_directories("C:/Program Files/Java/jdk-17.0.10+7/include/win32")
#include_directories(C:\Program Files\Java\jdk-17.0.10+7\include\linux)
include_directories(../JSerialPortAccess/build/generated/sources/headers/java/main)
include_directories(.)

#configure source files
add_library(serial SHARED serial_port_win.cpp serial_port_lin.cpp serial_port_jni.cpp)

set(EXECUTABLE_OUTPUT_PATH  ${PROJECT_SOURCE_DIR}/build/)

#set_target_properties(serial PROPERTIES PUBLIC_HEADER serial_port.h)
#install(TARGETS serial
       # PUBLIC_HEADER
#        DESTINATION ../../bin-amd64-win
#)
