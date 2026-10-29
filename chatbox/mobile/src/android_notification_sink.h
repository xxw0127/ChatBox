#ifndef CHATBOX_MOBILE_ANDROID_NOTIFICATION_SINK_H
#define CHATBOX_MOBILE_ANDROID_NOTIFICATION_SINK_H

#include "notify/notification_sink.h"

namespace chatbox {
namespace mobile {

class AndroidNotificationSink : public chatbox::clientcore::NotificationSink {
public:
    void postMessage(int id, const QString& title, const QString& body,
                     int kind, quint64 target_id) override;
    void cancel(int id) override;
    void setForegroundState(const QString& text) override;
};

}  // namespace mobile
}  // namespace chatbox

#endif  // CHATBOX_MOBILE_ANDROID_NOTIFICATION_SINK_H
