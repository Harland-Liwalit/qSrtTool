#include "whisperruntimeselector.h"

#include <QDir>
#include <QFileInfo>

WhisperRuntimeSelection WhisperRuntimeSelector::selectExecutable(bool preferCuda)
{
    const QStringList cudaCandidates = {
        QStringLiteral("whisper/cuda/whisper-cli.exe"),
        QStringLiteral("whisper/cuda/whisper.exe"),
        QStringLiteral("whisper/cuda/Release/whisper-cli.exe"),
        QStringLiteral("whisper/cuda/Release/whisper.exe"),
        QStringLiteral("whisper/cuda/Release-x64/whisper-cli.exe"),
        QStringLiteral("whisper/cuda/Release-x64/whisper.exe"),
        QStringLiteral("Release-cuda/whisper-cli.exe"),
        QStringLiteral("Release-cuda/whisper.exe")
    };

    const QStringList cpuCandidates = {
        QStringLiteral("whisper/cpu/whisper-cli.exe"),
        QStringLiteral("whisper/cpu/whisper.exe"),
        QStringLiteral("whisper/cpu/Release/whisper-cli.exe"),
        QStringLiteral("whisper/cpu/Release/whisper.exe"),
        QStringLiteral("whisper/cpu/Release-x64/whisper-cli.exe"),
        QStringLiteral("whisper/cpu/Release-x64/whisper.exe"),
        QStringLiteral("Release/whisper-cli.exe"),
        QStringLiteral("Release/whisper.exe"),
        QStringLiteral("whisper-cli.exe"),
        QStringLiteral("whisper.exe")
    };

    WhisperRuntimeSelection selection;

    if (preferCuda) {
        const QString cudaPath = findFirstExistingInDeps(cudaCandidates);
        if (!cudaPath.isEmpty()) {
            selection.executablePath = cudaPath;
            selection.usingCudaBuild = true;
            return selection;
        }

        const QString cpuPath = findFirstExistingInDeps(cpuCandidates);
        selection.executablePath = cpuPath;
        selection.usingCudaBuild = false;
        return selection;
    }

    const QString cpuPath = findFirstExistingInDeps(cpuCandidates);
    if (!cpuPath.isEmpty()) {
        selection.executablePath = cpuPath;
        selection.usingCudaBuild = false;
        return selection;
    }

    const QString cudaPath = findFirstExistingInDeps(cudaCandidates);
    selection.executablePath = cudaPath;
    selection.usingCudaBuild = !cudaPath.isEmpty();
    return selection;
}

QString WhisperRuntimeSelector::findFirstExistingInDeps(const QStringList &relativePaths)
{
    const QString depsRoot = QDir(QDir::currentPath()).filePath(QStringLiteral("deps"));

    for (const QString &relativePath : relativePaths) {
        const QString fullPath = QDir(depsRoot).filePath(relativePath);
        if (QFileInfo::exists(fullPath) && QFileInfo(fullPath).isFile()) {
            return fullPath;
        }
    }

    return QString();
}
