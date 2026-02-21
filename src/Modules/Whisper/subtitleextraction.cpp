#include "subtitleextraction.h"
#include "ui_subtitleextraction.h"
#include "whispersegmentmerger.h"
#include "whispercommandbuilder.h"

#include <QDesktopServices>
#include <QDateTime>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileInfoList>
#include <QMessageBox>
#include <QProcess>
#include <QRegularExpression>
#include <QShowEvent>
#include <QScrollBar>
#include <QTimer>
#include <QTextStream>
#include <QToolButton>
#include <QTransform>
#include <QUrl>
#include <QtMath>
#include <QElapsedTimer>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QThreadPool>
#include <QRunnable>

#include "../../Core/dependencymanager.h"

// 内部转录 Worker 类（用于线程池）
class TranscribeWorker : public QRunnable
{
public:
    struct SegmentInfo {
        int index;
        double startSeconds;
        double duration;
        QString audioPath;
        QString outputBase;
        QString srtPath;
        QString rangeLabel;
    };

    TranscribeWorker(SubtitleExtraction *parent, const SegmentInfo &seg, const QString &whisperPath,
                                     const QString &modelPath, const QString &languageCode, bool useGpu, int whisperThreadCount, int totalSegments,
                     QMutex *resultLock, QVector<bool> *results)
        : m_parent(parent), m_seg(seg), m_whisperPath(whisperPath), m_modelPath(modelPath),
                        m_languageCode(languageCode), m_useGpu(useGpu), m_whisperThreadCount(whisperThreadCount), m_totalSegments(totalSegments),
          m_resultLock(resultLock), m_results(results)
    {
    }

    void run() override
    {
        if (!m_parent) return;
        const bool result = m_parent->transcribeSegment(m_whisperPath, m_modelPath, m_seg.audioPath,
                                                       m_seg.outputBase, m_languageCode, m_useGpu, m_whisperThreadCount, m_seg.index,
                                                       m_totalSegments, m_seg.duration);
        {
            QMutexLocker lock(m_resultLock);
            (*m_results)[m_seg.index] = result;
        }
    }

private:
    SubtitleExtraction *m_parent;
    SegmentInfo m_seg;
    QString m_whisperPath;
    QString m_modelPath;
    QString m_languageCode;
    bool m_useGpu = false;
    int m_whisperThreadCount = 1;
    int m_totalSegments;
    QMutex *m_resultLock;
    QVector<bool> *m_results;
};

SubtitleExtraction::SubtitleExtraction(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SubtitleExtraction)
{
    ui->setupUi(this);

    ensureModelDirectories();
    refreshWhisperModelList();
    setupWorkflowUi();

    m_toolsBaseIcon = ui->toolsCheckButton->icon();
    m_toolsSpinTimer = new QTimer(this);
    m_toolsSpinTimer->setInterval(60);
    connect(m_toolsSpinTimer, &QTimer::timeout, this, &SubtitleExtraction::updateToolsSpinner);

    connect(ui->toolsCheckButton, &QToolButton::clicked, this, []() {
        DependencyManager::instance().checkForUpdates();
    });

    connect(ui->importModelButton, &QPushButton::clicked, this, [this]() {
        openWhisperModelsDirectory();
        refreshWhisperModelList();
    });

    auto &manager = DependencyManager::instance();
    connect(&manager, &DependencyManager::busyChanged, this, &SubtitleExtraction::setToolsLoading);
}

SubtitleExtraction::~SubtitleExtraction()
{
    delete ui;
}

/// @brief 加载视频文件到字幕提取界面
/// @details 将视频路径设置到输入框中，供后续字幕提取使用
void SubtitleExtraction::loadVideoFile(const QString &videoPath)
{
    if (!ui || !ui->inputLineEdit) {
        return;
    }

    if (videoPath.isEmpty() || !QFileInfo::exists(videoPath)) {
        return;
    }

    ui->inputLineEdit->setText(videoPath);
}

void SubtitleExtraction::setToolsLoading(bool loading)
{
    if (m_toolsLoading == loading) {
        return;
    }

    m_toolsLoading = loading;
    ui->toolsCheckButton->setEnabled(!loading);

    if (loading) {
        m_toolsSpinAngle = 0;
        m_toolsSpinTimer->start();
    } else {
        m_toolsSpinTimer->stop();
        ui->toolsCheckButton->setIcon(m_toolsBaseIcon);
    }
}

