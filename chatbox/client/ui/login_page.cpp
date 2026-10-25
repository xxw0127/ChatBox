#include "ui/login_page.h"

#include <QCheckBox>
#include <QColor>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include "core/credential_store.h"
#include "core/ui_theme.h"
#include "ui/more_dots.h"

namespace theme = chatbox::client::theme;  // Coinbase palette tokens

LoginPage::LoginPage(QWidget* parent) : QWidget(parent) {
    setObjectName("LoginPage");
    // Clicking the empty page must take focus away from username/password and
    // stop text input; the page itself only needs click focus, not Tab focus.
    setFocusPolicy(Qt::ClickFocus);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 24, 24, 24);
    root->addStretch();

    auto* card = new QFrame(this);
    card->setObjectName("LoginCard");
    card->setFocusPolicy(Qt::ClickFocus);
    card->setFixedWidth(390);
    auto* card_layout = new QVBoxLayout(card);
    card_layout->setContentsMargins(30, 22, 30, 24);
    card_layout->setSpacing(10);

    auto* top_row = new QHBoxLayout();
    top_row->setContentsMargins(0, 0, 0, 0);
    top_row->addStretch(1);
    advanced_toggle_ = new QToolButton(card);
    advanced_toggle_->setObjectName("AdvancedOptionsButton");
    advanced_toggle_->setIcon(chatbox::client::moreDotsIcon(QColor(theme::kInk)));
    advanced_toggle_->setIconSize(QSize(20, 20));
    advanced_toggle_->setToolTip("更多选项");
    advanced_toggle_->setCheckable(true);
    advanced_toggle_->setAutoRaise(true);
    advanced_toggle_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    advanced_toggle_->setFixedSize(32, 32);
    advanced_toggle_->setCursor(Qt::PointingHandCursor);
    top_row->addWidget(advanced_toggle_, 0, Qt::AlignTop | Qt::AlignRight);
    card_layout->addLayout(top_row);

    auto* logo = new QLabel("C", card);
    logo->setObjectName("AppLogo");
    logo->setFixedSize(56, 56);
    logo->setAlignment(Qt::AlignCenter);
    card_layout->addWidget(logo, 0, Qt::AlignHCenter);

    auto* title = new QLabel("ChatBox", card);
    title->setObjectName("AppTitle");
    title->setAlignment(Qt::AlignCenter);
    card_layout->addWidget(title);

    auto* subtitle = new QLabel("登录或创建账号", card);
    subtitle->setObjectName("AppSubtitle");
    subtitle->setAlignment(Qt::AlignCenter);
    card_layout->addWidget(subtitle);
    card_layout->addSpacing(8);

    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(8);

    host_edit_ = new QLineEdit("121.5.168.189", card);
    host_edit_->setObjectName("ServerHostEdit");
    port_spin_ = new QSpinBox(card);
    port_spin_->setObjectName("ServerPortSpin");
    port_spin_->setRange(1, 65535);
    port_spin_->setValue(9443);
    tls_check_ = new QCheckBox("TLS 加密连接", card);
    tls_check_->setObjectName("ServerTlsCheck");
    tls_check_->setChecked(true);
    host_edit_->setAccessibleName("服务器");
    port_spin_->setAccessibleName("端口");
    tls_check_->setAccessibleName("TLS 加密连接");
    username_edit_ = new QLineEdit(card);
    username_edit_->setObjectName("UsernameEdit");
    username_edit_->setAccessibleName("用户名");
    username_edit_->setPlaceholderText("3-32 位字母、数字、下划线");
    password_edit_ = new QLineEdit(card);
    password_edit_->setObjectName("PasswordEdit");
    password_edit_->setAccessibleName("密码");
    password_edit_->setEchoMode(QLineEdit::Password);
    password_edit_->setPlaceholderText("至少 8 位");
    nickname_label_ = new QLabel("昵称", card);
    nickname_edit_ = new QLineEdit(card);
    nickname_edit_->setObjectName("NicknameEdit");
    nickname_edit_->setAccessibleName("昵称");
    nickname_edit_->setPlaceholderText("注册时填写");

    QSettings settings;
    host_edit_->setText(settings.value("server/host", "121.5.168.189").toString());
    port_spin_->setValue(settings.value("server/port", 9443).toInt());
    tls_check_->setChecked(settings.value("server/tls", true).toBool());

    form->addRow("用户名", username_edit_);
    form->addRow("密码", password_edit_);
    form->addRow(nickname_label_, nickname_edit_);
    nickname_label_->hide();
    nickname_edit_->hide();
    card_layout->addLayout(form);

    auto* login_options = new QHBoxLayout();
    login_options->setSpacing(14);
    auto_login_check_ = new QCheckBox("自动登录", card);
    auto_login_check_->setObjectName("AutoLoginCheck");
    auto_login_check_->setAccessibleName("自动登录");
    auto_login_check_->setChecked(settings.value("login/auto_login", true).toBool());
    remember_password_check_ = new QCheckBox("记住密码", card);
    remember_password_check_->setObjectName("RememberPasswordCheck");
    remember_password_check_->setAccessibleName("记住密码");
    remember_password_check_->setChecked(
        settings.value("login/remember_password", false).toBool());
    if (!chatbox::client::passwordStorageSupported()) {
        remember_password_check_->setEnabled(false);
        remember_password_check_->setToolTip("当前平台不支持安全保存密码");
    }
    login_options->addWidget(auto_login_check_);
    login_options->addWidget(remember_password_check_);
    login_options->addStretch(1);
    card_layout->addLayout(login_options);

    reloadSavedCredentials();

    advanced_panel_ = new QWidget(card);
    advanced_panel_->setObjectName("AdvancedOptionsPanel");
    auto* advanced_form = new QFormLayout(advanced_panel_);
    advanced_form->setContentsMargins(0, 0, 0, 0);
    advanced_form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    advanced_form->setHorizontalSpacing(12);
    advanced_form->setVerticalSpacing(8);
    advanced_form->addRow("服务器", host_edit_);
    advanced_form->addRow("端口", port_spin_);
    advanced_form->addRow("连接", tls_check_);
    advanced_panel_->hide();
    card_layout->addWidget(advanced_panel_);

    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(8);
    login_button_ = new QPushButton("登录", card);
    login_button_->setObjectName("PrimaryButton");
    login_button_->setMinimumHeight(38);
    register_button_ = new QPushButton("注册", card);
    register_button_->setObjectName("RegisterButton");
    register_button_->setMinimumHeight(38);
    cancel_button_ = new QPushButton("取消", card);
    cancel_button_->hide();
    login_button_->setDefault(true);
    buttons->addWidget(login_button_, 1);
    buttons->addWidget(register_button_, 1);
    buttons->addWidget(cancel_button_);
    card_layout->addLayout(buttons);

    status_label_ = new QLabel(card);
    status_label_->setWordWrap(true);
    status_label_->setMinimumHeight(32);
    status_label_->setAlignment(Qt::AlignCenter);
    card_layout->addWidget(status_label_);

    root->addWidget(card, 0, Qt::AlignCenter);
    root->addStretch();

    connect(login_button_, &QPushButton::clicked, this, &LoginPage::submitPrimary);
    connect(register_button_, &QPushButton::clicked, this, &LoginPage::toggleRegisterMode);
    connect(cancel_button_, &QPushButton::clicked, this, &LoginPage::cancelRequested);
    connect(password_edit_, &QLineEdit::returnPressed, this, &LoginPage::submitPrimary);
    connect(nickname_edit_, &QLineEdit::returnPressed, this, &LoginPage::submitPrimary);
    connect(auto_login_check_, &QCheckBox::toggled, this, [](bool checked) {
        QSettings settings;
        settings.setValue("login/auto_login", checked);
        if (!checked) {
            settings.remove("session/token");
        }
    });
    connect(remember_password_check_, &QCheckBox::toggled, this, [this](bool checked) {
        QSettings settings;
        settings.setValue("login/remember_password", checked);
        if (!checked) {
            chatbox::client::clearSavedCredentials();
            chatbox::client::forgetAccountCredentials(
                username_edit_->text().trimmed());
        } else if (!username_edit_->text().trimmed().isEmpty() &&
                   !password_edit_->text().isEmpty()) {
            chatbox::client::saveSavedCredentials(
                username_edit_->text().trimmed(), password_edit_->text());
        }
    });
    connect(advanced_toggle_, &QToolButton::toggled, this, [this](bool expanded) {
        advanced_panel_->setVisible(expanded);
        advanced_toggle_->setToolTip(expanded ? "收起更多选项" : "更多选项");
    });
    connect(tls_check_, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked && port_spin_->value() == 9100) {
            port_spin_->setValue(9443);
        } else if (!checked && port_spin_->value() == 9443) {
            port_spin_->setValue(9100);
        }
    });

    QWidget::setTabOrder(advanced_toggle_, username_edit_);
    QWidget::setTabOrder(username_edit_, password_edit_);
    QWidget::setTabOrder(password_edit_, nickname_edit_);
    QWidget::setTabOrder(nickname_edit_, auto_login_check_);
    QWidget::setTabOrder(auto_login_check_, remember_password_check_);
    QWidget::setTabOrder(remember_password_check_, host_edit_);
    QWidget::setTabOrder(host_edit_, port_spin_);
    QWidget::setTabOrder(port_spin_, tls_check_);
    QWidget::setTabOrder(tls_check_, login_button_);
    QWidget::setTabOrder(login_button_, register_button_);
    username_edit_->setFocus();
}

