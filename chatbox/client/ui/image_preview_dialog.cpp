#include "ui/image_preview_dialog.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QMovie>
#include <QPixmap>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

ImagePreviewDialog::ImagePreviewDialog(const QString& path, QWidget* parent)
    : QDialog(parent), path_(path) {
    setObjectName(QStringLiteral("ImagePreviewDialog"));
    setWindowTitle(QFileInfo(path).fileName());
    resize(800, 600);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    image_ = new QLabel(this);
    image_->setObjectName(QStringLiteral("ImagePreviewLabel"));
    image_->setAlignment(Qt::AlignCenter);
    image_->setMinimumSize(200, 160);
    layout->addWidget(image_, 1);

    auto* actions = new QHBoxLayout();
    fit_button_ = new QPushButton(QStringLiteral("适应窗口"), this);
    fit_button_->setObjectName(QStringLiteral("ImagePreviewFitButton"));
    auto* actual = new QPushButton(QStringLiteral("原始大小"), this);
    actual->setObjectName(QStringLiteral("ImagePreviewActualButton"));
    auto* folder = new QPushButton(QStringLiteral("打开所在文件夹"), this);
    folder->setObjectName(QStringLiteral("ImagePreviewFolderButton"));
    auto* close = new QPushButton(QStringLiteral("关闭"), this);
    actions->addWidget(fit_button_);
    actions->addWidget(actual);
    actions->addWidget(folder);
    actions->addStretch(1);
    actions->addWidget(close);
    layout->addLayout(actions);

    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    connect(fit_button_, &QPushButton::clicked, this,
            &ImagePreviewDialog::fitToWindow);
    connect(actual, &QPushButton::clicked, this, [this] {
        QPixmap pixmap(path_);
        if (!pixmap.isNull()) {
            image_->setPixmap(pixmap);
        }
    });
    connect(folder, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(
            QUrl::fromLocalFile(QFileInfo(path_).absolutePath()));
    });

    QImageReader reader(path_);
    reader.setAutoTransform(true);
    if (reader.supportsAnimation() && reader.imageCount() > 1) {
        movie_ = new QMovie(path_, QByteArray(), this);
        image_->setMovie(movie_);
        movie_->start();
    } else {
        fitToWindow();
    }
}

void ImagePreviewDialog::fitToWindow() {
    if (movie_ != nullptr) {
        return;
    }
    QPixmap pixmap(path_);
    if (pixmap.isNull()) {
        image_->setText(QStringLiteral("无法预览该图片"));
        return;
    }
    image_->setPixmap(pixmap.scaled(image_->size(), Qt::KeepAspectRatio,
                                    Qt::SmoothTransformation));
}
