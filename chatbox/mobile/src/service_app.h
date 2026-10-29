#ifndef CHATBOX_MOBILE_SERVICE_APP_H
#define CHATBOX_MOBILE_SERVICE_APP_H

#include <QObject>
#include <QString>

namespace chatbox {
namespace clientcore {
class IpcServer;
}
}  // namespace chatbox

class ServiceApp : public QObject {
    Q_OBJECT

public:
    explicit ServiceApp(QObject* parent = nullptr);

    bool start(QString* error);

private:
    chatbox::clientcore::IpcServer* server_ = nullptr;
};

#endif  // CHATBOX_MOBILE_SERVICE_APP_H
