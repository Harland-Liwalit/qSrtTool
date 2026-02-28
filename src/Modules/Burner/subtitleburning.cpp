#include "subtitleburning.h"
#include "ui_subtitleburning.h"
#include "subtitleburncommandbuilder.h"
#include "subtitlecontainerprofile.h"
#include "subtitleburntaskrunner.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>
#include <QTimer>
#include <QToolButton>
#include <QTransform>

#include "../../Core/dependencymanager.h"

SubtitleBurning::SubtitleBurning(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SubtitleBurning)
{
    ui->setupUi(this);

    m_burnTaskRunner = new SubtitleBurnTaskRunner(this);
    setupBurnWorkflowUi();

    m_toolsBaseIcon = ui->toolsCheckButton->icon();
    m_toolsSpinTimer = new QTimer(this);
    m_toolsSpinTimer->setInterval(60);
    connect(m_toolsSpinTimer, &QTimer::timeout, this, &SubtitleBurning::updateToolsSpinner);

    connect(ui->toolsCheckButton, &QToolButton::clicked, this, []() {
        DependencyManager::instance().checkForUpdates();
    });

    auto &manager = DependencyManager::instance();
    connect(&manager, &DependencyManager::busyChanged, this, &SubtitleBurning::setToolsLoading);

    connect(m_burnTaskRunner, &SubtitleBurnTaskRunner::taskStarted, this, [this]() {
        updateRunningStateUi(true);
    });
    connect(m_burnTaskRunner, &SubtitleBurnTaskRunner::taskLog, this, &SubtitleBurning::appendLogLine);
    connect(m_burnTaskRunner, &SubtitleBurnTaskRunner::taskFinished, this,
            [this](bool success, const QString &message) {
                updateRunningStateUi(false);
                appendLogLine(message);
                if (!success) {
                    QMessageBox::warning(this, tr("字幕烧录"), message);
                }
            });
}

SubtitleBurning::~SubtitleBurning()
{
    delete ui;
}

void SubtitleBurning::setToolsLoading(bool loading)
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

void SubtitleBurning::updateToolsSpinner()
{
    const QSize iconSize = ui->toolsCheckButton->iconSize();
    QPixmap pixmap = m_toolsBaseIcon.pixmap(iconSize);
    QTransform transform;
    transform.rotate(m_toolsSpinAngle);
    ui->toolsCheckButton->setIcon(QIcon(pixmap.transformed(transform, Qt::SmoothTransformation)));
    m_toolsSpinAngle = (m_toolsSpinAngle + 30) % 360;
}

void SubtitleBurning::setupBurnWorkflowUi()
{
    populateContainerOptions();

    if (ui->logTextEdit) {
        ui->logTextEdit->clear();
        ui->logTextEdit->append(tr("就绪：请选择视频与字幕后开始压制。"));
    }

    if (ui->cancelBurnButton) {
        ui->cancelBurnButton->setEnabled(false);
    }

    connect(ui->importVideoButton, &QPushButton::clicked, this, [this]() {
        const QString selectedPath = QFileDialog::getOpenFileName(
            this,
            tr("选择视频文件"),
            defaultVideoImportDirectory(),
            tr("视频文件 (*.mp4 *.mkv *.avi *.mov *.flv *.webm);;所有文件 (*.*)"));

        if (selectedPath.isEmpty()) {
            return;
        }

        m_inputVideoPath = selectedPath;
        saveLastVideoImportDirectory(selectedPath);
        if (ui->previewLabel) {
            ui->previewLabel->setText(QFileInfo(selectedPath).fileName());
        }

        if (ui->outputPathLineEdit && ui->outputPathLineEdit->text().trimmed().isEmpty()) {
            ui->outputPathLineEdit->setText(suggestedOutputPath());
        }

        appendLogLine(tr("已导入视频：%1").arg(selectedPath));
    });

    connect(ui->importSubtitleButton, &QPushButton::clicked, this, [this]() {
        const QString selectedPath = QFileDialog::getOpenFileName(
            this,
            tr("选择字幕文件"),
            defaultSubtitleImportDirectory(),
            tr("字幕文件 (*.srt *.ass *.ssa *.vtt *.sub);;所有文件 (*.*)"));

        if (selectedPath.isEmpty()) {
            return;
        }

        m_externalSubtitlePath = selectedPath;
        saveLastSubtitleImportDirectory(selectedPath);
        appendLogLine(tr("已导入字幕：%1").arg(selectedPath));
    });

    connect(ui->browseOutputButton, &QPushButton::clicked, this, [this]() {
        const QString extension = selectedContainerExtension();
        const QString suggested = suggestedOutputPath();
        const QString filter = tr("输出文件 (*.%1);;所有文件 (*.*)").arg(extension);

        const QString outputPath = QFileDialog::getSaveFileName(
            this,
            tr("选择输出文件"),
            suggested,
            filter);

        if (!outputPath.isEmpty() && ui->outputPathLineEdit) {
            ui->outputPathLineEdit->setText(outputPath);
        }
    });

    connect(ui->advancedOptionsButton, &QPushButton::clicked, this, [this]() {
        QMessageBox::information(this,
                                 tr("高级输出选项"),
                                 tr("当前版本先提供基础烧录能力，高级编码参数将在后续版本开放。"));
    });

    connect(ui->startBurnButton, &QPushButton::clicked, this, [this]() {
        startBurnTask();
    });

    connect(ui->cancelBurnButton, &QPushButton::clicked, this, [this]() {
        if (!m_burnTaskRunner || !m_burnTaskRunner->isRunning()) {
            return;
        }
        m_burnTaskRunner->cancelTask();
    });

    connect(ui->containerComboBox, &QComboBox::currentTextChanged, this, [this](const QString &) {
        syncOutputPathExtensionWithContainer();
    });
}

