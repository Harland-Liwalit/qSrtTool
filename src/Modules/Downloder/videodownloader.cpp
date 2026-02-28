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
#include <QHeaderView>
#include <QMessageBox>
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

        delete ui->downloadsTree->takeTopLevelItem(row);
        appendLog(QStringLiteral("已删除队列任务。"));
        refreshActionButtons();
    });

    refreshActionButtons();
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
    ui->logTextEdit->append(QStringLiteral("[%1] %2").arg(timestamp, line));
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

    QTreeWidgetItem *item = createQueueItem(urlText);
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

QTreeWidgetItem *VideoDownloader::createQueueItem(const QString &url)
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
        setItemStatus(taskItem, QStringLiteral("0%"), QStringLiteral("下载中"));
        appendLog(QStringLiteral("任务已启动：%1").arg(taskItem->data(0, RoleUrl).toString()));
    });

    connect(runner, &VideoDownloadTaskRunner::taskLog, this, [this](const QString &line) {
        appendLog(line);
    });

    connect(runner, &VideoDownloadTaskRunner::progressChanged, this, [this, runner](int percent) {
        QTreeWidgetItem *taskItem = m_runningTaskMap.value(runner, nullptr);
        if (!taskItem) {
            return;
        }
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
        }

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

    const int index = ui->formatComboBox->currentIndex();
    switch (index) {
    case 1:
        return QStringLiteral("mp4");
    case 2:
        return QStringLiteral("mkv");
    case 3:
        return QStringLiteral("audio_mp3");
    default:
        return QStringLiteral("best");
    }
}

QString VideoDownloader::qualityIdFromUi() const
{
    if (!ui || !ui->qualityComboBox) {
        return QStringLiteral("best");
    }

    const QString text = ui->qualityComboBox->currentText().trimmed().toLower();
    if (text.contains(QStringLiteral("1080"))) {
        return QStringLiteral("1080p");
    }
    if (text.contains(QStringLiteral("720"))) {
        return QStringLiteral("720p");
    }
    if (text.contains(QStringLiteral("480"))) {
        return QStringLiteral("480p");
    }
    return QStringLiteral("best");
}

QString VideoDownloader::defaultOutputDir() const
{
    return QDir(QDir::currentPath()).filePath(QStringLiteral("output/downloads"));
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
