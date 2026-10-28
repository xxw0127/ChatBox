QT += core gui widgets testlib sql network
CONFIG += console c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = client_tests

INCLUDEPATH += $$PWD/..
RESOURCES += ../resources/resources.qrc
SOURCES += \
    test_client_core.cpp \
    ../core/app_settings.cpp \
    ../core/autostart.cpp \
    ../core/client_policy.cpp \
    ../core/credential_store.cpp \
    ../core/account_store.cpp \
    ../core/focus_ring.cpp \
    ../core/ui_theme.cpp \
    ../core/client_log.cpp \
    ../core/message_time.cpp \
    ../core/message_store.cpp \
    ../core/outbox.cpp \
    ../core/message_renderer.cpp \
    ../core/mention_query.cpp \
    ../core/contact_store.cpp \
    ../core/update_manifest.cpp \
    ../core/update_service.cpp \
    ../core/update_installer.cpp \
    ../core/avatar_utils.cpp \
    ../core/avatar_cache.cpp \
    ../core/history_store.cpp \
    ../ui/chat_view.cpp \
    ../ui/empty_state.cpp \
    ../ui/message_input.cpp \
    ../ui/mention_popup.cpp \
    ../ui/mention_item_delegate.cpp \
    ../ui/profile_dialog.cpp \
    ../ui/login_page.cpp \
    ../ui/more_dots.cpp \
    ../ui/settings_dialog.cpp \
    ../ui/update_dialog.cpp \
    ../ui/account_switcher_dialog.cpp
HEADERS += \
    ../core/app_settings.h \
    ../core/autostart.h \
    ../core/client_policy.h \
    ../core/credential_store.h \
    ../core/focus_ring.h \
    ../core/client_log.h \
    ../core/chat_message.h \
    ../core/message_time.h \
    ../core/message_store.h \
    ../core/outbox.h \
    ../core/message_renderer.h \
    ../core/mention_query.h \
    ../core/ui_theme.h \
    ../core/contact_store.h \
    ../core/update_manifest.h \
    ../core/update_service.h \
    ../core/update_installer.h \
    ../core/avatar_utils.h \
    ../core/avatar_cache.h \
    ../core/history_store.h \
    ../core/user_profile.h \
    ../ui/chat_view.h \
    ../ui/empty_state.h \
    ../ui/message_input.h \
    ../ui/mention_popup.h \
    ../ui/mention_item_delegate.h \
    ../ui/profile_dialog.h \
    ../ui/login_page.h \
    ../ui/more_dots.h \
    ../ui/settings_dialog.h \
    ../ui/update_dialog.h \
    ../ui/account_switcher_dialog.h

win32: LIBS += -lcrypt32
