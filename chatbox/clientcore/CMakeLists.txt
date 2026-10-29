cmake_minimum_required(VERSION 3.21)

project(chatbox_clientcore LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Network)

add_library(chatbox_clientcore STATIC
    transport/transport.cpp
    transport/qt_ssl_transport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/protocol.cpp
)

target_include_directories(chatbox_clientcore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(chatbox_clientcore PUBLIC
    Qt6::Core
    Qt6::Network
)
