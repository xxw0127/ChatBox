#include "ui/more_dots.h"

#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QRectF>

namespace chatbox::client {

QIcon moreDotsIcon(const QColor& color) {
    const int canvas = 20;
    const qreal dot = 3;
    const qreal gap = 1.6;
    const qreal corner = 1.2;
    const qreal total = dot * 3 + gap * 2;

    QPixmap pixmap(canvas, canvas);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);

    const qreal left = (canvas - total) / 2.0;
    const qreal top = (canvas - dot) / 2.0;
    for (int i = 0; i < 3; ++i) {
        painter.drawRoundedRect(QRectF(left + i * (dot + gap), top, dot, dot),
                                corner, corner);
    }
    painter.end();
    return QIcon(pixmap);
}

QIcon sidebarToggleIcon(bool expanded, const QColor& color) {
    const int canvas = 20;
    QPixmap pixmap(canvas, canvas);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF frame(3.0, 4.0, 14.0, 12.0);
    const qreal divider_x = frame.left() + frame.width() * 0.66;
    QPainterPath rounded;
    rounded.addRoundedRect(frame, 2.0, 2.0);

    if (expanded) {
        // Fill the sidebar pane, clipped so the rounded outline stays clean.
        painter.save();
        painter.setClipPath(rounded);
        painter.fillRect(QRectF(divider_x, frame.top(),
                                frame.right() - divider_x, frame.height()),
                         color);
        painter.restore();
    }

    QPen outline(color);
    outline.setWidthF(1.4);
    outline.setJoinStyle(Qt::RoundJoin);
    painter.setPen(outline);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(rounded);
    if (!expanded) {
        QPen divider(color);
        divider.setWidthF(1.2);
        painter.setPen(divider);
        painter.drawLine(QPointF(divider_x, frame.top() + 1.5),
                         QPointF(divider_x, frame.bottom() - 1.5));
    }
    painter.end();
    return QIcon(pixmap);
}

QIcon hamburgerIcon(const QColor& color) {
    const int canvas = 20;
    QPixmap pixmap(canvas, canvas);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color);
    pen.setWidthF(1.7);
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);
    const qreal left = 4.0;
    const qreal right = 16.0;
    for (qreal y : {6.0, 10.0, 14.0}) {
        painter.drawLine(QPointF(left, y), QPointF(right, y));
    }
    painter.end();
    return QIcon(pixmap);
}

}  // namespace chatbox::client
