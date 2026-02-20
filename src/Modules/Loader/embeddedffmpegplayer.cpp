#include "embeddedffmpegplayer.h"

#include <QByteArray>
#include <QAudioFormat>
#include <QAudioOutput>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPixmap>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSlider>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>

namespace {
constexpr int kSliderMax = 1000;  ///< 进度条滑块的最大值
}

/// @brief 构造函数 - 初始化播放器UI和信号连接
/// 
/// 流程：
/// 1. 创建主布局和视频显示标签
/// 2. 创建播放控制按钮（播放/暂停、快进、快退）
/// 3. 创建进度条和时间标签
/// 4. 应用样式表设置外观
/// 5. 初始化FFmpeg解码进程
/// 6. 连接所有信号和槽
EmbeddedFfmpegPlayer::EmbeddedFfmpegPlayer(QWidget *parent)
    : QWidget(parent)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(8);

    m_videoSurface = new QLabel(this);
    m_videoSurface->setMinimumHeight(360);
    m_videoSurface->setFocusPolicy(Qt::StrongFocus);
    m_videoSurface->setStyleSheet("background-color: black;");
    m_videoSurface->setAlignment(Qt::AlignCenter);
    m_videoSurface->setScaledContents(true);
    m_videoSurface->installEventFilter(this);
    mainLayout->addWidget(m_videoSurface, 1);

    auto *progressLayout = new QHBoxLayout();
    progressLayout->setContentsMargins(8, 2, 8, 0);
    progressLayout->setSpacing(8);

    auto *controlsLayout = new QHBoxLayout();
    controlsLayout->setContentsMargins(8, 0, 8, 6);
    controlsLayout->setSpacing(10);

    m_rewindButton = new QPushButton(QStringLiteral("⏪"), this);
    m_playPauseButton = new QPushButton(QStringLiteral("▶"), this);
    m_forwardButton = new QPushButton(QStringLiteral("⏩"), this);
    m_volumeIcon = new QLabel(QStringLiteral("🔊"), this);
    m_volumeSlider = new QSlider(Qt::Horizontal, this);
    m_progressSlider = new QSlider(Qt::Horizontal, this);
    m_timeLabel = new QLabel(tr("00:00 / 00:00"), this);

    m_progressSlider->setRange(0, kSliderMax);
    m_progressSlider->setEnabled(false);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(m_volumePercent);
    m_volumeSlider->setFixedWidth(120);
    m_volumeSlider->setToolTip(tr("音量"));

    m_playPauseButton->setMinimumSize(56, 42);
    m_rewindButton->setMinimumSize(44, 34);
    m_forwardButton->setMinimumSize(44, 34);
    m_volumeIcon->setMinimumWidth(20);
    m_volumeIcon->setAlignment(Qt::AlignCenter);

    const QString barStyle = QStringLiteral(
        "QPushButton { border: 1px solid #95a7bb; border-radius: 16px; background: #dce6f2; }"
        "QPushButton:pressed { background: #c4d3e4; }"
        "QSlider::groove:horizontal { border: 1px solid #9fb2c7; height: 6px; background: #d9e2ec; border-radius: 3px; }"
        "QSlider::sub-page:horizontal { background: #7ca3d1; border-radius: 3px; }"
        "QSlider::handle:horizontal { width: 14px; margin: -5px 0; border-radius: 7px; background: #f4f8fc; border: 1px solid #8ea4ba; }"
    );
    m_rewindButton->setStyleSheet(barStyle);
    m_playPauseButton->setStyleSheet(barStyle);
    m_forwardButton->setStyleSheet(barStyle);
    m_progressSlider->setStyleSheet(barStyle);
    m_volumeSlider->setStyleSheet(barStyle);

    progressLayout->addWidget(m_progressSlider, 1);
    progressLayout->addWidget(m_timeLabel);

    controlsLayout->addStretch();
    controlsLayout->addWidget(m_rewindButton);
    controlsLayout->addWidget(m_playPauseButton);
    controlsLayout->addWidget(m_forwardButton);
    controlsLayout->addSpacing(8);
    controlsLayout->addWidget(m_volumeIcon);
    controlsLayout->addWidget(m_volumeSlider);
    controlsLayout->addStretch();

    mainLayout->addLayout(progressLayout);
    mainLayout->addLayout(controlsLayout);

    m_progressTimer = new QTimer(this);
    m_progressTimer->setInterval(150);

    QAudioFormat audioFormat;
    audioFormat.setSampleRate(48000);
    audioFormat.setChannelCount(2);
    audioFormat.setSampleSize(16);
    audioFormat.setCodec(QStringLiteral("audio/pcm"));
    audioFormat.setByteOrder(QAudioFormat::LittleEndian);
    audioFormat.setSampleType(QAudioFormat::SignedInt);
    m_audioOutput = new QAudioOutput(audioFormat, this);
    m_audioOutput->setNotifyInterval(30);
    m_audioOutput->setVolume(static_cast<qreal>(m_volumePercent) / 100.0);

    setupDecoderProcess();
    setupAudioProcess();

    connect(m_playPauseButton, &QPushButton::clicked, this, &EmbeddedFfmpegPlayer::playPause);
    connect(m_rewindButton, &QPushButton::clicked, this, &EmbeddedFfmpegPlayer::seekBackward);
    connect(m_forwardButton, &QPushButton::clicked, this, &EmbeddedFfmpegPlayer::seekForward);
    connect(m_progressSlider, &QSlider::sliderPressed, this, &EmbeddedFfmpegPlayer::onSliderPressed);
    connect(m_progressSlider, &QSlider::sliderReleased, this, &EmbeddedFfmpegPlayer::onSliderReleased);
    connect(m_volumeSlider, &QSlider::valueChanged, this, &EmbeddedFfmpegPlayer::onVolumeChanged);
    connect(m_audioOutput, &QAudioOutput::notify, this, &EmbeddedFfmpegPlayer::flushAudioBuffer);

    connect(m_progressTimer, &QTimer::timeout, this, &EmbeddedFfmpegPlayer::onProgressTick);

    m_progressSlider->installEventFilter(this);
    setFocusPolicy(Qt::StrongFocus);
}

