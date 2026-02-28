#ifndef SUBTITLEBURNCOMMANDBUILDER_H
#define SUBTITLEBURNCOMMANDBUILDER_H

#include <QString>
#include <QStringList>

/// @brief 字幕烧录请求参数
/// @details 由 UI 收集的参数，供命令构建器统一转换为 FFmpeg 参数。
struct SubtitleBurnRequest
{
    QString inputVideoPath;
    QString externalSubtitlePath;
    QString outputPath;
    QString container;
    int burnModeIndex = 0;
    int subtitleTrackIndex = 0;
    bool mergeTracks = false;
    bool keepAudio = true;
};

/// @brief FFmpeg 字幕烧录命令构建器
/// @details 根据不同输出模式构建 FFmpeg 命令参数，业务层无需关心命令细节。
class SubtitleBurnCommandBuilder
{
public:
    /// @brief 构建 FFmpeg 参数
    /// @param request 烧录请求
    /// @param errorMessage 构建失败时返回错误信息
    /// @return FFmpeg 参数列表（不包含程序路径）
    static QStringList buildArguments(const SubtitleBurnRequest &request, QString *errorMessage = nullptr);

private:
    static QStringList buildHardBurnArgs(const SubtitleBurnRequest &request, QString *errorMessage);
    static QStringList buildSoftMuxArgs(const SubtitleBurnRequest &request, QString *errorMessage);
    static QStringList buildReplaceTrackArgs(const SubtitleBurnRequest &request, QString *errorMessage);

    static QString escapeSubtitlesFilterPath(const QString &path);
};

#endif // SUBTITLEBURNCOMMANDBUILDER_H
