#include "ui/settings_dialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>

#include "core/app_settings.h"
#include "core/autostart.h"
#include "core/client_config.h"
#include "core/client_log.h"
#include "core/ui_theme.h"
#include "ui/dialog_buttons.h"

namespace theme = chatbox::client::theme;  // Coinbase palette tokens

namespace {

// 本地聊天记录占用大小的文案：小于 1 KB 用字节，之后用 KB / MB。
QString formatHistorySize(qint64 bytes) {
    if (bytes < 0) {
        bytes = 0;
    }
    if (bytes < 1024) {
        return QStringLiteral("%1 B").arg(bytes);
    }
    if (bytes < 1024 * 1024) {
        return QString::number(static_cast<double>(bytes) / 1024.0, 'f', 1) +
               QStringLiteral(" KB");
    }
    return QString::number(static_cast<double>(bytes) / (1024.0 * 1024.0), 'f', 1) +
           QStringLiteral(" MB");
}

}  // namespace

SettingsDialog::SettingsDialog(const QString& public_id, const QString& history_directory,
                               qint64 history_size_bytes, bool history_available,
                               QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("设置");
    resize(520, 560);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    id_label_ = new QLabel(public_id, this);
    id_label_->setObjectName("SettingsIdLabel");
    id_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow("我的ID", id_label_);

    QSettings settings;
    auto* server_address = new QLabel(
        QStringLiteral("%1:%2").arg(QString::fromLatin1(chatbox::client::kServerHost))
            .arg(chatbox::client::kServerPort),
        this);
    server_address->setObjectName("ServerAddressLabel");
    server_address->setTextInteractionFlags(Qt::TextSelectableByMouse);
    notifications_check_ = new QCheckBox("消息提醒（任务栏闪烁/未读角标）", this);
    notifications_check_->setChecked(
        settings.value("settings/notifications", true).toBool());
    system_popup_check_ = new QCheckBox("系统弹窗通知", this);
    system_popup_check_->setChecked(
        settings.value("settings/system_popup", true).toBool());
    autostart_check_ = new QCheckBox("开机自启动（静默到托盘）", this);
    autostart_check_->setChecked(chatbox::client::isAutostartEnabled());
    close_to_tray_check_ =
        new QCheckBox("关闭窗口时最小化到托盘（不退出程序）", this);
    close_to_tray_check_->setObjectName("CloseToTrayCheck");
    close_to_tray_check_->setChecked(
        settings.value("settings/close_to_tray", true).toBool());
    appearance_combo_ = new QComboBox(this);
    appearance_combo_->setObjectName("AppearanceCombo");
    appearance_combo_->addItem("跟随系统",
                               static_cast<int>(chatbox::client::theme::ThemeMode::kSystem));
    appearance_combo_->addItem("浅色",
                               static_cast<int>(chatbox::client::theme::ThemeMode::kLight));
    appearance_combo_->addItem("深色",
                               static_cast<int>(chatbox::client::theme::ThemeMode::kDark));
    const int stored_appearance =
        settings.value("settings/appearance",
                       static_cast<int>(chatbox::client::theme::ThemeMode::kSystem))
            .toInt();
    for (int i = 0; i < appearance_combo_->count(); ++i) {
        if (appearance_combo_->itemData(i).toInt() == stored_appearance) {
            appearance_combo_->setCurrentIndex(i);
            break;
        }
    }

    form->addRow("服务器", server_address);
    form->addRow("通知", notifications_check_);
    form->addRow("", system_popup_check_);
    form->addRow("启动", autostart_check_);
    form->addRow("", close_to_tray_check_);
    form->addRow("外观", appearance_combo_);
    auto* device_button = new QPushButton(QStringLiteral("设备管理"), this);
    device_button->setObjectName(QStringLiteral("DeviceManagementButton"));
    connect(device_button, &QPushButton::clicked, this,
            &SettingsDialog::deviceManagementRequested);
    form->addRow("账号", device_button);
    layout->addLayout(form);

    // 本地聊天记录分组：只展示与发起意图，不在这里动数据库。
    auto* history_group = new QGroupBox("本地聊天记录", this);
    auto* history_layout = new QVBoxLayout(history_group);

    history_path_label_ = new QLabel(history_directory, this);
    history_path_label_->setObjectName("HistoryPathLabel");
    history_path_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    history_path_label_->setWordWrap(true);
    auto* path_caption = new QLabel("存放位置", history_group);
    path_caption->setObjectName("SettingsCaption");
    history_layout->addWidget(path_caption);
    history_layout->addWidget(history_path_label_);

    auto* size_row = new QHBoxLayout();
    auto* size_caption = new QLabel("占用空间", history_group);
    size_caption->setObjectName("SettingsCaption");
    history_size_label_ = new QLabel(this);
    history_size_label_->setObjectName("HistorySizeLabel");
    history_size_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    size_row->addWidget(size_caption);
    size_row->addWidget(history_size_label_);
    size_row->addStretch(1);
    history_layout->addLayout(size_row);

    auto* history_buttons = new QHBoxLayout();
    change_history_dir_button_ = new QPushButton("更改目录", history_group);
    change_history_dir_button_->setObjectName("ChangeHistoryDirButton");
    open_history_dir_button_ = new QPushButton("打开目录", history_group);
    open_history_dir_button_->setObjectName("OpenHistoryDirButton");
    clear_history_button_ = new QPushButton("清理", history_group);
    clear_history_button_->setObjectName("ClearHistoryButton");
    history_buttons->addWidget(change_history_dir_button_);
    history_buttons->addWidget(open_history_dir_button_);
    history_buttons->addWidget(clear_history_button_);
    history_buttons->addStretch(1);
    history_layout->addLayout(history_buttons);
    layout->addWidget(history_group);

    auto* files_group = new QGroupBox("接收文件", this);
    auto* files_layout = new QHBoxLayout(files_group);
    file_save_dir_ = chatbox::client::fileSaveDirectory();
    file_save_dir_label_ = new QLabel(file_save_dir_, files_group);
    file_save_dir_label_->setObjectName("FileSaveDirLabel");
    file_save_dir_label_->setWordWrap(true);
    file_save_dir_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* change_files_dir = new QPushButton("更改目录", files_group);
    change_files_dir->setObjectName("ChangeFileSaveDirButton");
    files_layout->addWidget(file_save_dir_label_, 1);
    files_layout->addWidget(change_files_dir);
    layout->addWidget(files_group);
    connect(change_files_dir, &QPushButton::clicked, this,
            &SettingsDialog::changeFileSaveDirectory);

    auto* log_layout = new QHBoxLayout();
    log_label_ = new QLabel(clientLogFilePath(), this);
    log_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* open_logs = new QPushButton("打开日志目录", this);
    log_layout->addWidget(log_label_, 1);
    log_layout->addWidget(open_logs);
    layout->addLayout(log_layout);

    auto* version = new QLabel(
        QString("ChatBox %1").arg(QCoreApplication::applicationVersion()), this);
    version->setObjectName("SettingsVersion");
    layout->addWidget(version);

    auto* update_row = new QHBoxLayout();
    check_update_button_ = new QPushButton("检查更新", this);
    check_update_button_->setObjectName("CheckUpdateButton");
    update_row->addWidget(check_update_button_);
    update_row->addStretch(1);
    layout->addLayout(update_row);
    connect(check_update_button_, &QPushButton::clicked, this,
            &SettingsDialog::updateCheckRequested);

    // 账号级操作放在设置里，右侧成员栏不再承载导航职责。
    auto* signout_row = new QHBoxLayout();
    auto* signout_button = new QPushButton("退出当前账号", this);
    signout_button->setObjectName("DangerOutlineButton");
    signout_row->addWidget(signout_button);
    signout_row->addStretch(1);
    layout->addLayout(signout_row);
    connect(signout_button, &QPushButton::clicked, this, [this] {
        if (chatbox::client::confirmDanger(this, "退出登录",
                                           "确定要退出当前账号吗？", "退出登录")) {
            emit logoutRequested();
            accept();
        }
    });

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Save)->setText("保存");
    buttons->button(QDialogButtonBox::Cancel)->setText("取消");
    layout->addWidget(buttons);

    connect(open_logs, &QPushButton::clicked, this, &SettingsDialog::openLogDirectory);
    connect(appearance_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                if (index < 0) {
                    return;
                }
                const auto mode = static_cast<chatbox::client::theme::ThemeMode>(
                    appearance_combo_->itemData(index).toInt());
                QSettings current;
                current.setValue("settings/appearance", static_cast<int>(mode));
                current.sync();
                chatbox::client::theme::applyTheme(qApp, mode);
                emit appearanceChanged();
            });
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::saveSettings);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(change_history_dir_button_, &QPushButton::clicked, this,
            &SettingsDialog::changeHistoryDirectory);
    connect(open_history_dir_button_, &QPushButton::clicked, this,
            &SettingsDialog::openHistoryDirectory);
    connect(clear_history_button_, &QPushButton::clicked, this,
            &SettingsDialog::openClearHistoryDialog);

    setHistoryInfo(history_directory, history_size_bytes, history_available);
}