void SubtitleBurning::updateRunningStateUi(bool running)
{
    if (ui->startBurnButton) {
        ui->startBurnButton->setEnabled(!running);
    }
    if (ui->cancelBurnButton) {
        ui->cancelBurnButton->setEnabled(running);
    }
    if (ui->importVideoButton) {
        ui->importVideoButton->setEnabled(!running);
    }
    if (ui->importSubtitleButton) {
        ui->importSubtitleButton->setEnabled(!running);
    }
    if (ui->browseOutputButton) {
        ui->browseOutputButton->setEnabled(!running);
    }
    if (ui->advancedOptionsButton) {
        ui->advancedOptionsButton->setEnabled(!running);
    }
    if (ui->containerComboBox) {
        ui->containerComboBox->setEnabled(!running);
    }
    if (ui->burnModeComboBox) {
        ui->burnModeComboBox->setEnabled(!running);
    }
    if (ui->subtitleTrackComboBox) {
        ui->subtitleTrackComboBox->setEnabled(!running);
    }
    if (ui->mergeTracksCheckBox) {
        ui->mergeTracksCheckBox->setEnabled(!running);
    }
    if (ui->keepAudioCheckBox) {
        ui->keepAudioCheckBox->setEnabled(!running);
    }
}

void SubtitleBurning::appendLogLine(const QString &message)
{
    if (!ui || !ui->logTextEdit || message.trimmed().isEmpty()) {
        return;
    }

    ui->logTextEdit->append(message);
}

QString SubtitleBurning::defaultVideoImportDirectory() const
{
    // 优先使用用户上一次导入目录；无历史时回退到当前已选文件目录，再回退到用户主目录。
    QSettings settings(QStringLiteral("qSrtTool"), QStringLiteral("qSrtTool"));
    const QString persistedDir = settings.value(QStringLiteral("burner/last_video_import_dir")).toString().trimmed();
    if (!persistedDir.isEmpty() && QDir(persistedDir).exists()) {
        return persistedDir;
    }

    const QFileInfo inputInfo(m_inputVideoPath);
    if (inputInfo.exists()) {
        return inputInfo.absolutePath();
    }

    return QDir::homePath();
}

QString SubtitleBurning::defaultSubtitleImportDirectory() const
{
    // 与视频目录分开记忆，避免两类素材位于不同目录时互相覆盖默认路径。
    QSettings settings(QStringLiteral("qSrtTool"), QStringLiteral("qSrtTool"));
    const QString persistedDir = settings.value(QStringLiteral("burner/last_subtitle_import_dir")).toString().trimmed();
    if (!persistedDir.isEmpty() && QDir(persistedDir).exists()) {
        return persistedDir;
    }

    const QFileInfo subtitleInfo(m_externalSubtitlePath);
    if (subtitleInfo.exists()) {
        return subtitleInfo.absolutePath();
    }

    return QDir::homePath();
}

void SubtitleBurning::saveLastVideoImportDirectory(const QString &filePath)
{
    const QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        return;
    }

    QSettings settings(QStringLiteral("qSrtTool"), QStringLiteral("qSrtTool"));
    settings.setValue(QStringLiteral("burner/last_video_import_dir"), fileInfo.absolutePath());
    settings.sync();
}

void SubtitleBurning::saveLastSubtitleImportDirectory(const QString &filePath)
{
    const QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        return;
    }

    QSettings settings(QStringLiteral("qSrtTool"), QStringLiteral("qSrtTool"));
    settings.setValue(QStringLiteral("burner/last_subtitle_import_dir"), fileInfo.absolutePath());
    settings.sync();
}

QString SubtitleBurning::selectedContainerExtension() const
{
    const SubtitleContainerProfile profile = SubtitleContainerProfileRegistry::resolveByIdOrExtension(selectedContainerId());
    return profile.extension.isEmpty() ? QStringLiteral("mp4") : profile.extension;
}

