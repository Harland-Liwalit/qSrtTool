#ifndef WHISPERCOMMANDBUILDER_H
#define WHISPERCOMMANDBUILDER_H

#include <QString>
#include <QStringList>

// Forward declaration
struct ExecutableCapabilities;

/// @brief Whisper 命令行构造器
/// @details 根据参数规范化构建 whisper、ffmpeg 等命令行参数
class WhisperCommandBuilder
{
public:
    /// @brief 构造 FFmpeg 音频提取命令
    /// @param ffmpegPath FFmpeg 可执行文件路径
    /// @param inputPath 输入媒体文件路径
    /// @param startSeconds 开始时间（秒）
    /// @param durationSeconds 提取时长（秒）
    /// @param outputPath 输出音频文件路径
    /// @return FFmpeg 完整命令行参数
    static QStringList buildFfmpegExtractArgs(const QString &inputPath,
                                              double startSeconds,
                                              double durationSeconds,
                                              const QString &outputPath);

    /// @brief 构造 Whisper 转录命令
    /// @param modelPath 模型文件路径
    /// @param audioPath 输入音频文件路径
    /// @param outputBasePath 输出文件基路径（无扩展名）
    /// @param languageCode 语言代码（如 "zh"、"en"，空字符串为自动检测）
    /// @param useGpu 是否启用 GPU 加速
    /// @param threadCountHint 线程数提示（-1 为自动检测）
    /// @param capabilities 可选的能力检测结果；如果提供，将根据版本动态调整参数
    /// @return Whisper 完整命令行参数
    static QStringList buildWhisperTranscribeArgs(const QString &modelPath,
                                                  const QString &audioPath,
                                                  const QString &outputBasePath,
                                                  const QString &languageCode,
                                                  bool useGpu,
                                                  int threadCountHint = -1,
                                                  const ExecutableCapabilities *capabilities = nullptr);

    /// @brief 根据 UI 文本获取语言代码
    /// @param uiText UI 中选择的文本（如 "中文"、"English"）
    /// @return 语言代码（如 "zh"、"en"，未知返回空字符串）
    static QString languageCodeFromUiText(const QString &uiText);

    /// @brief 根据 UI 文本获取输出文件扩展名
    /// @param uiText 输出格式 UI 文本（如 "SRT"、"WebVTT"）
    /// @return 文件扩展名（如 "srt"、"vtt"、"txt"）
    static QString outputFileExtensionFromUiText(const QString &uiText);
};

#endif // WHISPERCOMMANDBUILDER_H
