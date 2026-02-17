#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QButtonGroup>
#include <QTimer>
#include <QToolButton>

#include <QtGlobal>

#include "outputmanagement.h"
#include "subtitleburning.h"
#include "subtitleextraction.h"
#include "subtitletranslation.h"
#include "videodownloader.h"
#include "videoloader.h"

#ifdef Q_OS_WIN
#include <Windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#endif

#ifdef Q_OS_WIN
static QStringList expandPdhPaths(const wchar_t *wildcardPath)
{
    DWORD bufferSize = 0;
    PDH_STATUS status = PdhExpandWildCardPathW(nullptr, wildcardPath, nullptr, &bufferSize, 0);
    if (status != PDH_MORE_DATA || bufferSize == 0) {
        return {};
    }

    QVector<wchar_t> buffer(static_cast<int>(bufferSize));
    status = PdhExpandWildCardPathW(nullptr, wildcardPath, buffer.data(), &bufferSize, 0);
    if (status != ERROR_SUCCESS) {
        return {};
    }

    QStringList results;
    const wchar_t *current = buffer.constData();
    while (*current != L'\0') {
        results.append(QString::fromWCharArray(current));
        current += wcslen(current) + 1;
    }

    return results;
}

static PDH_STATUS addPdhCounter(PDH_HQUERY query, const wchar_t *path, PDH_HCOUNTER *counter)
{
    if (!query || !path || !counter) {
        return PDH_INVALID_ARGUMENT;
    }

    using AddEnglishFn = PDH_STATUS (WINAPI *)(PDH_HQUERY, LPCWSTR, DWORD_PTR, PDH_HCOUNTER *);
    HMODULE pdhModule = GetModuleHandleW(L"pdh.dll");
    if (pdhModule) {
        auto addEnglish = reinterpret_cast<AddEnglishFn>(GetProcAddress(pdhModule, "PdhAddEnglishCounterW"));
        if (addEnglish) {
            return addEnglish(query, path, 0, counter);
        }
    }

    return PdhAddCounterW(query, path, 0, counter);
}
#endif

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    QWidget *placeholder = ui->mainStackedWidget->findChild<QWidget *>("pagePlaceholder");
    if (placeholder) {
        ui->mainStackedWidget->removeWidget(placeholder);
        placeholder->deleteLater();
    }

    VideoDownloader *downloadPage = new VideoDownloader(this);
    VideoLoader *loaderPage = new VideoLoader(this);
    SubtitleExtraction *whisperPage = new SubtitleExtraction(this);
    SubtitleTranslation *translatePage = new SubtitleTranslation(this);
    SubtitleBurning *burnPage = new SubtitleBurning(this);
    OutputManagement *outputPage = new OutputManagement(this);

    ui->mainStackedWidget->addWidget(downloadPage);
    ui->mainStackedWidget->addWidget(loaderPage);
    ui->mainStackedWidget->addWidget(whisperPage);
    ui->mainStackedWidget->addWidget(translatePage);
    ui->mainStackedWidget->addWidget(burnPage);
    ui->mainStackedWidget->addWidget(outputPage);

    QButtonGroup *navGroup = new QButtonGroup(this);
    navGroup->setExclusive(true);
    navGroup->addButton(ui->navDownloadButton);
    navGroup->addButton(ui->navPreviewButton);
    navGroup->addButton(ui->navWhisperButton);
    navGroup->addButton(ui->navTranslateButton);
    navGroup->addButton(ui->navBurnButton);
    navGroup->addButton(ui->navOutputButton);

    auto bindNav = [this](QToolButton *button, QWidget *page) {
        connect(button, &QToolButton::clicked, this, [this, page]() {
            ui->mainStackedWidget->setCurrentWidget(page);
        });
    };

    bindNav(ui->navDownloadButton, downloadPage);
    bindNav(ui->navPreviewButton, loaderPage);
    bindNav(ui->navWhisperButton, whisperPage);
    bindNav(ui->navTranslateButton, translatePage);
    bindNav(ui->navBurnButton, burnPage);
    bindNav(ui->navOutputButton, outputPage);

    ui->navPreviewButton->setChecked(true);
    ui->mainStackedWidget->setCurrentWidget(loaderPage);

    setupPerformanceCounters();
    perfTimer = new QTimer(this);
    perfTimer->setInterval(1000);
    connect(perfTimer, &QTimer::timeout, this, &MainWindow::updatePerformanceMetrics);
    perfTimer->start();
    updatePerformanceMetrics();
}

MainWindow::~MainWindow()
{
    teardownPerformanceCounters();
    delete ui;
}

