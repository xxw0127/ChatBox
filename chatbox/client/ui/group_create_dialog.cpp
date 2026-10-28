#include "ui/group_create_dialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

GroupCreateDialog::GroupCreateDialog(QWidget* parent) : QDialog(parent) {
    setObjectName("GroupCreateDialog");
    setWindowTitle("创建群");
    setMinimumWidth(360);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 16, 18, 14);
    layout->setSpacing(8);
    auto* hint = new QLabel("群名 1–32 个字符，创建后生成 8 位群号。", this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    name_edit_ = new QLineEdit(this);
    name_edit_->setObjectName("GroupNameEdit");
    name_edit_->setPlaceholderText("群名");
    name_edit_->setMaxLength(64);
    layout->addWidget(name_edit_);

    error_label_ = new QLabel(this);
    error_label_->setObjectName("GroupCreateError");
    error_label_->setStyleSheet("color: #c0392b;");
    error_label_->hide();
    layout->addWidget(error_label_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    auto* submit = buttons->addButton("创建", QDialogButtonBox::AcceptRole);
    submit->setObjectName("GroupCreateSubmit");
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(submit, &QPushButton::clicked, this, &GroupCreateDialog::submit);
    connect(name_edit_, &QLineEdit::returnPressed, this, &GroupCreateDialog::submit);
}

void GroupCreateDialog::submit() {
    const QString name = name_edit_->text().trimmed();
    const int length = name.toUcs4().size();
    if (length < 1 || length > 32) {
        error_label_->setText("群名需要 1–32 个字符");
        error_label_->show();
        return;
    }
    error_label_->hide();
    emit createRequested(name);
}
