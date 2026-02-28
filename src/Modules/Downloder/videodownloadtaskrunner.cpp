#include "videodownloadtaskrunner.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QRegularExpression>
#include <QtMath>

VideoDownloadTaskRunner::VideoDownloadTaskRunner(QObject *parent)
    : QObject(parent)
    , m_process(new QProcess(this))
{
    m_process->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &VideoDownloadTaskRunner::onReadyReadOutput);
    connect(m_process, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this, &VideoDownloadTaskRunner::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred,
            this, &VideoDownloadTaskRunner::onProcessErrorOccurred);
}

bool VideoDownloadTaskRunner::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

void VideoDownloadTaskRunner::startTask(const VideoDownloadRequest &request)
{
    if (!m_process || isRunning()) {
        return;
    }

    const QString ytDlpPath = resolveYtDlpPath();
    if (ytDlpPath.isEmpty()) {
        emit taskFinished(false, false, QStringLiteral("未检测到 yt-dlp.exe，请先检查 deps 目录。"));
        return;
    }

    QString buildError;
    const QStringList args = VideoDownloadCommandBuilder::buildArguments(request, &buildError);
    if (args.isEmpty()) {
        emit taskFinished(false, false, buildError.isEmpty() ? QStringLiteral("构建 yt-dlp 参数失败。") : buildError);
        return;
    }

    m_cancelRequested = false;
    m_stdoutBuffer.clear();

    emit taskLog(QStringLiteral("开始执行 yt-dlp..."));
    emit taskLog(QStringLiteral("命令：%1 %2").arg(ytDlpPath, args.join(' ')));

    m_process->start(ytDlpPath, args);
    if (!m_process->waitForStarted(3000)) {
        emit taskFinished(false, false, QStringLiteral("yt-dlp 启动失败，请检查程序权限或路径。"));
        return;
    }

    emit taskStarted();
}

void VideoDownloadTaskRunner::cancelTask()
{
    if (!isRunning()) {
        return;
    }

    m_cancelRequested = true;
    emit taskLog(QStringLiteral("正在取消下载任务..."));

    m_process->terminate();
    if (!m_process->waitForFinished(2000)) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

void VideoDownloadTaskRunner::onReadyReadOutput()
{
    if (!m_process) {
        return;
    }

    m_stdoutBuffer += QString::fromLocal8Bit(m_process->readAllStandardOutput());
    int newlineIndex = m_stdoutBuffer.indexOf('\n');
    while (newlineIndex >= 0) {
        const QString line = m_stdoutBuffer.left(newlineIndex).trimmed();
        m_stdoutBuffer.remove(0, newlineIndex + 1);
        if (!line.isEmpty()) {
            processOutputLine(line);
        }
        newlineIndex = m_stdoutBuffer.indexOf('\n');
    }
}

void VideoDownloadTaskRunner::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (!m_stdoutBuffer.trimmed().isEmpty()) {
        processOutputLine(m_stdoutBuffer.trimmed());
    }
    m_stdoutBuffer.clear();

    const bool normalSuccess = (exitStatus == QProcess::NormalExit && exitCode == 0);

    if (m_cancelRequested) {
        emit taskFinished(false, true, QStringLiteral("下载任务已取消。"));
        return;
    }

    if (normalSuccess) {
        emit progressChanged(100);
        emit taskFinished(true, false, QStringLiteral("下载完成。"));
    } else {
        emit taskFinished(false, false, QStringLiteral("yt-dlp 执行失败，退出码：%1").arg(exitCode));
    }
}

void VideoDownloadTaskRunner::onProcessErrorOccurred(QProcess::ProcessError error)
{
    Q_UNUSED(error);

    if (m_cancelRequested) {
        return;
    }

    emit taskFinished(false, false, QStringLiteral("yt-dlp 进程异常中断。"));
}

QString VideoDownloadTaskRunner::resolveYtDlpPath() const
{
    return resolveExecutableInDeps(QStringList() << "yt-dlp.exe" << "ytdlp.exe");
}

QString VideoDownloadTaskRunner::resolveExecutableInDeps(const QStringList &candidateNames) const
{
    const QString depsDir = QDir(QDir::currentPath()).filePath("deps");

    for (const QString &name : candidateNames) {
        const QString directPath = QDir(depsDir).filePath(name);
        if (QFileInfo::exists(directPath)) {
            return directPath;
        }
    }

    QDirIterator it(depsDir, candidateNames, QDir::Files, QDirIterator::Subdirectories);
    if (it.hasNext()) {
        return it.next();
    }

    return QString();
}

void VideoDownloadTaskRunner::processOutputLine(const QString &line)
{
    emit taskLog(line);

    static const QRegularExpression percentRegex(QStringLiteral("(\\d+(?:\\.\\d+)?)%"));
    const QRegularExpressionMatch percentMatch = percentRegex.match(line);
    if (percentMatch.hasMatch()) {
        const int percent = qBound(0, qRound(percentMatch.captured(1).toDouble()), 100);
        emit progressChanged(percent);
    }

    static const QRegularExpression destinationRegex(
        QStringLiteral("(?:Destination:|Merging formats into)\\s*\"?(.+?)\"?$"));
    const QRegularExpressionMatch destinationMatch = destinationRegex.match(line);
    if (destinationMatch.hasMatch()) {
        const QString filePath = destinationMatch.captured(1).trimmed();
        const QString fileName = QFileInfo(filePath).fileName();
        if (!fileName.isEmpty()) {
            emit destinationResolved(fileName);
        }
    }
}
