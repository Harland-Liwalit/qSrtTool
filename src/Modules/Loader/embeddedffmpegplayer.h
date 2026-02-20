#ifndef EMBEDDEDFFMPEGPLAYER_H
#define EMBEDDEDFFMPEGPLAYER_H

#include <QWidget>
#include <QElapsedTimer>
#include <QProcess>

class QByteArray;
class QAudioFormat;
class QAudioOutput;
class QIODevice;

class QLabel;
class QPushButton;
class QSlider;
class QTimer;

/**
 * @class EmbeddedFfmpegPlayer
 * @brief 嵌入式FFmpeg视频播放器
 * 
 * 使用FFmpeg库进行视频解码和播放，支持拖拽导入、键盘快捷键、进度条拖拽等功能。
 * 提供完整的播放控制接口和信号通知机制。
 */
class EmbeddedFfmpegPlayer : public QWidget
{
    Q_OBJECT

public:
    explicit EmbeddedFfmpegPlayer(QWidget *parent = nullptr);
    ~EmbeddedFfmpegPlayer() override;

    /// @brief 加载视频文件
    /// @param filePath 视频文件的绝对路径
    /// @return 成功加载返回true，否则false（会发出ffmpegMissing或playbackError信号）
    bool loadVideo(const QString &filePath);

    /// @brief 获取当前加载的视频文件路径
    /// @return 完整路径，如果未加载返回空字符串
    QString currentFilePath() const;

    /// @brief 当前是否处于播放中状态
    /// @return 正在播放返回true，否则false
    bool isPlaying() const;

public slots:
    /// @brief 播放/暂停切换
    /// @details 如果当前未播放则开始从当前位置播放；如果正在播放则暂停
    void playPause();

    /// @brief 停止播放
    /// @details 停止当前播放并终止当前FFmpeg子进程，清空缓冲状态（不销毁QProcess对象）
    void stopPlayback();

    /// @brief 快进10秒
    void seekForward();

    /// @brief 快退10秒
    void seekBackward();

signals:
    /// @brief 状态消息信号
    /// @param message 状态消息文本
    void statusMessage(const QString &message);

    /// @brief 播放错误信号
    /// @param reason 错误原因说明
    void playbackError(const QString &reason);

    /// @brief FFmpeg缺失信号
    /// @details 系统找不到ffmpeg.exe时发出
    void ffmpegMissing();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    /// @brief FFmpeg解码进程完成时的回调
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

    /// @brief 进度刷新定时器触发的回调（150ms）
    void onProgressTick();

    /// @brief 进度条被按下的回调
    void onSliderPressed();

    /// @brief 进度条被释放的回调
    void onSliderReleased();

    /// @brief FFmpeg标准输出就绪的回调（接收视频帧数据）
    void onDecoderOutputReady();

    /// @brief FFmpeg标准错误输出就绪的回调
    void onDecoderErrorReady();

    /// @brief 音频解码标准输出就绪回调（接收PCM数据）
    void onAudioOutputReady();

    /// @brief 音频解码标准错误输出就绪回调
    void onAudioErrorReady();

    /// @brief 音量滑块变化回调（0-100）
    void onVolumeChanged(int value);

    /// @brief 音频解码进程结束回调
    void onAudioProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    /// @brief 从当前位置确定性启动播放
    /// @return 启动成功返回true，否则false
    bool beginPlaybackFromCurrentPosition();
    /// @brief 创建FFmpeg解码进程
    void setupDecoderProcess();

    /// @brief 创建FFmpeg音频解码进程
    void setupAudioProcess();

    /// @brief 销毁FFmpeg解码进程
    void destroyDecoderProcess();

    /// @brief 销毁FFmpeg音频解码进程
    void destroyAudioProcess();

    /// @brief 从指定位置启动音频播放
    bool startAudioPlaybackAt(qint64 positionMs);

    /// @brief 将缓存的PCM数据写入音频输出设备
    void flushAudioBuffer();

    /// @brief 查找系统中的ffmpeg.exe可执行文件
    /// @return FFmpeg可执行文件路径，未找到返回空字符串
    QString resolveFfmpegPath() const;

    /// @brief 查找系统中的ffprobe.exe可执行文件
    /// @return FFprobe可执行文件路径，未找到返回空字符串
    QString resolveFfprobePath() const;

