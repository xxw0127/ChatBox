#ifndef CHATBOX_CLIENTCORE_IPC_IPC_NAMES_H
#define CHATBOX_CLIENTCORE_IPC_IPC_NAMES_H

#include <QDir>
#include <QStandardPaths>
#include <QString>

namespace chatbox {
namespace clientcore {

inline QString ipcServerPath() {
    QString directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (directory.isEmpty()) {
        directory = QDir::homePath() + QStringLiteral("/.chatbox");
    }
    QDir().mkpath(directory);
    return directory + QStringLiteral("/chatbox_service");
}

}  // namespace clientcore
}  // namespace chatbox

#endif  // CHATBOX_CLIENTCORE_IPC_IPC_NAMES_H
