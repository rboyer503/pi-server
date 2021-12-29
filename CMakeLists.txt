cmake_minimum_required (VERSION 2.8)
 
project (pi-server)

find_package (OpenCV REQUIRED)
find_package (Boost REQUIRED COMPONENTS thread date_time system)

include_directories (${OpenCV_INCLUDE_DIRS})
include_directories (${Boost_INCLUDE_DIRS})

add_executable (pi-server
    Socket.cpp
    SocketMgr.cpp
    VideoCaptureMgr.cpp
    PiMgr.cpp
    pi-server.cpp)

target_link_libraries (pi-server
    ${OpenCV_LIBS}
    ${Boost_THREAD_LIBRARY}
    ${Boost_DATE_TIME_LIBRARY}
    ${Boost_SYSTEM_LIBRARY}
    -lv4l2
    -pthread)
