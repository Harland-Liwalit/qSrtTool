#ifndef VIDEOLOADER_H
#define VIDEOLOADER_H

#include <QWidget>

class QObject;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QMimeData;
class EmbeddedFfmpegPlayer;

namespace Ui {
class VideoLoader;
}

/**
 * @class VideoLoader
 * @brief 视频导入和加载模块（VideoLoader模块的主入口）
 * 
 * 职责：
 * 1. 管理视频导入（文件对话框或拖拽导入）
 * 2. 使用EmbeddedFfmpegPlayer播放视频
 * 3. 提供视频播放界面和控制
 * 4. 发出播放状态消息供主窗口显示
 */
class VideoLoader : public QWidget
{
    Q_OBJECT

public:
    explicit VideoLoader(QWidget *parent = nullptr);
    ~VideoLoader();

signals:
    /// @brief 播放器状态消息信号
    /// @param message 状态信息文本
    void statusMessage(const QString &message);

protected:
    /// @brief 处理拖入事件
    void dragEnterEvent(QDragEnterEvent *event) override;

    /// @brief 处理拖移事件
    void dragMoveEvent(QDragMoveEvent *event) override;

    /// @brief 处理拖放事件
    void dropEvent(QDropEvent *event) override;

    /// @brief 事件过滤器 - 将UI上的拖放事件传递给相应处理函数
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    /// @brief 点击"导入视频"按钮的处理函数
    /// @details 打开文件对话框让用户选择视频文件
    void onImportVideoClicked();

private:
    /// @brief 加载并播放视频文件
    /// @param filePath 视频文件路径
    void loadVideo(const QString &filePath);

    /// @brief 从拖放的MIME数据中提取本地文件路径
    /// @param mimeData MIME数据对象
    /// @return 本地文件路径，如果没有有效的本地文件返回空字符串
    QString extractDroppedLocalFile(const QMimeData *mimeData) const;

private:
    Ui::VideoLoader *ui;                        ///< UI对象指针
    EmbeddedFfmpegPlayer *m_player = nullptr;   ///< 嵌入式FFmpeg播放器实例
};

#endif // VIDEOLOADER_H
