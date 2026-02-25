#include "whispercommandbuilder.h"
#include "../../Core/executablecapabilities.h"

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
                                                              int threadCountHint,
                                                              const ExecutableCapabilities *capabilities)
{
    const int idealThreadCount = QThread::idealThreadCount();
    const int detectedThreadCount = idealThreadCount > 0 ? idealThreadCount : 4;
    const int threadCount = threadCountHint > 0 ? threadCountHint : detectedThreadCount;

    QStringList args;
    args << "-m" << modelPath
         << "-f" << audioPath
         << "-osrt"
         << "-of" << outputBasePath;

    // 条件性地添加线程参数
    if (capabilities) {
        if (capabilities->whisperSupportsThreads) {
            args << "-t" << QString::number(threadCount);
        }
    } else {
        // 默认行为：所有版本都支持线程参数
        args << "-t" << QString::number(threadCount);
    }

    if (!languageCode.isEmpty()) {
        // 条件性地添加语言参数
        if (capabilities) {
            if (capabilities->whisperSupportsLanguage) {
                args << "-l" << languageCode << "-np";
            }
        } else {
            // 默认行为：所有版本都支持语言参数
            args << "-l" << languageCode << "-np";
        }
    }

    // 条件性地添加 GPU 禁用标志
    if (!useGpu) {
        if (capabilities) {
            if (capabilities->whisperSupportsGpu) {
                args << "-ng";
            }
            // 如果版本太旧不支持 -ng，我们无法禁用 GPU
            // 但这种情况很少见
        } else {
            // 默认行为：添加 GPU 禁用标志
            args << "-ng";
        }
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