void SubtitleExtraction::setupWorkflowUi()
{
    // 默认目录：中间目录与最终输出目录分离，便于后续自动清理中间文件。
    const QString defaultTempDir = QDir(QDir::currentPath()).filePath("temp/whisper_work");
    const QString defaultFinalDir = QDir(QDir::currentPath()).filePath("output/whisper");

    QDir().mkpath(defaultTempDir);
    QDir().mkpath(defaultFinalDir);

    if (ui->tempDirLineEdit && ui->tempDirLineEdit->text().isEmpty()) {
        ui->tempDirLineEdit->setText(defaultTempDir);
    }
    if (ui->outputLineEdit && ui->outputLineEdit->text().isEmpty()) {
        ui->outputLineEdit->setText(defaultFinalDir);
    }

    // 日志区保留显示；当前版本仅暂不接入工作流日志输出逻辑。
    initializeLogConsole();

    connect(ui->inputBrowseButton, &QPushButton::clicked, this, [this]() {
        const QString filePath = QFileDialog::getOpenFileName(
            this,
            tr("选择音视频文件"),
            ui->inputLineEdit->text(),
            tr("媒体文件 (*.mp4 *.mkv *.avi *.mov *.mp3 *.wav *.flac);;所有文件 (*.*)")
        );
        if (!filePath.isEmpty()) {
            ui->inputLineEdit->setText(filePath);
        }
    });

    connect(ui->tempDirBrowseButton, &QPushButton::clicked, this, [this]() {
        const QString dirPath = QFileDialog::getExistingDirectory(this, tr("选择中间文件目录"), ui->tempDirLineEdit->text());
        if (!dirPath.isEmpty()) {
            ui->tempDirLineEdit->setText(dirPath);
        }
    });

    connect(ui->outputBrowseButton, &QPushButton::clicked, this, [this]() {
        const QString dirPath = QFileDialog::getExistingDirectory(this, tr("选择最终输出目录"), ui->outputLineEdit->text());
        if (!dirPath.isEmpty()) {
            ui->outputLineEdit->setText(dirPath);
        }
    });

    connect(ui->transcribeButton, &QPushButton::clicked, this, [this]() {
        if (m_isRunning) {
            requestStopWorkflow();
            return;
        }
        startTranscriptionWorkflow();
    });
}

void SubtitleExtraction::updateToolsSpinner()
{
    const QSize iconSize = ui->toolsCheckButton->iconSize();
    QPixmap pixmap = m_toolsBaseIcon.pixmap(iconSize);
    QTransform transform;
    transform.rotate(m_toolsSpinAngle);
    ui->toolsCheckButton->setIcon(QIcon(pixmap.transformed(transform, Qt::SmoothTransformation)));
    m_toolsSpinAngle = (m_toolsSpinAngle + 30) % 360;
}

void SubtitleExtraction::updateRunningStateUi(bool running)
{
    m_isRunning = running;

    if (ui->transcribeButton) {
        ui->transcribeButton->setText(running ? tr("停止") : tr("开始转写"));
    }
    if (ui->inputBrowseButton) {
        ui->inputBrowseButton->setEnabled(!running);
    }
    if (ui->tempDirBrowseButton) {
        ui->tempDirBrowseButton->setEnabled(!running);
    }
    if (ui->outputBrowseButton) {
        ui->outputBrowseButton->setEnabled(!running);
    }
    if (ui->importModelButton) {
        ui->importModelButton->setEnabled(!running);
    }
    if (ui->modelComboBox) {
        ui->modelComboBox->setEnabled(!running);
    }
}

QString SubtitleExtraction::whisperModelsDirPath() const
{
    return QDir(QDir::currentPath()).filePath("models/whisper");
}

void SubtitleExtraction::ensureModelDirectories()
{
    QDir runtimeDir(QDir::currentPath());
    runtimeDir.mkpath("models/whisper");
    runtimeDir.mkpath("models/LLM");
}

void SubtitleExtraction::refreshWhisperModelList()
{
    if (!ui || !ui->modelComboBox) {
        return;
    }

    const QString modelDirPath = whisperModelsDirPath();
    QDir modelDir(modelDirPath);
    if (!modelDir.exists()) {
        modelDir.mkpath(".");
    }

    const QString currentSelection = ui->modelComboBox->currentText();
    ui->modelComboBox->clear();

    const QFileInfoList entries = modelDir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &entry : entries) {
        ui->modelComboBox->addItem(entry.fileName());
    }

    if (!currentSelection.isEmpty()) {
        const int index = ui->modelComboBox->findText(currentSelection);
        if (index >= 0) {
            ui->modelComboBox->setCurrentIndex(index);
        }
    }
}

void SubtitleExtraction::openWhisperModelsDirectory()
{
    ensureModelDirectories();
    QDesktopServices::openUrl(QUrl::fromLocalFile(whisperModelsDirPath()));
}

void SubtitleExtraction::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    refreshWhisperModelList();
}

