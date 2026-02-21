#include "whispercommandbuilder.h"

#include <QThread>

QStringList WhisperCommandBuilder::buildFfmpegExtractArgs(const QString &inputPath,
                                                           double startSeconds,
                                                           double durationSeconds,
                                                           const QString &outputPath)
{
    return QStringList() << "-y"
                         << "-hide_banner"
                         << "-loglevel" << "error"
                         << "-ss" << QString::number(startSeconds, 'f', 3)
                         << "-t" << QString::number(durationSeconds, 'f', 3)
                         << "-i" << inputPath
                         << "-vn"
                         << "-ac" << "1"
                         << "-ar" << "16000"
                         << "-c:a" << "pcm_s16le"
                         << outputPath;
}

QStringList WhisperCommandBuilder::buildWhisperTranscribeArgs(const QString &modelPath,
                                                              const QString &audioPath,
                                                              const QString &outputBasePath,
                                                              const QString &languageCode,
                                                              bool useGpu,
                                                              int threadCountHint)
{
    const int idealThreadCount = QThread::idealThreadCount();
    const int detectedThreadCount = idealThreadCount > 0 ? idealThreadCount : 4;
    const int threadCount = threadCountHint > 0 ? threadCountHint : detectedThreadCount;

    QStringList args;
    args << "-m" << modelPath
         << "-f" << audioPath
         << "-osrt"
         << "-of" << outputBasePath
         << "-t" << QString::number(threadCount);

    if (!languageCode.isEmpty()) {
        args << "-l" << languageCode
             << "-np";
    }
    if (useGpu) {
        args << "-ng" << "99";
    }

    return args;
}

QString WhisperCommandBuilder::languageCodeFromUiText(const QString &uiText)
{
    if (uiText == QStringLiteral("中文")) {
        return "zh";
    }
    if (uiText == QStringLiteral("English")) {
        return "en";
    }
    if (uiText == QStringLiteral("日本語")) {
        return "ja";
    }
    if (uiText == QStringLiteral("한국어")) {
        return "ko";
    }
    if (uiText == QStringLiteral("Español")) {
        return "es";
    }
    if (uiText == QStringLiteral("Français")) {
        return "fr";
    }
    if (uiText == QStringLiteral("Deutsch")) {
        return "de";
    }
    if (uiText == QStringLiteral("Русский")) {
        return "ru";
    }

    return QString();
}

QString WhisperCommandBuilder::outputFileExtensionFromUiText(const QString &uiText)
{
    if (uiText == QStringLiteral("TXT") || uiText == QStringLiteral("TXT（带时间）")) {
        return QStringLiteral("txt");
    }
    if (uiText == QStringLiteral("WebVTT") || uiText == QStringLiteral("WEDTT")) {
        return QStringLiteral("vtt");
    }
    return QStringLiteral("srt");
}
