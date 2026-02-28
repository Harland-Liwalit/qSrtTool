#include "videodownloader.h"
#include "ui_videodownloader.h"
#include "videodownloadtaskrunner.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTimer>
#include <QTreeWidgetItem>
#include <QTransform>
#include <QToolButton>

#include "../../Core/dependencymanager.h"

namespace {
constexpr int RoleUrl = Qt::UserRole + 1;
constexpr int RoleStatus = Qt::UserRole + 2;
constexpr int RoleFormatId = Qt::UserRole + 3;
constexpr int RoleQualityId = Qt::UserRole + 4;
constexpr int RoleOutputDir = Qt::UserRole + 5;
constexpr int RoleLocalFilePath = Qt::UserRole + 6;
constexpr int RoleCookiePath = Qt::UserRole + 7;
constexpr int RoleCookieTemp = Qt::UserRole + 8;

const char *StatusPending = "pending";
const char *StatusRunning = "running";
const char *StatusCompleted = "completed";
const char *StatusFailed = "failed";
const char *StatusCanceled = "canceled";
}

VideoDownloader::VideoDownloader(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::VideoDownloader)
{
    ui->setupUi(this);

    setupDownloadUi();

    m_toolsBaseIcon = ui->toolsCheckButton->icon();
    m_toolsSpinTimer = new QTimer(this);
    m_toolsSpinTimer->setInterval(60);
    connect(m_toolsSpinTimer, &QTimer::timeout, this, &VideoDownloader::updateToolsSpinner);

    connect(ui->toolsCheckButton, &QToolButton::clicked, this, []() {
        DependencyManager::instance().checkForUpdates();
    });

    auto &manager = DependencyManager::instance();
    connect(&manager, &DependencyManager::busyChanged, this, &VideoDownloader::setToolsLoading);
}

VideoDownloader::~VideoDownloader()
{
    delete ui;
}

void VideoDownloader::setToolsLoading(bool loading)
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

void VideoDownloader::updateToolsSpinner()
{
    const QSize iconSize = ui->toolsCheckButton->iconSize();
    QPixmap pixmap = m_toolsBaseIcon.pixmap(iconSize);
    QTransform transform;
    transform.rotate(m_toolsSpinAngle);
    ui->toolsCheckButton->setIcon(QIcon(pixmap.transformed(transform, Qt::SmoothTransformation)));
    m_toolsSpinAngle = (m_toolsSpinAngle + 30) % 360;
}

bool VideoDownloader::hasRunningTask() const
{
    if (!m_runningTaskMap.isEmpty()) {
        return true;
    }

    if (!ui || !ui->downloadsTree) {
        return false;
    }

    const int count = ui->downloadsTree->topLevelItemCount();
    for (int i = 0; i < count; ++i) {
        QTreeWidgetItem *item = ui->downloadsTree->topLevelItem(i);
        if (!item) {
            continue;
        }

        if (item->data(0, RoleStatus).toString() == QString::fromLatin1(StatusPending)) {
            return true;
        }
    }

    return false;
}

void VideoDownloader::stopAllTasks()
{
    cancelAllPendingTasks();

    const auto runners = m_runningTaskMap.keys();
    for (VideoDownloadTaskRunner *runner : runners) {
        if (runner) {
            runner->cancelTask();
        }
    }

    refreshActionButtons();
}