QString SubtitleExtraction::resolveExecutableInDeps(const QStringList &candidateNames) const
{
    const QString depsDir = QDir(QDir::currentPath()).filePath("deps");

    for (const QString &name : candidateNames) {
        const QString direct = QDir(depsDir).filePath(name);
        if (QFileInfo::exists(direct)) {
            return direct;
        }
    }

    QDirIterator it(depsDir, candidateNames, QDir::Files, QDirIterator::Subdirectories);
    if (it.hasNext()) {
        return it.next();
    }

    return QString();
}

QString SubtitleExtraction::resolveFfmpegPath() const
{
    return resolveExecutableInDeps(QStringList() << "ffmpeg.exe");
}

QString SubtitleExtraction::resolveWhisperPath() const
{
    return resolveExecutableInDeps(QStringList() << "whisper.exe" << "whisper-cli.exe");
}

QString SubtitleExtraction::selectedModelPath() const
{
    if (!ui || !ui->modelComboBox) {
        return QString();
    }

    const QString modelName = ui->modelComboBox->currentText().trimmed();
    if (modelName.isEmpty()) {
        return QString();
    }

    const QString candidatePath = QDir(whisperModelsDirPath()).filePath(modelName);
    QFileInfo info(candidatePath);
    if (!info.exists()) {
        return QString();
    }

    // 允许直接选择模型文件；若为目录，则优先取目录中的 .bin 模型。
    if (info.isFile()) {
        return info.absoluteFilePath();
    }

    if (info.isDir()) {
        QDir modelDir(info.absoluteFilePath());
        const QFileInfoList bins = modelDir.entryInfoList(QStringList() << "*.bin", QDir::Files, QDir::Name);
        if (!bins.isEmpty()) {
            return bins.first().absoluteFilePath();
        }
    }

    return QString();
}

