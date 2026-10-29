#ifndef CHATBOX_CLIENTCORE_IPC_IPC_PROTOCOL_H
#define CHATBOX_CLIENTCORE_IPC_IPC_PROTOCOL_H

#include <QJsonObject>
#include <QString>
#include <QtGlobal>

namespace chatbox {
namespace clientcore {

// Response envelope: {id, ok: true, data} or {id, ok: false, error:{code,message}}.
QJsonObject ipcOk(quint64 id, const QJsonObject& data);
QJsonObject ipcError(quint64 id, int code, const QString& message);

// Request envelope: {id, cmd, args?}. On failure code/error describe the
// malformed request so the server can answer with ipcError.
bool decodeIpcRequest(const QJsonObject& object, QString& cmd,
                      QJsonObject& args, int& code, QString& error);

// Event envelope: {event, data}.
QJsonObject ipcEvent(const QString& event, const QJsonObject& data);

}  // namespace clientcore
}  // namespace chatbox

#endif  // CHATBOX_CLIENTCORE_IPC_IPC_PROTOCOL_H
