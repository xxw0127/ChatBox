#include "ui/file_manager_dialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "core/file_types.h"

using chatbox::client::formatFileSize;

FileManagerDialog::FileManagerDialog(int kind, quint64 target_id,
                                     bool can_upload, QWidget* parent)
    : QDialog(parent), kind_(kind), target_id_(target_id) {
    setObjectName(QStringLiteral("FileManagerDialog"));
    setWindowTitle(QStringLiteral("文件"));
    setMinimumSize(560, 420);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 16, 18, 14);
    layout->setSpacing(8);

    title_ = new QLabel(this);
    title_->setObjectName(QStringLiteral("FileManagerTitle"));
    layout->addWidget(title_);
    quota_ = new QLabel(this);
    quota_->setObjectName(QStringLiteral("FileQuotaLabel"));
    layout->addWidget(quota_);

    auto* actions = new QHBoxLayout();
    filter_ = new QLineEdit(this);
    filter_->setObjectName(QStringLiteral("FileFilterEdit"));
    filter_->setPlaceholderText(QStringLiteral("按文件名筛选"));
    actions->addWidget(filter_, 1);
    upload_ = new QPushButton(QStringLiteral("上传文件"), this);
    upload_->setObjectName(QStringLiteral("FileUploadButton"));
    upload_->setVisible(can_upload);
    download_ = new QPushButton(QStringLiteral("下载/打开"), this);
    download_->setObjectName(QStringLiteral("FileDownloadButton"));
    download_toggle_ = new QPushButton(QStringLiteral("暂停/继续下载"), this);
    download_toggle_->setObjectName(
        QStringLiteral("FileDownloadPauseButton"));
    remove_ = new QPushButton(QStringLiteral("删除服务器文件"), this);
    remove_->setObjectName(QStringLiteral("FileDeleteButton"));
    actions->addWidget(upload_);
    actions->addWidget(download_);
    actions->addWidget(download_toggle_);
    actions->addWidget(remove_);
    layout->addLayout(actions);

    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("FileList"));
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setIconSize(QSize(28, 28));
    layout->addWidget(list_, 1);

    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("FileManagerStatus"));
    layout->addWidget(status_);

    auto* close = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(close);

    connect(filter_, &QLineEdit::textChanged, this,
            &FileManagerDialog::applyFilter);
    connect(upload_, &QPushButton::clicked, this,
            &FileManagerDialog::uploadRequested);
    connect(download_, &QPushButton::clicked, this, [this] {
        QListWidgetItem* item = list_->currentItem();
        if (item != nullptr) {
            emit downloadRequested(item->data(Qt::UserRole).toULongLong(),
                                   item->data(Qt::UserRole + 1).toString(),
                                   item->data(Qt::UserRole + 2).toString());
        }
    });
    connect(download_toggle_, &QPushButton::clicked, this, [this] {
        QListWidgetItem* item = list_->currentItem();
        if (item != nullptr) {
            emit downloadToggleRequested(item->data(Qt::UserRole).toULongLong());
        }
    });
    connect(remove_, &QPushButton::clicked, this, [this] {
        QListWidgetItem* item = list_->currentItem();
        if (item == nullptr) {
            return;
        }
        if (QMessageBox::question(
                this, QStringLiteral("删除服务器文件"),
                QStringLiteral("删除后所有成员将看到“文件已删除”，本地已下载副本保留。确定删除？"),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No) != QMessageBox::Yes) {
            return;
        }
        emit deleteRequested(item->data(Qt::UserRole).toULongLong());
    });
    connect(list_, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem* item) {
                if (item != nullptr) {
                    emit downloadRequested(
                        item->data(Qt::UserRole).toULongLong(),
                        item->data(Qt::UserRole + 1).toString(),
                        item->data(Qt::UserRole + 2).toString());
                }
            });
    connect(list_, &QListWidget::itemSelectionChanged, this,
            &FileManagerDialog::updateButtons);
    updateButtons();
}

void FileManagerDialog::setFiles(
    quint32 total_count, quint64 used_bytes,
    const QVector<chatbox::client::FileEntryView>& entries) {
    entries_ = entries;
    list_->clear();
    for (const chatbox::client::FileEntryView& entry : entries_) {
        auto* item = new QListWidgetItem(
            QStringLiteral("%1  ·  %2  ·  %3")
                .arg(entry.name, formatFileSize(entry.size),
                     entry.uploader_name),
            list_);
        item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(entry.file_id));
        item->setData(Qt::UserRole + 1, entry.name);
        item->setData(Qt::UserRole + 2, entry.mime);
        item->setData(Qt::UserRole + 3, entry.can_delete);
        item->setData(Qt::UserRole + 4, entry.created_at);
    }
    title_->setText(kind_ == 1 ? QStringLiteral("群文件")
                                : QStringLiteral("私聊文件"));
    quota_->setText(
        kind_ == 1
            ? QStringLiteral("文件 %1 项 · 已用 %2 / 20GB")
                  .arg(total_count)
                  .arg(formatFileSize(used_bytes))
            : QStringLiteral("文件 %1 项").arg(total_count));
    applyFilter();
    updateButtons();
}

void FileManagerDialog::setStatus(const QString& text, bool error) {
    status_->setStyleSheet(error ? QStringLiteral("color:#c0392b;")
                                 : QString());
    status_->setText(text);
}

void FileManagerDialog::setBusy(bool busy) {
    list_->setEnabled(!busy);
    upload_->setEnabled(!busy);
    download_->setEnabled(!busy);
    download_toggle_->setEnabled(!busy);
    remove_->setEnabled(!busy);
}

void FileManagerDialog::applyFilter() {
    const QString keyword = filter_->text().trimmed();
    for (int i = 0; i < list_->count(); ++i) {
        QListWidgetItem* item = list_->item(i);
        item->setHidden(!keyword.isEmpty() &&
                        !item->text().contains(keyword, Qt::CaseInsensitive));
    }
}

void FileManagerDialog::updateButtons() {
    const QListWidgetItem* item = list_->currentItem();
    const bool has = item != nullptr;
    download_->setEnabled(has);
    download_toggle_->setEnabled(has);
    remove_->setEnabled(has && item->data(Qt::UserRole + 3).toBool());
}
