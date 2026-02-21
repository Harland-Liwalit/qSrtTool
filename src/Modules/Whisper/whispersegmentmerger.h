#ifndef WHISPERSEGMENTMERGER_H
#define WHISPERSEGMENTMERGER_H

#include <QString>
#include <QStringList>

/// @brief Whisper 分段字幕合并器
/// @details 负责解析 SRT 时间戳、合并多个分段 SRT 文件、处理格式转换
class WhisperSegmentMerger
{
public:
    /// @brief SRT 输出格式类型
    enum OutputFormat {
        Format_SRT,
        Format_TXT,
        Format_TXT_Timestamped,
        Format_WebVTT
    };

    /// @brief 解析 SRT 时间戳
    /// @param text 时间戳字符串（格式：HH:MM:SS,mmm）
    /// @param milliseconds 输出毫秒数
    /// @return 解析成功返回 true
    static bool parseSrtTimestamp(const QString &text, qint64 &milliseconds);

    /// @brief 格式化为 SRT 时间戳
    /// @param milliseconds 毫秒数
    /// @return SRT 格式的时间戳字符串（HH:MM:SS,mmm）
    static QString formatSrtTimestamp(qint64 milliseconds);

    /// @brief 对 SRT 内容整体时间偏移
    /// @param srtContent 原始 SRT 内容
    /// @param offsetMs 偏移毫秒数（可为负）
    /// @return 时间偏移后的 SRT 内容
    static QString shiftedSrtContent(const QString &srtContent, qint64 offsetMs);

    /// @brief SRT 转换为纯文本
    /// @param srtContent SRT 格式内容
    /// @return 不含时间戳的纯文本
    static QString srtToPlainText(const QString &srtContent);

    /// @brief SRT 转换为带时间的文本
    /// @param srtContent SRT 格式内容
    /// @return [HH:MM:SS --> HH:MM:SS] 文本 格式
    static QString srtToTimestampedText(const QString &srtContent);

    /// @brief SRT 转换为 WebVTT
    /// @param srtContent SRT 格式内容
    /// @return WebVTT 格式内容
    static QString srtToWebVtt(const QString &srtContent);

    /// @brief 合并多个分段 SRT 文件
    /// @param segmentSrtFiles 分段 SRT 文件路径列表
    /// @param segmentDurationSeconds 每个分段的时长（秒）
    /// @param format 输出格式
    /// @return 合并后的内容，失败返回空字符串
    static QString mergeSegmentSrtFiles(const QStringList &segmentSrtFiles,
                                        double segmentDurationSeconds,
                                        OutputFormat format);
};

#endif // WHISPERSEGMENTMERGER_H
