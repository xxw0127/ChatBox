#ifndef CHATBOX_CLIENT_UI_MENTION_POPUP_H
#define CHATBOX_CLIENT_UI_MENTION_POPUP_H

#include <QFrame>
#include <QPixmap>
#include <QString>
#include <QVector>
#include <QtGlobal>

class QListWidget;

struct MentionCandidate {
    quint64 user_id = 0;  // 0 means "所有人"
    QString name;
    QString public_id;
    bool online = false;
    QPixmap avatar;
};

// Frameless child popup anchored above the composer caret. It deliberately
// does not take focus: the QTextEdit keeps receiving keystrokes so typing
// keeps filtering while the list is open.
class MentionPopup : public QFrame {
    Q_OBJECT

public:
    explicit MentionPopup(QWidget* parent = nullptr);

    void setCandidates(const QVector<MentionCandidate>& candidates,
                       const QString& query);
    void showAt(const QPoint& global_pos);
    void moveSelection(int delta);
    bool hasSelection() const;
    MentionCandidate selectedCandidate() const;
    void activateSelected();
    int visibleCount() const;

signals:
    void chosen(quint64 user_id, const QString& name);

private:
    void applyFilter(const QString& query);
    void chooseRow(int row);

    QListWidget* list_ = nullptr;
    QVector<MentionCandidate> candidates_;
};

#endif  // CHATBOX_CLIENT_UI_MENTION_POPUP_H
