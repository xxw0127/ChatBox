QT += core network sql testlib
CONFIG += console c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = clientcore_tests

SOURCES += \
    test_main.cpp \
    test_session_client.cpp \
    test_mobile_store.cpp \
    test_secure_store.cpp \
    test_chat_service.cpp \
    test_ipc.cpp \
    ../session/session_client.cpp \
    ../store/mobile_store.cpp \
    ../secure/secure_store.cpp \
    ../service/chat_service.cpp \
    ../ipc/ipc_frame.cpp \
    ../ipc/ipc_protocol.cpp \
    ../ipc/ipc_client.cpp \
    ../ipc/ipc_server.cpp \
    ../transport/transport.cpp \
    ../transport/qt_ssl_transport.cpp \
    ../../common/protocol.cpp \
    ../../common/session_protocol.cpp \
    ../../common/group_protocol.cpp \
    ../../common/file_protocol.cpp

HEADERS += \
    fake_transport.h \
    test_session_client.h \
    test_mobile_store.h \
    test_secure_store.h \
    test_chat_service.h \
    test_ipc.h \
    ../session/session_client.h \
    ../store/mobile_store.h \
    ../secure/secure_store.h \
    ../service/chat_service.h \
    ../ipc/ipc_frame.h \
    ../ipc/ipc_protocol.h \
    ../ipc/ipc_client.h \
    ../ipc/ipc_server.h \
    ../transport/transport.h \
    ../transport/qt_ssl_transport.h

INCLUDEPATH += .. ../.. ../../..
