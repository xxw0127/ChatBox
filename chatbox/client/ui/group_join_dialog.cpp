#include "ui/group_join_dialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

GroupJoinDialog::GroupJoinDialog(QWidget* parent) : QDialog(parent) {
    setObjectName("GroupJoinDialog");
    setWindowTitle("加入群");
    setMinimumWidth(400);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 16, 18, 14);
    layout->setSpacing(8);

    auto* form = new QFormLayout();
    group_no_edit_ = new QLineEdit(this);
    group_no_edit_->setObjectName("GroupNumberEdit");
    group_no_edit_->setPlaceholderText("8 位群号");
    group_no_edit_->setMaxLength(8);
    form->addRow("群号", group_no_edit_);
    message_edit_ = new QLineEdit(this);
    message_edit_->setObjectName("GroupJoinMessageEdit");
    message_edit_->setPlaceholderText("申请说明（可选）");
    message_edit_->setMaxLength(200);
    form->addRow("申请", message_edit_);
    layout->addLayout(form);

    result_label_ = new QLabel(this);
    result_label_->setObjectName("GroupSearchResultLabel");
    result_label_->setWordWrap(true);
    layout->addWidget(result_label_);

    auto* actions = new QHBoxLayout();
    auto* search_button = new QPushButton("搜索", this);
    search_button->setObjectName("GroupJoinSearchButton");
    actions->addWidget(search_button);
    actions->addStretch(1);
    layout->addLayout(actions);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    submit_button_ = buttons->addButton("申请加入", QDialogButtonBox::AcceptRole);
    submit_button_->setObjectName("GroupJoinSubmit");
    submit_button_->setEnabled(false);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(search_button, &QPushButton::clicked, this, &GroupJoinDialog::search);
    connect(group_no_edit_, &QLineEdit::returnPressed, this, &GroupJoinDialog::search);
    connect(submit_button_, &QPushButton::clicked, this, &GroupJoinDialog::submit);
    connect(group_no_edit_, &QLineEdit::textChanged, this,
            &GroupJoinDialog::updateSubmitState);
}

void GroupJoinDialog::search() {
    const QString group_no = group_no_edit_->text().trimmed();
    static const QRegularExpression pattern("^[0-9]{8}$");
    if (!pattern.match(group_no).hasMatch()) {
        result_label_->setText("请输入 8 位数字群号");
        found_ = false;
        updateSubmitState();
        return;
    }
    result_label_->setText("正在搜索...");
    found_ = false;
    updateSubmitState();
    emit searchRequested(group_no);
}

void GroupJoinDialog::setSearchResult(bool found, const QString& name,
                                      int member_count, int membership) {
    found_ = found;
    membership_ = membership;
    if (!found) {
        result_label_->setText("没有找到这个群");
    } else if (membership == 1) {
        result_label_->setText(QString("%1 · %2 人 · 你已在群中").arg(name).arg(member_count));
    } else if (membership == 2) {
        result_label_->setText(QString("%1 · %2 人 · 已申请，等待审批").arg(name).arg(member_count));
    } else {
        result_label_->setText(QString("%1 · %2 人").arg(name).arg(member_count));
    }
    updateSubmitState();
}

void GroupJoinDialog::updateSubmitState() {
    if (submit_button_ == nullptr) {
        return;
    }
    submit_button_->setEnabled(found_ && membership_ == 0);
}

void GroupJoinDialog::submit() {
    if (!found_ || membership_ != 0) {
        return;
    }
    emit joinRequested(group_no_edit_->text().trimmed(),
                       message_edit_->text().trimmed());
}