void VideoDownloader::setupDownloadUi()
{
    if (!ui) {
        return;
    }

    if (ui->downloadsTree) {
        ui->downloadsTree->setColumnCount(6);
        ui->downloadsTree->setHeaderLabels(QStringList()
                                           << QStringLiteral("文件")
                                           << QStringLiteral("进度")
                                           << QStringLiteral("状态")
                                           << QStringLiteral("分辨率")
                                           << QStringLiteral("时长")
                                           << QStringLiteral("FPS"));
        ui->downloadsTree->setRootIsDecorated(false);
        ui->downloadsTree->setUniformRowHeights(true);
        ui->downloadsTree->setAlternatingRowColors(false);
        ui->downloadsTree->setSelectionMode(QAbstractItemView::SingleSelection);
        if (ui->downloadsTree->header()) {
            ui->downloadsTree->header()->setStretchLastSection(false);
            ui->downloadsTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
            ui->downloadsTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
            ui->downloadsTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
            ui->downloadsTree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
            ui->downloadsTree->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
            ui->downloadsTree->header()->setSectionResizeMode(5, QHeaderView::Fixed);
            ui->downloadsTree->header()->resizeSection(5, 56);
        }

        connect(ui->downloadsTree, &QTreeWidget::itemSelectionChanged, this, [this]() {
            refreshActionButtons();
        });
    }

    if (ui->outputLineEdit && ui->outputLineEdit->text().trimmed().isEmpty()) {
        ui->outputLineEdit->setText(defaultOutputDir());
    }

    if (ui->formatComboBox) {
        ui->formatComboBox->clear();
        ui->formatComboBox->addItem(QStringLiteral("自动最佳（推荐）"), QStringLiteral("best"));
        ui->formatComboBox->addItem(QStringLiteral("MP4（H.264 兼容）"), QStringLiteral("mp4"));
        ui->formatComboBox->addItem(QStringLiteral("MKV（高兼容封装）"), QStringLiteral("mkv"));
        ui->formatComboBox->addItem(QStringLiteral("仅音频 MP3"), QStringLiteral("audio_mp3"));
        ui->formatComboBox->addItem(QStringLiteral("仅音频 M4A"), QStringLiteral("audio_m4a"));
        ui->formatComboBox->addItem(QStringLiteral("仅音频 WAV"), QStringLiteral("audio_wav"));
    }

    if (ui->qualityComboBox) {
        ui->qualityComboBox->clear();
        ui->qualityComboBox->addItem(QStringLiteral("最佳"), QStringLiteral("best"));
        ui->qualityComboBox->addItem(QStringLiteral("2160p"), QStringLiteral("2160p"));
        ui->qualityComboBox->addItem(QStringLiteral("1440p"), QStringLiteral("1440p"));
        ui->qualityComboBox->addItem(QStringLiteral("1080p"), QStringLiteral("1080p"));
        ui->qualityComboBox->addItem(QStringLiteral("720p"), QStringLiteral("720p"));
        ui->qualityComboBox->addItem(QStringLiteral("480p"), QStringLiteral("480p"));
        ui->qualityComboBox->addItem(QStringLiteral("360p"), QStringLiteral("360p"));
    }

    setupCookieUi();

    if (ui->cancelButton) {
        ui->cancelButton->setEnabled(false);
    }

    appendLog(QStringLiteral("下载模块已就绪。"));

    connect(ui->pasteButton, &QPushButton::clicked, this, [this]() {
        enqueueUrlFromInput(true);
    });

    connect(ui->urlLineEdit, &QLineEdit::returnPressed, this, [this]() {
        enqueueUrlFromInput(false);
    });

    connect(ui->browseButton, &QPushButton::clicked, this, [this]() {
        const QString currentDir = ui->outputLineEdit ? ui->outputLineEdit->text() : QString();
        const QString selectedDir = QFileDialog::getExistingDirectory(this, tr("选择下载目录"), currentDir);
        if (!selectedDir.isEmpty() && ui->outputLineEdit) {
            ui->outputLineEdit->setText(selectedDir);
        }
    });

    connect(ui->downloadButton, &QPushButton::clicked, this, [this]() {
        enqueueUrlFromInput(false);

        if (!resolveNextPendingItem() && m_runningTaskMap.isEmpty()) {
            QMessageBox::information(this, tr("暂无任务"), tr("请先输入并保存一个下载 URL。"));
            return;
        }

        schedulePendingTasks();
    });

    connect(ui->cancelButton, &QPushButton::clicked, this, [this]() {
        cancelSelectedTask();
    });

    connect(ui->deleteButton, &QPushButton::clicked, this, [this]() {
        if (!ui || !ui->downloadsTree) {
            return;
        }

        QTreeWidgetItem *item = ui->downloadsTree->currentItem();
        if (!item) {
            QMessageBox::information(this, tr("未选择任务"), tr("请先在下载队列中选中一条任务。"));
            return;
        }

        if (item->data(0, RoleStatus).toString() == QString::fromLatin1(StatusRunning)) {
            QMessageBox::warning(this, tr("任务运行中"), tr("请先取消该任务，任务结束后再删除。"));
            return;
        }

        const int row = ui->downloadsTree->indexOfTopLevelItem(item);
        if (row < 0) {
            return;
        }

        const QString filePath = item->data(0, RoleLocalFilePath).toString().trimmed();
        bool deleteLocalFile = false;
        if (!filePath.isEmpty() && QFileInfo::exists(filePath)) {
            const auto answer = QMessageBox::question(
                this,
                tr("删除本地文件"),
                tr("检测到本地文件：\n%1\n\n是否同时删除本地文件？").arg(filePath),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No);
            deleteLocalFile = (answer == QMessageBox::Yes);
        }

        if (deleteLocalFile) {
            if (!QFile::remove(filePath)) {
                QMessageBox::warning(this, tr("删除失败"), tr("任务已删除，但本地文件删除失败：\n%1").arg(filePath));
            } else {
                appendLog(QStringLiteral("已删除本地文件：%1").arg(filePath));
            }
        }

        cleanupItemTempCookie(item);

        delete ui->downloadsTree->takeTopLevelItem(row);
        appendLog(QStringLiteral("已删除队列任务。"));
        refreshActionButtons();
    });

    refreshActionButtons();
}

