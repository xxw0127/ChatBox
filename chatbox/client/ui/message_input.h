#ifndef CHATBOX_CLIENT_UI_MESSAGE_INPUT_H
#define CHATBOX_CLIENT_UI_MESSAGE_INPUT_H

#include <QTextEdit>

class QKeyEvent;
class QMimeData;
class QDragEnterEvent;
class QDropEvent;
class QImage;

// Multi-line composer: Enter submits, Shift+Enter inserts a newline, and the
// control grows from 40 to 120px before scrolling internally. Rich paste is
// deliberately discarded so the protocol still receives plain UTF-8 text.
class MessageInput : public QTextEdit {
    Q_OBJECT

public:
    explicit MessageInput(QWidget* parent = nullptr);

signals:
    void submitRequested();
    void filesDropped(const QStringList& paths);
    void imagePasted(const QImage& image);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void insertFromMimeData(const QMimeData* source) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void adjustHeight();
};

#endif  // CHATBOX_CLIENT_UI_MESSAGE_INPUT_H
