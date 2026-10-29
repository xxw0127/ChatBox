#ifndef CHATBOX_CLIENTCORE_IPC_IPC_FRAME_H
#define CHATBOX_CLIENTCORE_IPC_IPC_FRAME_H

#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace chatbox {
namespace clientcore {

constexpr int kMaxIpcFrameBytes = 1024 * 1024;

QByteArray encodeIpcFrame(const QJsonObject& object);

// Returns false when the buffer does not yet contain a full frame.
// A non-empty error means the frame is invalid and the caller should close.
bool decodeIpcFrame(QByteArray& buffer, QJsonObject& object, QString* error);

}  // namespace clientcore
}  // namespace chatbox

#endif  // CHATBOX_CLIENTCORE_IPC_IPC_FRAME_H
