#ifndef SUBTITLEBURNTASKRUNNER_H
#define SUBTITLEBURNTASKRUNNER_H

#include <QObject>
#include <QProcess>

#include "subtitleburncommandbuilder.h"

/// @brief 字幕烧录任务执行器
/// @details 仅负责进程生命周期与日志转发，不包含 UI 逻辑。
class SubtitleBurnTaskRunner : public QObject
{
    Q_OBJECT
public:
    explicit SubtitleBurnTaskRunner(QObject *parent = nullptr);

    bool isRunning() const;

    /// @brief 启动字幕烧录任务
    /// @param request 烧录请求
    void startTask(const SubtitleBurnRequest &request);

    /// @brief 取消当前任务
    void cancelTask();

signals:
    void taskStarted();
    void taskLog(const QString &line);
    void taskFinished(bool success, const QString &message);

private slots:
    void onReadyReadOutput();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessErrorOccurred(QProcess::ProcessError error);

private:
    QString resolveFfmpegPath() const;
    QString resolveExecutableInDeps(const QStringList &candidateNames) const;

    QProcess *m_process = nullptr;
    bool m_cancelRequested = false;
};

#endif // SUBTITLEBURNTASKRUNNER_H