void VideoDownloader::setupCookieUi()
{
    if (!ui || !ui->optionsGrid || m_cookieModeComboBox) {
        return;
    }

    const int cookieRow = ui->optionsGrid->rowCount();

    QLabel *cookieLabel = new QLabel(QStringLiteral("权限 Cookie"), this);
    ui->optionsGrid->addWidget(cookieLabel, cookieRow, 0);

    QWidget *cookieContainer = new QWidget(this);
    QHBoxLayout *cookieLayout = new QHBoxLayout(cookieContainer);
    cookieLayout->setContentsMargins(0, 0, 0, 0);
    cookieLayout->setSpacing(6);

    m_cookieModeComboBox = new QComboBox(cookieContainer);
    m_cookieModeComboBox->addItem(QStringLiteral("不使用"), QStringLiteral("none"));
    m_cookieModeComboBox->addItem(QStringLiteral("Cookie 文件"), QStringLiteral("file"));
    m_cookieModeComboBox->addItem(QStringLiteral("Cookie 文本"), QStringLiteral("text"));

    m_cookieInputLineEdit = new QLineEdit(cookieContainer);
    m_cookieBrowseButton = new QPushButton(QStringLiteral("浏览"), cookieContainer);
    m_cookiePasteButton = new QPushButton(QStringLiteral("粘贴文本"), cookieContainer);

    cookieLayout->addWidget(m_cookieModeComboBox);
    cookieLayout->addWidget(m_cookieInputLineEdit, 1);
    cookieLayout->addWidget(m_cookieBrowseButton);
    cookieLayout->addWidget(m_cookiePasteButton);

    ui->optionsGrid->addWidget(cookieContainer, cookieRow, 1);

    connect(m_cookieModeComboBox, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, [this](int) {
        updateCookieUiState();
    });

    connect(m_cookieBrowseButton, &QPushButton::clicked, this, [this]() {
        if (!m_cookieModeComboBox || !m_cookieInputLineEdit) {
            return;
        }

        const QString mode = m_cookieModeComboBox->currentData().toString();
        if (mode == QStringLiteral("file")) {
            const QString filePath = QFileDialog::getOpenFileName(
                this,
                tr("选择 Cookie 文件"),
                m_cookieInputLineEdit->text().trimmed(),
                tr("Cookie 文件 (*.txt *.cookies *.cookie);;所有文件 (*.*)"));
            if (!filePath.isEmpty()) {
                m_cookieInputLineEdit->setText(filePath);
            }
            return;
        }

        if (mode == QStringLiteral("text")) {
            bool ok = false;
            const QString text = QInputDialog::getMultiLineText(
                this,
                tr("编辑 Cookie 文本"),
                tr("粘贴 Netscape 格式 Cookie 文本："),
                m_cookieTextBuffer,
                &ok);
            if (ok) {
                m_cookieTextBuffer = text.trimmed();
                if (m_cookieTextBuffer.isEmpty()) {
                    m_cookieInputLineEdit->clear();
                } else {
                    const int lineCount = m_cookieTextBuffer.split('\n', Qt::SkipEmptyParts).size();
                    m_cookieInputLineEdit->setText(QStringLiteral("已设置文本 Cookie（%1 行）").arg(lineCount));
                }
            }
            return;
        }

        m_cookieInputLineEdit->clear();
        m_cookieTextBuffer.clear();
    });

    connect(m_cookiePasteButton, &QPushButton::clicked, this, [this]() {
        if (!m_cookieModeComboBox || !m_cookieInputLineEdit) {
            return;
        }

        const QString text = QApplication::clipboard()->text().trimmed();
        if (text.isEmpty()) {
            QMessageBox::information(this, tr("剪贴板为空"), tr("未检测到可用 Cookie 文本。"));
            return;
        }

        m_cookieModeComboBox->setCurrentIndex(m_cookieModeComboBox->findData(QStringLiteral("text")));
        m_cookieTextBuffer = text;
        const int lineCount = m_cookieTextBuffer.split('\n', Qt::SkipEmptyParts).size();
        m_cookieInputLineEdit->setText(QStringLiteral("已粘贴文本 Cookie（%1 行）").arg(lineCount));
    });

    updateCookieUiState();
}

