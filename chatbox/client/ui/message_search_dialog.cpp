#include "ui/message_search_dialog.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVBoxLayout>

#include "core/message_time.h"
#include "core/ui_theme.h"

namespace theme = chatbox::client::theme;  // Coinbase palette tokens

MessageSearchDialog::MessageSearchDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("搜索消息");
    resize(520, 460);

    auto* layout = new QVBoxLayout(this);
    auto* search_layout = new QHBoxLayout();
    search_edit_ = new QLineEdit(this);
    search_edit_->setPlaceholderText("输入消息关键词");
    search_button_ = new QPushButton("搜索", this);
    search_layout->addWidget(search_edit_, 1);
    search_layout->addWidget(search_button_);
    layout->addLayout(search_layout);

    result_list_ = new QListWidget(this);
    layout->addWidget(result_list_, 1);

    status_label_ = new QLabel("输入关键词后搜索", this);
    status_label_->setWordWrap(true);
    layout->addWidget(status_label_);

    connect(search_button_, &QPushButton::clicked, this,
            &MessageSearchDialog::submitSearch);
    connect(search_edit_, &QLineEdit::returnPressed, this,
            &MessageSearchDialog::submitSearch);
    connect(result_list_, &QListWidget::itemActivated, this,
            [this](QListWidgetItem* item) {
                if (item != nullptr) {
                    emit resultActivated(item->data(Qt::UserRole).toULongLong());
                }
            });
    connect(result_list_, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem* item) {
                if (item != nullptr) {
                    emit resultActivated(item->data(Qt::UserRole).toULongLong());
                }
            });
}

void MessageSearchDialog::setKeyword(const QString& keyword) {
    if (search_edit_ != nullptr) {
        search_edit_->setText(keyword);
        search_edit_->selectAll();
        search_edit_->setFocus();
    }
}

void MessageSearchDialog::setResults(const QVector<MessageSearchResult>& results) {
    result_list_->clear();
    for (const MessageSearchResult& result : results) {
        const QDateTime utc = chatbox::client::parseServerUtc(result.created_at);
        const QString local_time =
            utc.isValid() ? chatbox::client::formatLocalDate(utc) + " " +
                                chatbox::client::formatLocalTime(utc)
                          : result.created_at;
        QString text = QString("%1  %2: %3")
                           .arg(local_time, result.nickname,
                                result.recalled ? QString("消息已撤回")
                                                : result.content);
        auto* item = new QListWidgetItem(text, result_list_);
        item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(result.message_id));
    }
    status_label_->setText(results.isEmpty()
                               ? "没有找到消息"
                               : QString("找到 %1 条消息").arg(results.size()));
    status_label_->setStyleSheet("color: " + QString::fromLatin1(theme::kBody) + ";");
}

void MessageSearchDialog::setBusy(bool busy) {
    search_button_->setEnabled(!busy);
    search_edit_->setEnabled(!busy);
}

void MessageSearchDialog::setStatus(const QString& text, bool error) {
    status_label_->setText(text);
    status_label_->setStyleSheet(error
                                     ? QString("color: %1;").arg(theme::kSemanticDown)
                                     : QString("color: %1;").arg(theme::kBody));
}

void MessageSearchDialog::clearResults() {
    result_list_->clear();
    search_edit_->clear();
    setStatus("输入关键词后搜索");
    setBusy(false);
}

void MessageSearchDialog::submitSearch() {
    const QString keyword = search_edit_->text().trimmed();
    if (keyword.isEmpty()) {
        setStatus("请输入关键词", true);
        return;
    }
    setBusy(true);
    setStatus("正在搜索...");
    emit searchRequested(keyword);
}
