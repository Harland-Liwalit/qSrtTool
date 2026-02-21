#include "subtitleextraction.h"
#include "ui_subtitleextraction.h"

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

#include "../../Core/dependencymanager.h"

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
            tr("媒体文件 (*.mp4 *.mkv *.avi *.mov *.mp3 *.wav *.flac);;所有文件 (*.*)"));
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
    updateRunningStateUi(true);

    // 为本次任务创建唯一中间目录，任务完成后按配置清理。
    const QString jobDirName = QString("job_%1").arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_zzz"));
    const QString jobDirPath = QDir(tempRoot).filePath(jobDirName);
    QDir().mkpath(jobDirPath);

    const QFileInfo inputInfo(inputPath);
    const QString outputFormatText = ui->outputFormatComboBox ? ui->outputFormatComboBox->currentText() : QStringLiteral("SRT");
    const QString outputExtension = outputFileExtensionFromUiText(outputFormatText);
    const QString outputFilePath = QDir(finalRoot).filePath(inputInfo.completeBaseName() + "_whisper." + outputExtension);

    if (ui->logTextEdit) {
        ui->logTextEdit->clear();
    }
    appendWorkflowLog(tr("任务开始：%1").arg(inputInfo.fileName()));
    appendWorkflowLog(tr("识别模型：%1").arg(QFileInfo(modelPath).fileName()));
    appendWorkflowLog(tr("输出格式：%1").arg(outputFormatText));

    double durationSeconds = 0.0;
    bool allSuccess = probeDurationSeconds(ffprobePath, inputPath, durationSeconds) && durationSeconds > 0.0;
    QString failureMessage;
    if (!allSuccess) {
        failureMessage = tr("无法读取媒体时长。");
    }

    QStringList segmentSrtFiles;
    const int segmentSeconds = 20 * 60;
    const int segmentCount = allSuccess ? qCeil(durationSeconds / static_cast<double>(segmentSeconds)) : 0;
    if (allSuccess) {
        appendWorkflowLog(tr("分段策略：每 20 分钟一段，共 %1 段").arg(segmentCount));
    }

    const QString languageCode = languageCodeFromUiText(ui->languageComboBox ? ui->languageComboBox->currentText() : QString());
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
        appendWorkflowLog(tr("第 %1/%2 段（%3）开始识别").arg(index + 1).arg(segmentCount).arg(rangeText));

        if (!extractSegmentAudio(ffmpegPath, inputPath, startSeconds, currentDuration, segmentAudioPath)) {
            allSuccess = false;
            failureMessage = tr("音频分段失败，请检查输入文件或 FFmpeg 是否可用。");
            appendWorkflowLog(tr("第 %1/%2 段分段失败（%3）").arg(index + 1).arg(segmentCount).arg(rangeText));
            break;
        }

        if (!transcribeSegment(whisperPath, modelPath, segmentAudioPath, segmentOutputBase, languageCode)) {
            allSuccess = false;
            failureMessage = tr("Whisper 识别失败，请检查模型文件和 whisper 版本。" );
            appendWorkflowLog(tr("第 %1/%2 段识别失败（%3）").arg(index + 1).arg(segmentCount).arg(rangeText));
            break;
        }

        if (!QFileInfo::exists(segmentSrtPath)) {
            allSuccess = false;
            failureMessage = tr("Whisper 未产出分段 SRT 文件。");
            appendWorkflowLog(tr("第 %1/%2 段未产出字幕文件（%3）").arg(index + 1).arg(segmentCount).arg(rangeText));
            break;
        }

        segmentSrtFiles << segmentSrtPath;
        const int segmentProgress = qRound((static_cast<double>(index + 1) / qMax(1, segmentCount)) * 100.0);
        appendWorkflowLog(tr("识别进度：%1%").arg(segmentProgress));
        appendWorkflowLog(tr("第 %1/%2 段识别完成（%3）").arg(index + 1).arg(segmentCount).arg(rangeText));
        QCoreApplication::processEvents();
    }

    if (allSuccess) {
        appendWorkflowLog(tr("开始合并片段字幕..."));
        QString mergedSrtContent;
        QTextStream mergedSrtOut(&mergedSrtContent, QIODevice::WriteOnly);
        int globalIndex = 1;

        for (int index = 0; allSuccess && index < segmentSrtFiles.size(); ++index) {
            QFile srtFile(segmentSrtFiles[index]);
            if (!srtFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                allSuccess = false;
                failureMessage = tr("读取分段 SRT 失败。\n%1").arg(segmentSrtFiles[index]);
                appendWorkflowLog(tr("合并失败：读取第 %1 段字幕失败").arg(index + 1));
                break;
            }

            QTextStream in(&srtFile);
            in.setCodec("UTF-8");
            const QString shifted = shiftedSrtContent(in.readAll(), static_cast<qint64>(index) * segmentSeconds * 1000);
            srtFile.close();

            const QStringList blocks = shifted.split(QRegularExpression("\\r?\\n\\r?\\n"), Qt::SkipEmptyParts);
            for (const QString &block : blocks) {
                const QStringList lines = block.split(QRegularExpression("\\r?\\n"), Qt::KeepEmptyParts);
                if (lines.size() < 2) {
                    continue;
                }

                mergedSrtOut << globalIndex++ << "\n";
                for (int i = 1; i < lines.size(); ++i) {
                    mergedSrtOut << lines[i] << "\n";
                }
                mergedSrtOut << "\n";
            }

            const int mergeProgress = qRound((static_cast<double>(index + 1) / qMax(1, segmentSrtFiles.size())) * 100.0);
            appendWorkflowLog(tr("合并进度：%1%").arg(mergeProgress));
        }

        if (allSuccess) {
            QString finalOutputContent = mergedSrtContent;
            if (outputFormatText == QStringLiteral("TXT")) {
                finalOutputContent = srtToPlainText(mergedSrtContent);
            } else if (outputFormatText == QStringLiteral("TXT（带时间）")) {
                finalOutputContent = srtToTimestampedText(mergedSrtContent);
            } else if (outputFormatText == QStringLiteral("WebVTT")) {
                finalOutputContent = srtToWebVtt(mergedSrtContent);
            }

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
            }
        }
    }

    const bool cleanTemp = ui->debugConsoleCheckBox ? ui->debugConsoleCheckBox->isChecked() : true;
    if (cleanTemp) {
        QDir(jobDirPath).removeRecursively();
        appendWorkflowLog(tr("已清理中间文件"));
    }

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
    if (m_activeProcess && m_activeProcess->state() != QProcess::NotRunning) {
        m_activeProcess->terminate();
        if (!m_activeProcess->waitForFinished(800)) {
            m_activeProcess->kill();
            m_activeProcess->waitForFinished(1000);
        }
    }
}

