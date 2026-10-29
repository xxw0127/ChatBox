#include "notify/notification_sink.h"

namespace chatbox {
namespace clientcore {

void NullNotificationSink::postMessage(int id, const QString& title,
                                       const QString& body, int kind,
                                       quint64 target_id) {
    Q_UNUSED(id);
    Q_UNUSED(title);
    Q_UNUSED(body);
    Q_UNUSED(kind);
    Q_UNUSED(target_id);
}

void NullNotificationSink::cancel(int id) {
    Q_UNUSED(id);
}

void NullNotificationSink::setForegroundState(const QString& text) {
    Q_UNUSED(text);
}

}  // namespace clientcore
}  // namespace chatbox
