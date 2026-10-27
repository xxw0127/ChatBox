#include "ui/update_dialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

UpdateDialog::UpdateDialog(const chatbox::client::UpdateManifest& manifest,
                           QWidget* parent)
    : QDialog(parent) {
    setObjectName(QStringLiteral("UpdateDialog"));
    setWindowTitle(QStringLiteral("发现新版本"));
    setModal(false);
    setMinimumWidth(420);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 16);
    layout->setSpacing(10);

    auto* title = new QLabel(QStringLiteral("ChatBox %1 可用").arg(manifest.version), this);
    title->setObjectName(QStringLiteral("UpdateTitleLabel"));
    layout->addWidget(title);

    notes_label_ = new QLabel(manifest.notes.isEmpty()
                                  ? QStringLiteral("新版本包含质量改进。")
                                  : manifest.notes,
                              this);
    notes_label_->setObjectName(QStringLiteral("UpdateNotesLabel"));
    notes_label_->setWordWrap(true);
    layout->addWidget(notes_label_);

    progress_bar_ = new QProgressBar(this);
    progress_bar_->setObjectName(QStringLiteral("UpdateProgressBar"));
    progress_bar_->setRange(0, 100);
    progress_bar_->setValue(0);
    progress_bar_->hide();
    layout->addWidget(progress_bar_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::NoButton, this);
    update_button_ = buttons->addButton(QStringLiteral("立即更新"),
                                         QDialogButtonBox::AcceptRole);
    update_button_->setObjectName(QStringLiteral("UpdateNowButton"));
    later_button_ = buttons->addButton(QStringLiteral("稍后"),
                                       QDialogButtonBox::RejectRole);
    later_button_->setObjectName(QStringLiteral("UpdateLaterButton"));
    // ActionRole keeps this button from closing the dialog, so the user can
    // retry after a cancelled download.
    cancel_button_ = buttons->addButton(QStringLiteral("取消下载"),
                                         QDialogButtonBox::ActionRole);
    cancel_button_->setObjectName(QStringLiteral("UpdateCancelButton"));
    cancel_button_->hide();
    layout->addWidget(buttons);

    connect(update_button_, &QPushButton::clicked, this,
            &UpdateDialog::updateRequested);
    connect(later_button_, &QPushButton::clicked, this, &QDialog::reject);
    connect(cancel_button_, &QPushButton::clicked, this,
            &UpdateDialog::cancelRequested);
}

void UpdateDialog::setDownloadProgress(qint64 received, qint64 total) {
    progress_bar_->show();
    update_button_->setEnabled(false);
    later_button_->hide();
    cancel_button_->show();
    cancel_button_->setEnabled(true);
    if (total > 0) {
        progress_bar_->setRange(0, 100);
        progress_bar_->setValue(static_cast<int>(received * 100 / total));
    } else {
        progress_bar_->setRange(0, 0);
    }
}

void UpdateDialog::setBusy(bool busy) {
    update_button_->setEnabled(!busy);
    if (busy) {
        progress_bar_->show();
        progress_bar_->setRange(0, 0);
        later_button_->hide();
        cancel_button_->show();
        cancel_button_->setEnabled(true);
    } else {
        progress_bar_->hide();
        later_button_->show();
        later_button_->setEnabled(true);
        cancel_button_->hide();
    }
}

