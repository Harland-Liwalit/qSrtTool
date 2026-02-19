#ifndef VIDEODOWNLOADER_H
#define VIDEODOWNLOADER_H

#include <QWidget>
#include <QIcon>

class QTimer;

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
    QTimer *m_toolsSpinTimer = nullptr;
    QIcon m_toolsBaseIcon;
    int m_toolsSpinAngle = 0;
    bool m_toolsLoading = false;

    void setToolsLoading(bool loading);
    void updateToolsSpinner();
};

#endif // VIDEODOWNLOADER_H
