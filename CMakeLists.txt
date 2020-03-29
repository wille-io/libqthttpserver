cmake_minimum_required(VERSION 3.5)

project(qthttpserver LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt5 COMPONENTS Core Network WebSockets REQUIRED)

set(src
    qthttpserverglobal.h
    qabstracthttpserver.h
    qabstracthttpserver_p.h
    qhttpserver.h
    qhttpserver_p.h
    qhttpserverrequest.h
    qhttpserverrequest_p.h
    qhttpserverresponder.h
    qhttpserverresponder_p.h
    qhttpserverresponse.h
    qhttpserverresponse_p.h
    qhttpserverrouter.h
    qhttpserverrouter_p.h
    qhttpserverrouterrule.h
    qhttpserverrouterrule_p.h

    qabstracthttpserver.cpp
    qhttpserver.cpp
    qhttpserverrequest.cpp
    qhttpserverresponder.cpp
    qhttpserverresponse.cpp
    qhttpserverrouter.cpp
    qhttpserverrouterrule.cpp
)

add_library(qthttpserver SHARED ${src})


INSTALL (
    DIRECTORY ${CMAKE_SOURCE_DIR}/include/
    DESTINATION include
    FILES_MATCHING PATTERN "*.h")


target_include_directories(qthttpserver PRIVATE include)

include(GNUInstallDirs)
install(TARGETS qthttpserver
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

target_link_libraries(qthttpserver http_parser Qt5::Core Qt5::Network Qt5::WebSockets)
