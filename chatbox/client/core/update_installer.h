#ifndef CHATBOX_CLIENT_CORE_UPDATE_INSTALLER_H
#define CHATBOX_CLIENT_CORE_UPDATE_INSTALLER_H

#include <QString>

namespace chatbox {
namespace client {

// Checks the capabilities required for a safe in-place portable update.
bool canInstallUpdates(QString* reason);

// Validates the extracted payload before the updater may replace anything.
bool validateUpdateDirectory(const QString& directory, QString* error);

// Extracts a release ZIP with Windows' bundled tar.exe and returns the
// directory containing chatbox.exe.
bool extractUpdateZip(const QString& zip, const QString& staging,
                      QString* extracted_root, QString* error);

// Starts the fixed PowerShell helper. Paths are passed as separate arguments;
// the update ZIP is never used as script input.
bool launchUpdateHelper(quint64 process_id, const QString& source_dir,
                        const QString& target_dir, const QString& executable,
                        QString* error);

// Reads and removes the failure marker left by the helper.
QString consumeUpdateResult();

}  // namespace client
}  // namespace chatbox

#endif  // CHATBOX_CLIENT_CORE_UPDATE_INSTALLER_H