    /// @brief 从指定位置开始播放
    /// @param positionMs 起始播放位置（毫秒）
    /// @return 成功启动返回true，否则false
    bool startPlaybackAt(qint64 positionMs);

    /// @brief 跳转到指定时间位置并继续播放
    /// @param positionMs 目标位置（毫秒）
    /// @details 会先停止当前播放并更新位置，如果跳转前处于播放状态则恢复播放
    void seekTo(qint64 positionMs);

    /// @brief 刷新视频元数据（分辨率、帧率、时长等）
    /// @details 使用ffprobe.exe查询视频信息
    void refreshVideoMeta();

    /// @brief 更新UI显示（进度条和时间标签）
    void updateProgressUi();

    /// @brief 将播放位置转换为进度条滑块值
    /// @param positionMs 播放位置（毫秒）
    /// @return 滑块值（0-1000）
    int sliderFromPosition(qint64 positionMs) const;

    /// @brief 将进度条滑块值转换为播放位置
    /// @param sliderValue 滑块值（0-1000）
    /// @return 播放位置（毫秒）
    qint64 positionFromSlider(int sliderValue) const;

    /// @brief 将毫秒时间格式化为可读的时间字符串
    /// @param ms 时间（毫秒）
    /// @return 格式化字符串，如"02:34:56"或"02:34"
    QString formatTime(qint64 ms) const;

    /// @brief 检查文件是否为支持的视频格式
    /// @param filePath 文件路径
    /// @return 是支持的视频格式返回true
    bool isVideoFile(const QString &filePath) const;

    /// @brief 清空帧缓冲区
    void clearFrameBuffer();

    // UI控件指针
    QLabel *m_videoSurface = nullptr;           ///< 视频画面显示标签
    QPushButton *m_rewindButton = nullptr;      ///< 快退按钮
    QPushButton *m_playPauseButton = nullptr;   ///< 播放/暂停按钮
    QPushButton *m_forwardButton = nullptr;     ///< 快进按钮
    QLabel *m_volumeIcon = nullptr;             ///< 音量图标
    QSlider *m_volumeSlider = nullptr;          ///< 音量调节滑块
    QSlider *m_progressSlider = nullptr;        ///< 进度条滑块
    QLabel *m_timeLabel = nullptr;              ///< 时间标签

    // 解码和播放相关
    QProcess *m_ffmpegProcess = nullptr;        ///< FFmpeg解码进程
    QProcess *m_audioProcess = nullptr;         ///< FFmpeg音频解码进程
    QTimer *m_progressTimer = nullptr;          ///< 进度刷新定时器
    QElapsedTimer m_playbackClock;              ///< 播放计时器
    QAudioOutput *m_audioOutput = nullptr;      ///< Qt音频输出
    QIODevice *m_audioSinkDevice = nullptr;     ///< 音频输出设备
    QByteArray m_audioBuffer;                   ///< 音频PCM缓冲区
    int m_volumePercent = 80;                   ///< 当前音量百分比

    // 视频信息
    QString m_currentFilePath;                  ///< 当前加载的视频文件路径
    QString m_cachedFfmpegPath;                 ///< 缓存的ffmpeg.exe路径
    int m_srcVideoWidth = 0;                    ///< 原始视频宽度
    int m_srcVideoHeight = 0;                   ///< 原始视频高度
    double m_srcFps = 25.0;                     ///< 原始视频帧率
    int m_outputWidth = 0;                      ///< 输出帧的宽度
    int m_outputHeight = 0;                     ///< 输出帧的高度
    int m_frameBytes = 0;                       ///< 单帧数据字节数
    QByteArray m_frameBuffer;                   ///< 视频帧数据缓冲区
    qint64 m_decodedFrameCount = 0;             ///< 已解码帧数
    qint64 m_durationMs = 0;                    ///< 视频总时长（毫秒）
    qint64 m_positionMs = 0;                    ///< 当前播放位置（毫秒）
    qint64 m_startPositionMs = 0;               ///< 本次播放的起始位置（毫秒）
    
    // 播放状态标志
    bool m_isPlaying = false;                   ///< 是否正在播放
    bool m_userStopping = false;                ///< 用户主动停止标志
    bool m_wasPlayingBeforeScrub = false;       ///< 拖动进度条前是否在播放
};

#endif // EMBEDDEDFFMPEGPLAYER_H

