#ifndef CHATBOX_MOBILE_SERVICE_APP_H
#define CHATBOX_MOBILE_SERVICE_APP_H

#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>

#include <memory>

namespace chatbox {
namespace clientcore {
class ChatService;
class IpcServer;
}  // namespace clientcore
namespace mobile {
class AndroidNotificationSink;
class AndroidSecureStore;
}  // namespace mobile
}  // namespace chatbox

class ServiceApp : public QObject {
    Q_OBJECT

public:
    explicit ServiceApp(QObject* parent = nullptr);
    ~ServiceApp() override;

    bool start(QString* error);

private:
    QJsonObject handleRequest(quint64 id, const QJsonObject& request);
    void forwardChatEvent(const QJsonObject& event);
    void postMessageNotification(const QJsonObject& event);
    static int notificationId(int kind, quint64 target_id);

    chatbox::clientcore::IpcServer* server_ = nullptr;
    chatbox::clientcore::ChatService* chat_ = nullptr;
    std::unique_ptr<chatbox::mobile::AndroidNotificationSink> notifications_;
    std::unique_ptr<chatbox::mobile::AndroidSecureStore> secure_;
    int active_kind_ = 0;
    quint64 active_target_id_ = 0;
    QSet<int> notification_ids_;
};

#endif  // CHATBOX_MOBILE_SERVICE_APP_H