void SubtitleBurning::populateContainerOptions()
{
    if (!ui || !ui->containerComboBox) {
        return;
    }

    const QList<SubtitleContainerProfile> profiles = SubtitleContainerProfileRegistry::allProfiles();
    const QString previousId = selectedContainerId();

    ui->containerComboBox->clear();
    for (const SubtitleContainerProfile &profile : profiles) {
        ui->containerComboBox->addItem(profile.displayName, profile.id);
    }

    const int previousIndex = ui->containerComboBox->findData(previousId);
    if (previousIndex >= 0) {
        ui->containerComboBox->setCurrentIndex(previousIndex);
    }
}

void SubtitleBurning::syncOutputPathExtensionWithContainer()
{
    if (!ui || !ui->outputPathLineEdit) {
        return;
    }

    const QString currentPath = ui->outputPathLineEdit->text().trimmed();
    if (currentPath.isEmpty()) {
        if (!m_inputVideoPath.isEmpty()) {
            ui->outputPathLineEdit->setText(suggestedOutputPath());
        }
        return;
    }

    const QString extension = selectedContainerExtension();
    QFileInfo currentInfo(currentPath);
    const QString baseName = currentInfo.completeBaseName().isEmpty()
                                 ? QStringLiteral("output")
                                 : currentInfo.completeBaseName();

    QDir outputDir = currentInfo.dir();
    if (!outputDir.exists()) {
        outputDir = QDir(QDir(QDir::currentPath()).filePath("output/burner"));
    }

    const QString syncedPath = outputDir.filePath(QStringLiteral("%1.%2").arg(baseName, extension));
    ui->outputPathLineEdit->setText(syncedPath);
}

QString SubtitleBurning::selectedContainerId() const
{
    if (!ui || !ui->containerComboBox) {
        return QStringLiteral("mp4");
    }

    const QString dataValue = ui->containerComboBox->currentData().toString().trimmed().toLower();
    if (!dataValue.isEmpty()) {
        return dataValue;
    }

    const QString textValue = ui->containerComboBox->currentText().trimmed().toLower();
    return textValue.isEmpty() ? QStringLiteral("mp4") : textValue;
}

QString SubtitleBurning::suggestedOutputPath() const
{
    const QString extension = selectedContainerExtension();
    const QFileInfo inputInfo(m_inputVideoPath);

    const QString outputDir = QDir(QDir::currentPath()).filePath("output/burner");
    QDir().mkpath(outputDir);

    const QString baseName = inputInfo.completeBaseName().isEmpty() ? QStringLiteral("output") : inputInfo.completeBaseName();
    return QDir(outputDir).filePath(QStringLiteral("%1_burned.%2").arg(baseName, extension));
}

bool SubtitleBurning::startBurnTask()
{
    if (!m_burnTaskRunner || m_burnTaskRunner->isRunning()) {
        return false;
    }

    if (m_inputVideoPath.trimmed().isEmpty() || !QFileInfo::exists(m_inputVideoPath)) {
        QMessageBox::warning(this, tr("输入无效"), tr("请先导入一个可访问的视频文件。"));
        return false;
    }

    const bool hasExternalSubtitle = !m_externalSubtitlePath.trimmed().isEmpty();
    if (hasExternalSubtitle && !QFileInfo::exists(m_externalSubtitlePath)) {
        QMessageBox::warning(this, tr("字幕无效"), tr("导入的字幕文件不存在或不可访问。"));
        return false;
    }

    QString outputPath = ui->outputPathLineEdit ? ui->outputPathLineEdit->text().trimmed() : QString();
    if (outputPath.isEmpty()) {
        outputPath = suggestedOutputPath();
        if (ui->outputPathLineEdit) {
            ui->outputPathLineEdit->setText(outputPath);
        }
    }

    QFileInfo outputInfo(outputPath);
    if (!outputInfo.dir().exists()) {
        QDir().mkpath(outputInfo.dir().absolutePath());
    }

    SubtitleBurnRequest request;
    request.inputVideoPath = m_inputVideoPath;
    request.externalSubtitlePath = m_externalSubtitlePath;
    request.outputPath = outputPath;
    request.container = selectedContainerId();
    request.burnModeIndex = ui->burnModeComboBox ? ui->burnModeComboBox->currentIndex() : 0;
    request.subtitleTrackIndex = ui->subtitleTrackComboBox ? ui->subtitleTrackComboBox->currentIndex() : 0;
    request.mergeTracks = ui->mergeTracksCheckBox && ui->mergeTracksCheckBox->isChecked();
    request.keepAudio = ui->keepAudioCheckBox && ui->keepAudioCheckBox->isChecked();

    appendLogLine(tr("开始压制任务，输出：%1").arg(outputPath));
    m_burnTaskRunner->startTask(request);
    return true;
}
