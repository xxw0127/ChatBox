QT += core network testlib
CONFIG += console c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = clientcore_tests

SOURCES += \
    test_session_client.cpp \
    ../session/session_client.cpp \
    ../transport/transport.cpp \
    ../../common/protocol.cpp \
    ../../common/session_protocol.cpp \
    ../../common/group_protocol.cpp \
    ../../common/file_protocol.cpp

HEADERS += \
    fake_transport.h \
    ../session/session_client.h \
    ../transport/transport.h

INCLUDEPATH += .. ../.. ../../..