EmbeddedFfmpegPlayer::~EmbeddedFfmpegPlayer()
{
    stopPlayback();
    destroyAudioProcess();
    destroyDecoderProcess();
}

/// @brief 加载视频文件
/// 
/// 调用链：VideoLoader::loadVideo() -> EmbeddedFfmpegPlayer::loadVideo()
/// 
/// 实现流程：
/// 1. 验证文件是有效的视频格式
/// 2. 停止当前播放并重建解码进程，清空旧状态
/// 3. 设置新视频路径并重置播放位置到0
/// 4. 查找FFmpeg可执行文件
/// 5. 使用ffprobe获取视频元数据（分辨率、时长、帧率等）
/// 6. 更新UI显示
/// 
/// @param filePath 视频文件的绝对路径
/// @return 成功加载返回true，否则发出错误信号并返回false
bool EmbeddedFfmpegPlayer::loadVideo(const QString &filePath)
{
    if (!isVideoFile(filePath)) {
        emit playbackError(tr("不是有效的视频文件"));
        return false;
    }

    stopPlayback();
    destroyAudioProcess();
    destroyDecoderProcess();
    setupAudioProcess();
    setupDecoderProcess();

    m_currentFilePath = QFileInfo(filePath).absoluteFilePath();
    m_positionMs = 0;
    m_startPositionMs = 0;
    m_decodedFrameCount = 0;
    m_isPlaying = false;
    m_playPauseButton->setText(QStringLiteral("▶"));
    clearFrameBuffer();
    m_audioBuffer.clear();
    m_videoSurface->setPixmap(QPixmap());

    m_cachedFfmpegPath = resolveFfmpegPath();
    if (m_cachedFfmpegPath.isEmpty()) {
        emit ffmpegMissing();
        return false;
    }

    refreshVideoMeta();
    updateProgressUi();
    emit statusMessage(tr("视频已加载"));
    return true;
}

QString EmbeddedFfmpegPlayer::currentFilePath() const
{
    return m_currentFilePath;
}