bool SubtitleExtraction::runProcessCancelable(const QString &program, const QStringList &arguments, QString *stdErrOutput)
{
    QProcess process;
    m_activeProcess = &process;
    process.setProgram(program);
    process.setArguments(arguments);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();

    if (!process.waitForStarted(5000)) {
        m_activeProcess = nullptr;
        return false;
    }

    while (process.state() != QProcess::NotRunning) {
        if (m_cancelRequested) {
            process.terminate();
            if (!process.waitForFinished(800)) {
                process.kill();
                process.waitForFinished(1200);
            }
            m_activeProcess = nullptr;
            return false;
        }

        process.waitForFinished(120);
        QCoreApplication::processEvents();
    }

    if (stdErrOutput) {
        *stdErrOutput = QString::fromLocal8Bit(process.readAllStandardError());
    }

    m_activeProcess = nullptr;
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
    const QStringList args = {
        "-y",
        "-hide_banner",
        "-loglevel", "error",
        "-ss", QString::number(startSeconds, 'f', 3),
        "-t", QString::number(durationSeconds, 'f', 3),
        "-i", inputPath,
        "-vn",
        "-ac", "1",
        "-ar", "16000",
        "-c:a", "pcm_s16le",
        segmentAudioPath
    };

    return runProcessCancelable(ffmpegPath, args, &stdErr);
}

