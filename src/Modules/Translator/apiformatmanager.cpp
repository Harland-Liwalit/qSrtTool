#include "apiformatmanager.h"

namespace {
QString normalized(const QString &text)
{
    return text.trimmed().toLower();
}

bool isEmptyOption(const QJsonValue &value)
{
    if (value.isNull() || value.isUndefined()) {
        return true;
    }

    if (value.isString()) {
        return value.toString().trimmed().isEmpty();
    }

    return false;
}
}

QString ApiFormatManager::providerId(const QString &provider, const QString &baseUrl)
{
    const QString providerText = normalized(provider);
    const QString urlText = normalized(baseUrl);

    if (providerText.contains("ollama") || urlText.contains(":11434")) {
        return QStringLiteral("ollama");
    }

    if (providerText.contains("deepseek") || urlText.contains("api.deepseek.com")) {
        return QStringLiteral("deepseek");
    }

    if (providerText.contains("openai api") || urlText.contains("api.openai.com")) {
        return QStringLiteral("openai");
    }

    if (providerText.contains("lm studio") || urlText.contains(":1234")) {
        return QStringLiteral("lmstudio");
    }

    return QStringLiteral("openai_compatible");
}

QString ApiFormatManager::modelListEndpoint(const QString &providerId)
{
    if (providerId == QStringLiteral("ollama")) {
        return QStringLiteral("/api/tags");
    }
    return QStringLiteral("/models");
}

QString ApiFormatManager::chatEndpoint(const QString &providerId)
{
    if (providerId == QStringLiteral("ollama")) {
        return QStringLiteral("/api/chat");
    }
    return QStringLiteral("/chat/completions");
}

QJsonObject ApiFormatManager::buildChatBody(const QString &providerId,
                                            const QString &model,
                                            bool stream,
                                            const QJsonArray &messages,
                                            const QJsonObject &options)
{
    QJsonObject body;
    body.insert(QStringLiteral("messages"), messages);
    body.insert(QStringLiteral("stream"), stream);

    const QString trimmedModel = model.trimmed();
    if (!trimmedModel.isEmpty()) {
        body.insert(QStringLiteral("model"), trimmedModel);
    }

    for (auto it = options.constBegin(); it != options.constEnd(); ++it) {
        if (it.key().trimmed().isEmpty() || isEmptyOption(it.value())) {
            continue;
        }

        if (it.key() == QStringLiteral("max_tokens") && it.value().toInt() <= 0) {
            continue;
        }

        if (providerId == QStringLiteral("deepseek") && it.key() == QStringLiteral("max_tokens")) {
            const int requestedMaxTokens = it.value().toInt();
            if (requestedMaxTokens <= 0) {
                continue;
            }

            const int deepSeekSafeCap = 8192;
            body.insert(it.key(), qMin(requestedMaxTokens, deepSeekSafeCap));
            continue;
        }

        if (providerId == QStringLiteral("deepseek")
            && it.key() == QStringLiteral("temperature")
            && trimmedModel.toLower().contains(QStringLiteral("reasoner"))) {
            continue;
        }

        body.insert(it.key(), it.value());
    }

    if (providerId == QStringLiteral("ollama")) {
        body.remove(QStringLiteral("max_tokens"));
    }

    return body;
}
