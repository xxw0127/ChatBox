#ifndef CHATBOX_CLIENTCORE_NOTIFY_NOTIFICATION_SINK_H
#define CHATBOX_CLIENTCORE_NOTIFY_NOTIFICATION_SINK_H

#include <QString>
#include <QtGlobal>

namespace chatbox {
namespace clientcore {

// Platform notification boundary. The service owns one sink; tests and other
// platforms use a no-op implementation.
class NotificationSink {
public:
    virtual ~NotificationSink() = default;

    // One notification per conversation; posting the same id replaces it.
    virtual void postMessage(int id, const QString& title, const QString& body,
                             int kind, quint64 target_id) = 0;
    virtual void cancel(int id) = 0;
    virtual void setForegroundState(const QString& text) = 0;
};

class NullNotificationSink : public NotificationSink {
public:
    void postMessage(int id, const QString& title, const QString& body,
                     int kind, quint64 target_id) override;
    void cancel(int id) override;
    void setForegroundState(const QString& text) override;
};

}  // namespace clientcore
}  // namespace chatbox

#endif  // CHATBOX_CLIENTCORE_NOTIFY_NOTIFICATION_SINK_H
