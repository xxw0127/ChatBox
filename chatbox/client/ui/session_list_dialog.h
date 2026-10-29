#ifndef CHATBOX_CLIENT_UI_SESSION_LIST_DIALOG_H
#define CHATBOX_CLIENT_UI_SESSION_LIST_DIALOG_H

#include <QDialog>
#include <QString>
#include <QVector>

class QTableWidget;

struct SessionView {
    QString session_id;
    QString device_name;
    QString platform;
    QString app_version;
    QString last_seen_at;
    bool current = false;
};

class SessionListDialog : public QDialog {
    Q_OBJECT

public:
    explicit SessionListDialog(QWidget* parent = nullptr);

    void setSessions(const QVector<SessionView>& sessions);

signals:
    void revokeRequested(const QString& session_id);
    void refreshRequested();

private:
    QTableWidget* table_ = nullptr;
};

#endif  // CHATBOX_CLIENT_UI_SESSION_LIST_DIALOG_H
