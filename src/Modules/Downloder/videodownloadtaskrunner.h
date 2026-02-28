#ifndef VIDEODOWNLOADTASKRUNNER_H
#define VIDEODOWNLOADTASKRUNNER_H

#include <QObject>
#include <QProcess>

#include "videodownloadcommandbuilder.h"

/// @brief yt-dlp 下载任务执行器
/// @details 负责进程生命周期、日志转发、进度提取与取消控制，不包含 UI 逻辑。
class VideoDownloadTaskRunner : public QObject
{
    Q_OBJECT
public:
    explicit VideoDownloadTaskRunner(QObject *parent = nullptr);

    bool isRunning() const;

    /// @brief 启动下载任务
    /// @param request 下载请求
    void startTask(const VideoDownloadRequest &request);

    /// @brief 取消当前任务
    void cancelTask();

signals:
    void taskStarted();
    void taskLog(const QString &line);
    void progressChanged(int percent);
    void destinationResolved(const QString &fileName);
    void taskFinished(bool success, bool canceled, const QString &message);

private slots:
    void onReadyReadOutput();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessErrorOccurred(QProcess::ProcessError error);

private:
    QString resolveYtDlpPath() const;
    QString resolveExecutableInDeps(const QStringList &candidateNames) const;
    void processOutputLine(const QString &line);

    QProcess *m_process = nullptr;
    bool m_cancelRequested = false;
    QString m_stdoutBuffer;
};

#endif // VIDEODOWNLOADTASKRUNNER_H
