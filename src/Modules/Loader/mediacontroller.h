#ifndef MEDIACONTROLLER_H
#define MEDIACONTROLLER_H

#include <QObject>

class QMediaPlayer;
class QVideoWidget;
class QProcess;

class MediaController : public QObject
{
    Q_OBJECT

public:
    explicit MediaController(QObject *parent = nullptr);

    void setUseFfplay(bool enabled);

    void setVideoOutput(QVideoWidget *videoWidget);

    bool loadVideo(const QString &filePath);
    void play();
    void pause();
    void stop();

    QString currentFilePath() const;

signals:
    void mediaLoaded(const QString &filePath);
    void mediaLoadFailed(const QString &reason);
    void mediaStatusMessage(const QString &message);

private:
    bool isVideoFile(const QString &filePath) const;
    QString resolveFfplayPath() const;
    bool startFfplay(const QString &filePath);

    QMediaPlayer *m_player = nullptr;
    QProcess *m_ffplayProcess = nullptr;
    QString m_currentFilePath;
    bool m_useFfplay = true;
    QString m_cachedFfplayPath;
};

#endif // MEDIACONTROLLER_H
