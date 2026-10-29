#ifndef CHATBOX_MOBILE_CONNECTION_TESTER_H
#define CHATBOX_MOBILE_CONNECTION_TESTER_H

#include <QObject>
#include <QString>

namespace chatbox {
namespace clientcore {
class QtSslTransport;
}
}  // namespace chatbox

class ConnectionTester : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    explicit ConnectionTester(QObject* parent = nullptr);

    QString status() const;
    Q_INVOKABLE void test();

signals:
    void statusChanged();

private:
    void setStatus(const QString& status);

    chatbox::clientcore::QtSslTransport* transport_ = nullptr;
    QString status_ = QStringLiteral("未连接");
};

#endif  // CHATBOX_MOBILE_CONNECTION_TESTER_H
