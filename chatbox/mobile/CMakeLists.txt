cmake_minimum_required(VERSION 3.21)

project(chatbox_mobile LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Network Sql)

qt_standard_project_setup()
qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0002 NEW)
qt_policy(SET QTP0004 NEW)

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../clientcore" clientcore)

qt_add_executable(chatbox_mobile
    src/main.cpp
    src/connection_tester.h
    src/connection_tester.cpp
    src/ipc_probe.h
    src/ipc_probe.cpp
    src/service_app.h
    src/service_app.cpp
    src/android_notification_sink.h
    src/android_notification_sink.cpp
    src/android_secure_store.h
    src/android_secure_store.cpp
)

qt_add_resources(chatbox_mobile "mobile_ca"
    PREFIX "/"
    FILES certs/ca.crt
)

set(CMAKE_ANDROID_OPENSSL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android/libs/${ANDROID_ABI}")
if(EXISTS "${CMAKE_ANDROID_OPENSSL_DIR}/libssl_3.so")
    set_target_properties(chatbox_mobile PROPERTIES
        QT_ANDROID_EXTRA_LIBS
        "${CMAKE_ANDROID_OPENSSL_DIR}/libssl_3.so;${CMAKE_ANDROID_OPENSSL_DIR}/libcrypto_3.so"
    )
endif()

set_target_properties(chatbox_mobile PROPERTIES
    QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android"
    QT_ANDROID_MIN_SDK_VERSION 28
    QT_ANDROID_TARGET_SDK_VERSION 35
    QT_ANDROID_PACKAGE_NAME "com.chatbox.mobile"
    QT_ANDROID_VERSION_CODE 1
    QT_ANDROID_VERSION_NAME "0.1.0"
)

qt_add_qml_module(chatbox_mobile
    URI ChatBox
    VERSION 1.0
    QML_FILES
        qml/Main.qml
)

target_link_libraries(chatbox_mobile PRIVATE
    Qt6::Core
    Qt6::CorePrivate
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Network
    Qt6::Sql
    chatbox_clientcore
)
