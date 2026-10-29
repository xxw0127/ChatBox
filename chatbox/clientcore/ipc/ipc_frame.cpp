#include "ipc/ipc_frame.h"

#include <QJsonDocument>
#include <QJsonParseError>

namespace chatbox {
namespace clientcore {

QByteArray encodeIpcFrame(const QJsonObject& object) {
    const QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (payload.isEmpty() || payload.size() > kMaxIpcFrameBytes) {
        return {};
    }
    QByteArray frame;
    frame.resize(4);
    const quint32 size = static_cast<quint32>(payload.size());
    frame[0] = static_cast<char>((size >> 24) & 0xFF);
    frame[1] = static_cast<char>((size >> 16) & 0xFF);
    frame[2] = static_cast<char>((size >> 8) & 0xFF);
    frame[3] = static_cast<char>(size & 0xFF);
    frame.append(payload);
    return frame;
}

bool decodeIpcFrame(QByteArray& buffer, QJsonObject& object, QString* error) {
    if (error != nullptr) {
        error->clear();
    }
    if (buffer.size() < 4) {
        return false;
    }
    const quint32 size = (static_cast<quint32>(static_cast<unsigned char>(buffer[0])) << 24) |
                         (static_cast<quint32>(static_cast<unsigned char>(buffer[1])) << 16) |
                         (static_cast<quint32>(static_cast<unsigned char>(buffer[2])) << 8) |
                         static_cast<quint32>(static_cast<unsigned char>(buffer[3]));
    if (size == 0 || size > static_cast<quint32>(kMaxIpcFrameBytes)) {
        if (error != nullptr) {
            *error = QStringLiteral("invalid IPC frame size");
        }
        return false;
    }
    if (buffer.size() < static_cast<int>(4 + size)) {
        return false;
    }

    QJsonParseError parse_error;
    const QJsonDocument document =
        QJsonDocument::fromJson(buffer.mid(4, static_cast<int>(size)), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        if (error != nullptr) {
            *error = QStringLiteral("invalid IPC JSON: ") + parse_error.errorString();
        }
        return false;
    }
    buffer.remove(0, static_cast<int>(4 + size));
    object = document.object();
    return true;
}

}  // namespace clientcore
}  // namespace chatbox
