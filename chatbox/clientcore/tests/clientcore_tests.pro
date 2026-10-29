QT += core network sql testlib
CONFIG += console c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = clientcore_tests

SOURCES += \
    test_main.cpp \
    test_session_client.cpp \
    test_mobile_store.cpp \
    ../session/session_client.cpp \
    ../store/mobile_store.cpp \
    ../transport/transport.cpp \
    ../../common/protocol.cpp \
    ../../common/session_protocol.cpp \
    ../../common/group_protocol.cpp \
    ../../common/file_protocol.cpp

HEADERS += \
    fake_transport.h \
    test_session_client.h \
    test_mobile_store.h \
    ../session/session_client.h \
    ../store/mobile_store.h \
    ../transport/transport.h

INCLUDEPATH += .. ../.. ../../..
