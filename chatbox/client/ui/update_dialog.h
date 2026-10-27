#ifndef CHATBOX_CLIENT_UI_UPDATE_DIALOG_H
#define CHATBOX_CLIENT_UI_UPDATE_DIALOG_H

#include <QDialog>

#include "core/update_manifest.h"

class QLabel;
class QProgressBar;
class QPushButton;

// Non-modal prompt shown after the background update check finds a newer
// release. It never blocks login or the main window event loop.
class UpdateDialog : public QDialog {
    Q_OBJECT

public:
    explicit UpdateDialog(const chatbox::client::UpdateManifest& manifest,
                          QWidget* parent = nullptr);

    void setDownloadProgress(qint64 received, qint64 total);
    void setBusy(bool busy);

signals:
    void updateRequested();

private:
    QLabel* notes_label_ = nullptr;
    QProgressBar* progress_bar_ = nullptr;
    QPushButton* update_button_ = nullptr;
    QPushButton* later_button_ = nullptr;
};

#endif  // CHATBOX_CLIENT_UI_UPDATE_DIALOG_H
