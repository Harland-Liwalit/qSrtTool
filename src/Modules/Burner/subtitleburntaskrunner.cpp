#include "subtitleburntaskrunner.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QProcess>

SubtitleBurnTaskRunner::SubtitleBurnTaskRunner(QObject *parent)
    : QObject(parent)
    , m_process(new QProcess(this))
{
    m_process->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &SubtitleBurnTaskRunner::onReadyReadOutput);
    connect(m_process, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this, &SubtitleBurnTaskRunner::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred,
            this, &SubtitleBurnTaskRunner::onProcessErrorOccurred);
}

bool SubtitleBurnTaskRunner::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

void SubtitleBurnTaskRunner::startTask(const SubtitleBurnRequest &request)
{
    if (!m_process || isRunning()) {
        return;
    }

    const QString ffmpegPath = resolveFfmpegPath();
    if (ffmpegPath.isEmpty()) {
        emit taskFinished(false, QStringLiteral("未检测到 ffmpeg.exe，请先检查 deps 目录。"));
        return;
    }

    QString buildError;
    const QStringList args = SubtitleBurnCommandBuilder::buildArguments(request, &buildError);
    if (args.isEmpty()) {
        emit taskFinished(false, buildError.isEmpty() ? QStringLiteral("构建 FFmpeg 参数失败。") : buildError);
        return;
    }

    m_cancelRequested = false;

    emit taskLog(QStringLiteral("开始执行 FFmpeg..."));
    emit taskLog(QStringLiteral("命令：%1 %2").arg(ffmpegPath, args.join(' ')));

    m_process->start(ffmpegPath, args);
    if (!m_process->waitForStarted(3000)) {
        emit taskFinished(false, QStringLiteral("FFmpeg 启动失败，请确认可执行文件权限或路径有效。"));
        return;
    }

    emit taskStarted();
}

void SubtitleBurnTaskRunner::cancelTask()
{
    if (!isRunning()) {
        return;
    }

    m_cancelRequested = true;
    emit taskLog(QStringLiteral("正在取消当前任务..."));

    m_process->terminate();
    if (!m_process->waitForFinished(2000)) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

void SubtitleBurnTaskRunner::onReadyReadOutput()
{
    if (!m_process) {
        return;
    }

    const QString chunk = QString::fromLocal8Bit(m_process->readAllStandardOutput());
    const QStringList lines = chunk.split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        emit taskLog(line.trimmed());
    }
}

void SubtitleBurnTaskRunner::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    const bool normalSuccess = (exitStatus == QProcess::NormalExit && exitCode == 0);

    if (m_cancelRequested) {
        emit taskFinished(false, QStringLiteral("任务已取消。"));
        return;
    }

    if (normalSuccess) {
        emit taskFinished(true, QStringLiteral("字幕烧录完成。"));
    } else {
        emit taskFinished(false, QStringLiteral("FFmpeg 执行失败，退出码：%1").arg(exitCode));
    }
}

void SubtitleBurnTaskRunner::onProcessErrorOccurred(QProcess::ProcessError error)
{
    Q_UNUSED(error);

    if (m_cancelRequested) {
        return;
    }

    emit taskFinished(false, QStringLiteral("FFmpeg 进程异常中断。"));
}

QString SubtitleBurnTaskRunner::resolveFfmpegPath() const
{
    return resolveExecutableInDeps(QStringList() << "ffmpeg.exe");
}

QString SubtitleBurnTaskRunner::resolveExecutableInDeps(const QStringList &candidateNames) const
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
