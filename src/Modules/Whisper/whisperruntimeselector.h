#ifndef WHISPERRUNTIMESELECTOR_H
#define WHISPERRUNTIMESELECTOR_H

#include <QString>

struct WhisperRuntimeSelection {
    QString executablePath;
    bool usingCudaBuild = false;
};

class WhisperRuntimeSelector
{
public:
    static WhisperRuntimeSelection selectExecutable(bool preferCuda);

private:
    static QString findFirstExistingInDeps(const QStringList &relativePaths);
};

#endif // WHISPERRUNTIMESELECTOR_H