void VideoDownloader::updateCookieUiState()
{
    if (!m_cookieModeComboBox || !m_cookieInputLineEdit || !m_cookieBrowseButton || !m_cookiePasteButton) {
        return;
    }

    const QString mode = m_cookieModeComboBox->currentData().toString();
    if (mode == QStringLiteral("file")) {
        m_cookieInputLineEdit->setPlaceholderText(QStringLiteral("选择 Cookie 文件（Netscape 格式）"));
        m_cookieInputLineEdit->setReadOnly(false);
        m_cookieBrowseButton->setText(QStringLiteral("浏览"));
        m_cookiePasteButton->setEnabled(true);
    } else if (mode == QStringLiteral("text")) {
        m_cookieInputLineEdit->setPlaceholderText(QStringLiteral("点击“浏览”可编辑文本，或直接点“粘贴文本”"));
        m_cookieInputLineEdit->setReadOnly(true);
        m_cookieBrowseButton->setText(QStringLiteral("编辑"));
        m_cookiePasteButton->setEnabled(true);
    } else {
        m_cookieInputLineEdit->setPlaceholderText(QStringLiteral("无需 Cookie 时保持此模式"));
        m_cookieInputLineEdit->clear();
        m_cookieTextBuffer.clear();
        m_cookieInputLineEdit->setReadOnly(true);
        m_cookieBrowseButton->setText(QStringLiteral("清空"));
        m_cookiePasteButton->setEnabled(false);
    }
}

void VideoDownloader::updateRunningStateUi(bool running)
{
    Q_UNUSED(running);
    refreshActionButtons();
}

void VideoDownloader::appendLog(const QString &line)
{
    if (!ui || !ui->logTextEdit || line.trimmed().isEmpty()) {
        return;
    }

    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    m_logHistory << QStringLiteral("[%1] %2").arg(timestamp, line);
    renderLogConsole();
}

void VideoDownloader::renderLogConsole()
{
    if (!ui || !ui->logTextEdit) {
        return;
    }

    QStringList lines = m_logHistory;

    // 活动任务行始终渲染在底部，效果对齐 Whisper 的“活动行 + 历史行”展示。
    if (ui->downloadsTree) {
        const int count = ui->downloadsTree->topLevelItemCount();
        for (int i = 0; i < count; ++i) {
            QTreeWidgetItem *item = ui->downloadsTree->topLevelItem(i);
            if (!item) {
                continue;
            }

            VideoDownloadTaskRunner *runner = runnerForItem(item);
            if (!runner) {
                continue;
            }

            const QString activeLine = m_activeTaskLogLines.value(runner).trimmed();
            if (!activeLine.isEmpty()) {
                lines << activeLine;
            }
        }
    }

    ui->logTextEdit->setPlainText(lines.join("\n"));
    if (ui->logTextEdit->verticalScrollBar()) {
        ui->logTextEdit->verticalScrollBar()->setValue(ui->logTextEdit->verticalScrollBar()->maximum());
    }
}

void VideoDownloader::enqueueUrlFromInput(bool fromClipboard)
{
    if (!ui || !ui->urlLineEdit) {
        return;
    }

    QString urlText = ui->urlLineEdit->text().trimmed();
    if (fromClipboard) {
        const QString clipboardText = QApplication::clipboard()->text().trimmed();
        if (!clipboardText.isEmpty()) {
            urlText = clipboardText;
            ui->urlLineEdit->setText(urlText);
        }
    }

    if (urlText.isEmpty()) {
        return;
    }

    QString cookiePath;
    bool cookieTempFile = false;
    QString cookieError;
    if (!resolveCookieSnapshotForQueue(&cookiePath, &cookieTempFile, &cookieError)) {
        QMessageBox::warning(this, tr("Cookie 配置无效"), cookieError);
        return;
    }

    QTreeWidgetItem *item = createQueueItem(urlText, cookiePath, cookieTempFile);
    if (ui->downloadsTree && item) {
        ui->downloadsTree->addTopLevelItem(item);
        ui->downloadsTree->setCurrentItem(item);
        appendLog(QStringLiteral("已加入下载队列：%1").arg(urlText));
        ui->urlLineEdit->clear();
        refreshActionButtons();

        // 入队后立即尝试调度：若当前并发未满，应立刻启动该任务而非停留在“待下载”。
        schedulePendingTasks();
    }
}

QTreeWidgetItem *VideoDownloader::createQueueItem(const QString &url, const QString &cookiePath, bool cookieTempFile)
{
    if (url.trimmed().isEmpty()) {
        return nullptr;
    }

    QTreeWidgetItem *item = new QTreeWidgetItem();
    item->setText(0, url);
    item->setText(1, QStringLiteral("0%"));
    item->setText(2, QStringLiteral("待下载"));
    item->setText(3, QStringLiteral("--"));
    item->setText(4, QStringLiteral("--"));
    item->setText(5, QStringLiteral("--"));
    item->setData(0, RoleUrl, url);
    item->setData(0, RoleStatus, QString::fromLatin1(StatusPending));
    item->setData(0, RoleFormatId, formatIdFromUi());
    item->setData(0, RoleQualityId, qualityIdFromUi());
    item->setData(0, RoleOutputDir, ui && ui->outputLineEdit ? ui->outputLineEdit->text().trimmed() : defaultOutputDir());
    item->setData(0, RoleCookiePath, cookiePath);
    item->setData(0, RoleCookieTemp, cookieTempFile);
    return item;
}

