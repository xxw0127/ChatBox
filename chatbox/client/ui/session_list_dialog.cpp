#include "ui/session_list_dialog.h"

#include <QHeaderView>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

SessionListDialog::SessionListDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("设备管理"));
    resize(620, 340);
    setObjectName(QStringLiteral("SessionListDialog"));

    auto* layout = new QVBoxLayout(this);
    table_ = new QTableWidget(this);
    table_->setObjectName(QStringLiteral("SessionTable"));
    table_->setColumnCount(5);
    table_->setHorizontalHeaderLabels(
        {QStringLiteral("设备"), QStringLiteral("平台"), QStringLiteral("版本"),
         QStringLiteral("最后活跃"), QStringLiteral("操作")});
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    layout->addWidget(table_, 1);

    auto* buttons = new QHBoxLayout();
    auto* refresh = new QPushButton(QStringLiteral("刷新"), this);
    refresh->setObjectName(QStringLiteral("SessionRefreshButton"));
    auto* close = new QPushButton(QStringLiteral("关闭"), this);
    buttons->addWidget(refresh);
    buttons->addStretch(1);
    buttons->addWidget(close);
    layout->addLayout(buttons);
    connect(refresh, &QPushButton::clicked, this, &SessionListDialog::refreshRequested);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
}

void SessionListDialog::setSessions(const QVector<SessionView>& sessions) {
    table_->setRowCount(0);
    for (const SessionView& session : sessions) {
        const int row = table_->rowCount();
        table_->insertRow(row);
        table_->setItem(row, 0, new QTableWidgetItem(session.device_name));
        table_->setItem(row, 1, new QTableWidgetItem(session.platform));
        table_->setItem(row, 2, new QTableWidgetItem(session.app_version));
        table_->setItem(row, 3, new QTableWidgetItem(session.last_seen_at));
        if (session.current) {
            table_->setItem(row, 4, new QTableWidgetItem(QStringLiteral("当前设备")));
            continue;
        }
        auto* revoke = new QPushButton(QStringLiteral("注销"), this);
        revoke->setObjectName(QStringLiteral("SessionRevokeButton"));
        const QString session_id = session.session_id;
        connect(revoke, &QPushButton::clicked, this, [this, session_id]() {
            if (QMessageBox::question(this, QStringLiteral("注销设备"),
                                      QStringLiteral("注销后该设备会立即退出登录，继续吗？")) ==
                QMessageBox::Yes) {
                emit revokeRequested(session_id);
            }
        });
        table_->setCellWidget(row, 4, revoke);
    }
}
