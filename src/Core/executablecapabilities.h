#ifndef EXECUTABLECAPABILITIES_H
#define EXECUTABLECAPABILITIES_H

#include <QString>

/// @brief 可执行文件的能力与版本检测结果
/// 用于存储第三方软件的版本信息与功能支持状态
struct ExecutableCapabilities {
    // 基本信息
    QString name;                  // 软件名称（ffmpeg, whisper, yt-dlp）
    QString executablePath;        // 可执行文件路径
    QString version;               // 版本字符串（如 "1.5.4"）
    
    // 通用状态
    bool isAvailable = false;      // 是否可执行
    bool isSupported = false;      // 版本是否被当前代码支持
    QString unsupportedReason;     // 不支持的原因
    
    // Whisper 专用标志
    bool whisperSupportsGpu = false;           // 支持 -ng GPU 禁用标志
    bool whisperSupportsThreads = false;       // 支持 -t 多线程参数
    bool whisperSupportsLanguage = false;      // 支持 -l 语言参数
    
    // FFmpeg 专用标志
    bool ffmpegHasRtmp = false;                // 支持 RTMP 协议
    bool ffmpegHasHardwareAccel = false;       // 支持硬件加速
    
    // yt-dlp 专用标志
    bool ytDlpSupportsPlaylist = false;        // 支持播放列表下载
    bool ytDlpSupportsFragments = false;       // 支持分片合并
};

/// @brief 可执行文件能力检测器
/// 统一负责检测 ffmpeg、whisper、yt-dlp 等第三方软件的版本和功能支持
class ExecutableCapabilitiesDetector
{
public:
    /// @brief 检测 whisper 版本与能力
    static ExecutableCapabilities detectWhisper(const QString &execPath);

    /// @brief 检测 ffmpeg 版本与能力
    static ExecutableCapabilities detectFfmpeg(const QString &execPath);

    /// @brief 检测 yt-dlp 版本与能力
    static ExecutableCapabilities detectYtDlp(const QString &execPath);

private:
    /// @brief 执行命令并获取输出（带超时）
    static QString executeCommandWithTimeout(const QString &program, const QStringList &args, int timeoutMs = 3000);

    /// @brief 从版本字符串中提取版本号（支持 vX.Y.Z, X.Y.Z 等格式）
    static QString extractVersionNumber(const QString &versionOutput);
};

#endif // EXECUTABLECAPABILITIES_H