void SubtitleExtraction::startTranscriptionWorkflow()
{
    if (!ui) {
        return;
    }

    const QString inputPath = ui->inputLineEdit ? ui->inputLineEdit->text().trimmed() : QString();
    if (inputPath.isEmpty() || !QFileInfo::exists(inputPath)) {
        QMessageBox::warning(this, tr("输入文件无效"), tr("请选择一个可访问的音频或视频文件后再开始识别。"));
        return;
    }

    const QString ffmpegPath = resolveFfmpegPath();
    const QString whisperPath = resolveWhisperPath();
    const QString modelPath = selectedModelPath();
    if (ffmpegPath.isEmpty()) {
        QMessageBox::warning(this, tr("依赖缺失"), tr("未检测到 ffmpeg.exe，请先在 deps 目录准备 FFmpeg。"));
        return;
    }
    if (whisperPath.isEmpty()) {
        QMessageBox::warning(this, tr("依赖缺失"), tr("未检测到 whisper 可执行文件（whisper.exe 或 whisper-cli.exe）。"));
        return;
    }
    if (modelPath.isEmpty()) {
        QMessageBox::warning(this, tr("模型不可用"), tr("请选择一个可用的 Whisper 模型文件（.bin）。"));
        return;
    }

    const QString tempRoot = ui->tempDirLineEdit ? ui->tempDirLineEdit->text().trimmed() : QString();
    const QString finalRoot = ui->outputLineEdit ? ui->outputLineEdit->text().trimmed() : QString();
    if (tempRoot.isEmpty() || finalRoot.isEmpty()) {
        QMessageBox::warning(this, tr("目录未设置"), tr("请先设置“中间文件目录”和“最终字幕输出目录”。"));
        return;
    }

    QDir().mkpath(tempRoot);
    QDir().mkpath(finalRoot);

    const QString ffprobePath = resolveExecutableInDeps(QStringList() << "ffprobe.exe");
    if (ffprobePath.isEmpty()) {
        QMessageBox::warning(this, tr("依赖缺失"), tr("未检测到 ffprobe.exe，无法获取媒体时长。"));
        return;
    }

    m_cancelRequested = false;
    m_lastProgressPercent = -1;
    updateRunningStateUi(true);

    // 为本次任务创建唯一中间目录，任务完成后按配置清理。
    const QString jobDirName = QString("job_%1").arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_zzz"));
    const QString jobDirPath = QDir(tempRoot).filePath(jobDirName);
    QDir().mkpath(jobDirPath);

    const QFileInfo inputInfo(inputPath);
    const QString outputFormatText = ui->outputFormatComboBox ? ui->outputFormatComboBox->currentText() : QStringLiteral("SRT");
    const QString outputExtension = WhisperCommandBuilder::outputFileExtensionFromUiText(outputFormatText);
    const QString outputFilePath = QDir(finalRoot).filePath(inputInfo.completeBaseName() + "_whisper." + outputExtension);

    m_workflowLogHistory.clear();
    m_activeSegmentLogLines.clear();
    if (ui->logTextEdit) {
        ui->logTextEdit->clear();
    }
    appendWorkflowLog(tr("任务开始：%1").arg(inputInfo.fileName()));
    appendWorkflowLog(tr("识别模型：%1").arg(QFileInfo(modelPath).fileName()));
    appendWorkflowLog(tr("输出格式：%1").arg(outputFormatText));
    appendWorkflowLog(tr("GPU 加速：%1").arg(ui->gpuCheckBox && ui->gpuCheckBox->isChecked() ? tr("已开启") : tr("未开启")));
    emit progressChanged(0);

    double durationSeconds = 0.0;
    bool allSuccess = probeDurationSeconds(ffprobePath, inputPath, durationSeconds) && durationSeconds > 0.0;
    QString failureMessage;
    if (!allSuccess) {
        failureMessage = tr("无法读取媒体时长。");
    }

    QStringList segmentSrtFiles;
    const int segmentSeconds = 5 * 60;
    const int segmentCount = allSuccess ? qCeil(durationSeconds / static_cast<double>(segmentSeconds)) : 0;
    if (allSuccess) {
        appendWorkflowLog(tr("分段策略：每 5 分钟一段，共 %1 段").arg(segmentCount));
    }

    // 初始化分段进度跟踪
    {
        QMutexLocker lock(&m_progressLock);
        m_segmentProgress.clear();
        for (int i = 0; i < segmentCount; ++i) {
            m_segmentProgress[i] = -1;
        }
    }

    const QString languageCode = WhisperCommandBuilder::languageCodeFromUiText(ui->languageComboBox ? ui->languageComboBox->currentText() : QString());
    
    // 第一阶段：提取所有分段音频
    typedef TranscribeWorker::SegmentInfo SegmentInfo;
    QVector<SegmentInfo> segments;

    for (int index = 0; allSuccess && index < segmentCount; ++index) {
        if (m_cancelRequested) {
            allSuccess = false;
            failureMessage = tr("任务已停止。");
            break;
        }

        const double startSeconds = index * segmentSeconds;
        const double currentDuration = qMin(static_cast<double>(segmentSeconds), durationSeconds - startSeconds);
        const QString segmentPrefix = QString("segment_%1").arg(index, 4, 10, QLatin1Char('0'));
        const QString segmentAudioPath = QDir(jobDirPath).filePath(segmentPrefix + ".wav");
        const QString segmentOutputBase = QDir(jobDirPath).filePath(segmentPrefix);
        const QString segmentSrtPath = segmentOutputBase + ".srt";
        const QString rangeText = segmentRangeLabel(startSeconds, currentDuration);

        appendWorkflowLog(tr("第 %1/%2 段（%3）开始提取音频").arg(index + 1).arg(segmentCount).arg(rangeText));

        if (!extractSegmentAudio(ffmpegPath, inputPath, startSeconds, currentDuration, segmentAudioPath)) {
            allSuccess = false;
            if (!m_cancelRequested) {
                failureMessage = tr("音频分段失败，请检查输入文件或 FFmpeg 是否可用。");
                appendWorkflowLog(tr("第 %1/%2 段分段失败（%3）").arg(index + 1).arg(segmentCount).arg(rangeText));
            }
            break;
        }

        segments.append({ index, startSeconds, currentDuration, segmentAudioPath, segmentOutputBase, segmentSrtPath, rangeText });
    }

    // 第二阶段：并行转录所有分段
    if (allSuccess && !segments.isEmpty()) {
        appendWorkflowLog(tr("开始并行识别 %1 个音频分段...").arg(segments.size()));
        const bool useGpu = ui->gpuCheckBox && ui->gpuCheckBox->isChecked();
        
        QThreadPool *pool = QThreadPool::globalInstance();
        const int cpuThreads = qMax(2, QThread::idealThreadCount());
        const int maxWorkers = qMax(1, qMin(4, cpuThreads / 4));
        const int reservedForUi = 2;
        const int availableForWhisper = qMax(1, cpuThreads - reservedForUi);
        const int whisperThreadCount = qMax(1, availableForWhisper / maxWorkers);
        pool->setMaxThreadCount(maxWorkers);
        appendWorkflowLog(tr("并行策略：%1 个 worker，每个 whisper %2 线程（CPU 总线程 %3）")
                          .arg(maxWorkers)
                          .arg(whisperThreadCount)
                          .arg(cpuThreads));

        QVector<bool> segmentResults(segments.size(), false);
        QMutex resultLock;

        for (int i = 0; i < segments.size(); ++i) {
            if (m_cancelRequested) {
                allSuccess = false;
                failureMessage = tr("任务已停止。");
                break;
            }

            const SegmentInfo seg = segments[i];
            const QString whisperPathLocal = resolveWhisperPath();
            const QString modelPathLocal = selectedModelPath();

            TranscribeWorker *worker = new TranscribeWorker(this, seg, whisperPathLocal, modelPathLocal,
                                                            languageCode, useGpu, whisperThreadCount, segments.size(), &resultLock, &segmentResults);
            pool->start(worker);
        }

        while (pool->activeThreadCount() > 0) {
            if (m_cancelRequested) {
                pool->clear();
            }
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(15);
        }

        for (int i = 0; i < segments.size(); ++i) {
            if (m_cancelRequested) {
                allSuccess = false;
                failureMessage = tr("任务已停止。");
                break;
            }

            if (!segmentResults[i]) {
                allSuccess = false;
                if (!m_cancelRequested) {
                    failureMessage = tr("Whisper 识别失败，请检查模型文件和 whisper 版本。");
                    appendWorkflowLog(tr("第 %1 段识别失败").arg(segments[i].index + 1));
                }
                break;
            }

            if (!QFileInfo::exists(segments[i].srtPath)) {
                allSuccess = false;
                failureMessage = tr("Whisper 未产出分段 SRT 文件。");
                appendWorkflowLog(tr("第 %1 段未产出字幕文件").arg(segments[i].index + 1));
                break;
            }

            segmentSrtFiles << segments[i].srtPath;
            appendWorkflowLog(tr("第 %1/%2 段识别完成（%3）").arg(segments[i].index + 1).arg(segments.size()).arg(segments[i].rangeLabel));
            QCoreApplication::processEvents();
        }
    }

    if (allSuccess) {
        appendWorkflowLog(tr("开始合并片段字幕..."));
        
        // 映射输出格式
        WhisperSegmentMerger::OutputFormat mergerFormat = WhisperSegmentMerger::Format_SRT;
        if (outputFormatText == QStringLiteral("TXT")) {
            mergerFormat = WhisperSegmentMerger::Format_TXT;
        } else if (outputFormatText == QStringLiteral("TXT（带时间）")) {
            mergerFormat = WhisperSegmentMerger::Format_TXT_Timestamped;
        } else if (outputFormatText == QStringLiteral("WebVTT")) {
            mergerFormat = WhisperSegmentMerger::Format_WebVTT;
        }
        
        const QString finalOutputContent = WhisperSegmentMerger::mergeSegmentSrtFiles(
            segmentSrtFiles, static_cast<double>(segmentSeconds), mergerFormat);
        
        if (finalOutputContent.isEmpty()) {
            allSuccess = false;
            failureMessage = tr("合并字幕失败。");
            appendWorkflowLog(tr("合并失败：无法生成合并内容"));
        } else {
            QFile outputFile(outputFilePath);
            if (!outputFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                allSuccess = false;
                failureMessage = tr("无法写入最终输出文件。请检查输出目录权限。");
                appendWorkflowLog(tr("输出失败：无法写入最终文件"));
            } else {
                QTextStream out(&outputFile);
                out.setCodec("UTF-8");
                out << finalOutputContent;
                outputFile.close();
                appendWorkflowLog(tr("合并进度：100%"));
            }
        }
    }

    const bool cleanTemp = ui->debugConsoleCheckBox ? ui->debugConsoleCheckBox->isChecked() : true;
    if (cleanTemp) {
        QDir(jobDirPath).removeRecursively();
        appendWorkflowLog(tr("已清理中间文件"));
    }

    m_activeSegmentLogLines.clear();
    renderWorkflowLogConsole();

    updateRunningStateUi(false);

    if (!allSuccess) {
        appendWorkflowLog(tr("任务结束：%1").arg(failureMessage.isEmpty() ? tr("任务已停止或执行失败。") : failureMessage));
        QMessageBox::warning(this, tr("识别未完成"), failureMessage.isEmpty() ? tr("任务已停止或执行失败。") : failureMessage);
        return;
    }

    appendWorkflowLog(tr("全部完成，字幕已生成"));
    QMessageBox::information(this, tr("识别完成"), tr("字幕文件已输出到：\n%1").arg(outputFilePath));
}

