#ifndef SUBTITLECONTAINERPROFILE_H
#define SUBTITLECONTAINERPROFILE_H

#include <QString>
#include <QList>

/// @brief 字幕输出容器配置
/// @details 统一描述某个输出容器的扩展名、显示名及软字幕可用性。
struct SubtitleContainerProfile
{
    SubtitleContainerProfile() = default;
    SubtitleContainerProfile(const QString &containerId,
                             const QString &containerDisplayName,
                             const QString &containerExtension,
                             const QString &codec,
                             bool softSubtitleSupported)
        : id(containerId)
        , displayName(containerDisplayName)
        , extension(containerExtension)
        , subtitleCodec(codec)
        , supportsSoftSubtitle(softSubtitleSupported)
    {
    }

    QString id;
    QString displayName;
    QString extension;
    QString subtitleCodec;
    bool supportsSoftSubtitle = false;
};

/// @brief 字幕容器配置中心
/// @details 为 UI 下拉框与 FFmpeg 命令构建提供统一格式规则。
class SubtitleContainerProfileRegistry
{
public:
    static QList<SubtitleContainerProfile> allProfiles();
    static SubtitleContainerProfile resolveByIdOrExtension(const QString &value);
};

#endif // SUBTITLECONTAINERPROFILE_H