/// @brief 播放/暂停切换
/// 
/// 调用链：
/// - 用户点击按钮或按Space/K键 -> playPause()
/// - 或 seekTo()/onSliderReleased() 恢复播放 -> beginPlaybackFromCurrentPosition()
/// 
/// 实现流程：
/// 1. 未播放状态：调用startPlaybackAt(m_positionMs)启动FFmpeg解码进程，进度定时器每150ms刷新UI
/// 2. 播放中状态：计算当前播放位置，停止FFmpeg进程，暂停播放直到下次调用
/// 
/// 内部状态管理：
/// - m_playbackClock: 用于计算运行时间（毫秒），与解码帧数配合获取准确时间
/// - m_startPositionMs: 记录本次播放的起始位置
/// - m_progressTimer: 每150ms触发一次刷新，更新时间标签和进度条显示
void EmbeddedFfmpegPlayer::playPause()
{
    if (m_currentFilePath.isEmpty()) {
        emit playbackError(tr("请先导入视频"));
        return;
    }

    if (!m_isPlaying) {
        if (!beginPlaybackFromCurrentPosition()) {
            emit playbackError(tr("无法启动 FFmpeg 播放"));
            return;
        }
        emit statusMessage(tr("开始播放"));
        return;
    }

    m_positionMs = m_startPositionMs + m_playbackClock.elapsed();
    stopPlayback();
    m_isPlaying = false;
    m_playPauseButton->setText(QStringLiteral("▶"));
    updateProgressUi();
    emit statusMessage(tr("已暂停"));
}

void EmbeddedFfmpegPlayer::stopPlayback()
{
    m_progressTimer->stop();
    m_frameBytes = 0;
    m_audioSinkDevice = nullptr;

    if (m_ffmpegProcess && m_ffmpegProcess->state() != QProcess::NotRunning) {
        const bool previouslyBlocked = m_ffmpegProcess->blockSignals(true);
        m_userStopping = true;
        m_ffmpegProcess->terminate();
        if (!m_ffmpegProcess->waitForFinished(800)) {
            m_ffmpegProcess->kill();
            m_ffmpegProcess->waitForFinished(1500);
        }
        m_userStopping = false;
        m_ffmpegProcess->blockSignals(previouslyBlocked);
    }
    if (m_ffmpegProcess && m_ffmpegProcess->isOpen()) {
        m_ffmpegProcess->readAllStandardOutput();
        m_ffmpegProcess->readAllStandardError();
    }

    if (m_audioProcess && m_audioProcess->state() != QProcess::NotRunning) {
        const bool previouslyBlocked = m_audioProcess->blockSignals(true);
        m_audioProcess->terminate();
        if (!m_audioProcess->waitForFinished(800)) {
            m_audioProcess->kill();
            m_audioProcess->waitForFinished(1500);
        }
        m_audioProcess->blockSignals(previouslyBlocked);
    }
    if (m_audioProcess && m_audioProcess->isOpen()) {
        m_audioProcess->readAllStandardOutput();
        m_audioProcess->readAllStandardError();
    }

    if (m_audioOutput) {
        m_audioOutput->stop();
    }
    m_audioBuffer.clear();
    clearFrameBuffer();
}

void EmbeddedFfmpegPlayer::seekForward()
{
    if (m_currentFilePath.isEmpty()) {
        return;
    }
    qint64 target = m_positionMs + 10000;
    if (m_durationMs > 0) {
        target = qMin(target, m_durationMs);
    }
    seekTo(target);
}

void EmbeddedFfmpegPlayer::seekBackward()
{
    if (m_currentFilePath.isEmpty()) {
        return;
    }
    seekTo(qMax<qint64>(0, m_positionMs - 10000));
}

