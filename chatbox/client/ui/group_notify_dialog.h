#ifndef CHATBOX_CLIENT_UI_GROUP_NOTIFY_DIALOG_H
#define CHATBOX_CLIENT_UI_GROUP_NOTIFY_DIALOG_H

#include <QDialog>
#include <QVector>

#include "core/group_types.h"

class QListWidget;
class QPushButton;

class GroupNotifyDialog : public QDialog {
    Q_OBJECT

public:
    explicit GroupNotifyDialog(QWidget* parent = nullptr);

    void setData(const QVector<chatbox::client::GroupRequestInfo>& requests,
                 const QVector<chatbox::client::GroupRequestInfo>& invites,
                 bool admin);

signals:
    void actionRequested(int type, quint64 id, bool accept);

private:
    void updateActionState();
    QListWidget* list_ = nullptr;
    QPushButton* accept_button_ = nullptr;
    QPushButton* reject_button_ = nullptr;
};

#endif  // CHATBOX_CLIENT_UI_GROUP_NOTIFY_DIALOG_H