QTreeWidgetItem *VideoDownloader::resolveNextPendingItem() const
{
    if (!ui || !ui->downloadsTree) {
        return nullptr;
    }

    QTreeWidgetItem *selected = ui->downloadsTree->currentItem();
    if (selected && selected->data(0, RoleStatus).toString() == QString::fromLatin1(StatusPending)) {
        return selected;
    }

    const int count = ui->downloadsTree->topLevelItemCount();
    for (int i = 0; i < count; ++i) {
        QTreeWidgetItem *item = ui->downloadsTree->topLevelItem(i);
        if (!item) {
            continue;
        }

        if (item->data(0, RoleStatus).toString() == QString::fromLatin1(StatusPending)) {
            return item;
        }
    }

    return nullptr;
}

bool VideoDownloader::buildRequestForItem(QTreeWidgetItem *item, QString *errorMessage)
{
    if (!item) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("没有可执行的下载任务。");
        }
        return false;
    }

    const QString outputDir = item->data(0, RoleOutputDir).toString().trimmed();
    if (outputDir.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("请先选择保存目录。");
        }
        return false;
    }

    QDir().mkpath(outputDir);
    if (!QFileInfo(outputDir).exists()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("保存目录不可用，请重新选择。");
        }
        return false;
    }

    VideoDownloadRequest request;
    request.url = item->data(0, RoleUrl).toString().trimmed();
    request.outputDirectory = outputDir;
    request.formatId = item->data(0, RoleFormatId).toString().trimmed();
    request.qualityId = item->data(0, RoleQualityId).toString().trimmed();
    request.cookieFilePath = item->data(0, RoleCookiePath).toString().trimmed();

    if (request.formatId.isEmpty()) {
        request.formatId = QStringLiteral("best");
    }
    if (request.qualityId.isEmpty()) {
        request.qualityId = QStringLiteral("best");
    }

    if (request.url.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("队列中的 URL 为空，请重新添加。");
        }
        return false;
    }

    item->setData(0, RoleStatus, QString::fromLatin1(StatusRunning));
    setItemStatus(item, QStringLiteral("0%"), QStringLiteral("准备中"));
    appendLog(QStringLiteral("开始下载：%1").arg(request.url));

    VideoDownloadTaskRunner *runner = new VideoDownloadTaskRunner(this);
    m_runningTaskMap.insert(runner, item);

    connect(runner, &VideoDownloadTaskRunner::taskStarted, this, [this, runner]() {
        QTreeWidgetItem *taskItem = m_runningTaskMap.value(runner, nullptr);
        if (!taskItem) {
            return;
        }

        m_runnerProgressPercent[runner] = 0;
        m_runnerSpeedText[runner] = QStringLiteral("--");
        refreshActiveTaskLogLine(runner);

        setItemStatus(taskItem, QStringLiteral("0%"), QStringLiteral("下载中"));
        appendLog(QStringLiteral("任务已启动：%1").arg(taskItem->data(0, RoleUrl).toString()));
    });

    connect(runner, &VideoDownloadTaskRunner::taskLog, this, [this, runner](const QString &line) {
        const QString speedText = extractSpeedText(line);
        if (!speedText.isEmpty()) {
            m_runnerSpeedText[runner] = speedText;
            refreshActiveTaskLogLine(runner);
        }
    });

    connect(runner, &VideoDownloadTaskRunner::progressChanged, this, [this, runner](int percent) {
        QTreeWidgetItem *taskItem = m_runningTaskMap.value(runner, nullptr);
        if (!taskItem) {
            return;
        }

        m_runnerProgressPercent[runner] = percent;
        refreshActiveTaskLogLine(runner);
        setItemStatus(taskItem, QStringLiteral("%1%").arg(percent), QStringLiteral("下载中"));
    });

    connect(runner, &VideoDownloadTaskRunner::destinationResolved, this, [this, runner](const QString &filePath) {
        QTreeWidgetItem *taskItem = m_runningTaskMap.value(runner, nullptr);
        if (!taskItem || filePath.trimmed().isEmpty()) {
            return;
        }

        const QFileInfo info(filePath.trimmed());
        if (!info.fileName().isEmpty()) {
            taskItem->setText(0, info.fileName());
        }
        taskItem->setData(0, RoleLocalFilePath, info.absoluteFilePath());
    });

    connect(runner, &VideoDownloadTaskRunner::metadataResolved, this,
            [this, runner](const QString &resolution, const QString &duration, const QString &fps) {
        QTreeWidgetItem *taskItem = m_runningTaskMap.value(runner, nullptr);
        if (!taskItem) {
            return;
        }

        taskItem->setText(3, resolution.isEmpty() ? QStringLiteral("--") : resolution);
        taskItem->setText(4, duration.isEmpty() ? QStringLiteral("--") : duration);
        taskItem->setText(5, fps.isEmpty() ? QStringLiteral("--") : fps);
        refreshActiveTaskLogLine(runner);
    });

    connect(runner, &VideoDownloadTaskRunner::taskFinished, this,
            [this, runner](bool success, bool canceled, const QString &message) {
        QTreeWidgetItem *taskItem = m_runningTaskMap.take(runner);

        if (taskItem) {
            if (success) {
                setItemStatus(taskItem, QStringLiteral("100%"), QStringLiteral("完成"));
                taskItem->setData(0, RoleStatus, QString::fromLatin1(StatusCompleted));
            } else if (canceled) {
                setItemStatus(taskItem, taskItem->text(1), QStringLiteral("已取消"));
                taskItem->setData(0, RoleStatus, QString::fromLatin1(StatusCanceled));
            } else {
                setItemStatus(taskItem, taskItem->text(1), QStringLiteral("失败"));
                taskItem->setData(0, RoleStatus, QString::fromLatin1(StatusFailed));
            }

            const int cleanedCount = cleanupIntermediateFilesForItem(taskItem);
            if (cleanedCount > 0) {
                appendLog(QStringLiteral("已自动清理 %1 个中间文件。")
                              .arg(cleanedCount));
            }

            cleanupItemTempCookie(taskItem);
        }

        clearActiveTaskLogLine(runner);
        appendLog(message);
        runner->deleteLater();
        refreshActionButtons();

        // 有任务结束后自动补位：拉起下一个排队任务。
        schedulePendingTasks();
    });

    refreshActionButtons();
    runner->startTask(request);
    return true;
}

