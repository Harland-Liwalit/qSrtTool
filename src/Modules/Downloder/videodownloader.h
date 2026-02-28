#ifndef VIDEODOWNLOADER_H
#define VIDEODOWNLOADER_H

#include <QWidget>
#include <QIcon>
#include <QHash>
#include <QStringList>

class QTimer;
class QTreeWidgetItem;
class VideoDownloadTaskRunner;
class QComboBox;
class QLineEdit;
class QPushButton;

namespace Ui {
class VideoDownloader;
}

class VideoDownloader : public QWidget
{
    Q_OBJECT

public:
    explicit VideoDownloader(QWidget *parent = nullptr);
    ~VideoDownloader();

    /// @brief 当前页面是否有下载任务在运行
    bool hasRunningTask() const;

    /// @brief 停止当前页面的下载任务
    void stopAllTasks();

private:
    Ui::VideoDownloader *ui;
    QTimer *m_toolsSpinTimer = nullptr;
    QIcon m_toolsBaseIcon;
    int m_toolsSpinAngle = 0;
    bool m_toolsLoading = false;
    int m_maxParallelTasks = 5;
    QHash<VideoDownloadTaskRunner *, QTreeWidgetItem *> m_runningTaskMap;
    QStringList m_logHistory;
    QHash<VideoDownloadTaskRunner *, QString> m_activeTaskLogLines;
    QHash<VideoDownloadTaskRunner *, QString> m_runnerSpeedText;
    QHash<VideoDownloadTaskRunner *, int> m_runnerProgressPercent;
    QComboBox *m_cookieModeComboBox = nullptr;
    QLineEdit *m_cookieInputLineEdit = nullptr;
    QPushButton *m_cookieBrowseButton = nullptr;
    QPushButton *m_cookiePasteButton = nullptr;
    QString m_cookieTextBuffer;

    void setToolsLoading(bool loading);
    void updateToolsSpinner();

    /// @brief 初始化下载页面 UI 行为与信号绑定
    void setupDownloadUi();
    void setupCookieUi();
    void updateCookieUiState();
    /// @brief 设置页面运行态
    void updateRunningStateUi(bool running);

    /// @brief 追加一行日志到日志框
    void appendLog(const QString &line);
    /// @brief 重新渲染日志框（历史在上、活动任务行在下）
    void renderLogConsole();
    /// @brief 将输入框中的 URL 加入下载队列
    void enqueueUrlFromInput(bool fromClipboard);
    /// @brief 创建队列项
    QTreeWidgetItem *createQueueItem(const QString &url, const QString &cookiePath, bool cookieTempFile);
    /// @brief 获取第一个待下载队列项（优先当前选中项）
    QTreeWidgetItem *resolveNextPendingItem() const;

    /// @brief 从界面组合下载请求
    bool buildRequestForItem(QTreeWidgetItem *item, QString *errorMessage);
    QString formatIdFromUi() const;
    QString qualityIdFromUi() const;
    QString defaultOutputDir() const;
    bool resolveCookieSnapshotForQueue(QString *cookiePath, bool *cookieTempFile, QString *errorMessage);
    QString createCookieTempFile(const QString &cookieText, QString *errorMessage) const;
    void cleanupItemTempCookie(QTreeWidgetItem *item);
    int cleanupIntermediateFilesForItem(QTreeWidgetItem *item);

    /// @brief 更新队列项可视状态
    void setItemStatus(QTreeWidgetItem *item, const QString &progressText, const QString &statusText);

    /// @brief 取消当前选中的任务（运行中或待下载）
    void cancelSelectedTask();
    /// @brief 根据队列项查找正在执行该任务的 runner
    VideoDownloadTaskRunner *runnerForItem(QTreeWidgetItem *item) const;
    /// @brief 解析 yt-dlp 输出中的速度文本
    QString extractSpeedText(const QString &rawLine) const;
    /// @brief 刷新某个运行任务在日志中的活动行
    void refreshActiveTaskLogLine(VideoDownloadTaskRunner *runner);
    /// @brief 清理任务对应的活动日志状态
    void clearActiveTaskLogLine(VideoDownloadTaskRunner *runner);

    /// @brief 启动并发调度，尽可能补满并发槽位
    void schedulePendingTasks();
    /// @brief 获取一个可启动的待处理队列项
    QTreeWidgetItem *takeOnePendingItem() const;
    /// @brief 标记所有待处理任务为取消
    void cancelAllPendingTasks();
    /// @brief 更新按钮状态（基于当前运行/排队任务）
    void refreshActionButtons();
};

#endif // VIDEODOWNLOADER_H
