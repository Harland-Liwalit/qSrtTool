#include "executablecapabilities.h"

#include <QProcess>
#include <QRegularExpression>
#include <QEventLoop>
#include <QTimer>

QString ExecutableCapabilitiesDetector::executeCommandWithTimeout(const QString &program, const QStringList &args, int timeoutMs)
{
    QProcess process;
    process.setProgram(program);
    process.setArguments(args);
    process.start();

    QTimer timer;
    timer.setSingleShot(true);

    QEventLoop loop;
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), &loop, &QEventLoop::quit);

    timer.start(timeoutMs);
    loop.exec();

    if (timer.isActive()) {
        timer.stop();
    } else {
        process.kill();
        process.waitForFinished(1000);
        return QString();
    }

    const QString output = QString::fromLocal8Bit(process.readAllStandardOutput());
    const QString error = QString::fromLocal8Bit(process.readAllStandardError());
    return output.isEmpty() ? error : output;
}

QString ExecutableCapabilitiesDetector::extractVersionNumber(const QString &versionOutput)
{
    // 支持多种版本号格式：v1.5.4, 1.5.4, version 1.5.4 等
    QRegularExpression versionRx(R"(v?(\d+)\.(\d+)(?:\.(\d+))?)");
    QRegularExpressionMatch match = versionRx.match(versionOutput);
    
    if (match.hasMatch()) {
        QString version = match.captured(0);
        if (version.startsWith('v')) {
            version = version.mid(1);
        }
        return version;
    }

    return QString();
}

ExecutableCapabilities ExecutableCapabilitiesDetector::detectWhisper(const QString &execPath)
{
    ExecutableCapabilities caps;
    caps.name = "whisper.cpp";
    caps.executablePath = execPath;

    if (execPath.isEmpty()) {
        caps.isAvailable = false;
        caps.unsupportedReason = "Executable path is empty";
        return caps;
    }

    // 获取版本号
    const QString versionOutput = executeCommandWithTimeout(execPath, QStringList() << "--version");
    if (versionOutput.isEmpty()) {
        caps.isAvailable = false;
        caps.unsupportedReason = "Cannot execute or timeout";
        return caps;
    }

    caps.isAvailable = true;
    caps.version = extractVersionNumber(versionOutput);

    // 解析版本号
    QStringList parts = caps.version.split('.');
    int major = 0, minor = 0;
    if (parts.size() >= 1) major = parts[0].toInt();
    if (parts.size() >= 2) minor = parts[1].toInt();

    // 版本支持判断
    if (major == 0) {
        caps.isSupported = false;
        caps.unsupportedReason = "Pre-release version may lack features";
    } else if (major == 1 && minor < 4) {
        caps.isSupported = false;
        caps.unsupportedReason = "Version too old (< 1.4), may lack critical features";
    } else {
        caps.isSupported = true;
    }

    // 功能检测：v1.5+ 支持 GPU 控制标志
    caps.whisperSupportsGpu = (major > 1 || (major == 1 && minor >= 5));

    // 功能检测：v1.4+ 支持多线程
    caps.whisperSupportsThreads = (major > 1 || (major == 1 && minor >= 4));

    // 所有版本都支持语言参数
    caps.whisperSupportsLanguage = true;

    return caps;
}

ExecutableCapabilities ExecutableCapabilitiesDetector::detectFfmpeg(const QString &execPath)
{
    ExecutableCapabilities caps;
    caps.name = "FFmpeg";
    caps.executablePath = execPath;

    if (execPath.isEmpty()) {
        caps.isAvailable = false;
        caps.unsupportedReason = "Executable path is empty";
        return caps;
    }

    // FFmpeg 的版本输出格式：ffmpeg version 6.0
    const QString versionOutput = executeCommandWithTimeout(execPath, QStringList() << "-version");
    if (versionOutput.isEmpty()) {
        caps.isAvailable = false;
        caps.unsupportedReason = "Cannot execute or timeout";
        return caps;
    }

    caps.isAvailable = true;
    caps.version = extractVersionNumber(versionOutput);

    QStringList parts = caps.version.split('.');
    int major = 0;
    if (parts.size() >= 1) major = parts[0].toInt();

    // 版本支持判断
    if (major < 5) {
        caps.isSupported = false;
        caps.unsupportedReason = "FFmpeg version too old (< 5.0)";
    } else {
        caps.isSupported = true;
    }

    // 功能检测：查询完整配置
    const QString fullOutput = executeCommandWithTimeout(execPath, QStringList() << "-hide_banner");
    
    // 检查 RTMP 支持
    caps.ffmpegHasRtmp = fullOutput.contains("rtmp", Qt::CaseInsensitive);

    // 检查硬件加速支持（NVIDIA NVENC 等）
    caps.ffmpegHasHardwareAccel = fullOutput.contains("cuda", Qt::CaseInsensitive)
                                 || fullOutput.contains("hevc_nvenc", Qt::CaseInsensitive)
                                 || fullOutput.contains("h264_nvenc", Qt::CaseInsensitive);

    return caps;
}

ExecutableCapabilities ExecutableCapabilitiesDetector::detectYtDlp(const QString &execPath)
{
    ExecutableCapabilities caps;
    caps.name = "yt-dlp";
    caps.executablePath = execPath;

    if (execPath.isEmpty()) {
        caps.isAvailable = false;
        caps.unsupportedReason = "Executable path is empty";
        return caps;
    }

    // yt-dlp 的版本输出格式：yt-dlp 2024.02.01
    const QString versionOutput = executeCommandWithTimeout(execPath, QStringList() << "--version");
    if (versionOutput.isEmpty()) {
        caps.isAvailable = false;
        caps.unsupportedReason = "Cannot execute or timeout";
        return caps;
    }

    caps.isAvailable = true;
    
    // yt-dlp 使用日期格式作为版本号：2024.02.01
    QRegularExpression dateVer(R"((\d{4}\.\d{2}\.\d{2}))");
    QRegularExpressionMatch match = dateVer.match(versionOutput);
    if (match.hasMatch()) {
        caps.version = match.captured(1);
    } else {
        // 尝试提取一般的版本号
        caps.version = extractVersionNumber(versionOutput);
    }

    // 版本支持判断
    // yt-dlp 相对稳定，2023.01.01 之后的版本都可以用
    if (caps.version.isEmpty()) {
        caps.isSupported = false;
        caps.unsupportedReason = "Cannot determine version";
    } else {
        // 简单检查：非常旧的版本（2020 之前）
        if (caps.version.startsWith("2020") || caps.version.startsWith("2021")) {
            caps.isSupported = false;
            caps.unsupportedReason = "yt-dlp version too old (< 2022.01.01)";
        } else {
            caps.isSupported = true;
        }
    }

    // 功能检测：yt-dlp 大多数版本都支持
    caps.ytDlpSupportsPlaylist = true;    // 基本功能
    caps.ytDlpSupportsFragments = true;   // 基本功能

    return caps;
}