void MainWindow::setupPerformanceCounters()
{
#ifdef Q_OS_WIN
    if (PdhOpenQueryW(nullptr, 0, &perfQuery) != ERROR_SUCCESS) {
        ui->statusPerfLabel->setText(tr("CPU: -- | GPU: -- | RAM: -- | VRAM: --"));
        return;
    }

    addPdhCounter(perfQuery, L"\\Processor(_Total)\\% Processor Time", &cpuCounter);
    addPdhCounter(perfQuery, L"\\Memory\\% Committed Bytes In Use", &ramCounter);

    const QStringList gpuPaths = expandPdhPaths(L"\\GPU Engine(*)\\Utilization Percentage");
    for (const QString &path : gpuPaths) {
        const QString lowerPath = path.toLower();
        if (lowerPath.contains("engtype_3d") || lowerPath.contains("engtype_compute")) {
            PDH_HCOUNTER counter = nullptr;
            if (addPdhCounter(perfQuery, reinterpret_cast<const wchar_t *>(path.utf16()), &counter) == ERROR_SUCCESS) {
                gpuCounters.push_back(counter);
            }
        }
    }

    const QStringList vramUsagePaths = expandPdhPaths(L"\\GPU Adapter Memory(*)\\Dedicated Usage");
    const QStringList vramLimitPaths = expandPdhPaths(L"\\GPU Adapter Memory(*)\\Dedicated Limit");
    const int vramCount = qMin(vramUsagePaths.size(), vramLimitPaths.size());
    for (int i = 0; i < vramCount; ++i) {
        PDH_HCOUNTER usageCounter = nullptr;
        PDH_HCOUNTER limitCounter = nullptr;
        if (addPdhCounter(perfQuery, reinterpret_cast<const wchar_t *>(vramUsagePaths[i].utf16()), &usageCounter) == ERROR_SUCCESS
            && addPdhCounter(perfQuery, reinterpret_cast<const wchar_t *>(vramLimitPaths[i].utf16()), &limitCounter) == ERROR_SUCCESS) {
            vramUsageCounters.push_back(usageCounter);
            vramLimitCounters.push_back(limitCounter);
        }
    }

    PdhCollectQueryData(perfQuery);
#else
    ui->statusPerfLabel->setText(tr("CPU: -- | GPU: -- | RAM: -- | VRAM: --"));
#endif
}

void MainWindow::updatePerformanceMetrics()
{
#ifdef Q_OS_WIN
    if (!perfQuery) {
        return;
    }

    if (PdhCollectQueryData(perfQuery) != ERROR_SUCCESS) {
        return;
    }

    auto readPercent = [](PDH_HCOUNTER counter) -> double {
        PDH_FMT_COUNTERVALUE value;
        if (!counter) {
            return 0.0;
        }
        if (PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, nullptr, &value) != ERROR_SUCCESS) {
            return 0.0;
        }
        return value.doubleValue;
    };

    double cpuPercent = readPercent(cpuCounter);
    double ramPercent = readPercent(ramCounter);

    double gpuPercent = 0.0;
    for (PDH_HCOUNTER counter : gpuCounters) {
        gpuPercent += readPercent(counter);
    }
    if (gpuPercent > 100.0) {
        gpuPercent = 100.0;
    }

    ULONGLONG vramUsage = 0;
    ULONGLONG vramLimit = 0;
    for (int i = 0; i < vramUsageCounters.size(); ++i) {
        PDH_FMT_COUNTERVALUE usageValue;
        PDH_FMT_COUNTERVALUE limitValue;
        if (PdhGetFormattedCounterValue(vramUsageCounters[i], PDH_FMT_LARGE, nullptr, &usageValue) == ERROR_SUCCESS
            && PdhGetFormattedCounterValue(vramLimitCounters[i], PDH_FMT_LARGE, nullptr, &limitValue) == ERROR_SUCCESS) {
            vramUsage += static_cast<ULONGLONG>(usageValue.largeValue);
            vramLimit += static_cast<ULONGLONG>(limitValue.largeValue);
        }
    }

    double vramPercent = 0.0;
    if (vramLimit > 0) {
        vramPercent = (static_cast<double>(vramUsage) / static_cast<double>(vramLimit)) * 100.0;
    }

    auto toPercentString = [](double value) {
        const int rounded = qBound(0, static_cast<int>(value + 0.5), 100);
        return QString::number(rounded) + "%";
    };

    const QString perfText = tr("CPU: %1 | GPU: %2 | RAM: %3 | VRAM: %4")
                                 .arg(toPercentString(cpuPercent))
                                 .arg(toPercentString(gpuPercent))
                                 .arg(toPercentString(ramPercent))
                                 .arg(toPercentString(vramPercent));
    ui->statusPerfLabel->setText(perfText);
#endif
}

void MainWindow::teardownPerformanceCounters()
{
#ifdef Q_OS_WIN
    gpuCounters.clear();
    vramUsageCounters.clear();
    vramLimitCounters.clear();

    if (perfQuery) {
        PdhCloseQuery(perfQuery);
        perfQuery = nullptr;
    }
#endif
}