void SettingsDialog::setHistoryInfo(const QString& directory, qint64 size_bytes,
                                    bool available) {
    history_directory_ = directory;
    history_available_ = available;
    if (history_path_label_ != nullptr) {
        history_path_label_->setText(
            available ? directory
                      : QStringLiteral("本地历史不可用（缺少 SQLite 驱动）"));
    }
    if (history_size_label_ != nullptr) {
        history_size_label_->setText(formatHistorySize(size_bytes));
    }
    if (change_history_dir_button_ != nullptr) {
        change_history_dir_button_->setEnabled(available);
    }
    if (open_history_dir_button_ != nullptr) {
        open_history_dir_button_->setEnabled(available);
    }
    if (clear_history_button_ != nullptr) {
        clear_history_button_->setEnabled(available);
    }
}

void SettingsDialog::changeFileSaveDirectory() {
    const QString picked = QFileDialog::getExistingDirectory(
        this, QStringLiteral("\u9009\u62e9\u63a5\u6536\u6587\u4ef6\u4fdd\u5b58\u76ee\u5f55"),
        file_save_dir_);
    if (picked.isEmpty()) {
        return;
    }
    QString error;
    if (!chatbox::client::validateFileSaveDirectory(picked, &error)) {
        QMessageBox::warning(this, QStringLiteral("\u4fdd\u5b58\u4f4d\u7f6e"), error);
        return;
    }
    file_save_dir_ = QDir(picked).absolutePath();
    file_save_dir_label_->setText(file_save_dir_);
}

