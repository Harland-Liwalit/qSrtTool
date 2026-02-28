#include "outputmanagement.h"
#include "ui_outputmanagement.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QMessageBox>
#include <QFrame>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QUrl>
#include <algorithm>

OutputManagement::OutputManagement(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::OutputManagement)
{
    ui->setupUi(this);

    if (QFrame *legacyDetailsFrame = findChild<QFrame *>(QStringLiteral("detailsFrame"))) {
        legacyDetailsFrame->setVisible(false);
        legacyDetailsFrame->setParent(nullptr);
        legacyDetailsFrame->deleteLater();
    }

    setupUiState();
    setupConnections();
    refreshOutputs();
}

OutputManagement::~OutputManagement()
{
    delete ui;
}

void OutputManagement::refreshOutputs()
{
    QList<OutputRecord> records;
    const QString outputRoot = workspaceOutputRoot();

    collectOutputsFromDirectory(QDir(outputRoot).filePath(QStringLiteral("whisper")),
                                tr("字幕提取"),
                                tr("Whisper"),
                                &records);
    collectOutputsFromDirectory(QDir(outputRoot).filePath(QStringLiteral("translator_final")),
                                tr("字幕翻译"),
                                tr("翻译模块"),
                                &records);
    collectOutputsFromDirectory(QDir(outputRoot).filePath(QStringLiteral("burner")),
                                tr("烧录压制"),
                                tr("烧录模块"),
                                &records);
    collectOutputsFromDirectory(QDir(outputRoot).filePath(QStringLiteral("downloads")),
                                tr("视频下载"),
                                tr("下载模块"),
                                &records);

    std::sort(records.begin(), records.end(), [](const OutputRecord &a, const OutputRecord &b) {
        return a.modifiedAt > b.modifiedAt;
    });

    m_allRecords = records;
    applyFilters();
}

void OutputManagement::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_initialized) {
        refreshOutputs();
    }
}

void OutputManagement::applyFilters()
{
    if (!ui) {
        return;
    }

    const QString keyword = ui->searchLineEdit ? ui->searchLineEdit->text().trimmed() : QString();
    const QString selectedType = ui->typeComboBox ? ui->typeComboBox->currentText().trimmed() : QString();
    const QDate fromDate = ui->fromDateEdit ? ui->fromDateEdit->date() : QDate();
    const QDate toDate = ui->toDateEdit ? ui->toDateEdit->date() : QDate();

    QList<OutputRecord> filtered;
    filtered.reserve(m_allRecords.size());

    for (const OutputRecord &record : m_allRecords) {
        if (!keyword.isEmpty()) {
            const QString haystack = record.name + "\n" + record.type + "\n" + record.source + "\n" + record.path;
            if (!haystack.contains(keyword, Qt::CaseInsensitive)) {
                continue;
            }
        }

        if (selectedType != tr("全部类型") && !selectedType.isEmpty() && record.type != selectedType) {
            continue;
        }

        const QDate fileDate = record.modifiedAt.date();
        if (fromDate.isValid() && fileDate < fromDate) {
            continue;
        }
        if (toDate.isValid() && fileDate > toDate) {
            continue;
        }

        filtered.push_back(record);
    }

    populateTable(filtered);

    if (ui->totalCountLabel) {
        ui->totalCountLabel->setText(tr("总计 %1").arg(filtered.size()));
    }
    if (ui->successCountLabel) {
        ui->successCountLabel->setText(tr("已完成 %1").arg(filtered.size()));
    }
    if (ui->runningCountLabel) {
        ui->runningCountLabel->hide();
    }
    if (ui->failedCountLabel) {
        ui->failedCountLabel->hide();
    }
}

void OutputManagement::onOpenOutputFolderClicked()
{
    QString targetDirectory;

    if (ui && ui->outputsTable) {
        const int row = ui->outputsTable->currentRow();
        if (row >= 0) {
            QTableWidgetItem *pathItem = ui->outputsTable->item(row, 5);
            if (pathItem) {
                const QString filePath = pathItem->text().trimmed();
                const QFileInfo fileInfo(filePath);
                if (fileInfo.exists()) {
                    targetDirectory = fileInfo.absolutePath();
                }
            }
        }
    }

    if (targetDirectory.isEmpty()) {
        targetDirectory = workspaceOutputRoot();
    }

    QDir().mkpath(targetDirectory);
    QDesktopServices::openUrl(QUrl::fromLocalFile(targetDirectory));
}

