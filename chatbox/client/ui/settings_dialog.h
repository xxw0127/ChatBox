#ifndef CHATBOX_CLIENT_UI_SETTINGS_DIALOG_H
#define CHATBOX_CLIENT_UI_SETTINGS_DIALOG_H

#include <QDialog>
#include <QtGlobal>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;

// 设置对话框：除了服务器/通知/自启动外，还负责展示并管理本机的
// 聊天记录副本（存放目录、占用大小、清理）。
// 本对话框只负责收集用户意图，不直接碰数据库：
// 改目录与清理都通过信号交给 MainWindow 去执行。
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    // history_available 为假时（缺少 SQLite 驱动等）三个历史按钮全部禁用。
    SettingsDialog(const QString& public_id, const QString& history_directory,
                   qint64 history_size_bytes, bool history_available,
                   QWidget* parent = nullptr);

    // 刷新本地聊天记录信息：目录、大小与可用状态。
    void setHistoryInfo(const QString& directory, qint64 size_bytes, bool available);

signals:
    void settingsSaved(bool notifications, bool system_popup, bool autostart);
    void appearanceChanged();
    void updateCheckRequested();
    void deviceManagementRequested();
    // 用户在设置里选择退出当前账号；真正的断开由 MainWindow 执行。
    void logoutRequested();
    // 请求把本地聊天记录换到新目录；migrate 为真表示复制现有记录。
    void historyPathChangeRequested(const QString& directory, bool migrate);
    // 请求清空本机的聊天记录副本。
    void historyClearAllRequested();
    // 请求清理本机 days 天前的聊天记录副本。
    void historyClearOlderRequested(int days);

private slots:
    void saveSettings();
    void openLogDirectory();
    void changeHistoryDirectory();
    void changeFileSaveDirectory();
    void openHistoryDirectory();
    void openClearHistoryDialog();

private:
    QCheckBox* notifications_check_ = nullptr;
    QCheckBox* system_popup_check_ = nullptr;
    QCheckBox* autostart_check_ = nullptr;
    QComboBox* appearance_combo_ = nullptr;
    QLabel* log_label_ = nullptr;
    QLabel* id_label_ = nullptr;
    QLabel* history_path_label_ = nullptr;
    QLabel* history_size_label_ = nullptr;
    QPushButton* change_history_dir_button_ = nullptr;
    QPushButton* open_history_dir_button_ = nullptr;
    QPushButton* clear_history_button_ = nullptr;
    QPushButton* check_update_button_ = nullptr;
    QString history_directory_;
    QString file_save_dir_;
    QLabel* file_save_dir_label_ = nullptr;
    bool history_available_ = false;
};

#endif  // CHATBOX_CLIENT_UI_SETTINGS_DIALOG_H
