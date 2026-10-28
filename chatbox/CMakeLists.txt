cmake_minimum_required(VERSION 3.16)
project(chatbox_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -O2)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

find_path(MYSQL_INCLUDE_DIR mysql.h PATHS /usr/include/mysql)
find_library(MYSQL_LIBRARY NAMES mysqlclient)
find_library(ARGON2_LIBRARY NAMES argon2)
if(NOT MYSQL_INCLUDE_DIR OR NOT MYSQL_LIBRARY)
    message(FATAL_ERROR "MySQL client development files not found")
endif()
if(NOT ARGON2_LIBRARY)
    message(FATAL_ERROR "libargon2 not found")
endif()

add_library(chatbox_common
    common/protocol.cpp
    common/group_protocol.cpp
    common/file_protocol.cpp
    common/text_utils.cpp
)
target_include_directories(chatbox_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(chatbox_metrics
    server/util/logging.cpp
    server/util/metrics.cpp
)
target_include_directories(chatbox_metrics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(chatbox_metrics PUBLIC Threads::Threads)

add_library(chatbox_net
    server/net/buffer.cpp
    server/net/connection.cpp
    server/net/connection_limiter.cpp
    server/net/io_thread.cpp
    server/net/acceptor.cpp
)
target_include_directories(chatbox_net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(chatbox_net PUBLIC chatbox_common chatbox_metrics Threads::Threads)

add_executable(chatbox_server
    server/main.cpp
    server/config.cpp
    server/db/mysql_connection.cpp
    server/db/mysql_connection_groups.cpp
    server/db/mysql_connection_files.cpp
    server/auth/password_hash.cpp
    server/auth/token.cpp
    server/auth/login_rate_limiter.cpp
    server/auth/registration_rate_limiter.cpp
    server/auth/session_manager.cpp
    server/auth/auth_service.cpp
    server/business/worker_pool.cpp
    server/group/group_rules.cpp
    server/group/group_service.cpp
    server/file/file_rules.cpp
    server/file/file_service.cpp
    server/file/file_thumbnailer.cpp
    server/room/room_manager.cpp
    server/room/room_service.cpp
    server/private/private_service.cpp
    server/history/history_service.cpp
    server/friend/friend_service.cpp
    server/profile/profile_service.cpp
    server/presence/presence_service.cpp
)
target_include_directories(chatbox_server PRIVATE ${MYSQL_INCLUDE_DIR})
target_link_libraries(chatbox_server PRIVATE
    chatbox_net
    ${MYSQL_LIBRARY}
    ${ARGON2_LIBRARY}
    OpenSSL::Crypto
)

enable_testing()

add_executable(test_protocol tests/test_protocol.cpp)
target_link_libraries(test_protocol PRIVATE chatbox_common)
add_test(NAME protocol COMMAND test_protocol)

add_executable(test_presence tests/test_presence.cpp server/presence/presence_service.cpp)
target_link_libraries(test_presence PRIVATE chatbox_common)
add_test(NAME presence COMMAND test_presence)

add_executable(test_session_manager
    tests/test_session_manager.cpp
    server/auth/session_manager.cpp)
target_include_directories(test_session_manager PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_session_manager PRIVATE Threads::Threads)
add_test(NAME session_manager COMMAND test_session_manager)

add_executable(test_text_utils tests/test_text_utils.cpp)
target_link_libraries(test_text_utils PRIVATE chatbox_common)
add_test(NAME text_utils COMMAND test_text_utils)

add_executable(test_buffer tests/test_buffer.cpp)
target_link_libraries(test_buffer PRIVATE chatbox_net)
add_test(NAME buffer COMMAND test_buffer)

add_executable(test_metrics tests/test_metrics.cpp)
target_link_libraries(test_metrics PRIVATE chatbox_metrics)
add_test(NAME metrics COMMAND test_metrics)

add_executable(test_auth
    tests/test_auth.cpp
    server/auth/password_hash.cpp
    server/auth/token.cpp
)
target_include_directories(test_auth PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_auth PRIVATE ${ARGON2_LIBRARY} OpenSSL::Crypto)
add_test(NAME auth COMMAND test_auth)

add_executable(test_registration_rate_limiter
    tests/test_registration_rate_limiter.cpp
    server/auth/registration_rate_limiter.cpp)
target_include_directories(test_registration_rate_limiter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_registration_rate_limiter PRIVATE Threads::Threads)
add_test(NAME registration_rate_limiter COMMAND test_registration_rate_limiter)

add_executable(test_connection_limiter tests/test_connection_limiter.cpp)
target_link_libraries(test_connection_limiter PRIVATE chatbox_net)
add_test(NAME connection_limiter COMMAND test_connection_limiter)

add_executable(test_connection_proxy tests/test_connection_proxy.cpp)
target_link_libraries(test_connection_proxy PRIVATE chatbox_net)
add_test(NAME connection_proxy COMMAND test_connection_proxy)

add_executable(test_group_rules
    tests/test_group_rules.cpp
    server/group/group_rules.cpp)
target_include_directories(test_group_rules PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_group_rules PRIVATE chatbox_common)
add_test(NAME group_rules COMMAND test_group_rules)

add_executable(test_group_protocol tests/test_group_protocol.cpp)
target_link_libraries(test_group_protocol PRIVATE chatbox_common)
add_test(NAME group_protocol COMMAND test_group_protocol)

add_executable(test_file_rules
    tests/test_file_rules.cpp
    server/file/file_rules.cpp)
target_include_directories(test_file_rules PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_file_rules PRIVATE chatbox_common)
add_test(NAME file_rules COMMAND test_file_rules)

add_executable(test_file_protocol tests/test_file_protocol.cpp)
target_link_libraries(test_file_protocol PRIVATE chatbox_common)
add_test(NAME file_protocol COMMAND test_file_protocol)

add_executable(test_file_thumbnail_logic
    tests/test_file_thumbnail_logic.cpp
    server/file/file_thumbnailer.cpp)
target_include_directories(test_file_thumbnail_logic PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME file_thumbnail_logic COMMAND test_file_thumbnail_logic)