void LoginPage::setStatus(const QString& text, bool error) {
    status_label_->setText(text);
    status_label_->setStyleSheet(error
                                     ? QString("color: %1;").arg(theme::kSemanticDown)
                                     : QString("color: %1;").arg(theme::kBody));
}

void LoginPage::setBusy(bool busy) {
    login_button_->setDisabled(busy);
    register_button_->setDisabled(busy);
    cancel_button_->setVisible(busy);
}

void LoginPage::clearPassword() {
    password_edit_->clear();
}

void LoginPage::setServerSettings(const QString& host, quint16 port, bool tls) {
    QSignalBlocker blocker(tls_check_);
    host_edit_->setText(host);
    port_spin_->setValue(port);
    tls_check_->setChecked(tls);
}

void LoginPage::setUsername(const QString& username) {
    username_edit_->setText(username);
    password_edit_->clear();
    username_edit_->setFocus();
}

void LoginPage::setRegisterMode(bool enabled) {
    register_mode_ = enabled;
    nickname_label_->setVisible(enabled);
    nickname_edit_->setVisible(enabled);
    login_button_->setText(enabled ? "注册" : "登录");
    register_button_->setText(enabled ? "返回登录" : "注册");
    if (enabled) {
        nickname_edit_->setFocus();
    }
}

