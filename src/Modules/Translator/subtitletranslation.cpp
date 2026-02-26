#include "subtitletranslation.h"
#include "ui_subtitletranslation.h"

#include "promptediting.h"

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>

namespace {
QString presetDirectoryPath()
{
    return QDir::currentPath() + "/presets/translator";
}
}

SubtitleTranslation::SubtitleTranslation(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SubtitleTranslation)
{
    ui->setupUi(this);

    initializePresetStorage();
    refreshPresetList();

    connect(ui->importPresetButton, &QPushButton::clicked, this, &SubtitleTranslation::importPresetToStorage);
    connect(ui->editPresetButton, &QPushButton::clicked, this, &SubtitleTranslation::openPromptEditingDialog);
}

SubtitleTranslation::~SubtitleTranslation()
{
    delete ui;
}

void SubtitleTranslation::initializePresetStorage()
{
    m_presetDirectory = presetDirectoryPath();
    QDir().mkpath(m_presetDirectory);
}

void SubtitleTranslation::refreshPresetList(const QString &preferredPath)
{
    ui->presetComboBox->clear();

    QDir dir(m_presetDirectory);
    const QStringList files = dir.entryList(QStringList() << "*.json", QDir::Files, QDir::Name);
    for (const QString &fileName : files) {
        const QString fullPath = dir.filePath(fileName);
        ui->presetComboBox->addItem(fileName, fullPath);
    }

    if (ui->presetComboBox->count() == 0) {
        ui->presetComboBox->addItem(tr("未找到预设"), QString());
        return;
    }

    int targetIndex = 0;
    if (!preferredPath.isEmpty()) {
        const QString normalizedPreferred = QFileInfo(preferredPath).absoluteFilePath();
        for (int index = 0; index < ui->presetComboBox->count(); ++index) {
            const QString path = QFileInfo(ui->presetComboBox->itemData(index).toString()).absoluteFilePath();
            if (path == normalizedPreferred) {
                targetIndex = index;
                break;
            }
        }
    }
    ui->presetComboBox->setCurrentIndex(targetIndex);
}

QString SubtitleTranslation::selectedPresetPath() const
{
    const QVariant data = ui->presetComboBox->currentData();
    const QString dataPath = data.toString().trimmed();
    if (!dataPath.isEmpty()) {
        return dataPath;
    }

    const QString text = ui->presetComboBox->currentText().trimmed();
    if (text.isEmpty() || text == tr("未找到预设")) {
        return QString();
    }

    QString fileName = text;
    if (!fileName.endsWith(".json", Qt::CaseInsensitive)) {
        fileName += ".json";
    }
    return QDir(m_presetDirectory).filePath(fileName);
}

void SubtitleTranslation::importPresetToStorage()
{
    const QString sourcePath = QFileDialog::getOpenFileName(this,
                                                            tr("导入预设"),
                                                            QDir::homePath(),
                                                            tr("JSON 文件 (*.json)"));
    if (sourcePath.isEmpty()) {
        return;
    }

    const QFileInfo fileInfo(sourcePath);
    const QString destinationPath = QDir(m_presetDirectory).filePath(fileInfo.fileName());

    if (QFileInfo(sourcePath).absoluteFilePath() != QFileInfo(destinationPath).absoluteFilePath()) {
        if (QFile::exists(destinationPath)) {
            QFile::remove(destinationPath);
        }
        if (!QFile::copy(sourcePath, destinationPath)) {
            QMessageBox::warning(this, tr("导入失败"), tr("无法复制预设到目录：%1").arg(destinationPath));
            return;
        }
    }

    refreshPresetList(destinationPath);
}

void SubtitleTranslation::openPromptEditingDialog()
{
    QString presetPath = selectedPresetPath();
    if (!presetPath.isEmpty() && !QFileInfo::exists(presetPath)) {
        presetPath.clear();
    }

    PromptEditing dialog(m_presetDirectory, presetPath, this);
    if (dialog.exec() == QDialog::Accepted) {
        refreshPresetList(dialog.savedPresetPath());
    }
}
