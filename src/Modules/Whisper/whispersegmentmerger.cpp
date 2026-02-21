#include "whispersegmentmerger.h"

#include <QRegularExpression>
#include <QFile>
#include <QTextStream>
#include <QtMath>

bool WhisperSegmentMerger::parseSrtTimestamp(const QString &text, qint64 &milliseconds)
{
    const QRegularExpression re("^(\\d{2}):(\\d{2}):(\\d{2}),(\\d{3})$");
    const QRegularExpressionMatch match = re.match(text.trimmed());
    if (!match.hasMatch()) {
        return false;
    }

    const qint64 h = match.captured(1).toLongLong();
    const qint64 m = match.captured(2).toLongLong();
    const qint64 s = match.captured(3).toLongLong();
    const qint64 ms = match.captured(4).toLongLong();
    milliseconds = (((h * 60) + m) * 60 + s) * 1000 + ms;
    return true;
}

QString WhisperSegmentMerger::formatSrtTimestamp(qint64 milliseconds)
{
    milliseconds = qMax<qint64>(0, milliseconds);
    const qint64 totalSeconds = milliseconds / 1000;
    const qint64 ms = milliseconds % 1000;
    const qint64 seconds = totalSeconds % 60;
    const qint64 minutes = (totalSeconds / 60) % 60;
    const qint64 hours = totalSeconds / 3600;

    return QString("%1:%2:%3,%4")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'))
        .arg(ms, 3, 10, QLatin1Char('0'));
}

QString WhisperSegmentMerger::shiftedSrtContent(const QString &srtContent, qint64 offsetMs)
{
    QStringList outputLines;
    const QStringList lines = srtContent.split(QRegularExpression("\\r?\\n"), Qt::KeepEmptyParts);
    const QRegularExpression timingRe("^(\\d{2}:\\d{2}:\\d{2},\\d{3})\\s*-->\\s*(\\d{2}:\\d{2}:\\d{2},\\d{3})(.*)$");

    for (const QString &line : lines) {
        const QRegularExpressionMatch match = timingRe.match(line);
        if (!match.hasMatch()) {
            outputLines << line;
            continue;
        }

        qint64 startMs = 0;
        qint64 endMs = 0;
        if (!parseSrtTimestamp(match.captured(1), startMs) || !parseSrtTimestamp(match.captured(2), endMs)) {
            outputLines << line;
            continue;
        }

        const QString shiftedLine = QString("%1 --> %2%3")
                                        .arg(formatSrtTimestamp(startMs + offsetMs))
                                        .arg(formatSrtTimestamp(endMs + offsetMs))
                                        .arg(match.captured(3));
        outputLines << shiftedLine;
    }

    return outputLines.join("\n");
}

QString WhisperSegmentMerger::srtToPlainText(const QString &srtContent)
{
    QStringList lines;
    const QStringList blocks = srtContent.split(QRegularExpression("\\r?\\n\\r?\\n"), Qt::SkipEmptyParts);
    for (const QString &block : blocks) {
        const QStringList blockLines = block.split(QRegularExpression("\\r?\\n"), Qt::KeepEmptyParts);
        for (int i = 2; i < blockLines.size(); ++i) {
            if (!blockLines[i].trimmed().isEmpty()) {
                lines << blockLines[i].trimmed();
            }
        }
    }
    return lines.join("\n");
}

QString WhisperSegmentMerger::srtToTimestampedText(const QString &srtContent)
{
    QStringList lines;
    const QStringList blocks = srtContent.split(QRegularExpression("\\r?\\n\\r?\\n"), Qt::SkipEmptyParts);
    for (const QString &block : blocks) {
        const QStringList blockLines = block.split(QRegularExpression("\\r?\\n"), Qt::KeepEmptyParts);
        if (blockLines.size() < 2) {
            continue;
        }
        const QString timeLine = blockLines[1].trimmed();
        QString textLine;
        for (int i = 2; i < blockLines.size(); ++i) {
            if (!blockLines[i].trimmed().isEmpty()) {
                if (!textLine.isEmpty()) {
                    textLine += " ";
                }
                textLine += blockLines[i].trimmed();
            }
        }
        if (!textLine.isEmpty()) {
            lines << QString("[%1] %2").arg(timeLine, textLine);
        }
    }
    return lines.join("\n");
}

QString WhisperSegmentMerger::srtToWebVtt(const QString &srtContent)
{
    QStringList out;
    out << "WEBVTT" << "";

    const QStringList blocks = srtContent.split(QRegularExpression("\\r?\\n\\r?\\n"), Qt::SkipEmptyParts);
    for (const QString &block : blocks) {
        const QStringList blockLines = block.split(QRegularExpression("\\r?\\n"), Qt::KeepEmptyParts);
        if (blockLines.size() < 2) {
            continue;
        }

        QString timing = blockLines[1];
        timing.replace(",", ".");
        out << timing;
        for (int i = 2; i < blockLines.size(); ++i) {
            out << blockLines[i];
        }
        out << "";
    }

    return out.join("\n");
}

QString WhisperSegmentMerger::mergeSegmentSrtFiles(const QStringList &segmentSrtFiles,
                                                    double segmentDurationSeconds,
                                                    OutputFormat format)
{
    if (segmentSrtFiles.isEmpty()) {
        return QString();
    }

    QString mergedSrtContent;
    QTextStream mergedSrtOut(&mergedSrtContent, QIODevice::WriteOnly);
    int globalIndex = 1;
    const int segmentSeconds = static_cast<int>(segmentDurationSeconds);

    for (int index = 0; index < segmentSrtFiles.size(); ++index) {
        QFile srtFile(segmentSrtFiles[index]);
        if (!srtFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return QString();
        }

        QTextStream in(&srtFile);
        in.setCodec("UTF-8");
        const QString shifted = shiftedSrtContent(in.readAll(), static_cast<qint64>(index) * segmentSeconds * 1000);
        srtFile.close();

        const QStringList blocks = shifted.split(QRegularExpression("\\r?\\n\\r?\\n"), Qt::SkipEmptyParts);
        for (const QString &block : blocks) {
            const QStringList lines = block.split(QRegularExpression("\\r?\\n"), Qt::KeepEmptyParts);
            if (lines.size() < 2) {
                continue;
            }

            mergedSrtOut << globalIndex++ << "\n";
            for (int i = 1; i < lines.size(); ++i) {
                mergedSrtOut << lines[i] << "\n";
            }
            mergedSrtOut << "\n";
        }
    }

    // 格式转换
    QString finalContent = mergedSrtContent;
    if (format == Format_TXT) {
        finalContent = srtToPlainText(mergedSrtContent);
    } else if (format == Format_TXT_Timestamped) {
        finalContent = srtToTimestampedText(mergedSrtContent);
    } else if (format == Format_WebVTT) {
        finalContent = srtToWebVtt(mergedSrtContent);
    }

    return finalContent;
}
