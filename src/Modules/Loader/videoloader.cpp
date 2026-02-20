#include "videoloader.h"
#include "ui_videoloader.h"

#include "embeddedffmpegplayer.h"

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QDir>
#include <QFileDialog>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include "../../Core/dependencymanager.h"

/// @brief VideoLoader构造函数 - 初始化视频导入界面
/// 
/// 初始化流程：
/// 1. 创建UI
/// 2. 启用拖放功能
/// 3. 创建EmbeddedFfmpegPlayer实例并嵌入到占位符中
/// 4. 连接播放器信号到UI更新和事件处理
/// 5. 连接依赖管理器信号处理FFmpeg下载
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

    m_player = new EmbeddedFfmpegPlayer(ui->videoPlaceholder);
    placeholderLayout->addWidget(m_player);

    connect(ui->importVideoButton, &QPushButton::clicked, this, &VideoLoader::onImportVideoClicked);
    connect(m_player, &EmbeddedFfmpegPlayer::playbackError, this, [this](const QString &reason) {
        QMessageBox::warning(this, tr("播放失败"), reason);
        emit statusMessage(tr("播放失败: %1").arg(reason));
    });
    connect(m_player, &EmbeddedFfmpegPlayer::statusMessage, this, &VideoLoader::statusMessage);
    connect(m_player, &EmbeddedFfmpegPlayer::ffmpegMissing, this, [this]() {
        const auto result = QMessageBox::question(
            this,
            tr("缺少 FFmpeg"),
            tr("未找到 ffmpeg.exe，是否下载 FFmpeg 到 deps 目录？"));
        if (result == QMessageBox::Yes) {
            const QString savePath = QDir::currentPath() + "/deps/ffmpeg.zip";
            DependencyManager::instance().downloadUpdate("ffmpeg", savePath);
            emit statusMessage(tr("正在下载 FFmpeg..."));
        }
    });

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
    // 处理拖入事件 - 验证拖入的数据是否包含本地视频文件
    // 流程：
    // 1. 检查MIME数据中是否有URL
    // 2. 提取本地文件路径
    // 3. 如果包含有效文件，接受拖放操作并改变UI视觉反馈
    if (!event || !event->mimeData()) {
        return;
    }

    if (!extractDroppedLocalFile(event->mimeData()).isEmpty()) {
        event->acceptProposedAction();
    }
}

void VideoLoader::dropEvent(QDropEvent *event)
{
    // 处理拖放事件 - 用户松开鼠标时加载拖入的视频文件
    // 流程：
    // 1. 从MIME数据中提取文件路径
    // 2. 验证文件有效性
    // 3. 调用loadVideo()加载视频
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
    // 处理拖移事件 - 当鼠标在窗口上移动时维持视觉反馈
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

/// @brief 点击"导入视频"按钮的处理函数
/// 
/// 流程：
/// 1. 打开文件对话框，用户选择视频文件
/// 2. 获取选定的文件路径
/// 3. 调用loadVideo()加载视频
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
    // 加载并播放视频文件
    // 调用链：onImportVideoClicked()/dropEvent() -> loadVideo() -> m_player->loadVideo()
    // 
    // 流程：
    // 1. 检查m_player是否有效
    // 2. 调用EmbeddedFfmpegPlayer::loadVideo()加载视频
    // 3. 加载成功后自动开始播放
    if (!m_player) {
        return;
    }

    emit statusMessage(tr("已选择视频，准备加载..."));
    if (!m_player->loadVideo(filePath)) {
        return;
    }
    m_player->playPause();
}

/// @brief 从拖放的MIME数据中提取本地文件路径
/// 
/// 流程：
/// 1. 检查MIME数据中是否包含URL列表
/// 2. 遍历所有URL，寻找本地文件URL
/// 3. 返回第一个本地文件路径
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
