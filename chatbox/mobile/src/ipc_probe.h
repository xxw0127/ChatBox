#ifndef CHATBOX_MOBILE_IPC_PROBE_H
#define CHATBOX_MOBILE_IPC_PROBE_H

#include <QByteArray>
#include <QObject>
#include <QString>

class QLocalSocket;

class IpcProbe : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    explicit IpcProbe(QObject* parent = nullptr);

    QString status() const;
    Q_INVOKABLE void ping();

signals:
    void statusChanged();

private:
    void setStatus(const QString& status);

    QLocalSocket* socket_ = nullptr;
    QByteArray buffer_;
    QString status_ = QStringLiteral("service: 未连接");
};

#endif  // CHATBOX_MOBILE_IPC_PROBE_H
