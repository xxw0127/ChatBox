#ifndef CHATBOX_CLIENT_UI_FILE_MANAGER_DIALOG_H
#define CHATBOX_CLIENT_UI_FILE_MANAGER_DIALOG_H

#include <QDialog>
#include <QVector>

#include "core/file_types.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

class FileManagerDialog : public QDialog {
    Q_OBJECT

public:
    FileManagerDialog(int kind, quint64 target_id, bool can_upload,
                      QWidget* parent = nullptr);

    void setFiles(quint32 total_count, quint64 used_bytes,
                  const QVector<chatbox::client::FileEntryView>& entries);
    void setStatus(const QString& text, bool error = false);
    void setBusy(bool busy);

signals:
    void refreshRequested();
    void uploadRequested();
    void downloadRequested(quint64 file_id, const QString& name,
                           const QString& mime);
    void downloadToggleRequested(quint64 file_id);
    void deleteRequested(quint64 file_id);

private:
    void applyFilter();
    void updateButtons();

    int kind_ = 1;
    quint64 target_id_ = 0;
    QLabel* title_ = nullptr;
    QLabel* quota_ = nullptr;
    QLabel* status_ = nullptr;
    QLineEdit* filter_ = nullptr;
    QListWidget* list_ = nullptr;
    QPushButton* upload_ = nullptr;
    QPushButton* download_ = nullptr;
    QPushButton* download_toggle_ = nullptr;
    QPushButton* remove_ = nullptr;
    QVector<chatbox::client::FileEntryView> entries_;
};

#endif  // CHATBOX_CLIENT_UI_FILE_MANAGER_DIALOG_H