bool EmbeddedFfmpegPlayer::eventFilter(QObject *watched, QEvent *event)
{
    if (!event) {
        return QWidget::eventFilter(watched, event);
    }

    if (watched == m_videoSurface && event->type() == QEvent::MouseButtonPress) {
        m_videoSurface->setFocus();
    }

    // 点击进度条时仅设置滑块位置，实际跳转由QSlider正常的按下/释放流程触发
    if (watched == m_progressSlider && m_progressSlider && event->type() == QEvent::MouseButtonPress) {
        if (m_durationMs <= 0 || !m_progressSlider->isEnabled()) {
            return false;
        }

        const auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (!mouseEvent) {
            return true;
        }

        const int width = m_progressSlider->width();
        if (width <= 1) {
            return true;
        }

        const int x = qBound(0, mouseEvent->pos().x(), width - 1);
        const double ratio = static_cast<double>(x) / static_cast<double>(width - 1);
        const qint64 targetPos = static_cast<qint64>(ratio * static_cast<double>(m_durationMs));
        const qint64 safeTarget = qBound<qint64>(0, targetPos, m_durationMs);

        m_progressSlider->setSliderPosition(sliderFromPosition(safeTarget));
        return false;
    }

    return QWidget::eventFilter(watched, event);
}

void EmbeddedFfmpegPlayer::keyPressEvent(QKeyEvent *event)
{
    if (!event) {
        return;
    }

    switch (event->key()) {
    case Qt::Key_Space:
    case Qt::Key_K:
        playPause();
        break;
    case Qt::Key_Left:
    case Qt::Key_J:
        seekBackward();
        break;
    case Qt::Key_Right:
    case Qt::Key_L:
        seekForward();
        break;
    default:
        QWidget::keyPressEvent(event);
        return;
    }

    event->accept();
}

void EmbeddedFfmpegPlayer::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitCode);

    QProcess *proc = qobject_cast<QProcess *>(sender());
    if (!proc || proc != m_ffmpegProcess) {
        return;
    }

    const bool stoppedByUser = m_userStopping;
    m_isPlaying = false;
    m_progressTimer->stop();
    m_playPauseButton->setText(QStringLiteral("▶"));

    if (stoppedByUser) {
        updateProgressUi();
        return;
    }

    if (exitStatus == QProcess::CrashExit) {
        const QString err = QString::fromLocal8Bit(proc->readAllStandardError()).trimmed();
        emit playbackError(err.isEmpty() ? tr("FFmpeg 解码进程异常退出") : err);
        emit statusMessage(tr("播放异常中断"));
        return;
    }

    if (m_durationMs > 0 && m_positionMs >= m_durationMs - 400) {
        m_positionMs = m_durationMs;
        emit statusMessage(tr("播放完成"));
    }
    updateProgressUi();
}

void EmbeddedFfmpegPlayer::onProgressTick()
{
    if (!m_isPlaying) {
        return;
    }

    qint64 estimated = m_startPositionMs + m_playbackClock.elapsed();
    if (m_srcFps > 0.1) {
        const qint64 frameBased = m_startPositionMs + static_cast<qint64>((m_decodedFrameCount * 1000.0) / m_srcFps);
        estimated = qMax(estimated, frameBased);
    }

    m_positionMs = estimated;
    if (m_durationMs > 0) {
        m_positionMs = qMin(m_positionMs, m_durationMs);
    }
    updateProgressUi();
}

void EmbeddedFfmpegPlayer::onSliderPressed()
{
    m_wasPlayingBeforeScrub = m_isPlaying;
    if (m_isPlaying) {
        m_positionMs = m_startPositionMs + m_playbackClock.elapsed();
        stopPlayback();
        m_isPlaying = false;
        m_playPauseButton->setText(QStringLiteral("▶"));
    }
}

void EmbeddedFfmpegPlayer::onSliderReleased()
{
    const bool resume = m_wasPlayingBeforeScrub;
    m_wasPlayingBeforeScrub = false;

    if (m_durationMs <= 0) {
        return;
    }

    m_positionMs = positionFromSlider(m_progressSlider->value());
    updateProgressUi();

    if (resume) {
        beginPlaybackFromCurrentPosition();
    }
}

