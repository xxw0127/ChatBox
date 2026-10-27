#include "core/update_installer.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryFile>

namespace chatbox {
namespace client {

namespace {

// A runnable portable client needs the Qt core/GUI/widgets modules, the
// Windows platform plugin, the SQL driver for local history, the HTTPS stack
// with OpenSSL, and the pinned CA. Accepting only chatbox.exe would let a
// broken package pass the five-second liveness check.
const QStringList kRequiredFiles = {
    QStringLiteral("chatbox.exe"),
    QStringLiteral("Qt5Core.dll"),
    QStringLiteral("Qt5Gui.dll"),
    QStringLiteral("Qt5Widgets.dll"),
    QStringLiteral("Qt5Sql.dll"),
    QStringLiteral("Qt5Network.dll"),
    QStringLiteral("platforms/qwindows.dll"),
    QStringLiteral("sqldrivers/qsqlite.dll"),
    QStringLiteral("certs/ca.crt"),
};

QString resultMarkerPath() {
    return QDir::tempPath() + QStringLiteral("/ChatBox/update-result.txt");
}

QString interruptedMarkerPath() {
    return QDir::tempPath() + QStringLiteral("/ChatBox/update-in-progress");
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
    QTemporaryFile probe(QDir(QCoreApplication::applicationDirPath())
                             .filePath(QStringLiteral(".chatbox-write-XXXXXX")));
    probe.setAutoRemove(true);
    if (!probe.open()) {
        setError(reason, QStringLiteral("客户端目录不可写，请移动到可写目录后重试"));
        return false;
    }
    return true;
#else
    Q_UNUSED(reason);
    return false;
#endif
}

bool validateUpdateDirectory(const QString& directory, QString* error) {
    const QDir root(directory);
    for (const QString& relative : kRequiredFiles) {
        if (!QFileInfo(root.filePath(relative)).isFile()) {
            setError(error, QStringLiteral("更新包缺少 %1").arg(relative));
            return false;
        }
    }
    const QStringList ssl =
        root.entryList({QStringLiteral("libssl-*.dll")}, QDir::Files);
    const QStringList crypto =
        root.entryList({QStringLiteral("libcrypto-*.dll")}, QDir::Files);
    if (ssl.isEmpty() || crypto.isEmpty()) {
        setError(error, QStringLiteral("更新包缺少 OpenSSL 运行库"));
        return false;
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

bool consumeUpdateInterrupted() {
    const QString marker = interruptedMarkerPath();
    if (!QFileInfo::exists(marker)) {
        return false;
    }
    QFile::remove(marker);
    return true;
}

QString consumeUpdateResult() {
    QFile file(resultMarkerPath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    QString result = QString::fromUtf8(file.readAll()).trimmed();
    // Windows PowerShell 5.1 writes UTF-8 with a BOM; strip it so the next
    // startup warning starts with the actual message.
    if (result.startsWith(QChar(0xFEFF))) {
        result.remove(0, 1);
    }
    file.close();
    QFile::remove(resultMarkerPath());
    return result;
}

}  // namespace client
}  // namespace chatbox
