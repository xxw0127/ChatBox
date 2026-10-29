cmake_minimum_required(VERSION 3.21)

project(chatbox_clientcore LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Network Sql)

add_library(chatbox_clientcore STATIC
    transport/transport.cpp
    transport/qt_ssl_transport.cpp
    ipc/ipc_frame.cpp
    ipc/ipc_protocol.cpp
    ipc/ipc_client.cpp
    ipc/ipc_server.cpp
    session/session_client.cpp
    store/mobile_store.cpp
    secure/secure_store.cpp
    service/chat_service.cpp
    notify/notification_sink.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/protocol.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/session_protocol.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/group_protocol.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/file_protocol.cpp
)

target_include_directories(chatbox_clientcore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(chatbox_clientcore PUBLIC
    Qt6::Core
    Qt6::Network
    Qt6::Sql
)
