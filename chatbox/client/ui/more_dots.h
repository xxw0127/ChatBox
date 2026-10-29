#ifndef CHATBOX_CLIENT_UI_MORE_DOTS_H
#define CHATBOX_CLIENT_UI_MORE_DOTS_H

#include <QColor>
#include <QIcon>

namespace chatbox::client {

// Three rounded-square dots for the compact "more" button.
QIcon moreDotsIcon(const QColor& color = QColor(10, 11, 13));

// Window-with-side-panel glyph for the right-sidebar toggle. The right pane
// is filled when the panel is expanded and left as an outline when collapsed.
QIcon sidebarToggleIcon(bool expanded, const QColor& color = QColor(10, 11, 13));

// Three horizontal bars for settings/menu entry points.
QIcon hamburgerIcon(const QColor& color = QColor(10, 11, 13));

}  // namespace chatbox::client

#endif  // CHATBOX_CLIENT_UI_MORE_DOTS_H