void LoginPage::reloadSavedCredentials() {
    if (!remember_password_check_->isChecked()) {
        return;
    }
    QString saved_username;
    QString saved_password;
    if (!chatbox::client::loadSavedCredentials(&saved_username, &saved_password)) {
        return;
    }
    username_edit_->setText(saved_username);
    password_edit_->setText(saved_password);
}

void LoginPage::refreshThemeIcons() {
    if (advanced_toggle_ != nullptr) {
        advanced_toggle_->setIcon(chatbox::client::moreDotsIcon(QColor(theme::kInk)));
    }
}

void LoginPage::resetToLogin() {
    setRegisterMode(false);
    refreshThemeIcons();
    reloadSavedCredentials();
    username_edit_->setFocus();
}

void LoginPage::toggleRegisterMode() {
    setRegisterMode(!register_mode_);
}

void LoginPage::submitPrimary() {
    if (register_mode_) {
        submitRegister();
    } else {
        submitLogin();
    }
}

void LoginPage::submitLogin() {
    const QString host = host_edit_->text().trimmed();
    const QString username = username_edit_->text().trimmed();
    const QString password = password_edit_->text();
    if (host.isEmpty() || username.isEmpty() || password.isEmpty()) {
        setStatus("请填写服务器、用户名和密码", true);
        return;
    }
    if (password.size() < 8) {
        setStatus("密码至少 8 位", true);
        return;
    }
    if (remember_password_check_->isChecked()) {
        chatbox::client::saveSavedCredentials(username, password);
    } else {
        chatbox::client::clearSavedCredentials();
        chatbox::client::forgetAccountCredentials(username);
    }
    setStatus("正在连接并登录...");
    emit loginRequested(host, static_cast<quint16>(port_spin_->value()), username, password,
                        tls_check_->isChecked());
}

void LoginPage::submitRegister() {
    const QString host = host_edit_->text().trimmed();
    const QString username = username_edit_->text().trimmed();
    const QString password = password_edit_->text();
    const QString nickname = nickname_edit_->text().trimmed();
    if (host.isEmpty() || username.isEmpty() || password.isEmpty() || nickname.isEmpty()) {
        setStatus("注册需要服务器、用户名、密码和昵称", true);
        return;
    }
    if (password.size() < 8) {
        setStatus("密码至少 8 位", true);
        return;
    }
    if (nickname.toUcs4().size() > 32) {
        setStatus("昵称不能超过 32 个字符", true);
        return;
    }
    if (remember_password_check_->isChecked()) {
        chatbox::client::saveSavedCredentials(username, password);
    } else {
        chatbox::client::clearSavedCredentials();
        chatbox::client::forgetAccountCredentials(username);
    }
    setStatus("正在注册...");
    emit registerRequested(host, static_cast<quint16>(port_spin_->value()), username, password,
                           nickname, tls_check_->isChecked());
}
