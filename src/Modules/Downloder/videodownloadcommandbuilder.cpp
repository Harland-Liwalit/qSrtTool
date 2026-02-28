#include "videodownloadcommandbuilder.h"

#include <QDir>

QStringList VideoDownloadCommandBuilder::buildArguments(const VideoDownloadRequest &request, QString *errorMessage)
{
    const QString trimmedUrl = request.url.trimmed();
    const QString outputDir = request.outputDirectory.trimmed();

    if (trimmedUrl.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("下载地址不能为空。");
        }
        return QStringList();
    }

    if (outputDir.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("保存目录不能为空。");
        }
        return QStringList();
    }

    QStringList args;
    args << "--newline"
         << "--no-color"
         << "--progress"
         << "-P" << QDir::toNativeSeparators(outputDir)
         << "-o" << "%(title).120s [%(id)s].%(ext)s";

    const QString normalizedFormatId = request.formatId.trimmed().toLower();
    const QString normalizedQualityId = request.qualityId.trimmed().toLower();

    if (normalizedFormatId == QStringLiteral("audio_mp3")) {
        args << "-x"
             << "--audio-format" << "mp3"
             << "--audio-quality" << "0";
    } else {
        const QString formatSelector = videoFormatSelector(normalizedQualityId);
        if (!formatSelector.isEmpty()) {
            args << "-f" << formatSelector;
        }

        if (normalizedFormatId == QStringLiteral("mp4") || normalizedFormatId == QStringLiteral("mkv")) {
            args << "--merge-output-format" << normalizedFormatId;
        }
    }

    args << trimmedUrl;
    return args;
}

QString VideoDownloadCommandBuilder::videoFormatSelector(const QString &qualityId)
{
    if (qualityId == QStringLiteral("1080p")) {
        return QStringLiteral("bv*[height<=1080]+ba/b[height<=1080]");
    }

    if (qualityId == QStringLiteral("720p")) {
        return QStringLiteral("bv*[height<=720]+ba/b[height<=720]");
    }

    if (qualityId == QStringLiteral("480p")) {
        return QStringLiteral("bv*[height<=480]+ba/b[height<=480]");
    }

    return QStringLiteral("bv*+ba/b");
}