bool SubtitleExtraction::transcribeSegment(const QString &whisperPath,
                                           const QString &modelPath,
                                           const QString &segmentAudioPath,
                                           const QString &segmentOutputBasePath,
                                           const QString &languageCode)
{
    QStringList args;
    args << "-m" << modelPath
         << "-f" << segmentAudioPath
         << "-osrt"
         << "-of" << segmentOutputBasePath;

    if (!languageCode.isEmpty()) {
        args << "-l" << languageCode;
    }
    if (ui->gpuCheckBox && ui->gpuCheckBox->isChecked()) {
        args << "-ng" << "0";
    }

    QString stdErr;
    return runProcessCancelable(whisperPath, args, &stdErr);
}

bool SubtitleExtraction::parseSrtTimestamp(const QString &text, qint64 &milliseconds)
{
    const QRegularExpression re("^(\\d{2}):(\\d{2}):(\\d{2}),(\\d{3})$");
    const QRegularExpressionMatch match = re.match(text.trimmed());
    if (!match.hasMatch()) {
        return false;
    }

    const qint64 h = match.captured(1).toLongLong();
    const qint64 m = match.captured(2).toLongLong();
    const qint64 s = match.captured(3).toLongLong();
    const qint64 ms = match.captured(4).toLongLong();
    milliseconds = (((h * 60) + m) * 60 + s) * 1000 + ms;
    return true;
}

QString SubtitleExtraction::formatSrtTimestamp(qint64 milliseconds)
{
    milliseconds = qMax<qint64>(0, milliseconds);
    const qint64 totalSeconds = milliseconds / 1000;
    const qint64 ms = milliseconds % 1000;
    const qint64 seconds = totalSeconds % 60;
    const qint64 minutes = (totalSeconds / 60) % 60;
    const qint64 hours = totalSeconds / 3600;

    return QString("%1:%2:%3,%4")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'))
        .arg(ms, 3, 10, QLatin1Char('0'));
}

QString SubtitleExtraction::shiftedSrtContent(const QString &srtContent, qint64 offsetMs)
{
    QStringList outputLines;
    const QStringList lines = srtContent.split(QRegularExpression("\\r?\\n"), Qt::KeepEmptyParts);
    const QRegularExpression timingRe("^(\\d{2}:\\d{2}:\\d{2},\\d{3})\\s*-->\\s*(\\d{2}:\\d{2}:\\d{2},\\d{3})(.*)$");

    for (const QString &line : lines) {
        const QRegularExpressionMatch match = timingRe.match(line);
        if (!match.hasMatch()) {
            outputLines << line;
            continue;
        }

        qint64 startMs = 0;
        qint64 endMs = 0;
        if (!parseSrtTimestamp(match.captured(1), startMs) || !parseSrtTimestamp(match.captured(2), endMs)) {
            outputLines << line;
            continue;
        }

        const QString shiftedLine = QString("%1 --> %2%3")
                                        .arg(formatSrtTimestamp(startMs + offsetMs))
                                        .arg(formatSrtTimestamp(endMs + offsetMs))
                                        .arg(match.captured(3));
        outputLines << shiftedLine;
    }

    return outputLines.join("\n");
}

QString SubtitleExtraction::languageCodeFromUiText(const QString &uiText)
{
    if (uiText == QStringLiteral("中文")) {
        return "zh";
    }
    if (uiText == QStringLiteral("English")) {
        return "en";
    }
    if (uiText == QStringLiteral("日本語")) {
        return "ja";
    }
    if (uiText == QStringLiteral("한국어")) {
        return "ko";
    }
    if (uiText == QStringLiteral("Español")) {
        return "es";
    }
    if (uiText == QStringLiteral("Français")) {
        return "fr";
    }
    if (uiText == QStringLiteral("Deutsch")) {
        return "de";
    }
    if (uiText == QStringLiteral("Русский")) {
        return "ru";
    }

    return QString();
}

