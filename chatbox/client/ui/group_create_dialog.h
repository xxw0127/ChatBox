#ifndef CHATBOX_CLIENT_UI_GROUP_CREATE_DIALOG_H
#define CHATBOX_CLIENT_UI_GROUP_CREATE_DIALOG_H

#include <QDialog>

class QLabel;
class QLineEdit;

class GroupCreateDialog : public QDialog {
    Q_OBJECT

public:
    explicit GroupCreateDialog(QWidget* parent = nullptr);

signals:
    void createRequested(const QString& name);

private:
    void submit();
    QLineEdit* name_edit_ = nullptr;
    QLabel* error_label_ = nullptr;
};

#endif  // CHATBOX_CLIENT_UI_GROUP_CREATE_DIALOG_H
