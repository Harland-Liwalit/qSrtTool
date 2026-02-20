#ifndef VIDEOLOADER_H
#define VIDEOLOADER_H

#include <QWidget>

class MediaController;
class QObject;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QMimeData;
class QVideoWidget;

namespace Ui {
class VideoLoader;
}

class VideoLoader : public QWidget
{
    Q_OBJECT

public:
    explicit VideoLoader(QWidget *parent = nullptr);
    ~VideoLoader();

signals:
    void statusMessage(const QString &message);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onImportVideoClicked();

private:
    void loadVideo(const QString &filePath);
    QString extractDroppedLocalFile(const QMimeData *mimeData) const;

private:
    Ui::VideoLoader *ui;
    MediaController *m_mediaController = nullptr;
    QVideoWidget *m_videoWidget = nullptr;
};

#endif // VIDEOLOADER_H