void EmbeddedFfmpegPlayer::onDecoderOutputReady()
{
    if (m_frameBytes <= 0 || !m_ffmpegProcess || !m_videoSurface || m_outputWidth <= 0 || m_outputHeight <= 0) {
        return;
    }

    QProcess *proc = qobject_cast<QProcess *>(sender());
    if (proc != m_ffmpegProcess) {
        return;
    }

    if (!proc->isOpen()) {
        return;
    }

    m_frameBuffer.append(proc->readAllStandardOutput());

    while (m_frameBuffer.size() >= m_frameBytes) {
        const QByteArray frame = m_frameBuffer.left(m_frameBytes);
        m_frameBuffer.remove(0, m_frameBytes);

        QImage image(reinterpret_cast<const uchar *>(frame.constData()),
                     m_outputWidth,
                     m_outputHeight,
                     m_outputWidth * 3,
                     QImage::Format_RGB888);

        m_videoSurface->setPixmap(QPixmap::fromImage(image.copy()));
        ++m_decodedFrameCount;
    }
}

void EmbeddedFfmpegPlayer::onDecoderErrorReady()
{
    if (!m_ffmpegProcess) {
        return;
    }

    QProcess *proc = qobject_cast<QProcess *>(sender());
    if (proc != m_ffmpegProcess) {
        return;
    }

    if (proc->isOpen()) {
        proc->readAllStandardError();
    }
}

void EmbeddedFfmpegPlayer::onAudioOutputReady()
{
    if (!m_audioProcess) {
        return;
    }

    QProcess *proc = qobject_cast<QProcess *>(sender());
    if (proc != m_audioProcess) {
        return;
    }

    if (proc->isOpen()) {
        m_audioBuffer.append(proc->readAllStandardOutput());
        flushAudioBuffer();
    }
}

void EmbeddedFfmpegPlayer::onAudioErrorReady()
{
    if (!m_audioProcess) {
        return;
    }

    QProcess *proc = qobject_cast<QProcess *>(sender());
    if (proc != m_audioProcess) {
        return;
    }

    if (proc->isOpen()) {
        proc->readAllStandardError();
    }
}

void EmbeddedFfmpegPlayer::onVolumeChanged(int value)
{
    m_volumePercent = qBound(0, value, 100);

    if (m_audioOutput) {
        m_audioOutput->setVolume(static_cast<qreal>(m_volumePercent) / 100.0);
    }

    if (!m_volumeIcon) {
        return;
    }

    if (m_volumePercent == 0) {
        m_volumeIcon->setText(QStringLiteral("🔇"));
    } else if (m_volumePercent <= 35) {
        m_volumeIcon->setText(QStringLiteral("🔈"));
    } else if (m_volumePercent <= 70) {
        m_volumeIcon->setText(QStringLiteral("🔉"));
    } else {
        m_volumeIcon->setText(QStringLiteral("🔊"));
    }
}

void EmbeddedFfmpegPlayer::onAudioProcessFinished(int, QProcess::ExitStatus)
{
    flushAudioBuffer();
}

void EmbeddedFfmpegPlayer::flushAudioBuffer()
{
    if (!m_audioSinkDevice || m_audioBuffer.isEmpty()) {
        return;
    }

    while (!m_audioBuffer.isEmpty()) {
        const qint64 written = m_audioSinkDevice->write(m_audioBuffer.constData(), m_audioBuffer.size());
        if (written <= 0) {
            break;
        }
        m_audioBuffer.remove(0, static_cast<int>(written));
    }
}

QString EmbeddedFfmpegPlayer::resolveFfmpegPath() const
{
    if (!m_cachedFfmpegPath.isEmpty() && QFileInfo::exists(m_cachedFfmpegPath)) {
        return m_cachedFfmpegPath;
    }

    const QString baseDir = QDir::currentPath() + "/deps";
    const QStringList candidates = {
        baseDir + "/ffmpeg.exe",
        baseDir + "/ffmpeg/ffmpeg.exe",
        baseDir + "/ffmpeg/bin/ffmpeg.exe",
        baseDir + "/ffmpeg-master-latest-win64-gpl/bin/ffmpeg.exe"
    };

    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }

    QDirIterator it(baseDir, QStringList() << "ffmpeg.exe", QDir::Files, QDirIterator::Subdirectories);
    if (it.hasNext()) {
        return it.next();
    }

    return QString();
}

