#ifndef CHATBOX_CLIENT_UI_LOGIN_PAGE_H
#define CHATBOX_CLIENT_UI_LOGIN_PAGE_H

#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;

class LoginPage : public QWidget {
    Q_OBJECT

public:
    explicit LoginPage(QWidget* parent = nullptr);

    void setStatus(const QString& text, bool error = false);
    void setBusy(bool busy);
    void clearPassword();
    void resetToLogin();
    // 切换账号时预填用户名，用户只需要补密码。
    void setUsername(const QString& username);
signals:
    void loginRequested(const QString& host, quint16 port, const QString& username,
                        const QString& password, bool tls);
    void registerRequested(const QString& host, quint16 port, const QString& username,
                           const QString& password, const QString& nickname, bool tls);
    void cancelRequested();

private slots:
    void submitLogin();
    void submitRegister();
    void submitPrimary();
    void toggleRegisterMode();

private:
    void setRegisterMode(bool enabled);
    void reloadSavedCredentials();

    QLineEdit* username_edit_ = nullptr;
    QLineEdit* password_edit_ = nullptr;
    QLabel* nickname_label_ = nullptr;
    QLineEdit* nickname_edit_ = nullptr;
    QCheckBox* auto_login_check_ = nullptr;
    QCheckBox* remember_password_check_ = nullptr;
    bool register_mode_ = false;
    QPushButton* login_button_ = nullptr;
    QPushButton* register_button_ = nullptr;
    QPushButton* cancel_button_ = nullptr;
    QLabel* status_label_ = nullptr;
};

#endif  // CHATBOX_CLIENT_UI_LOGIN_PAGE_H