QString VideoDownloader::formatIdFromUi() const
{
    if (!ui || !ui->formatComboBox) {
        return QStringLiteral("best");
    }

    const QString value = ui->formatComboBox->currentData().toString().trimmed();
    return value.isEmpty() ? QStringLiteral("best") : value;
}

QString VideoDownloader::qualityIdFromUi() const
{
    if (!ui || !ui->qualityComboBox) {
        return QStringLiteral("best");
    }

    const QString value = ui->qualityComboBox->currentData().toString().trimmed();
    return value.isEmpty() ? QStringLiteral("best") : value;
}

QString VideoDownloader::defaultOutputDir() const
{
    return QDir(QDir::currentPath()).filePath(QStringLiteral("output/downloads"));
}

bool VideoDownloader::resolveCookieSnapshotForQueue(QString *cookiePath, bool *cookieTempFile, QString *errorMessage)
{
    if (cookiePath) {
        cookiePath->clear();
    }
    if (cookieTempFile) {
        *cookieTempFile = false;
    }

    if (!m_cookieModeComboBox || !m_cookieInputLineEdit) {
        return true;
    }

    const QString mode = m_cookieModeComboBox->currentData().toString();
    if (mode == QStringLiteral("none")) {
        return true;
    }

    if (mode == QStringLiteral("file")) {
        const QString path = m_cookieInputLineEdit->text().trimmed();
        if (path.isEmpty() || !QFileInfo::exists(path)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("请选择一个有效的 Cookie 文件。") ;
            }
            return false;
        }

        if (cookiePath) {
            *cookiePath = path;
        }
        return true;
    }

    if (mode == QStringLiteral("text")) {
        const QString cookieText = m_cookieTextBuffer.trimmed();
        if (cookieText.isEmpty()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Cookie 文本为空，请粘贴或编辑后再入队。") ;
            }
            return false;
        }

        QString writeError;
        const QString tempFile = createCookieTempFile(cookieText, &writeError);
        if (tempFile.isEmpty()) {
            if (errorMessage) {
                *errorMessage = writeError.isEmpty() ? QStringLiteral("创建临时 Cookie 文件失败。") : writeError;
            }
            return false;
        }

        if (cookiePath) {
            *cookiePath = tempFile;
        }
        if (cookieTempFile) {
            *cookieTempFile = true;
        }
        return true;
    }

    return true;
}

QString VideoDownloader::createCookieTempFile(const QString &cookieText, QString *errorMessage) const
{
    const QString dirPath = QDir(QDir::currentPath()).filePath(QStringLiteral("temp/yt_cookie"));
    if (!QDir().mkpath(dirPath)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法创建临时 Cookie 目录。") ;
        }
        return QString();
    }

    const QString filePath = QDir(dirPath).filePath(
        QStringLiteral("cookie_%1.txt").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_hhmmss_zzz"))));

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法写入临时 Cookie 文件。") ;
        }
        return QString();
    }

    file.write(cookieText.toUtf8());
    file.close();
    return filePath;
}

