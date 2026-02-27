#include "promptrequestcomposer.h"

#include <QStringList>
#include <QJsonObject>

QString PromptRequestComposer::buildFinalInstruction(const PromptComposeInput &input)
{
    QString instruction = input.naturalInstruction.trimmed();
    if (instruction.isEmpty()) {
        instruction = QStringLiteral("这是一个影视字幕翻译任务，请将内容翻译为%1，并保持术语统一与表达自然。")
                          .arg(input.targetLanguage.trimmed().isEmpty()
                                   ? QStringLiteral("中文")
                                   : input.targetLanguage.trimmed());
    }

    QStringList contextLines;
    contextLines << QStringLiteral("源语言：%1")
                        .arg(input.sourceLanguage.trimmed().isEmpty()
                                 ? QStringLiteral("自动检测")
                                 : input.sourceLanguage.trimmed());
    contextLines << QStringLiteral("目标语言：%1")
                        .arg(input.targetLanguage.trimmed().isEmpty()
                                 ? QStringLiteral("中文")
                                 : input.targetLanguage.trimmed());
    contextLines << QStringLiteral("保留时间轴：%1").arg(input.keepTimeline ? QStringLiteral("是")
                                                                      : QStringLiteral("否"));
    contextLines << QStringLiteral("逐句校对润色：%1").arg(input.reviewPolish ? QStringLiteral("是")
                                                                        : QStringLiteral("否"));

    if (!input.srtPath.trimmed().isEmpty()) {
        contextLines << QStringLiteral("待处理字幕路径：%1").arg(input.srtPath.trimmed());
    }

    QString result;
    result += instruction;
    result += QStringLiteral("\n\n【任务上下文】\n");
    result += contextLines.join(QStringLiteral("\n"));

    if (!input.presetJson.trimmed().isEmpty()) {
        result += QStringLiteral("\n\n【完整预设（JSON）】\n");
        result += input.presetJson.trimmed();
    }

    return result;
}

QJsonArray PromptRequestComposer::buildSingleTurnMessages(const PromptComposeInput &input)
{
    QJsonArray messages;

    QJsonObject userMessage;
    userMessage.insert(QStringLiteral("role"), QStringLiteral("user"));
    userMessage.insert(QStringLiteral("content"), buildFinalInstruction(input));
    messages.append(userMessage);

    return messages;
}