void OutputManagement::onExportListClicked()
{
    const QString defaultPath = QDir(workspaceOutputRoot()).filePath(
        QStringLiteral("output_list_%1.csv").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"))));

    const QString savePath = QFileDialog::getSaveFileName(this,
                                                          tr("导出输出清单"),
                                                          defaultPath,
                                                          tr("CSV 文件 (*.csv)"));
    if (savePath.isEmpty()) {
        return;
    }

    QFile file(savePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QMessageBox::warning(this, tr("导出失败"), tr("无法写入文件：%1").arg(savePath));
        return;
    }

    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    stream << QStringLiteral("名称,类型,来源,状态,时间,路径\n");
    for (const OutputRecord &record : m_allRecords) {
        auto escapeCsv = [](const QString &value) {
            QString escaped = value;
            escaped.replace('"', QStringLiteral("\"\""));
            return QStringLiteral("\"%1\"").arg(escaped);
        };

        stream << escapeCsv(record.name) << ','
               << escapeCsv(record.type) << ','
               << escapeCsv(record.source) << ','
               << escapeCsv(record.status) << ','
               << escapeCsv(record.modifiedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))) << ','
               << escapeCsv(record.path) << '\n';
    }

    file.close();
    QMessageBox::information(this, tr("导出成功"), tr("输出清单已保存到：\n%1").arg(savePath));
}

void OutputManagement::setupUiState()
{
    if (!ui) {
        return;
    }

    if (ui->outputsTable) {
        ui->outputsTable->setSortingEnabled(false);
        if (ui->outputsTable->horizontalHeader()) {
            ui->outputsTable->horizontalHeader()->setStretchLastSection(true);
        }
    }

    if (ui->statusComboBox) {
        ui->statusComboBox->clear();
        ui->statusComboBox->addItem(tr("已完成"));
        ui->statusComboBox->setEnabled(false);
    }

    if (ui->fromDateEdit) {
        ui->fromDateEdit->setDate(QDate(2000, 1, 1));
    }
    if (ui->toDateEdit) {
        ui->toDateEdit->setDate(QDate::currentDate());
    }

    m_initialized = true;
}

void OutputManagement::setupConnections()
{
    if (!ui) {
        return;
    }

    if (ui->searchLineEdit) {
        connect(ui->searchLineEdit, &QLineEdit::textChanged, this, &OutputManagement::applyFilters);
    }
    if (ui->typeComboBox) {
        connect(ui->typeComboBox, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
                this, &OutputManagement::applyFilters);
    }
    if (ui->fromDateEdit) {
        connect(ui->fromDateEdit, &QDateEdit::dateChanged, this, [this](const QDate &) { applyFilters(); });
    }
    if (ui->toDateEdit) {
        connect(ui->toDateEdit, &QDateEdit::dateChanged, this, [this](const QDate &) { applyFilters(); });
    }
    if (ui->refreshButton) {
        connect(ui->refreshButton, &QPushButton::clicked, this, &OutputManagement::refreshOutputs);
    }
    if (ui->openFolderButton) {
        connect(ui->openFolderButton, &QPushButton::clicked, this, &OutputManagement::onOpenOutputFolderClicked);
    }
    if (ui->exportListButton) {
        connect(ui->exportListButton, &QPushButton::clicked, this, &OutputManagement::onExportListClicked);
    }
}

void OutputManagement::collectOutputsFromDirectory(const QString &directoryPath,
                                                   const QString &type,
                                                   const QString &source,
                                                   QList<OutputRecord> *records) const
{
    if (!records) {
        return;
    }

    QDir dir(directoryPath);
    if (!dir.exists()) {
        return;
    }

    QDirIterator it(directoryPath, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString filePath = it.next();
        const QFileInfo info(filePath);
        if (!shouldIncludeFile(info)) {
            continue;
        }

        OutputRecord record;
        record.name = info.fileName();
        record.type = type;
        record.source = source;
        record.status = tr("已完成");
        record.modifiedAt = info.lastModified();
        record.path = info.absoluteFilePath();
        records->append(record);
    }
}

bool OutputManagement::shouldIncludeFile(const QFileInfo &info) const
{
    if (!info.exists() || !info.isFile()) {
        return false;
    }

    const QString suffix = info.suffix().trimmed().toLower();
    if (suffix.isEmpty()) {
        return false;
    }

    static const QStringList excludedSuffixes = {
        QStringLiteral("part"),
        QStringLiteral("tmp"),
        QStringLiteral("temp"),
        QStringLiteral("aria2"),
        QStringLiteral("ytdl"),
        QStringLiteral("log")
    };

    if (excludedSuffixes.contains(suffix)) {
        return false;
    }

    return info.size() > 0;
}

void OutputManagement::populateTable(const QList<OutputRecord> &records)
{
    if (!ui || !ui->outputsTable) {
        return;
    }

    ui->outputsTable->setRowCount(records.size());

    for (int row = 0; row < records.size(); ++row) {
        const OutputRecord &record = records.at(row);

        ui->outputsTable->setItem(row, 0, new QTableWidgetItem(record.name));
        ui->outputsTable->setItem(row, 1, new QTableWidgetItem(record.type));
        ui->outputsTable->setItem(row, 2, new QTableWidgetItem(record.source));
        ui->outputsTable->setItem(row, 3, new QTableWidgetItem(record.status));
        ui->outputsTable->setItem(row, 4, new QTableWidgetItem(record.modifiedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
        ui->outputsTable->setItem(row, 5, new QTableWidgetItem(record.path));
    }
}

QString OutputManagement::workspaceOutputRoot() const
{
    return QDir(QDir::currentPath()).filePath(QStringLiteral("output"));
}
