#include "videoloader.h"
#include "ui_videoloader.h"

#include "mediacontroller.h"

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QDir>
#include <QFileDialog>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QUrl>
#include <QVideoWidget>
#include <QVBoxLayout>

#include "../../Core/dependencymanager.h"

VideoLoader::VideoLoader(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::VideoLoader)
{
    ui->setupUi(this);

    setAcceptDrops(true);
    ui->videoPlaceholder->setAcceptDrops(true);
    ui->videoPlaceholder->installEventFilter(this);

    auto *placeholderLayout = new QVBoxLayout(ui->videoPlaceholder);
    placeholderLayout->setContentsMargins(0, 0, 0, 0);
    placeholderLayout->setSpacing(0);

    m_videoWidget = new QVideoWidget(ui->videoPlaceholder);
    placeholderLayout->addWidget(m_videoWidget);

    m_mediaController = new MediaController(this);
    m_mediaController->setVideoOutput(m_videoWidget);

    connect(ui->importVideoButton, &QPushButton::clicked, this, &VideoLoader::onImportVideoClicked);
    connect(m_mediaController, &MediaController::mediaLoadFailed, this, [this](const QString &reason) {
        if (reason.contains("ffplay.exe", Qt::CaseInsensitive)) {
            const auto result = QMessageBox::question(
                this,
                tr("缺少 FFmpeg"),
                tr("未找到 ffplay.exe，是否下载 FFmpeg 到 deps 目录？"));
            if (result == QMessageBox::Yes) {
                const QString savePath = QDir::currentPath() + "/deps/ffmpeg.zip";
                DependencyManager::instance().downloadUpdate("ffmpeg", savePath);
                emit statusMessage(tr("正在下载 FFmpeg..."));
                return;
            }
        }
        QMessageBox::warning(this, tr("加载失败"), reason);
        emit statusMessage(tr("加载失败: %1").arg(reason));
    });
    connect(m_mediaController, &MediaController::mediaStatusMessage, this, &VideoLoader::statusMessage);

    auto &depManager = DependencyManager::instance();
    connect(&depManager, &DependencyManager::downloadFinished, this, [this](const QString &depId, const QString &) {
        if (depId == QStringLiteral("ffmpeg")) {
            emit statusMessage(tr("FFmpeg 下载完成"));
        }
    });
    connect(&depManager, &DependencyManager::downloadFailed, this, [this](const QString &depId, const QString &error) {
        if (depId == QStringLiteral("ffmpeg")) {
            emit statusMessage(tr("FFmpeg 下载失败: %1").arg(error));
            QMessageBox::warning(this, tr("FFmpeg 下载失败"), error);
        }
    });

    ui->nextStepButton->setEnabled(false);
}

VideoLoader::~VideoLoader()
{
    delete ui;
}

void VideoLoader::dragEnterEvent(QDragEnterEvent *event)
{
    if (!event || !event->mimeData()) {
        return;
    }

    if (!extractDroppedLocalFile(event->mimeData()).isEmpty()) {
        event->acceptProposedAction();
    }
}

void VideoLoader::dropEvent(QDropEvent *event)
{
    if (!event || !event->mimeData()) {
        return;
    }

    const QString localFile = extractDroppedLocalFile(event->mimeData());
    if (localFile.isEmpty()) {
        return;
    }

    event->acceptProposedAction();
    loadVideo(localFile);
}

void VideoLoader::dragMoveEvent(QDragMoveEvent *event)
{
    if (!event || !event->mimeData()) {
        return;
    }

    if (!extractDroppedLocalFile(event->mimeData()).isEmpty()) {
        event->acceptProposedAction();
    }
}

bool VideoLoader::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == ui->videoPlaceholder && event) {
        switch (event->type()) {
        case QEvent::DragEnter:
            dragEnterEvent(static_cast<QDragEnterEvent *>(event));
            return true;
        case QEvent::Drop:
            dropEvent(static_cast<QDropEvent *>(event));
            return true;
        case QEvent::DragMove:
            dragMoveEvent(static_cast<QDragMoveEvent *>(event));
            return true;
        default:
            break;
        }
    }

    return QWidget::eventFilter(watched, event);
}

void VideoLoader::onImportVideoClicked()
{
    emit statusMessage(tr("正在选择视频..."));
    const QString filePath = QFileDialog::getOpenFileName(
        this,
        tr("选择视频文件"),
        QString(),
        tr("视频文件 (*.mp4 *.mkv *.avi *.mov *.wmv *.flv *.webm *.m4v);;所有文件 (*.*)"));

    if (filePath.isEmpty()) {
        emit statusMessage(tr("未选择视频"));
        return;
    }

    loadVideo(filePath);
}

void VideoLoader::loadVideo(const QString &filePath)
{
    if (!m_mediaController) {
        return;
    }

    emit statusMessage(tr("已选择视频，准备加载..."));
    if (!m_mediaController->loadVideo(filePath)) {
        return;
    }
    m_mediaController->play();
}

QString VideoLoader::extractDroppedLocalFile(const QMimeData *mimeData) const
{
    if (!mimeData || !mimeData->hasUrls()) {
        return QString();
    }

    const QList<QUrl> urls = mimeData->urls();
    for (const QUrl &url : urls) {
        if (url.isLocalFile()) {
            return url.toLocalFile();
        }
    }

    return QString();
}