QString SubtitleExtraction::outputFileExtensionFromUiText(const QString &uiText)
{
    if (uiText == QStringLiteral("TXT") || uiText == QStringLiteral("TXT（带时间）")) {
        return QStringLiteral("txt");
    }
    if (uiText == QStringLiteral("WebVTT") || uiText == QStringLiteral("WEDTT")) {
        return QStringLiteral("vtt");
    }
    return QStringLiteral("srt");
}

QString SubtitleExtraction::srtToPlainText(const QString &srtContent)
{
    QStringList lines;
    const QStringList blocks = srtContent.split(QRegularExpression("\\r?\\n\\r?\\n"), Qt::SkipEmptyParts);
    for (const QString &block : blocks) {
        const QStringList blockLines = block.split(QRegularExpression("\\r?\\n"), Qt::KeepEmptyParts);
        for (int i = 2; i < blockLines.size(); ++i) {
            if (!blockLines[i].trimmed().isEmpty()) {
                lines << blockLines[i].trimmed();
            }
        }
    }
    return lines.join("\n");
}

QString SubtitleExtraction::srtToTimestampedText(const QString &srtContent)
{
    QStringList lines;
    const QStringList blocks = srtContent.split(QRegularExpression("\\r?\\n\\r?\\n"), Qt::SkipEmptyParts);
    for (const QString &block : blocks) {
        const QStringList blockLines = block.split(QRegularExpression("\\r?\\n"), Qt::KeepEmptyParts);
        if (blockLines.size() < 2) {
            continue;
        }
        const QString timeLine = blockLines[1].trimmed();
        QString textLine;
        for (int i = 2; i < blockLines.size(); ++i) {
            if (!blockLines[i].trimmed().isEmpty()) {
                if (!textLine.isEmpty()) {
                    textLine += " ";
                }
                textLine += blockLines[i].trimmed();
            }
        }
        if (!textLine.isEmpty()) {
            lines << QString("[%1] %2").arg(timeLine, textLine);
        }
    }
    return lines.join("\n");
}

QString SubtitleExtraction::srtToWebVtt(const QString &srtContent)
{
    QStringList out;
    out << "WEBVTT" << "";

    const QStringList blocks = srtContent.split(QRegularExpression("\\r?\\n\\r?\\n"), Qt::SkipEmptyParts);
    for (const QString &block : blocks) {
        const QStringList blockLines = block.split(QRegularExpression("\\r?\\n"), Qt::KeepEmptyParts);
        if (blockLines.size() < 2) {
            continue;
        }

        QString timing = blockLines[1];
        timing.replace(",", ".");
        out << timing;
        for (int i = 2; i < blockLines.size(); ++i) {
            out << blockLines[i];
        }
        out << "";
    }

    return out.join("\n");
}

QString SubtitleExtraction::segmentRangeLabel(double startSeconds, double durationSeconds)
{
    const int startMin = qFloor(startSeconds / 60.0);
    const int endMin = qCeil((startSeconds + durationSeconds) / 60.0);
    return QString("%1-%2 分钟").arg(startMin).arg(endMin);
}

void SubtitleExtraction::appendWorkflowLog(const QString &message)
{
    if (!ui || !ui->logTextEdit) {
        return;
    }

    const QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    ui->logTextEdit->append(QString("[%1] %2").arg(timestamp, message));
    if (ui->logTextEdit->verticalScrollBar()) {
        ui->logTextEdit->verticalScrollBar()->setValue(ui->logTextEdit->verticalScrollBar()->maximum());
    }
}

void SubtitleExtraction::initializeLogConsole()
{
    if (!ui || !ui->logTextEdit) {
        return;
    }

    ui->logTextEdit->setReadOnly(true);
    ui->logTextEdit->setPlaceholderText(tr("转写进度将在此按阶段实时显示..."));
}
