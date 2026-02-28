#ifndef VIDEODOWNLOADCOMMANDBUILDER_H
#define VIDEODOWNLOADCOMMANDBUILDER_H

#include <QString>
#include <QStringList>

/// @brief 视频下载请求参数
/// @details 由 UI 收集参数，统一交给命令构建器转换为 yt-dlp 命令行参数。
struct VideoDownloadRequest
{
    QString url;
    QString outputDirectory;
    QString formatId;
    QString qualityId;
    QString cookieFilePath;
};

/// @brief yt-dlp 命令构建器
/// @details 仅负责参数拼装与校验，不处理任何进程/界面行为。
class VideoDownloadCommandBuilder
{
public:
    /// @brief 构建 yt-dlp 参数列表
    /// @param request 下载请求
    /// @param errorMessage 构建失败时返回错误信息
    /// @return 参数列表（不包含程序路径）
    static QStringList buildArguments(const VideoDownloadRequest &request, QString *errorMessage = nullptr);

private:
    static QString videoFormatSelector(const QString &qualityId);
};

#endif // VIDEODOWNLOADCOMMANDBUILDER_H
