#ifndef CHATBOX_CLIENT_UI_MESSAGE_SEARCH_DIALOG_H
#define CHATBOX_CLIENT_UI_MESSAGE_SEARCH_DIALOG_H

#include <QDialog>
#include <QVector>

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;

struct MessageSearchResult {
    quint64 message_id = 0;
    quint64 sender_id = 0;
    QString nickname;
    QString content;
    QString created_at;
    bool recalled = false;
};

class MessageSearchDialog : public QDialog {
    Q_OBJECT

public:
    explicit MessageSearchDialog(QWidget* parent = nullptr);

    void setKeyword(const QString& keyword);
    void setResults(const QVector<MessageSearchResult>& results);
    void setBusy(bool busy);
    void setStatus(const QString& text, bool error = false);
    void clearResults();

signals:
    void searchRequested(const QString& keyword);

private slots:
    void submitSearch();

private:
    QLineEdit* search_edit_ = nullptr;
    QPushButton* search_button_ = nullptr;
    QListWidget* result_list_ = nullptr;
    QLabel* status_label_ = nullptr;
};

#endif  // CHATBOX_CLIENT_UI_MESSAGE_SEARCH_DIALOG_H
