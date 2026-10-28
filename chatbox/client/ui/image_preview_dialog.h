#ifndef CHATBOX_CLIENT_UI_IMAGE_PREVIEW_DIALOG_H
#define CHATBOX_CLIENT_UI_IMAGE_PREVIEW_DIALOG_H

#include <QDialog>

class QLabel;
class QMovie;
class QPushButton;

class ImagePreviewDialog : public QDialog {
    Q_OBJECT

public:
    explicit ImagePreviewDialog(const QString& path, QWidget* parent = nullptr);

private:
    void fitToWindow();

    QString path_;
    QLabel* image_ = nullptr;
    QMovie* movie_ = nullptr;
    QPushButton* fit_button_ = nullptr;
};

#endif  // CHATBOX_CLIENT_UI_IMAGE_PREVIEW_DIALOG_H
