QT += core gui widgets sql

CONFIG += c++17
TEMPLATE = app
TARGET = chatbox

INCLUDEPATH += $$PWD/..

RESOURCES += resources/resources.qrc

win32 {
    RC_FILE = resources/app.rc
    RC_INCLUDEPATH += $$PWD/resources
    OPENSSL_DIR = "C:/Program Files/OpenSSL-Win64"
    INCLUDEPATH += "$$OPENSSL_DIR/include"
    LIBS += "$$OPENSSL_DIR/lib/VC/x64/MD/libssl.lib" \
            "$$OPENSSL_DIR/lib/VC/x64/MD/libcrypto.lib" \
            -lws2_32 -lcrypt32
}

SOURCES += \
    main.cpp \
    ../common/protocol.cpp \
    core/app_settings.cpp \
    core/autostart.cpp \
    core/client_policy.cpp \
    core/credential_store.cpp \
    core/account_store.cpp \
    core/focus_ring.cpp \
    core/ui_theme.cpp \
    core/socket_compat.cpp \
    core/protocol_qt.cpp \
    core/network_thread.cpp \
    core/client_log.cpp \
    core/message_time.cpp \
    core/message_store.cpp \
    core/outbox.cpp \
    core/message_renderer.cpp \
    core/contact_store.cpp \
    core/update_manifest.cpp \
    core/avatar_utils.cpp \
    core/avatar_cache.cpp \
    core/history_store.cpp \
    ui/chat_view.cpp \
    ui/account_switcher_dialog.cpp \
    ui/empty_state.cpp \
    ui/message_input.cpp \
    ui/message_action_bar.cpp \
    ui/login_page.cpp \
    ui/more_dots.cpp \
    ui/friend_request_dialog.cpp \
    ui/friend_item_delegate.cpp \
    ui/friend_list_widget.cpp \
    ui/main_window.cpp \
    ui/main_window_chat.cpp \
    ui/main_window_contacts.cpp \
    ui/main_window_net.cpp \
    ui/main_window_profile.cpp \
    ui/main_window_tray.cpp \
    ui/message_search_dialog.cpp \
    ui/settings_dialog.cpp \
    ui/user_search_dialog.cpp \
    ui/profile_dialog.cpp

HEADERS += \
    core/app_settings.h \
    core/autostart.h \
    core/client_policy.h \
    core/credential_store.h \
    core/account_store.h \
    core/focus_ring.h \
    core/socket_compat.h \
    core/protocol_qt.h \
    core/network_thread.h \
    core/client_log.h \
    core/chat_message.h \
    core/message_time.h \
    core/message_store.h \
    core/outbox.h \
    core/message_renderer.h \
    core/ui_theme.h \
    core/contact_store.h \
    core/update_manifest.h \
    core/avatar_utils.h \
    core/avatar_cache.h \
    core/history_store.h \
    core/friend_view.h \
    core/user_profile.h \
    ui/chat_view.h \
    ui/account_switcher_dialog.h \
    ui/dialog_buttons.h \
    ui/empty_state.h \
    ui/message_input.h \
    ui/message_action_bar.h \
    ui/login_page.h \
    ui/more_dots.h \
    ui/friend_request_dialog.h \
    ui/friend_item_delegate.h \
    ui/friend_list_widget.h \
    ui/main_window.h \
    ui/main_window_support.h \
    ui/message_search_dialog.h \
    ui/settings_dialog.h \
    ui/user_search_dialog.h \
    ui/profile_dialog.h

win32-g++: QMAKE_CXXFLAGS += -Wall -Wextra

# Keep the CA bundle next to the executable for both release and debug builds.
CONFIG(debug, debug|release) {
    CERT_OUT_DIR = $$OUT_PWD/debug
} else {
    CERT_OUT_DIR = $$OUT_PWD/release
}
QMAKE_POST_LINK += $$QMAKE_COPY_DIR $$shell_path($$PWD/certs) $$shell_path($$CERT_OUT_DIR/certs)

