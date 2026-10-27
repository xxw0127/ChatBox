#ifndef CHATBOX_CLIENT_UI_MAIN_WINDOW_SUPPORT_H
#define CHATBOX_CLIENT_UI_MAIN_WINDOW_SUPPORT_H

// Private support header for the split MainWindow translation units. It keeps
// the original anonymous helpers available to every implementation file
// without exposing them through main_window.h.

#include "ui/main_window.h"

#include <algorithm>
#include <cstddef>
#include <string>

#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QCursor>
#include <QDebug>
#include <QFont>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QShortcut>
#include <QSize>
#include <QSplitter>
#include <QPainter>
#include <QPixmap>
#include <QScreen>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QUuid>
#include <QUrl>
#include <QVariant>
#include <QVBoxLayout>
#include <QWidget>

#include "core/avatar_utils.h"
#include "core/autostart.h"
#include "core/client_config.h"
#include "core/client_policy.h"
#include "core/history_store.h"
#include "core/identity.h"
#include "core/message_renderer.h"
#include "core/message_time.h"
#include "core/network_thread.h"
#include "core/protocol_qt.h"
#include "core/ui_theme.h"
#include "ui/chat_view.h"
#include "ui/dialog_buttons.h"
#include "ui/empty_state.h"
#include "ui/friend_item_delegate.h"
#include "ui/friend_list_widget.h"
#include "ui/friend_request_dialog.h"
#include "ui/login_page.h"
#include "ui/message_action_bar.h"
#include "ui/message_input.h"
#include "ui/message_search_dialog.h"
#include "ui/more_dots.h"
#include "ui/profile_dialog.h"
#include "ui/settings_dialog.h"
#include "ui/user_search_dialog.h"

namespace theme = chatbox::client::theme;

namespace chatbox {
namespace client {
namespace main_window_internal {

inline bool readString(ByteReader& reader, QString& value) {
    std::string text;
    if (!reader.getString(text)) {
        return false;
    }
    value = fromUtf8(text);
    return true;
}

inline QIcon avatarIcon(const QString& name, quint64 seed) {
    return QIcon(
        QPixmap::fromImage(chatbox::client::fallbackAvatar(name, seed, 36)));
}

inline QString errorText(quint16 code, const QString& message) {
    switch (code) {
        case 1001:
            return "请求参数错误";
        case 1002:
            return "会话已失效，请重新登录";
        case 1003:
            return "用户名已存在";
        case 1004:
            return "用户名或密码错误";
        case 1005:
            return "操作过于频繁，请稍后再试";
        case 1008:
            return "用户不存在";
        case 1009:
            return "会话已过期，请重新登录";
        case 1010:
            return "账号已在其他连接登录";
        case 1012:
            return "你们已经是好友";
        case 1013:
            return "好友申请已发送，请等待对方处理";
        case 1014:
            return "对方已向你发送好友申请，请先处理";
        case 1015:
            return "好友申请已被处理或不存在";
        case 1016:
            return "你们还不是好友";
        case 1017:
            return "头像格式不支持或超过 48 KiB";
        case 2001:
            return "服务器内部错误";
        default:
            return message;
    }
}

inline ConversationRow conversationRow(ChatContext context, quint64 target_id,
                                         const QString& title) {
    ConversationRow row;
    row.context = context;
    row.target_id = target_id;
    row.title = title;
    return row;
}

inline QString conversationAccessibleText(const QString& name, bool online,
                                           int unread,
                                           const QString& last_content) {
    QString text = name + (online ? "，在线" : "，离线");
    if (unread > 0) {
        text += QString("，%1 条未读").arg(unread);
    }
    if (!last_content.isEmpty()) {
        text += "，最后一条：" + last_content;
    }
    return text;
}

// Sidebar glyphs ship as very light artwork; drawing them as-is leaves them
// around 2:1 on a white sidebar. Tinting with the theme's muted colour keeps
// them legible in both appearances and keeps them in step with the palette.
inline QIcon tintedIcon(const QString& path, const QColor& color) {
    QPixmap source(path);
    if (source.isNull()) {
        return QIcon();
    }
    QPixmap tinted(source.size());
    tinted.fill(Qt::transparent);
    QPainter painter(&tinted);
    painter.drawPixmap(0, 0, source);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(tinted.rect(), color);
    painter.end();
    return QIcon(tinted);
}

}  // namespace main_window_internal
}  // namespace client
}  // namespace chatbox

using namespace chatbox::client::main_window_internal;

#endif  // CHATBOX_CLIENT_UI_MAIN_WINDOW_SUPPORT_H
