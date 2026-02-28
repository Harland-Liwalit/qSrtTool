#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QButtonGroup>
#include <QTimer>
#include <QToolButton>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QStringList>

#include <QtGlobal>

#include "Modules/OutputMgr/outputmanagement.h"
#include "Modules/Burner/subtitleburning.h"
#include "Modules/Whisper/subtitleextraction.h"
#include "Modules/Translator/subtitletranslation.h"
#include "Modules/Downloder/videodownloader.h"
#include "Modules/Loader/videoloader.h"
#include "Core/dependencymanager.h"
#include "Widgets/pageswitchconfirmdialog.h"

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
    if (status != static_cast<PDH_STATUS>(PDH_MORE_DATA) || bufferSize == 0) {
        return {};
    }

    QVector<wchar_t> buffer(static_cast<int>(bufferSize));
    status = PdhExpandWildCardPathW(nullptr, wildcardPath, buffer.data(), &bufferSize, 0);
    if (status != static_cast<PDH_STATUS>(ERROR_SUCCESS)) {
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

    initializeDependencies();

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

    m_loaderPage = loaderPage;
    m_whisperPage = whisperPage;
    m_downloadPage = downloadPage;

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

    bindNavigationButton(ui->navDownloadButton, downloadPage, tr("视频下载"));
    bindNavigationButton(ui->navPreviewButton, loaderPage, tr("视频预览"));
    bindNavigationButton(ui->navWhisperButton, whisperPage, tr("字幕提取"));
    bindNavigationButton(ui->navTranslateButton, translatePage, tr("字幕翻译"));
    bindNavigationButton(ui->navBurnButton, burnPage, tr("字幕烧录"));
    bindNavigationButton(ui->navOutputButton, outputPage, tr("输出管理"));

    connect(loaderPage, &VideoLoader::statusMessage, this, &MainWindow::setStatusHint);
    connect(whisperPage, &SubtitleExtraction::statusMessage, this, &MainWindow::setStatusHint);
    connect(loaderPage, &VideoLoader::requestNextStep, this, [this, whisperPage](const QString &videoPath) {
        if (!whisperPage || !ui || !ui->mainStackedWidget) {
            return;
        }
        
        // 停止当前页面的所有任务（工作流切换，无需确认）
        QWidget *currentPage = ui->mainStackedWidget->currentWidget();
        if (currentPage) {
            stopAllTasksOnPage(currentPage);
        }
        
        whisperPage->loadVideoFile(videoPath);
        ui->mainStackedWidget->setCurrentWidget(whisperPage);
        syncNavigationSelection(whisperPage);
    });

    connect(whisperPage, &SubtitleExtraction::requestNextStep, this, [this, translatePage](const QString &subtitlePath) {
        if (!translatePage || !ui || !ui->mainStackedWidget) {
            return;
        }

        const QFileInfo subtitleInfo(subtitlePath);
        if (subtitlePath.trimmed().isEmpty() || !subtitleInfo.exists()) {
            QMessageBox::warning(this, tr("字幕文件不可用"), tr("未找到识别输出文件，请先完成识别后再进入下一步。"));
            return;
        }

        QWidget *currentPage = ui->mainStackedWidget->currentWidget();
        if (currentPage) {
            stopAllTasksOnPage(currentPage);
        }

        translatePage->setPendingSubtitleFile(subtitleInfo.absoluteFilePath());
        ui->mainStackedWidget->setCurrentWidget(translatePage);
        syncNavigationSelection(translatePage);
        setStatusHint(tr("已进入字幕翻译：%1").arg(subtitleInfo.fileName()));
    });

    connect(ui->navDownloadButton, &QToolButton::clicked, this, &MainWindow::triggerDependencyCheckOnce);
    connect(ui->navWhisperButton, &QToolButton::clicked, this, &MainWindow::triggerDependencyCheckOnce);
    connect(ui->navBurnButton, &QToolButton::clicked, this, &MainWindow::triggerDependencyCheckOnce);

    ui->navPreviewButton->setChecked(true);
    ui->mainStackedWidget->setCurrentWidget(loaderPage);

    auto &depManager = DependencyManager::instance();
    connect(&depManager, &DependencyManager::busyChanged, this, [this](bool busy) {
        if (!busy) {
            m_dependencyDownloadProgress.clear();
            const QString cachePath = QDir::currentPath() + "/.qsrottool_dep_cache";
            DependencyManager::instance().saveVersionCache(cachePath);
        }
        setStatusHint(busy ? tr("正在检查/下载依赖...") : tr("依赖检查完成"));
    });
    connect(&depManager, &DependencyManager::updateCheckFailed, this, [this](const QString &error) {
        setStatusHint(tr("依赖检查失败: %1").arg(error));
        QMessageBox::warning(this, tr("依赖检查失败"), error);
    });
    connect(&depManager, &DependencyManager::updateCheckFinished, this, [this]() {
        const auto deps = DependencyManager::instance().getAllDependencies();
        bool hasUpdates = false;
        for (const auto &info : deps) {
            if (info.needsUpdate) {
                hasUpdates = true;
                break;
            }
        }
        setStatusHint(hasUpdates ? tr("发现更新，开始下载...") : tr("依赖已是最新"));
    });
    connect(&depManager, &DependencyManager::downloadFinished, this, [this](const QString &depId, const QString &savePath) {
        Q_UNUSED(savePath);
        m_dependencyDownloadProgress.remove(depId);
        QString name = depId;
        const auto deps = DependencyManager::instance().getAllDependencies();
        for (const auto &info : deps) {
            if (info.id == depId) {
                name = info.name.isEmpty() ? depId : info.name;
                break;
            }
        }
        if (!m_dependencyDownloadProgress.isEmpty()) {
            refreshDependencyDownloadStatus();
            return;
        }

        setStatusHint(tr("下载完成: %1").arg(name));
    });
    connect(&depManager, &DependencyManager::downloadFailed, this, [this](const QString &depId, const QString &error) {
        m_dependencyDownloadProgress.remove(depId);
        if (!m_dependencyDownloadProgress.isEmpty()) {
            refreshDependencyDownloadStatus();
        }
        setStatusHint(tr("依赖下载失败: %1").arg(error));
        QMessageBox::warning(this, tr("依赖下载失败"), error);
    });
    connect(&depManager, &DependencyManager::downloadProgress, this, [this](const QString &depId, qint64 received, qint64 total) {
        DownloadProgressInfo &info = m_dependencyDownloadProgress[depId];
        info.received = qMax<qint64>(0, received);
        info.total = qMax<qint64>(0, total);
        refreshDependencyDownloadStatus();
    });

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

void MainWindow::initializeDependencies()
{
    QDir runtimeDir(QDir::currentPath());
    runtimeDir.mkpath("models/whisper");
    runtimeDir.mkpath("models/LLM");

    const QString appDir = QCoreApplication::applicationDirPath();
    const QString appPath = QDir(appDir).filePath("resources/dependencies.json");
    const QString cwdPath = QDir::currentPath() + "/resources/dependencies.json";
    const QString resourcePath = ":/resources/dependencies.json";

    QString resolvedPath;
    if (QFileInfo::exists(appPath)) {
        resolvedPath = appPath;
    } else if (QFileInfo::exists(cwdPath)) {
        resolvedPath = cwdPath;
    } else if (QFile::exists(resourcePath)) {
        resolvedPath = resourcePath;
    }

    if (!resolvedPath.isEmpty()) {
        DependencyManager::instance().initialize(resolvedPath);
        
        const QString cachePath = QDir::currentPath() + "/.qsrottool_dep_cache";
        DependencyManager::instance().loadVersionCache(cachePath);
        
        setStatusHint(tr("依赖清单已加载"));
    } else {
        setStatusHint(tr("未找到依赖清单: resources/dependencies.json"));
        QMessageBox::warning(this, tr("依赖清单缺失"), tr("未找到 resources/dependencies.json，无法检查依赖更新。"));
    }
}

void MainWindow::triggerDependencyCheckOnce()
{
    if (m_dependencyAutoTriggered) {
        return;
    }

    m_dependencyAutoTriggered = true;
    DependencyManager::instance().checkForUpdates();
}

void MainWindow::setStatusHint(const QString &message)
{
    if (!ui || !ui->statusHintLabel) {
        return;
    }
    ui->statusHintLabel->setText(message);
}

QString MainWindow::dependencyDisplayName(const QString &depId) const
{
    const auto deps = DependencyManager::instance().getAllDependencies();
    for (const auto &info : deps) {
        if (info.id == depId) {
            return info.name.isEmpty() ? depId : info.name;
        }
    }

    return depId;
}

void MainWindow::refreshDependencyDownloadStatus()
{
    if (m_dependencyDownloadProgress.isEmpty()) {
        return;
    }

    QStringList taskSummaries;
    for (auto it = m_dependencyDownloadProgress.constBegin(); it != m_dependencyDownloadProgress.constEnd(); ++it) {
        const QString name = dependencyDisplayName(it.key());
        const qint64 received = it.value().received;
        const qint64 total = it.value().total;

        QString progressText;
        if (total > 0) {
            const int percent = qBound(0, static_cast<int>((received * 100) / total), 100);
            progressText = tr("%1% (%2/%3 MB)")
                               .arg(percent)
                               .arg(QString::number(received / 1024.0 / 1024.0, 'f', 1))
                               .arg(QString::number(total / 1024.0 / 1024.0, 'f', 1));
        } else {
            progressText = tr("已下载 %1 MB")
                               .arg(QString::number(received / 1024.0 / 1024.0, 'f', 1));
        }

        taskSummaries << tr("%1：%2").arg(name, progressText);
    }

    setStatusHint(tr("下载中（%1项）：%2")
                      .arg(m_dependencyDownloadProgress.size())
                      .arg(taskSummaries.join(tr(" | "))));
}

/// @brief 绑定侧边导航按钮的点击事件到页面切换逻辑
/// @details 记录按钮和页面的映射关系，当点击按钮时触发任务确认流程
void MainWindow::bindNavigationButton(QToolButton *button, QWidget *page, const QString &featureName)
{
    if (!button || !page) {
        return;
    }

    m_navToPage[button] = page;
    m_navFeatureNames[button] = featureName;

    connect(button, &QToolButton::clicked, this, [this, page, featureName]() {
        requestPageSwitch(page, featureName);
    });
}

/// @brief 处理页面切换请求，支持任务确认弹窗和自动停止
/// @details 逻辑流程：
/// 1. 如已经在目标页面，直接返回
/// 2. 获取当前页面，判断是否有正在运行的任务
/// 3. 若无任务，直接切换
/// 4. 若有任务但用户已勾选过"不再提示"（程序运行期间永久生效），直接停止任务并切换
/// 5. 若有任务且未勾选跳过，弹窗询问用户
/// 6. 用户确认后停止当前页所有任务，切换到目标页
/// 7. 如果用户勾选"不再提示"，后续整个程序运行期间都不再弹窗
void MainWindow::requestPageSwitch(QWidget *targetPage, const QString &featureName)
{
    if (!ui || !ui->mainStackedWidget || !targetPage) {
        return;
    }

    if (ui->mainStackedWidget->currentWidget() == targetPage) {
        return;
    }

    QWidget *currentPage = ui->mainStackedWidget->currentWidget();
    if (!currentPage) {
        ui->mainStackedWidget->setCurrentWidget(targetPage);
        syncNavigationSelection(targetPage);
        return;
    }

    const bool hasTasks = hasActiveTasksOnPage(currentPage);
    
    // 如果当前页面没有任务，直接切换
    if (!hasTasks) {
        ui->mainStackedWidget->setCurrentWidget(targetPage);
        syncNavigationSelection(targetPage);
        return;
    }

    // 有任务但用户已勾选"不再提示"（程序运行期间永久生效），直接停止任务并切换
    if (m_skipPromptForCurrentTask) {
        stopAllTasksOnPage(currentPage);
        ui->mainStackedWidget->setCurrentWidget(targetPage);
        syncNavigationSelection(targetPage);
        return;
    }

    // 有任务且需要提示，显示确认对话框
    PageSwitchConfirmDialog dialog(this);
    dialog.setTargetName(featureName);

    const int result = dialog.exec();
    if (result != QDialog::Accepted) {
        syncNavigationSelection(currentPage);
        return;
    }

    if (dialog.skipPromptForCurrentTask()) {
        m_skipPromptForCurrentTask = true;
    }

    stopAllTasksOnPage(currentPage);
    ui->mainStackedWidget->setCurrentWidget(targetPage);
    syncNavigationSelection(targetPage);
}

/// @brief 判断指定页面上是否有运行中的任务
/// @details 根据页面类型调用对应的任务状态查询接口
bool MainWindow::hasActiveTasksOnPage(QWidget *page) const
{
    if (!page) {
        return false;
    }

    if (page == m_downloadPage && m_downloadPage) {
        return m_downloadPage->hasRunningTask();
    }

    if (page == m_loaderPage && m_loaderPage) {
        return m_loaderPage->hasRunningTask();
    }

    return DependencyManager::instance().isBusy();
}

/// @brief 停止指定页面上所有正在运行的任务
/// @details 根据页面类型调用对应的停止方法
void MainWindow::stopAllTasksOnPage(QWidget *page)
{
    if (!page) {
        return;
    }

    if (page == m_downloadPage && m_downloadPage) {
        m_downloadPage->stopAllTasks();
    }

    if (page == m_loaderPage && m_loaderPage) {
        m_loaderPage->stopAllTasks();
    }

    if (DependencyManager::instance().isBusy()) {
        DependencyManager::instance().cancelAllOperations();
    }
}

/// @brief 根据当前页面同步左侧导航按钮的选中状态
/// @details 确保切换后视觉状态与实际页面一致
void MainWindow::syncNavigationSelection(QWidget *page)
{
    if (!page) {
        return;
    }

    for (auto it = m_navToPage.constBegin(); it != m_navToPage.constEnd(); ++it) {
        QToolButton *button = it.key();
        if (!button) {
            continue;
        }

        if (it.value() == page) {
            button->setChecked(true);
            return;
        }
    }
}