QString EmbeddedFfmpegPlayer::resolveFfprobePath() const
{
    const QString ffmpegPath = resolveFfmpegPath();
    if (ffmpegPath.isEmpty()) {
        return QString();
    }

    const QFileInfo info(ffmpegPath);
    const QString direct = info.absoluteDir().filePath("ffprobe.exe");
    if (QFileInfo::exists(direct)) {
        return direct;
    }

    QDirIterator it(QDir::currentPath() + "/deps", QStringList() << "ffprobe.exe", QDir::Files, QDirIterator::Subdirectories);
    if (it.hasNext()) {
        return it.next();
    }

    return QString();
}

bool EmbeddedFfmpegPlayer::startPlaybackAt(qint64 positionMs)
{
    if (m_currentFilePath.isEmpty()) {
        return false;
    }

    const QString ffmpegPath = resolveFfmpegPath();
    if (ffmpegPath.isEmpty()) {
        emit ffmpegMissing();
        return false;
    }

    stopPlayback();

    m_outputWidth = qMax(320, m_videoSurface->width());
    m_outputHeight = qMax(180, m_videoSurface->height());
    m_frameBytes = m_outputWidth * m_outputHeight * 3;
    m_decodedFrameCount = 0;
    clearFrameBuffer();

    const qint64 safePositionMs = qMax<qint64>(0, positionMs);
    const QString vf = QString("scale=%1:%2:force_original_aspect_ratio=decrease,pad=%1:%2:(ow-iw)/2:(oh-ih)/2:black")
                           .arg(m_outputWidth)
                           .arg(m_outputHeight);

    QStringList args;
    args << "-hide_banner" << "-loglevel" << "error";
    args << "-ss" << QString::number(static_cast<double>(safePositionMs) / 1000.0, 'f', 3);
    args << "-re" << "-i" << m_currentFilePath;
    args << "-an" << "-sn";
    args << "-vf" << vf;
    args << "-pix_fmt" << "rgb24";
    args << "-f" << "rawvideo" << "-";

    m_ffmpegProcess->setProgram(ffmpegPath);
    m_ffmpegProcess->setArguments(args);
    m_ffmpegProcess->setWorkingDirectory(QFileInfo(ffmpegPath).absolutePath());
    m_ffmpegProcess->setProcessChannelMode(QProcess::SeparateChannels);
    m_ffmpegProcess->blockSignals(false);
    m_ffmpegProcess->start();

    if (!m_ffmpegProcess->waitForStarted(3000)) {
        return false;
    }

    m_positionMs = safePositionMs;
    m_startPositionMs = m_positionMs;
    startAudioPlaybackAt(safePositionMs);
    updateProgressUi();
    return true;
}

bool EmbeddedFfmpegPlayer::startAudioPlaybackAt(qint64 positionMs)
{
    if (!m_audioProcess || !m_audioOutput || m_currentFilePath.isEmpty()) {
        return false;
    }

    const QString ffmpegPath = resolveFfmpegPath();
    if (ffmpegPath.isEmpty()) {
        return false;
    }

    if (m_audioProcess->state() != QProcess::NotRunning) {
        m_audioProcess->terminate();
        if (!m_audioProcess->waitForFinished(500)) {
            m_audioProcess->kill();
            m_audioProcess->waitForFinished(1000);
        }
    }

    m_audioOutput->stop();
    m_audioSinkDevice = m_audioOutput->start();
    m_audioBuffer.clear();

    const qint64 safePositionMs = qMax<qint64>(0, positionMs);
    QStringList args;
    args << "-hide_banner" << "-loglevel" << "error";
    args << "-ss" << QString::number(static_cast<double>(safePositionMs) / 1000.0, 'f', 3);
    args << "-i" << m_currentFilePath;
    args << "-vn" << "-sn";
    args << "-ac" << "2" << "-ar" << "48000";
    args << "-f" << "s16le" << "-";

    m_audioProcess->setProgram(ffmpegPath);
    m_audioProcess->setArguments(args);
    m_audioProcess->setWorkingDirectory(QFileInfo(ffmpegPath).absolutePath());
    m_audioProcess->setProcessChannelMode(QProcess::SeparateChannels);
    m_audioProcess->blockSignals(false);
    m_audioProcess->start();

    return m_audioProcess->waitForStarted(3000);
}

