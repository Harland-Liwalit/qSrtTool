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
    // 组合自然语言指令、语言设置、预设 JSON，输出最终指令文本。
    static QString buildFinalInstruction(const PromptComposeInput &input);
    // 构造单轮消息数组，供聊天补全接口直接发送。
    static QJsonArray buildSingleTurnMessages(const PromptComposeInput &input);
};

#endif // PROMPTREQUESTCOMPOSER_H
