#ifndef APIFORMATMANAGER_H
#define APIFORMATMANAGER_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

class ApiFormatManager
{
public:
    // 根据 provider 文本与 baseUrl 推断统一的 Provider ID。
    static QString providerId(const QString &provider, const QString &baseUrl);
    // 返回模型列表接口路径（如 /v1/models）。
    static QString modelListEndpoint(const QString &providerId);
    // 返回对话补全接口路径（如 /v1/chat/completions）。
    static QString chatEndpoint(const QString &providerId);

    // 按目标 Provider 规范构造请求体，并做必要参数清洗。
    static QJsonObject buildChatBody(const QString &providerId,
                                     const QString &model,
                                     bool stream,
                                     const QJsonArray &messages,
                                     const QJsonObject &options);
};

#endif // APIFORMATMANAGER_H