void SettingsDialog::saveSettings() {
    QString path_error;
    if (!chatbox::client::setFileSaveDirectory(file_save_dir_, &path_error)) {
        QMessageBox::warning(this, "保存位置", path_error);
        return;
    }
    emit settingsSaved(notifications_check_->isChecked(),
                       system_popup_check_->isChecked(),
                       autostart_check_->isChecked(),
                       close_to_tray_check_->isChecked());
    accept();
}

void SettingsDialog::openLogDirectory() {
    const QString directory = QFileInfo(clientLogFilePath()).absolutePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
}

// 打开本地聊天记录所在的文件夹。
void SettingsDialog::openHistoryDirectory() {
    if (history_directory_.trimmed().isEmpty()) {
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(history_directory_));
}

// 更改存放目录：先选文件夹，再问要不要把现有记录一起搬过去。
// 真正的搬移由 MainWindow 执行（moveTo），这里只发信号。
void SettingsDialog::changeHistoryDirectory() {
    const QString current = QDir(history_directory_).absolutePath();
    const QString picked = QFileDialog::getExistingDirectory(
        this, "选择本地聊天记录存放目录", current);
    if (picked.isEmpty()) {
        return;  // 用户取消
    }
    const QString target = QDir(picked).absolutePath();
    if (target.compare(current, Qt::CaseInsensitive) == 0) {
        QMessageBox::information(this, "更改目录", "已经是当前使用的目录");
        return;
    }
    if (chatbox::client::isHistoryDirectoryInsideInstall(target)) {
        QMessageBox::warning(
            this, "更改目录",
            "本地聊天记录不能放在客户端安装目录内，否则更新时会丢失。请选择其他目录。");
        return;
    }

    QMessageBox box(this);
    box.setWindowTitle("更改目录");
    box.setText("要把已保存的本地聊天记录复制到新目录吗？");
    box.setInformativeText(
        "选择「迁移」会复制现有记录到新目录；选择「不迁移」会在新目录创建一个空的记录库。");
    QPushButton* migrate_button = box.addButton("迁移", QMessageBox::AcceptRole);
    QPushButton* keep_button = box.addButton("不迁移", QMessageBox::DestructiveRole);
    box.addButton("取消", QMessageBox::RejectRole);
    box.exec();

    if (box.clickedButton() == migrate_button) {
        emit historyPathChangeRequested(target, true);
    } else if (box.clickedButton() == keep_button) {
        emit historyPathChangeRequested(target, false);
    }
}

// 清理本地聊天记录：只收集清理范围，勾选确认后才发信号，
// 对话框本身不删除任何数据。
void SettingsDialog::openClearHistoryDialog() {
    QDialog dialog(this);
    dialog.setWindowTitle("清理本地聊天记录");
    dialog.setMinimumWidth(430);
    auto* layout = new QVBoxLayout(&dialog);

    auto* intro = new QLabel("选择要清理的范围：", &dialog);
    layout->addWidget(intro);

    auto* clear_all_radio = new QRadioButton("清空全部本地记录", &dialog);
    clear_all_radio->setChecked(true);
    layout->addWidget(clear_all_radio);

    auto* older_row = new QHBoxLayout();
    auto* older_radio = new QRadioButton("清理以下天数之前的本地记录", &dialog);
    auto* days_spin = new QSpinBox(&dialog);
    days_spin->setRange(1, 365);
    days_spin->setValue(30);
    days_spin->setSuffix(" 天前");
    older_row->addWidget(older_radio);
    older_row->addWidget(days_spin);
    older_row->addStretch(1);
    layout->addLayout(older_row);

    auto* confirm = new QCheckBox("只删除本机副本，不影响服务器历史", &dialog);
    layout->addWidget(confirm);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText("开始清理");
    buttons->button(QDialogButtonBox::Cancel)->setText("取消");
    buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    layout->addWidget(buttons);

    connect(confirm, &QCheckBox::toggled, buttons, [buttons](bool checked) {
        buttons->button(QDialogButtonBox::Ok)->setEnabled(checked);
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    if (!confirm->isChecked()) {
        return;  // 没勾选确认就不执行清理
    }
    if (older_radio->isChecked()) {
        emit historyClearOlderRequested(days_spin->value());
    } else {
        emit historyClearAllRequested();
    }
}