void EmbeddedFfmpegPlayer::seekTo(qint64 positionMs)
{
    if (m_currentFilePath.isEmpty()) {
        return;
    }

    qint64 target = qMax<qint64>(0, positionMs);
    if (m_durationMs > 0) {
        target = qMin(target, m_durationMs);
    }

    const bool resume = m_isPlaying;
    if (m_isPlaying) {
        m_positionMs = m_startPositionMs + m_playbackClock.elapsed();
        stopPlayback();
        m_isPlaying = false;
    }

    m_positionMs = target;
    updateProgressUi();

    if (resume && !m_currentFilePath.isEmpty()) {
        beginPlaybackFromCurrentPosition();
    }
}

bool EmbeddedFfmpegPlayer::beginPlaybackFromCurrentPosition()
{
    if (!startPlaybackAt(m_positionMs)) {
        return false;
    }

    m_isPlaying = true;
    m_playPauseButton->setText(QStringLiteral("⏸"));
    m_progressTimer->start();
    m_playbackClock.restart();
    m_startPositionMs = m_positionMs;
    return true;
}

void EmbeddedFfmpegPlayer::refreshVideoMeta()
{
    m_durationMs = 0;
    m_srcVideoWidth = 0;
    m_srcVideoHeight = 0;
    m_srcFps = 25.0;

    const QString ffprobePath = resolveFfprobePath();
    if (ffprobePath.isEmpty()) {
        return;
    }

    QProcess probe;
    QStringList args;
    args << "-v" << "error"
         << "-select_streams" << "v:0"
         << "-show_entries" << "stream=width,height,avg_frame_rate:format=duration"
         << "-of" << "default=noprint_wrappers=1"
         << m_currentFilePath;

    probe.start(ffprobePath, args);
    if (!probe.waitForFinished(4000)) {
        return;
    }

    const QString output = QString::fromLocal8Bit(probe.readAllStandardOutput());

    QRegularExpression widthRe("width=(\\d+)");
    QRegularExpression heightRe("height=(\\d+)");
    QRegularExpression fpsRe("avg_frame_rate=(\\d+)/(\\d+)");
    QRegularExpression durationRe("duration=([0-9]+(?:\\.[0-9]+)?)");

    const auto widthMatch = widthRe.match(output);
    const auto heightMatch = heightRe.match(output);
    const auto fpsMatch = fpsRe.match(output);
    const auto durationMatch = durationRe.match(output);

    m_srcVideoWidth = widthMatch.hasMatch() ? widthMatch.captured(1).toInt() : 0;
    m_srcVideoHeight = heightMatch.hasMatch() ? heightMatch.captured(1).toInt() : 0;

    if (fpsMatch.hasMatch()) {
        const double num = fpsMatch.captured(1).toDouble();
        const double den = fpsMatch.captured(2).toDouble();
        if (den > 0.0) {
            const double fps = num / den;
            if (fps > 1.0 && fps < 240.0) {
                m_srcFps = fps;
            }
        }
    }

    if (durationMatch.hasMatch()) {
        bool ok = false;
        const double seconds = durationMatch.captured(1).toDouble(&ok);
        if (ok && seconds > 0) {
            m_durationMs = static_cast<qint64>(seconds * 1000.0);
        }
    }
}

void EmbeddedFfmpegPlayer::updateProgressUi()
{
    if (m_durationMs > 0) {
        m_progressSlider->setEnabled(true);
        m_progressSlider->setRange(0, kSliderMax);
        m_progressSlider->setValue(sliderFromPosition(m_positionMs));
    } else {
        m_progressSlider->setEnabled(false);
        m_progressSlider->setRange(0, kSliderMax);
        m_progressSlider->setValue(0);
    }

    const QString left = formatTime(m_positionMs);
    const QString right = formatTime(m_durationMs);
    m_timeLabel->setText(QString("%1 / %2").arg(left, right));
}

