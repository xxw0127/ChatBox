#ifndef CHATBOX_CLIENT_CORE_APP_SETTINGS_H
#define CHATBOX_CLIENT_CORE_APP_SETTINGS_H

#include <QSettings>
#include <QString>

namespace chatbox {
namespace client {

// One-time migration of the stored settings from the pre-ChatBox identifiers
// (organisation "Chatroom", application "ChatroomClient") to the ChatBox
// identifiers. Copies every key only when the current store is still empty and
// the legacy store has values, then syncs; returns true when values were
// copied. Existing values are never overwritten, so calling it on every start
// is safe.
bool migrateLegacySettings(QSettings& current, QSettings& legacy);

// Rewrites any saved server endpoint to the fixed production endpoint.
// Old session tokens belong to the old host, so the session group is cleared
// to force a fresh login.
bool migrateServerEndpoint(QSettings& settings);

// True when the history directory is the install directory or below it. Such
// a location would be deleted by the updater's mirror replacement.
bool isHistoryDirectoryInsideInstall(const QString& directory);

// Default and persisted location for received files:
// QStandardPaths::DownloadLocation + "/ChatBox".
QString defaultFileSaveDirectory();
QString fileSaveDirectory();
bool validateFileSaveDirectory(const QString& directory, QString* error);
bool setFileSaveDirectory(const QString& directory, QString* error);
// Returns a non-existing path inside directory; collisions get " (1)" etc.
QString uniqueFilePath(const QString& directory, const QString& name);

}  // namespace client
}  // namespace chatbox

#endif  // CHATBOX_CLIENT_CORE_APP_SETTINGS_H
