#ifndef PROMPTREQUESTCOMPOSER_H
#define PROMPTREQUESTCOMPOSER_H

#include <QJsonArray>
#include <QString>

struct PromptComposeInput
{
    QString naturalInstruction;
    QString sourceLanguage;
    QString targetLanguage;
    bool keepTimeline = true;
    bool reviewPolish = false;
    QString presetJson;
    QString srtPath;
};

class PromptRequestComposer
{
public:
    static QString buildFinalInstruction(const PromptComposeInput &input);
    static QJsonArray buildSingleTurnMessages(const PromptComposeInput &input);
};

#endif // PROMPTREQUESTCOMPOSER_H
