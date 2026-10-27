#include "core/update_installer.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

namespace chatbox {
namespace client {

namespace {

const QStringList kRequiredFiles = {
    QStringLiteral("chatbox.exe"),
    QStringLiteral("Qt5Sql.dll"),
    QStringLiteral("Qt5Network.dll"),
    QStringLiteral("sqldrivers/qsqlite.dll"),
};

QString resultMarkerPath() {
    return QDir::tempPath() + QStringLiteral("/ChatBox/update-result.txt");
}

void setError(QString* error, const QString& message) {
    if (error != nullptr) {
        *error = message;
    }
}

}  // namespace

bool canInstallUpdates(QString* reason) {
#ifdef Q_OS_WIN
    const QString tar = QStandardPaths::findExecutable(QStringLiteral("tar.exe"));
    const QString powershell =
        QStandardPaths::findExecutable(QStringLiteral("powershell.exe"));
    if (tar.isEmpty()) {
        setError(reason, QStringLiteral("系统缺少 tar.exe，无法解压更新包"));
        return false;
    }
    if (powershell.isEmpty()) {
        setError(reason, QStringLiteral("系统缺少 PowerShell，无法安装更新"));
        return false;
    }
    const QString probe = QDir(QCoreApplication::applicationDirPath())
                              .filePath(QStringLiteral(".chatbox-write-test"));
    QFile file(probe);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setError(reason, QStringLiteral("客户端目录不可写，请移动到可写目录后重试"));
        return false;
    }
    file.close();
    QFile::remove(probe);
    return true;
#else
    Q_UNUSED(reason);
    return false;
#endif
}

bool validateUpdateDirectory(const QString& directory, QString* error) {
    for (const QString& relative : kRequiredFiles) {
        const QString path = QDir(directory).filePath(relative);
        if (!QFileInfo::exists(path)) {
            setError(error, QStringLiteral("更新包缺少 %1").arg(relative));
            return false;
        }
    }
    return true;
}

bool extractUpdateZip(const QString& zip, const QString& staging,
                      QString* extracted_root, QString* error) {
    if (!QFileInfo::exists(zip)) {
        setError(error, QStringLiteral("更新包不存在"));
        return false;
    }
    const QString tar = QStandardPaths::findExecutable(QStringLiteral("tar.exe"));
    if (tar.isEmpty()) {
        setError(error, QStringLiteral("系统缺少 tar.exe"));
        return false;
    }
    QDir().mkpath(staging);
    QProcess process;
    process.setProgram(tar);
    process.setArguments({QStringLiteral("-xf"), zip, QStringLiteral("-C"),
                          staging});
    process.start();
    if (!process.waitForFinished(120000) || process.exitStatus() != QProcess::NormalExit ||
        process.exitCode() != 0) {
        setError(error, QStringLiteral("解压更新包失败"));
        return false;
    }

    QString root = staging;
    if (!QFileInfo::exists(QDir(root).filePath(QStringLiteral("chatbox.exe")))) {
        const QFileInfoList entries =
            QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        root.clear();
        for (const QFileInfo& entry : entries) {
            if (QFileInfo::exists(entry.filePath() +
                                  QStringLiteral("/chatbox.exe"))) {
                root = entry.filePath();
                break;
            }
        }
    }
    if (root.isEmpty() || !validateUpdateDirectory(root, error)) {
        if (root.isEmpty()) {
            setError(error, QStringLiteral("更新包中没有找到 chatbox.exe"));
        }
        return false;
    }
    if (extracted_root != nullptr) {
        *extracted_root = root;
    }
    return true;
}

bool launchUpdateHelper(quint64 process_id, const QString& source_dir,
                        const QString& target_dir, const QString& executable,
                        QString* error) {
#ifdef Q_OS_WIN
    const QString powershell =
        QStandardPaths::findExecutable(QStringLiteral("powershell.exe"));
    if (powershell.isEmpty()) {
        setError(error, QStringLiteral("系统缺少 PowerShell"));
        return false;
    }
    const QString script = QDir::tempPath() +
                           QStringLiteral("/ChatBox/update-helper.ps1");
    QDir().mkpath(QFileInfo(script).absolutePath());
    QFile template_file(QStringLiteral(":/update_helper.ps1"));
    if (!template_file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setError(error, QStringLiteral("找不到更新助手模板"));
        return false;
    }
    QFile script_file(script);
    if (!script_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setError(error, QStringLiteral("无法写入更新助手"));
        return false;
    }
    script_file.write(template_file.readAll());
    script_file.close();

    QProcess helper;
    helper.setProgram(powershell);
    helper.setArguments({
        QStringLiteral("-NoProfile"),
        QStringLiteral("-WindowStyle"),
        QStringLiteral("Hidden"),
        QStringLiteral("-ExecutionPolicy"),
        QStringLiteral("Bypass"),
        QStringLiteral("-File"),
        script,
        QStringLiteral("-ProcessId"),
        QString::number(process_id),
        QStringLiteral("-SourceDir"),
        source_dir,
        QStringLiteral("-TargetDir"),
        target_dir,
        QStringLiteral("-Executable"),
        executable,
    });
    if (!helper.startDetached()) {
        setError(error, QStringLiteral("无法启动更新助手"));
        return false;
    }
    return true;
#else
    Q_UNUSED(process_id);
    Q_UNUSED(source_dir);
    Q_UNUSED(target_dir);
    Q_UNUSED(executable);
    setError(error, QStringLiteral("当前平台不支持自动更新"));
    return false;
#endif
}

QString consumeUpdateResult() {
    QFile file(resultMarkerPath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    const QString result = QString::fromUtf8(file.readAll()).trimmed();
    file.close();
    QFile::remove(resultMarkerPath());
    return result;
}

}  // namespace client
}  // namespace chatbox
