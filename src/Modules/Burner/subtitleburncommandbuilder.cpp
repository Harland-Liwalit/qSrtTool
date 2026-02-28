#include "subtitleburncommandbuilder.h"
#include "subtitlecontainerprofile.h"

#include <QFileInfo>

QStringList SubtitleBurnCommandBuilder::buildArguments(const SubtitleBurnRequest &request, QString *errorMessage)
{
    switch (request.burnModeIndex) {
    case 0:
        return buildHardBurnArgs(request, errorMessage);
    case 1:
        return buildSoftMuxArgs(request, errorMessage);
    case 2:
        return buildReplaceTrackArgs(request, errorMessage);
    default:
        if (errorMessage) {
            *errorMessage = QStringLiteral("未知输出模式");
        }
        return QStringList();
    }
}

QStringList SubtitleBurnCommandBuilder::buildHardBurnArgs(const SubtitleBurnRequest &request, QString *errorMessage)
{
    const bool hasExternalSubtitle = !request.externalSubtitlePath.trimmed().isEmpty();

    if (!hasExternalSubtitle && request.subtitleTrackIndex == 3) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("硬字幕模式至少需要外部字幕或一个内嵌字幕轨道。");
        }
        return QStringList();
    }

    QStringList args;
    args << "-y"
         << "-hide_banner"
         << "-i" << request.inputVideoPath;

    QString subtitleFilter;
    if (hasExternalSubtitle) {
        subtitleFilter = QStringLiteral("subtitles='%1'").arg(escapeSubtitlesFilterPath(request.externalSubtitlePath));
    } else {
        QString trackSelector;
        if (request.subtitleTrackIndex == 1) {
            trackSelector = QStringLiteral(":si=0");
        } else if (request.subtitleTrackIndex == 2) {
            trackSelector = QStringLiteral(":si=1");
        }
        subtitleFilter = QStringLiteral("subtitles='%1'%2")
                             .arg(escapeSubtitlesFilterPath(request.inputVideoPath), trackSelector);
    }

    args << "-vf" << subtitleFilter
         << "-c:v" << "libx264"
         << "-preset" << "medium"
         << "-crf" << "23";

    if (request.keepAudio) {
        args << "-map" << "0:v:0"
             << "-map" << "0:a?"
             << "-c:a" << "copy";
    } else {
        args << "-an";
    }

    args << request.outputPath;
    return args;
}

QStringList SubtitleBurnCommandBuilder::buildSoftMuxArgs(const SubtitleBurnRequest &request, QString *errorMessage)
{
    const bool hasExternalSubtitle = !request.externalSubtitlePath.trimmed().isEmpty();

    if (!hasExternalSubtitle && request.subtitleTrackIndex == 3) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("软封装模式至少需要外部字幕或一个内嵌字幕轨道。");
        }
        return QStringList();
    }

    QStringList args;
    args << "-y"
         << "-hide_banner"
         << "-i" << request.inputVideoPath;

    int externalInputIndex = -1;
    if (hasExternalSubtitle) {
        externalInputIndex = 1;
        args << "-i" << request.externalSubtitlePath;
    }

    args << "-map" << "0:v:0";
    if (request.keepAudio) {
        args << "-map" << "0:a?";
    }

    bool hasMappedSubtitle = false;
    if (request.mergeTracks) {
        if (request.subtitleTrackIndex == 1) {
            args << "-map" << "0:s:0?";
            hasMappedSubtitle = true;
        } else if (request.subtitleTrackIndex == 2) {
            args << "-map" << "0:s:1?";
            hasMappedSubtitle = true;
        } else if (request.subtitleTrackIndex == 0) {
            args << "-map" << "0:s?";
            hasMappedSubtitle = true;
        }

        if (externalInputIndex >= 0) {
            args << "-map" << QString::number(externalInputIndex) + ":0?";
            hasMappedSubtitle = true;
        }
    } else {
        if (externalInputIndex >= 0) {
            args << "-map" << QString::number(externalInputIndex) + ":0?";
            hasMappedSubtitle = true;
        } else if (request.subtitleTrackIndex == 1) {
            args << "-map" << "0:s:0?";
            hasMappedSubtitle = true;
        } else if (request.subtitleTrackIndex == 2) {
            args << "-map" << "0:s:1?";
            hasMappedSubtitle = true;
        } else if (request.subtitleTrackIndex == 0) {
            args << "-map" << "0:s?";
            hasMappedSubtitle = true;
        }
    }

    if (!hasMappedSubtitle) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("当前选择没有可封装的字幕轨道。");
        }
        return QStringList();
    }

    args << "-c:v" << "copy";
    if (request.keepAudio) {
        args << "-c:a" << "copy";
    }
    const QString outputSuffix = QFileInfo(request.outputPath).suffix();
    const QString containerHint = outputSuffix.isEmpty() ? request.container : outputSuffix;
    const SubtitleContainerProfile profile = SubtitleContainerProfileRegistry::resolveByIdOrExtension(containerHint);

    if (!profile.supportsSoftSubtitle || profile.subtitleCodec.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("当前封装格式 %1 不支持软字幕封装，请切换为硬字幕模式或改用支持软字幕的格式。")
                                .arg(profile.displayName.isEmpty() ? containerHint : profile.displayName);
        }
        return QStringList();
    }

    args << "-c:s" << profile.subtitleCodec
         << request.outputPath;

    return args;
}

QStringList SubtitleBurnCommandBuilder::buildReplaceTrackArgs(const SubtitleBurnRequest &request, QString *errorMessage)
{
    if (request.externalSubtitlePath.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("替换字幕轨道模式必须提供外部字幕文件。");
        }
        return QStringList();
    }

    QStringList args;
    args << "-y"
         << "-hide_banner"
         << "-i" << request.inputVideoPath
         << "-i" << request.externalSubtitlePath
         << "-map" << "0:v:0";

    if (request.keepAudio) {
        args << "-map" << "0:a?";
    }

    args << "-map" << "1:0"
         << "-c:v" << "copy";

    if (request.keepAudio) {
        args << "-c:a" << "copy";
    }

    const QString outputSuffix = QFileInfo(request.outputPath).suffix();
    const QString containerHint = outputSuffix.isEmpty() ? request.container : outputSuffix;
    const SubtitleContainerProfile profile = SubtitleContainerProfileRegistry::resolveByIdOrExtension(containerHint);

    if (!profile.supportsSoftSubtitle || profile.subtitleCodec.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("当前封装格式 %1 不支持替换字幕轨道，请切换为硬字幕模式或改用支持软字幕的格式。")
                                .arg(profile.displayName.isEmpty() ? containerHint : profile.displayName);
        }
        return QStringList();
    }

    args << "-c:s" << profile.subtitleCodec
         << request.outputPath;

    return args;
}

QString SubtitleBurnCommandBuilder::escapeSubtitlesFilterPath(const QString &path)
{
    QString normalized = QFileInfo(path).absoluteFilePath();
    normalized.replace("\\", "/");
    normalized.replace(":", "\\:");
    normalized.replace("'", "\\\\'");
    normalized.replace("[", "\\[");
    normalized.replace("]", "\\]");
    normalized.replace(",", "\\,");
    return normalized;
}