void VideoDownloader::cleanupItemTempCookie(QTreeWidgetItem *item)
{
    if (!item) {
        return;
    }

    const bool isTemp = item->data(0, RoleCookieTemp).toBool();
    if (!isTemp) {
        return;
    }

    const QString path = item->data(0, RoleCookiePath).toString().trimmed();
    if (!path.isEmpty() && QFileInfo::exists(path)) {
        QFile::remove(path);
    }

    item->setData(0, RoleCookieTemp, false);
}

int VideoDownloader::cleanupIntermediateFilesForItem(QTreeWidgetItem *item)
{
    if (!item) {
        return 0;
    }

    const QString finalPath = item->data(0, RoleLocalFilePath).toString().trimmed();
    if (finalPath.isEmpty()) {
        return 0;
    }

    const QFileInfo finalInfo(finalPath);
    const QDir dir = finalInfo.absoluteDir();
    if (!dir.exists()) {
        return 0;
    }

    const QString fileName = finalInfo.fileName();
    const QString baseName = finalInfo.completeBaseName();

    QStringList nameFilters;
    nameFilters << (fileName + QStringLiteral(".part*"))
                << (fileName + QStringLiteral(".ytdl"))
                << (fileName + QStringLiteral(".temp"))
                << (fileName + QStringLiteral(".aria2"))
                << (fileName + QStringLiteral(".part-Frag*"))
                << (baseName + QStringLiteral(".f*.part*"))
                << (baseName + QStringLiteral(".f*.m4s"))
                << (baseName + QStringLiteral(".f*.ts"))
                << (baseName + QStringLiteral(".f*.mp4"))
                << (baseName + QStringLiteral(".f*.webm"))
                << (baseName + QStringLiteral(".f*.m4a"));

    QStringList fileCandidates;
    for (const QString &pattern : nameFilters) {
        const QStringList matches = dir.entryList(QStringList() << pattern, QDir::Files | QDir::NoSymLinks);
        for (const QString &name : matches) {
            const QString absPath = dir.absoluteFilePath(name);
            if (absPath.compare(finalPath, Qt::CaseInsensitive) == 0) {
                continue;
            }
            if (!fileCandidates.contains(absPath)) {
                fileCandidates << absPath;
            }
        }
    }

    int removed = 0;
    for (const QString &candidatePath : fileCandidates) {
        if (QFileInfo::exists(candidatePath) && QFile::remove(candidatePath)) {
            ++removed;
        }
    }

    return removed;
}

void VideoDownloader::setItemStatus(QTreeWidgetItem *item, const QString &progressText, const QString &statusText)
{
    if (!item) {
        return;
    }

    item->setText(1, progressText);
    item->setText(2, statusText);
}

void VideoDownloader::schedulePendingTasks()
{
    // 并发调度：最多同时启动 m_maxParallelTasks 个下载任务。
    while (m_runningTaskMap.size() < m_maxParallelTasks) {
        QTreeWidgetItem *pendingItem = takeOnePendingItem();
        if (!pendingItem) {
            break;
        }

        QString errorMessage;
        if (!buildRequestForItem(pendingItem, &errorMessage)) {
            setItemStatus(pendingItem, pendingItem->text(1), QStringLiteral("失败"));
            pendingItem->setData(0, RoleStatus, QString::fromLatin1(StatusFailed));
            appendLog(errorMessage);
        }
    }

    refreshActionButtons();
}

QTreeWidgetItem *VideoDownloader::takeOnePendingItem() const
{
    return resolveNextPendingItem();
}

void VideoDownloader::cancelAllPendingTasks()
{
    if (!ui || !ui->downloadsTree) {
        return;
    }

    const int count = ui->downloadsTree->topLevelItemCount();
    for (int i = 0; i < count; ++i) {
        QTreeWidgetItem *item = ui->downloadsTree->topLevelItem(i);
        if (!item) {
            continue;
        }

        if (item->data(0, RoleStatus).toString() == QString::fromLatin1(StatusPending)) {
            setItemStatus(item, item->text(1), QStringLiteral("已取消"));
            item->setData(0, RoleStatus, QString::fromLatin1(StatusCanceled));
            cleanupItemTempCookie(item);
        }
    }
}

