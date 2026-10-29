#include "ipc/ipc_protocol.h"

#include <QJsonValue>

namespace chatbox {
namespace clientcore {

QJsonObject ipcOk(quint64 id, const QJsonObject& data) {
    QJsonObject response;
    response.insert(QStringLiteral("id"),
                    static_cast<qint64>(id));
    response.insert(QStringLiteral("ok"), true);
    response.insert(QStringLiteral("data"), data);
    return response;
}

QJsonObject ipcError(quint64 id, int code, const QString& message) {
    QJsonObject response;
    response.insert(QStringLiteral("id"),
                    static_cast<qint64>(id));
    response.insert(QStringLiteral("ok"), false);
    QJsonObject error;
    error.insert(QStringLiteral("code"), code);
    error.insert(QStringLiteral("message"), message);
    response.insert(QStringLiteral("error"), error);
    return response;
}

bool decodeIpcRequest(const QJsonObject& object, QString& cmd,
                      QJsonObject& args, int& code, QString& error) {
    code = 0;
    error.clear();
    args = QJsonObject();

    const QJsonValue cmd_value = object.value(QStringLiteral("cmd"));
    if (!cmd_value.isString() || cmd_value.toString().isEmpty()) {
        code = 400;
        error = QStringLiteral("missing or invalid cmd");
        return false;
    }
    cmd = cmd_value.toString();

    const QJsonValue args_value = object.value(QStringLiteral("args"));
    if (!args_value.isUndefined() && !args_value.isNull()) {
        if (!args_value.isObject()) {
            code = 400;
            error = QStringLiteral("args must be an object");
            return false;
        }
        args = args_value.toObject();
    }
    return true;
}

QJsonObject ipcEvent(const QString& event, const QJsonObject& data) {
    QJsonObject object;
    object.insert(QStringLiteral("event"), event);
    object.insert(QStringLiteral("data"), data);
    return object;
}

}  // namespace clientcore
}  // namespace chatbox