void SubtitleExtraction::requestStopWorkflow()
{
    // 仅设置停止标记；若外部进程正在运行则尝试终止。
    m_cancelRequested = true;
    appendWorkflowLog(tr("正在停止任务，请稍候..."));
    QProcess *activeProcess = nullptr;
    {
        QMutexLocker locker(&m_processLock);
        activeProcess = m_activeProcess;
    }

    if (activeProcess && activeProcess->state() != QProcess::NotRunning) {
        activeProcess->terminate();
        if (!activeProcess->waitForFinished(800)) {
            activeProcess->kill();
            activeProcess->waitForFinished(1000);
        }
    }
}

bool SubtitleExtraction::runProcessCancelable(const QString &program, const QStringList &arguments, QString *stdErrOutput)
{
    QProcess process;
    QString stdErrTail;
    {
        QMutexLocker locker(&m_processLock);
        m_activeProcess = &process;
    }
    process.setProgram(program);
    process.setArguments(arguments);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();

    if (!process.waitForStarted(5000)) {
        if (stdErrOutput) {
            *stdErrOutput = process.errorString();
        }
        {
            QMutexLocker locker(&m_processLock);
            if (m_activeProcess == &process) {
                m_activeProcess = nullptr;
            }
        }
        return false;
    }

    while (process.state() != QProcess::NotRunning) {
        if (m_cancelRequested) {
            process.terminate();
            if (!process.waitForFinished(800)) {
                process.kill();
                process.waitForFinished(1200);
            }
            {
                QMutexLocker locker(&m_processLock);
                if (m_activeProcess == &process) {
                    m_activeProcess = nullptr;
                }
            }
            return false;
        }

        process.waitForFinished(120);
        if (process.bytesAvailable() > 0 || process.bytesToWrite() >= 0) {
            const QString outChunk = QString::fromLocal8Bit(process.readAllStandardOutput());
            Q_UNUSED(outChunk);
            const QString errChunk = QString::fromLocal8Bit(process.readAllStandardError());
            if (!errChunk.isEmpty()) {
                stdErrTail += errChunk;
                if (stdErrTail.size() > 32768) {
                    stdErrTail = stdErrTail.right(32768);
                }
            }
        }
        QCoreApplication::processEvents();
    }

    if (stdErrOutput) {
        stdErrTail += QString::fromLocal8Bit(process.readAllStandardError());
        if (stdErrTail.size() > 32768) {
            stdErrTail = stdErrTail.right(32768);
        }
        *stdErrOutput = stdErrTail;
    }

    {
        QMutexLocker locker(&m_processLock);
        if (m_activeProcess == &process) {
            m_activeProcess = nullptr;
        }
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

bool SubtitleExtraction::probeDurationSeconds(const QString &ffprobePath, const QString &inputPath, double &durationSeconds)
{
    QProcess probe;
    const QStringList args = {
        "-v", "error",
        "-show_entries", "format=duration",
        "-of", "default=noprint_wrappers=1:nokey=1",
        inputPath
    };

    probe.start(ffprobePath, args);
    if (!probe.waitForFinished(5000)) {
        probe.kill();
        probe.waitForFinished(1000);
        return false;
    }

    bool ok = false;
    const QString value = QString::fromLocal8Bit(probe.readAllStandardOutput()).trimmed();
    const double seconds = value.toDouble(&ok);
    if (!ok) {
        return false;
    }

    durationSeconds = seconds;
    return true;
}

bool SubtitleExtraction::extractSegmentAudio(const QString &ffmpegPath,
                                             const QString &inputPath,
                                             double startSeconds,
                                             double durationSeconds,
                                             const QString &segmentAudioPath)
{
    QString stdErr;
    const QStringList args = WhisperCommandBuilder::buildFfmpegExtractArgs(
        inputPath, startSeconds, durationSeconds, segmentAudioPath);

    const bool ok = runProcessCancelable(ffmpegPath, args, &stdErr);
    if (!ok && !stdErr.trimmed().isEmpty()) {
        appendWorkflowLog(tr("FFmpeg 错误：%1").arg(stdErr.trimmed()));
    }
    return ok;
}

bool SubtitleExtraction::transcribeSegment(const QString &whisperPath,
                                           const QString &modelPath,
                                           const QString &segmentAudioPath,
                                           const QString &segmentOutputBasePath,
                                           const QString &languageCode,
                                           bool useGpu,
                                           int whisperThreadCount,
                                           int segmentIndex,
                                           int segmentCount,
                                           double segmentDurationSeconds)
{
    const QStringList args = WhisperCommandBuilder::buildWhisperTranscribeArgs(
        modelPath, segmentAudioPath, segmentOutputBasePath, languageCode,
        useGpu, whisperThreadCount);

    QString stdErr;
    QString stdErrTail;
    QProcess process;
    {
        QMutexLocker locker(&m_processLock);
        m_activeProcess = &process;
    }
    process.setProgram(whisperPath);
    process.setArguments(args);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();

    if (!process.waitForStarted(5000)) {
        stdErr = process.errorString();
        {
            QMutexLocker locker(&m_processLock);
            if (m_activeProcess == &process) {
                m_activeProcess = nullptr;
            }
        }
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    const double safeSegmentSeconds = qMax(1.0, segmentDurationSeconds);
    int lastReportedSegmentProgress = -1;
    qint64 lastTailRefreshMs = -1;
    qint64 lastActivityMs = 0;

    {
        QMutexLocker lock(&m_progressLock);
        m_segmentProgress[segmentIndex] = 0;
    }
    updateSegmentProgressLog(segmentIndex, 0, false);

    while (process.state() != QProcess::NotRunning) {
        if (m_cancelRequested) {
            process.terminate();
            if (!process.waitForFinished(800)) {
                process.kill();
                process.waitForFinished(1200);
            }
            {
                QMutexLocker locker(&m_processLock);
                if (m_activeProcess == &process) {
                    m_activeProcess = nullptr;
                }
            }
            return false;
        }

        process.waitForFinished(200);
        const QString outChunk = QString::fromLocal8Bit(process.readAllStandardOutput());
        if (!outChunk.isEmpty()) {
            lastActivityMs = timer.elapsed();
        }
        const QString errChunk = QString::fromLocal8Bit(process.readAllStandardError());
        if (!errChunk.isEmpty()) {
            stdErrTail += errChunk;
            if (stdErrTail.size() > 32768) {
                stdErrTail = stdErrTail.right(32768);
            }
            lastActivityMs = timer.elapsed();
        }

        const double elapsedSeconds = timer.elapsed() / 1000.0;
        const double segmentRatio = qBound(0.0, elapsedSeconds / safeSegmentSeconds, 1.0);
        int segmentProgress = qBound(0, qFloor(segmentRatio * 100.0), 100);
        if (segmentProgress >= 100) {
            segmentProgress = 99;
        }

        if (segmentProgress != lastReportedSegmentProgress) {
            lastReportedSegmentProgress = segmentProgress;
            lastActivityMs = timer.elapsed();
            int overallPercent = 0;
            QString parallelSummary;
            {
                QMutexLocker lock(&m_progressLock);
                m_segmentProgress[segmentIndex] = segmentProgress;

                int progressSum = 0;
                for (int i = 0; i < segmentCount; ++i) {
                    progressSum += qMax(0, m_segmentProgress.value(i, -1));
                }
                overallPercent = progressSum / qMax(1, segmentCount);
                if (overallPercent != m_lastProgressPercent) {
                    m_lastProgressPercent = overallPercent;
                    emit progressChanged(overallPercent);
                }

                parallelSummary = buildParallelStatusSummaryLocked(segmentCount, overallPercent);
            }
            updateSegmentProgressLog(segmentIndex, segmentProgress, false);
            emit statusMessage(parallelSummary);
        }

        if (segmentProgress == 99) {
            const qint64 elapsedMs = timer.elapsed();
            if (lastTailRefreshMs < 0 || elapsedMs - lastTailRefreshMs >= 1000) {
                lastTailRefreshMs = elapsedMs;
                updateSegmentProgressLog(segmentIndex, 99, false);
            }

            const qint64 finishingTimeoutMs = 120000;
            if (elapsedMs - lastActivityMs > finishingTimeoutMs) {
                stdErrTail += tr("\nWhisper 收尾超时（超过 %1 秒无进度/无输出），已终止该分段进程。")
                                  .arg(finishingTimeoutMs / 1000);
                process.terminate();
                if (!process.waitForFinished(1000)) {
                    process.kill();
                    process.waitForFinished(1200);
                }
                break;
            }
        }

    }

    stdErrTail += QString::fromLocal8Bit(process.readAllStandardError());
    if (stdErrTail.size() > 32768) {
        stdErrTail = stdErrTail.right(32768);
    }
    stdErr = stdErrTail;
    int finalOverallPercent = 100;
    QString finalParallelSummary;
    {
        QMutexLocker lock(&m_progressLock);
        m_segmentProgress[segmentIndex] = 100;

        int progressSum = 0;
        for (int i = 0; i < segmentCount; ++i) {
            progressSum += qMax(0, m_segmentProgress.value(i, -1));
        }
        finalOverallPercent = progressSum / qMax(1, segmentCount);
        finalParallelSummary = buildParallelStatusSummaryLocked(segmentCount, finalOverallPercent);
        if (finalOverallPercent != m_lastProgressPercent) {
            m_lastProgressPercent = finalOverallPercent;
            emit progressChanged(finalOverallPercent);
        }
    }
    updateSegmentProgressLog(segmentIndex, 100, true);
    emit statusMessage(finalParallelSummary);
    const bool ok = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    {
        QMutexLocker locker(&m_processLock);
        if (m_activeProcess == &process) {
            m_activeProcess = nullptr;
        }
    }
    if (!ok && !stdErr.trimmed().isEmpty()) {
        appendWorkflowLog(tr("Whisper 错误：%1").arg(stdErr.trimmed()));
    }
    return ok;
}

bool SubtitleExtraction::parseSrtTimestamp(const QString &text, qint64 &milliseconds)
{
    return WhisperSegmentMerger::parseSrtTimestamp(text, milliseconds);
}

QString SubtitleExtraction::formatSrtTimestamp(qint64 milliseconds)
{
    return WhisperSegmentMerger::formatSrtTimestamp(milliseconds);
}

QString SubtitleExtraction::shiftedSrtContent(const QString &srtContent, qint64 offsetMs)
{
    return WhisperSegmentMerger::shiftedSrtContent(srtContent, offsetMs);
}

QString SubtitleExtraction::languageCodeFromUiText(const QString &uiText)
{
    return WhisperCommandBuilder::languageCodeFromUiText(uiText);
}

QString SubtitleExtraction::outputFileExtensionFromUiText(const QString &uiText)
{
    return WhisperCommandBuilder::outputFileExtensionFromUiText(uiText);
}

QString SubtitleExtraction::srtToPlainText(const QString &srtContent)
{
    return WhisperSegmentMerger::srtToPlainText(srtContent);
}

QString SubtitleExtraction::srtToTimestampedText(const QString &srtContent)
{
    return WhisperSegmentMerger::srtToTimestampedText(srtContent);
}

QString SubtitleExtraction::srtToWebVtt(const QString &srtContent)
{
    return WhisperSegmentMerger::srtToWebVtt(srtContent);
}

QString SubtitleExtraction::segmentRangeLabel(double startSeconds, double durationSeconds)
{
    const int startMin = qFloor(startSeconds / 60.0);
    const int endMin = qCeil((startSeconds + durationSeconds) / 60.0);
    return QString("%1-%2 分钟").arg(startMin).arg(endMin);
}

QString SubtitleExtraction::buildParallelStatusSummaryLocked(int segmentCount, int overallPercent) const
{
    QStringList activeSegments;
    for (int i = 0; i < segmentCount; ++i) {
        const int progress = m_segmentProgress.value(i, -1);
        if (progress >= 0 && progress < 100) {
            activeSegments << tr("第%1段 %2%").arg(i + 1).arg(progress);
        }
    }

    if (activeSegments.isEmpty()) {
        return tr("识别总进度：%1%").arg(overallPercent);
    }

    return tr("进行中：%1 ｜ 总进度：%2%")
        .arg(activeSegments.join(" | "))
        .arg(overallPercent);
}

void SubtitleExtraction::renderWorkflowLogConsole()
{
    if (!ui || !ui->logTextEdit) {
        return;
    }

    QStringList lines = m_workflowLogHistory;
    for (auto it = m_activeSegmentLogLines.constBegin(); it != m_activeSegmentLogLines.constEnd(); ++it) {
        lines << it.value();
    }

    ui->logTextEdit->setPlainText(lines.join("\n"));
    if (ui->logTextEdit->verticalScrollBar()) {
        ui->logTextEdit->verticalScrollBar()->setValue(ui->logTextEdit->verticalScrollBar()->maximum());
    }
}

void SubtitleExtraction::updateSegmentProgressLog(int segmentIndex, int progressPercent, bool finished)
{
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this, segmentIndex, progressPercent, finished]() {
            updateSegmentProgressLog(segmentIndex, progressPercent, finished);
        }, Qt::QueuedConnection);
        return;
    }

    const QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    if (finished) {
        m_activeSegmentLogLines.remove(segmentIndex);
        m_workflowLogHistory << QString("[%1] %2")
                                .arg(timestamp,
                                     tr("第 %1 段识别完成：100%").arg(segmentIndex + 1));
    } else {
        m_activeSegmentLogLines[segmentIndex] = QString("[%1] %2")
                                                    .arg(timestamp,
                                                         tr("第 %1 段识别进度=%2%").arg(segmentIndex + 1).arg(progressPercent));
    }

    renderWorkflowLogConsole();
}

void SubtitleExtraction::appendWorkflowLog(const QString &message)
{
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this, message]() {
            appendWorkflowLog(message);
        }, Qt::QueuedConnection);
        return;
    }

    if (!ui || !ui->logTextEdit) {
        return;
    }

    const QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    m_workflowLogHistory << QString("[%1] %2").arg(timestamp, message);
    renderWorkflowLogConsole();
    emit statusMessage(message);
}

void SubtitleExtraction::initializeLogConsole()
{
    if (!ui || !ui->logTextEdit) {
        return;
    }

    ui->logTextEdit->setReadOnly(true);
    ui->logTextEdit->setPlaceholderText(tr("转写进度将在此按阶段实时显示..."));
}
