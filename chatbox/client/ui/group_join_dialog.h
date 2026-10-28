#ifndef CHATBOX_CLIENT_UI_GROUP_JOIN_DIALOG_H
#define CHATBOX_CLIENT_UI_GROUP_JOIN_DIALOG_H

#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;

class GroupJoinDialog : public QDialog {
    Q_OBJECT

public:
    explicit GroupJoinDialog(QWidget* parent = nullptr);

    void setSearchResult(bool found, const QString& name, int member_count,
                         int membership);

signals:
    void searchRequested(const QString& group_no);
    void joinRequested(const QString& group_no, const QString& message);

private:
    void search();
    void submit();
    void updateSubmitState();

    QLineEdit* group_no_edit_ = nullptr;
    QLineEdit* message_edit_ = nullptr;
    QLabel* result_label_ = nullptr;
    QPushButton* submit_button_ = nullptr;
    bool found_ = false;
    int membership_ = 0;
};

#endif  // CHATBOX_CLIENT_UI_GROUP_JOIN_DIALOG_H
