#include "subtitlecontainerprofile.h"

namespace {
QList<SubtitleContainerProfile> buildProfiles()
{
    // 说明：
    // - supportsSoftSubtitle=true 表示支持“软封装/替换轨道”两类模式。
    // - subtitleCodec 为空表示该容器不支持软字幕封装，仅可使用硬字幕模式。
    return QList<SubtitleContainerProfile>{
        SubtitleContainerProfile{ QStringLiteral("mp4"),  QStringLiteral("MP4"),  QStringLiteral("mp4"),  QStringLiteral("mov_text"), true },
        SubtitleContainerProfile{ QStringLiteral("mkv"),  QStringLiteral("MKV"),  QStringLiteral("mkv"),  QStringLiteral("srt"),      true },
        SubtitleContainerProfile{ QStringLiteral("mov"),  QStringLiteral("MOV"),  QStringLiteral("mov"),  QStringLiteral("mov_text"), true },
        SubtitleContainerProfile{ QStringLiteral("m4v"),  QStringLiteral("M4V"),  QStringLiteral("m4v"),  QStringLiteral("mov_text"), true },
        SubtitleContainerProfile{ QStringLiteral("webm"), QStringLiteral("WEBM"), QStringLiteral("webm"), QStringLiteral("webvtt"),   true },
        SubtitleContainerProfile{ QStringLiteral("avi"),  QStringLiteral("AVI"),  QStringLiteral("avi"),  QString(),                    false }
    };
}
}

QList<SubtitleContainerProfile> SubtitleContainerProfileRegistry::allProfiles()
{
    static const QList<SubtitleContainerProfile> kProfiles = buildProfiles();
    return kProfiles;
}

SubtitleContainerProfile SubtitleContainerProfileRegistry::resolveByIdOrExtension(const QString &value)
{
    const QString normalized = value.trimmed().toLower();
    const QList<SubtitleContainerProfile> profiles = allProfiles();

    for (const SubtitleContainerProfile &profile : profiles) {
        if (profile.id == normalized || profile.extension == normalized) {
            return profile;
        }
    }

    // 默认回退到 MP4，保证行为可预期。
    return profiles.isEmpty() ? SubtitleContainerProfile() : profiles.first();
}
