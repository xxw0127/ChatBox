#include "ui/mention_popup.h"

#include <QGraphicsDropShadowEffect>
#include <QLabel>
#include <QListWidget>
#include <QVBoxLayout>

#include "ui/mention_item_delegate.h"

namespace {

MentionCandidate rowCandidate(const QListWidgetItem* item) {
    MentionCandidate candidate;
    candidate.user_id = item->data(MentionItemDelegate::kUserIdRole).toULongLong();
    candidate.name = item->data(MentionItemDelegate::kNameRole).toString();
    candidate.public_id = item->data(MentionItemDelegate::kPublicIdRole).toString();
    candidate.online = item->data(MentionItemDelegate::kOnlineRole).toBool();
    candidate.avatar = item->data(MentionItemDelegate::kAvatarRole).value<QPixmap>();
    return candidate;
}

}  // namespace

MentionPopup::MentionPopup(QWidget* parent) : QFrame(parent) {
    setObjectName(QStringLiteral("MentionPopup"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFocusPolicy(Qt::NoFocus);
    setFixedWidth(280);
    hide();

    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(18);
    shadow->setOffset(0, 6);
    shadow->setColor(QColor(0, 0, 0, 32));
    setGraphicsEffect(shadow);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(0);

    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("MentionList"));
    list_->setFocusPolicy(Qt::NoFocus);
    list_->setFrameShape(QFrame::NoFrame);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setMouseTracking(true);
    list_->setUniformItemSizes(true);
    list_->setItemDelegate(new MentionItemDelegate(list_));
    layout->addWidget(list_);

    empty_label_ = new QLabel(QString::fromUtf8("\u6ca1\u6709\u5339\u914d\u7684\u6210\u5458"), this);
    empty_label_->setObjectName(QStringLiteral("MentionEmpty"));
    empty_label_->setAlignment(Qt::AlignCenter);
    empty_label_->hide();
    layout->addWidget(empty_label_);

    connect(list_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (item == nullptr) {
            return;
        }
        chooseRow(list_->row(item));
    });
}

void MentionPopup::setCandidates(const QVector<MentionCandidate>& candidates,
                                 const QString& query) {
    candidates_ = candidates;
    bool has_all = false;
    for (const MentionCandidate& candidate : candidates_) {
        if (candidate.user_id == 0) {
            has_all = true;
            break;
        }
    }
    if (!has_all) {
        MentionCandidate all;
        all.name = QString::fromUtf8("\u6240\u6709\u4eba");
        all.online = true;
        candidates_.prepend(all);
    }
    applyFilter(query);
}

void MentionPopup::applyFilter(const QString& query) {
    list_->clear();
    for (const MentionCandidate& candidate : candidates_) {
        if (!query.isEmpty() &&
            !candidate.name.contains(query, Qt::CaseInsensitive) &&
            !candidate.public_id.contains(query, Qt::CaseInsensitive)) {
            continue;
        }
        auto* item = new QListWidgetItem(list_);
        item->setData(MentionItemDelegate::kUserIdRole,
                      QVariant::fromValue<qulonglong>(candidate.user_id));
        item->setData(MentionItemDelegate::kNameRole, candidate.name);
        item->setData(MentionItemDelegate::kPublicIdRole, candidate.public_id);
        item->setData(MentionItemDelegate::kOnlineRole, candidate.online);
        item->setData(MentionItemDelegate::kAvatarRole, candidate.avatar);
        item->setSizeHint(QSize(0, 46));
    }
    if (list_->count() > 0) {
        // With a query, the first member is the useful default; "所有人"
        // stays reachable with Up. An empty query highlights 所有人, matching
        // the reference panel.
        list_->setCurrentRow(query.isEmpty() ? 0 : qMin(1, list_->count() - 1));
    }
    if (list_->count() == 0) {
        list_->hide();
        empty_label_->show();
        setFixedHeight(44);
        return;
    }
    empty_label_->hide();
    list_->show();
    setFixedHeight(qMin(6, list_->count()) * 46 + 12);
}

void MentionPopup::showAt(const QPoint& global_pos) {
    if (parentWidget() == nullptr) {
        return;
    }
    adjustSize();
    move(parentWidget()->mapFromGlobal(global_pos));
    show();
    raise();
}

void MentionPopup::moveSelection(int delta) {
    if (list_->count() == 0) {
        return;
    }
    const int count = list_->count();
    const int current = list_->currentRow() < 0 ? 0 : list_->currentRow();
    const int next = ((current + delta) % count + count) % count;
    list_->setCurrentRow(next);
    list_->scrollToItem(list_->currentItem());
}

bool MentionPopup::hasSelection() const {
    return list_->count() > 0 && list_->currentRow() >= 0;
}

MentionCandidate MentionPopup::selectedCandidate() const {
    const QListWidgetItem* item = list_->currentItem();
    return item == nullptr ? MentionCandidate() : rowCandidate(item);
}

void MentionPopup::activateSelected() {
    if (!hasSelection()) {
        return;
    }
    chooseRow(list_->currentRow());
}

void MentionPopup::chooseRow(int row) {
    if (row < 0 || row >= list_->count()) {
        return;
    }
    const MentionCandidate candidate = rowCandidate(list_->item(row));
    hide();
    emit chosen(candidate.user_id, candidate.name);
}

int MentionPopup::visibleCount() const {
    return list_->count();
}
