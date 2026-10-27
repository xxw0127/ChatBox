#ifndef CHATBOX_CLIENT_CORE_UPDATE_MANIFEST_H
#define CHATBOX_CLIENT_CORE_UPDATE_MANIFEST_H

#include <QByteArray>
#include <QString>
#include <QUrl>

namespace chatbox {
namespace client {

// The small, network-independent part of the update protocol. The transport
// is responsible for fetching the bytes; this type only validates their shape.
struct UpdateManifest {
    QString version;
    QUrl url;
    QString sha256;
    QString notes;
    bool valid = false;
};

UpdateManifest parseUpdateManifest(const QByteArray& json, QString* error);
bool isNewerVersion(const QString& current, const QString& remote);

}  // namespace client
}  // namespace chatbox

#endif  // CHATBOX_CLIENT_CORE_UPDATE_MANIFEST_H
