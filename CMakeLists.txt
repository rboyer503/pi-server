cmake_minimum_required (VERSION 3.9)

project (pi-server)

find_package (OpenCV REQUIRED)
find_package (Boost REQUIRED COMPONENTS thread date_time system)
find_package (OpenMP REQUIRED)
find_package (CURL REQUIRED)

include_directories (${OpenCV_INCLUDE_DIRS})
include_directories (${Boost_INCLUDE_DIRS})

add_executable (pi-server
    Socket.cpp
    SocketMgr.cpp
    VideoCaptureMgr.cpp
    MotionDetector.cpp
    NotificationMgr.cpp
    PiMgr.cpp
    pi-server.cpp)

target_link_libraries (pi-server
    ${OpenCV_LIBS}
    ${Boost_THREAD_LIBRARY}
    ${Boost_DATE_TIME_LIBRARY}
    ${Boost_SYSTEM_LIBRARY}
    -lv4l2
    -pthread
    OpenMP::OpenMP_CXX
    CURL::libcurl)
