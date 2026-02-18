#ifndef VIDEODOWNLOADER_H
#define VIDEODOWNLOADER_H

#include <QWidget>

namespace Ui {
class VideoDownloader;
}

class VideoDownloader : public QWidget
{
    Q_OBJECT

public:
    explicit VideoDownloader(QWidget *parent = nullptr);
    ~VideoDownloader();

private:
    Ui::VideoDownloader *ui;
};

#endif // VIDEODOWNLOADER_H