QString EmbeddedFfmpegPlayer::formatTime(qint64 ms) const
{
    const qint64 totalSec = qMax<qint64>(0, ms / 1000);
    const qint64 hours = totalSec / 3600;
    const qint64 minutes = (totalSec % 3600) / 60;
    const qint64 seconds = totalSec % 60;

    if (hours > 0) {
        return QString("%1:%2:%3")
            .arg(hours, 2, 10, QLatin1Char('0'))
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'));
    }

    return QString("%1:%2")
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

bool EmbeddedFfmpegPlayer::isVideoFile(const QString &filePath) const
{
    const QFileInfo info(filePath);
    if (!info.exists() || !info.isFile()) {
        return false;
    }

    static const QStringList supportedExtensions = {
        QStringLiteral("mp4"),
        QStringLiteral("mkv"),
        QStringLiteral("avi"),
        QStringLiteral("mov"),
        QStringLiteral("wmv"),
        QStringLiteral("flv"),
        QStringLiteral("webm"),
        QStringLiteral("m4v")
    };
    return supportedExtensions.contains(info.suffix().toLower());
}

void EmbeddedFfmpegPlayer::clearFrameBuffer()
{
    m_frameBuffer.clear();
}

int EmbeddedFfmpegPlayer::sliderFromPosition(qint64 positionMs) const
{
    if (m_durationMs <= 0) {
        return 0;
    }

    const double ratio = static_cast<double>(qBound<qint64>(0, positionMs, m_durationMs)) / static_cast<double>(m_durationMs);
    return qBound(0, static_cast<int>(ratio * kSliderMax), kSliderMax);
}

qint64 EmbeddedFfmpegPlayer::positionFromSlider(int sliderValue) const
{
    if (m_durationMs <= 0) {
        return 0;
    }

    const int bounded = qBound(0, sliderValue, kSliderMax);
    const double ratio = static_cast<double>(bounded) / static_cast<double>(kSliderMax);
    return static_cast<qint64>(ratio * static_cast<double>(m_durationMs));
}

void EmbeddedFfmpegPlayer::setupDecoderProcess()
{
    if (m_ffmpegProcess) {
        return;
    }

    m_ffmpegProcess = new QProcess(this);
    connect(m_ffmpegProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &EmbeddedFfmpegPlayer::onProcessFinished);
    connect(m_ffmpegProcess, &QProcess::readyReadStandardOutput,
            this, &EmbeddedFfmpegPlayer::onDecoderOutputReady);
    connect(m_ffmpegProcess, &QProcess::readyReadStandardError,
            this, &EmbeddedFfmpegPlayer::onDecoderErrorReady);
}

void EmbeddedFfmpegPlayer::setupAudioProcess()
{
    if (m_audioProcess) {
        return;
    }

    m_audioProcess = new QProcess(this);
    connect(m_audioProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &EmbeddedFfmpegPlayer::onAudioProcessFinished);
    connect(m_audioProcess, &QProcess::readyReadStandardOutput,
            this, &EmbeddedFfmpegPlayer::onAudioOutputReady);
    connect(m_audioProcess, &QProcess::readyReadStandardError,
            this, &EmbeddedFfmpegPlayer::onAudioErrorReady);
}

void EmbeddedFfmpegPlayer::destroyDecoderProcess()
{
    if (!m_ffmpegProcess) {
        return;
    }

    if (m_ffmpegProcess->state() != QProcess::NotRunning) {
        m_ffmpegProcess->terminate();
        if (!m_ffmpegProcess->waitForFinished(500)) {
            m_ffmpegProcess->kill();
            m_ffmpegProcess->waitForFinished(1000);
        }
    }

    disconnect(m_ffmpegProcess, nullptr, this, nullptr);
    delete m_ffmpegProcess;
    m_ffmpegProcess = nullptr;
}

void EmbeddedFfmpegPlayer::destroyAudioProcess()
{
    if (!m_audioProcess) {
        return;
    }

    if (m_audioProcess->state() != QProcess::NotRunning) {
        m_audioProcess->terminate();
        if (!m_audioProcess->waitForFinished(500)) {
            m_audioProcess->kill();
            m_audioProcess->waitForFinished(1000);
        }
    }

    disconnect(m_audioProcess, nullptr, this, nullptr);
    delete m_audioProcess;
    m_audioProcess = nullptr;
}
