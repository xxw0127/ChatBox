#ifndef CHATBOX_CLIENT_UI_GROUP_SETTINGS_DIALOG_H
#define CHATBOX_CLIENT_UI_GROUP_SETTINGS_DIALOG_H

#include <QDialog>
#include <QVector>

#include "core/group_types.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

class GroupSettingsDialog : public QDialog {
    Q_OBJECT

public:
    GroupSettingsDialog(const chatbox::client::GroupInfo& group,
                        const QVector<chatbox::client::GroupMemberInfo>& members,
                        quint64 current_user_id, QWidget* parent = nullptr);

    void setMembers(const QVector<chatbox::client::GroupMemberInfo>& members);

signals:
    void renameRequested(quint64 room_id, const QString& name);
    void memberActionRequested(quint64 room_id, quint64 user_id, int action);
    void disbandRequested(quint64 room_id);
    void requestsRequested(quint64 room_id);

private:
    void refreshMembers();
    void updateActionState();
    void renameGroup();
    void disbandGroup();
    quint64 selectedUserId() const;
    int selectedRole() const;

    chatbox::client::GroupInfo group_;
    QVector<chatbox::client::GroupMemberInfo> members_;
    quint64 current_user_id_ = 0;
    QLabel* title_label_ = nullptr;
    QLineEdit* name_edit_ = nullptr;
    QPushButton* rename_button_ = nullptr;
    QListWidget* member_list_ = nullptr;
    QPushButton* set_admin_button_ = nullptr;
    QPushButton* unset_admin_button_ = nullptr;
    QPushButton* remove_button_ = nullptr;
    QPushButton* transfer_button_ = nullptr;
    QPushButton* disband_button_ = nullptr;
};

#endif  // CHATBOX_CLIENT_UI_GROUP_SETTINGS_DIALOG_H
