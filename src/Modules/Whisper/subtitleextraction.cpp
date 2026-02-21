#include "subtitleextraction.h"
#include "ui_subtitleextraction.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFileInfoList>
#include <QShowEvent>
#include <QTimer>
#include <QToolButton>
#include <QTransform>
#include <QUrl>

#include "../../Core/dependencymanager.h"

SubtitleExtraction::SubtitleExtraction(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SubtitleExtraction)
{
    ui->setupUi(this);

    ensureModelDirectories();
    refreshWhisperModelList();

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

void SubtitleExtraction::updateToolsSpinner()
{
    const QSize iconSize = ui->toolsCheckButton->iconSize();
    QPixmap pixmap = m_toolsBaseIcon.pixmap(iconSize);
    QTransform transform;
    transform.rotate(m_toolsSpinAngle);
    ui->toolsCheckButton->setIcon(QIcon(pixmap.transformed(transform, Qt::SmoothTransformation)));
    m_toolsSpinAngle = (m_toolsSpinAngle + 30) % 360;
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