void VideoDownloader::refreshActionButtons()
{
    if (!ui) {
        return;
    }

    if (ui->downloadButton) {
        ui->downloadButton->setEnabled(true);
    }
    if (ui->cancelButton) {
        bool cancellableSelected = false;
        if (ui->downloadsTree && ui->downloadsTree->currentItem()) {
            const QString status = ui->downloadsTree->currentItem()->data(0, RoleStatus).toString();
            cancellableSelected = (status == QString::fromLatin1(StatusRunning)
                                   || status == QString::fromLatin1(StatusPending));
        }
        ui->cancelButton->setEnabled(cancellableSelected);
    }

    if (ui->deleteButton) {
        const bool hasSelection = (ui->downloadsTree && ui->downloadsTree->currentItem());
        ui->deleteButton->setEnabled(hasSelection);
    }
}

void VideoDownloader::cancelSelectedTask()
{
    if (!ui || !ui->downloadsTree) {
        return;
    }

    QTreeWidgetItem *item = ui->downloadsTree->currentItem();
    if (!item) {
        QMessageBox::information(this, tr("未选择任务"), tr("请先在下载队列中选中一条任务。"));
        return;
    }

    const QString status = item->data(0, RoleStatus).toString();
    if (status == QString::fromLatin1(StatusPending)) {
        setItemStatus(item, item->text(1), QStringLiteral("已取消"));
        item->setData(0, RoleStatus, QString::fromLatin1(StatusCanceled));
        appendLog(QStringLiteral("已取消排队任务：%1").arg(item->data(0, RoleUrl).toString()));
        refreshActionButtons();
        return;
    }

    if (status == QString::fromLatin1(StatusRunning)) {
        VideoDownloadTaskRunner *runner = runnerForItem(item);
        if (runner) {
            runner->cancelTask();
        }
        return;
    }

    QMessageBox::information(this, tr("无法取消"), tr("当前任务状态不可取消。"));
}

VideoDownloadTaskRunner *VideoDownloader::runnerForItem(QTreeWidgetItem *item) const
{
    if (!item) {
        return nullptr;
    }

    for (auto it = m_runningTaskMap.constBegin(); it != m_runningTaskMap.constEnd(); ++it) {
        if (it.value() == item) {
            return it.key();
        }
    }

    return nullptr;
}

QString VideoDownloader::extractSpeedText(const QString &rawLine) const
{
    const QString line = rawLine.trimmed();
    if (line.isEmpty()) {
        return QString();
    }

    static const QRegularExpression speedRegex(QStringLiteral("\\bat\\s+([^\\s]+(?:/s|ps))"),
                                               QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = speedRegex.match(line);
    if (match.hasMatch()) {
        return match.captured(1).trimmed();
    }

    static const QRegularExpression altSpeedRegex(QStringLiteral("([^\\s]+(?:B/s|iB/s|Bps|bps))"),
                                                  QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch altMatch = altSpeedRegex.match(line);
    if (altMatch.hasMatch()) {
        return altMatch.captured(1).trimmed();
    }

    return QString();
}

void VideoDownloader::refreshActiveTaskLogLine(VideoDownloadTaskRunner *runner)
{
    if (!runner) {
        return;
    }

    QTreeWidgetItem *taskItem = m_runningTaskMap.value(runner, nullptr);
    if (!taskItem) {
        return;
    }

    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    const QString fileText = taskItem->text(0).trimmed().isEmpty() ? taskItem->data(0, RoleUrl).toString() : taskItem->text(0).trimmed();
    const QString progressText = QStringLiteral("%1%").arg(qBound(0, m_runnerProgressPercent.value(runner, 0), 100));
    const QString speedText = m_runnerSpeedText.value(runner, QStringLiteral("--"));
    const QString statusText = taskItem->text(2).trimmed().isEmpty() ? QStringLiteral("下载中") : taskItem->text(2).trimmed();

    const QString metaText = QStringLiteral("%1 | %2 | %3fps")
                                 .arg(taskItem->text(3).trimmed().isEmpty() ? QStringLiteral("--") : taskItem->text(3).trimmed())
                                 .arg(taskItem->text(4).trimmed().isEmpty() ? QStringLiteral("--") : taskItem->text(4).trimmed())
                                 .arg(taskItem->text(5).trimmed().isEmpty() ? QStringLiteral("--") : taskItem->text(5).trimmed());

    m_activeTaskLogLines[runner] = QStringLiteral("[%1] %2 | %3 | 速度 %4 | %5 | %6")
                                       .arg(timestamp, fileText, progressText, speedText, statusText, metaText);
    renderLogConsole();
}

void VideoDownloader::clearActiveTaskLogLine(VideoDownloadTaskRunner *runner)
{
    if (!runner) {
        return;
    }

    m_activeTaskLogLines.remove(runner);
    m_runnerSpeedText.remove(runner);
    m_runnerProgressPercent.remove(runner);
    renderLogConsole();
}
