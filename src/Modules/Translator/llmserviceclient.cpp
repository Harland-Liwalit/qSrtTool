#include "llmserviceclient.h"

#include <QJsonDocument>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace {
QString normalizedProvider(const QString &provider)
{
    return provider.trimmed().toLower();
}

QString joinUrl(const QString &baseUrl, const QString &path)
{
    QString normalized = baseUrl.trimmed();
    while (normalized.endsWith('/')) {
        normalized.chop(1);
    }

    QString endpoint = path;
    if (!endpoint.startsWith('/')) {
        endpoint.prepend('/');
    }
    return normalized + endpoint;
}
}

QString LlmServiceConfig::normalizedBaseUrl() const
{
    QString base = baseUrl.trimmed();
    if (base.isEmpty()) {
        base = defaultBaseUrlForProvider(provider);
    }
    while (base.endsWith('/')) {
        base.chop(1);
    }
    return base;
}

bool LlmServiceConfig::isValid() const
{
    return !normalizedBaseUrl().isEmpty();
}

QString LlmServiceConfig::defaultBaseUrlForProvider(const QString &provider)
{
    const QString normalized = normalizedProvider(provider);
    if (normalized.contains("lm studio")) {
        return QStringLiteral("http://127.0.0.1:1234/v1");
    }
    if (normalized.contains("ollama")) {
        return QStringLiteral("http://127.0.0.1:11434");
    }
    if (normalized.contains("openai api")) {
        return QStringLiteral("https://api.openai.com/v1");
    }
    return QStringLiteral("http://127.0.0.1:1234/v1");
}

LlmServiceClient::LlmServiceClient(QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
{
}

LlmServiceClient::~LlmServiceClient()
{
    cancelAll();
}

void LlmServiceClient::requestModels(const LlmServiceConfig &config)
{
    if (!config.isValid()) {
        emit requestFailed(tr("模型列表"), tr("服务地址为空，无法请求模型列表"));
        return;
    }

    const QString provider = normalizedProvider(config.provider);
    const QString endpoint = provider.contains("ollama") ? QStringLiteral("/api/tags")
                                                          : QStringLiteral("/models");
    const QNetworkRequest request = buildRequest(config, endpoint);
    sendRequest(request, QByteArray(), ReplyKind::ModelList, qMax(5000, config.timeoutMs));
}

void LlmServiceClient::requestChatCompletion(const LlmServiceConfig &config,
                                             const QJsonArray &messages,
                                             const QJsonObject &options)
{
    if (!config.isValid()) {
        emit requestFailed(tr("翻译请求"), tr("服务地址为空，无法发送请求"));
        return;
    }

    if (messages.isEmpty()) {
        emit requestFailed(tr("翻译请求"), tr("消息内容为空"));
        return;
    }

    const QString provider = normalizedProvider(config.provider);
    const QString endpoint = provider.contains("ollama") ? QStringLiteral("/api/chat")
                                                          : QStringLiteral("/chat/completions");

    if (config.stream) {
        emit requestFailed(tr("翻译请求"), tr("当前版本暂不支持流式响应，请先关闭“流式传输”"));
        return;
    }

    const QJsonObject body = buildChatBody(config, messages, options);
    const QNetworkRequest request = buildRequest(config, endpoint);
    sendRequest(request,
                QJsonDocument(body).toJson(QJsonDocument::Compact),
                ReplyKind::ChatCompletion,
                qMax(10000, config.timeoutMs));
}

void LlmServiceClient::cancelAll()
{
    const QList<QNetworkReply *> replies = m_replyKinds.keys();
    for (QNetworkReply *reply : replies) {
        if (reply) {
            reply->abort();
        }
    }
}

void LlmServiceClient::sendRequest(const QNetworkRequest &request,
                                   const QByteArray &payload,
                                   ReplyKind kind,
                                   int timeoutMs)
{
    QNetworkReply *reply = nullptr;
    if (payload.isEmpty()) {
        reply = m_networkManager->get(request);
    } else {
        reply = m_networkManager->post(request, payload);
    }

    if (!reply) {
        emit requestFailed(tr("网络"), tr("无法创建网络请求"));
        return;
    }

    attachReply(reply, kind, timeoutMs);
}

QNetworkRequest LlmServiceClient::buildRequest(const LlmServiceConfig &config, const QString &endpointPath) const
{
    const QUrl url(joinUrl(config.normalizedBaseUrl(), endpointPath));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");

    const QString token = config.apiKey.trimmed();
    if (!token.isEmpty()) {
        request.setRawHeader("Authorization", QString("Bearer %1").arg(token).toUtf8());
    }

    const QString serverPassword = config.serverPassword.trimmed();
    if (!serverPassword.isEmpty()) {
        request.setRawHeader("X-Server-Password", serverPassword.toUtf8());
    }

    return request;
}

QJsonObject LlmServiceClient::buildChatBody(const LlmServiceConfig &config,
                                            const QJsonArray &messages,
                                            const QJsonObject &options) const
{
    const QString provider = normalizedProvider(config.provider);

    QJsonObject body;
    body.insert("messages", messages);
    body.insert("stream", config.stream);

    if (!config.model.trimmed().isEmpty()) {
        body.insert("model", config.model.trimmed());
    }

    for (auto it = options.constBegin(); it != options.constEnd(); ++it) {
        body.insert(it.key(), it.value());
    }

    if (provider.contains("ollama")) {
        body.remove("max_tokens");
    }

    return body;
}

QString LlmServiceClient::extractChatContent(const QJsonObject &responseObject) const
{
    const QJsonArray choices = responseObject.value("choices").toArray();
    if (!choices.isEmpty()) {
        const QJsonObject firstChoice = choices.first().toObject();
        const QJsonObject messageObject = firstChoice.value("message").toObject();
        const QString content = messageObject.value("content").toString().trimmed();
        if (!content.isEmpty()) {
            return content;
        }

        const QString textFallback = firstChoice.value("text").toString().trimmed();
        if (!textFallback.isEmpty()) {
            return textFallback;
        }
    }

    const QJsonObject messageObject = responseObject.value("message").toObject();
    const QString directMessage = messageObject.value("content").toString().trimmed();
    if (!directMessage.isEmpty()) {
        return directMessage;
    }

    const QString ollamaResponse = responseObject.value("response").toString().trimmed();
    if (!ollamaResponse.isEmpty()) {
        return ollamaResponse;
    }

    return responseObject.value("output_text").toString().trimmed();
}

QStringList LlmServiceClient::extractModelList(const QJsonObject &responseObject) const
{
    QStringList models;

    const QJsonArray dataArray = responseObject.value("data").toArray();
    for (const QJsonValue &value : dataArray) {
        const QJsonObject modelObject = value.toObject();
        const QString id = modelObject.value("id").toString().trimmed();
        if (!id.isEmpty()) {
            models.append(id);
        }
    }

    const QJsonArray modelArray = responseObject.value("models").toArray();
    for (const QJsonValue &value : modelArray) {
        const QJsonObject modelObject = value.toObject();
        QString id = modelObject.value("name").toString().trimmed();
        if (id.isEmpty()) {
            id = modelObject.value("model").toString().trimmed();
        }
        if (!id.isEmpty()) {
            models.append(id);
        }
    }

    models.removeDuplicates();
    return models;
}

QString LlmServiceClient::normalizeErrorMessage(QNetworkReply *reply, const QByteArray &responseBody) const
{
    if (!reply) {
        return tr("未知网络错误");
    }

    if (!responseBody.isEmpty()) {
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(responseBody, &parseError);
        if (parseError.error == QJsonParseError::NoError && document.isObject()) {
            const QJsonObject root = document.object();
            const QJsonObject errorObject = root.value("error").toObject();
            const QString errorMessage = errorObject.value("message").toString().trimmed();
            if (!errorMessage.isEmpty()) {
                return errorMessage;
            }

            const QString fallbackMessage = root.value("message").toString().trimmed();
            if (!fallbackMessage.isEmpty()) {
                return fallbackMessage;
            }
        }
    }

    const QString qtError = reply->errorString().trimmed();
    return qtError.isEmpty() ? tr("请求失败") : qtError;
}

void LlmServiceClient::attachReply(QNetworkReply *reply, ReplyKind kind, int timeoutMs)
{
    ++m_activeRequests;
    if (m_activeRequests == 1) {
        emit busyChanged(true);
    }

    m_replyKinds.insert(reply, kind);

    QTimer *timer = new QTimer(reply);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, reply]() {
        if (reply) {
            reply->abort();
        }
    });
    timer->start(timeoutMs);
    m_replyTimers.insert(reply, timer);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const ReplyKind kind = m_replyKinds.value(reply, ReplyKind::ChatCompletion);

        const bool success = (reply->error() == QNetworkReply::NoError);
        const QByteArray payload = reply->readAll();

        if (!success) {
            emit requestFailed(kind == ReplyKind::ModelList ? tr("模型列表") : tr("翻译请求"),
                              normalizeErrorMessage(reply, payload));
            finalizeReply(reply);
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            emit requestFailed(kind == ReplyKind::ModelList ? tr("模型列表") : tr("翻译请求"),
                              tr("响应不是有效 JSON：%1").arg(parseError.errorString()));
            finalizeReply(reply);
            return;
        }

        const QJsonObject object = document.object();
        if (kind == ReplyKind::ModelList) {
            const QStringList models = extractModelList(object);
            if (models.isEmpty()) {
                emit requestFailed(tr("模型列表"), tr("未从响应中解析到模型列表"));
            } else {
                emit modelsReady(models);
            }
        } else {
            const QString content = extractChatContent(object);
            if (content.isEmpty()) {
                emit requestFailed(tr("翻译请求"), tr("响应中未找到可用文本内容"));
            } else {
                emit chatCompleted(content, object);
            }
        }

        finalizeReply(reply);
    });
}

void LlmServiceClient::finalizeReply(QNetworkReply *reply)
{
    if (!reply) {
        return;
    }

    m_replyKinds.remove(reply);

    QTimer *timer = m_replyTimers.take(reply);
    if (timer) {
        timer->stop();
        timer->deleteLater();
    }

    reply->deleteLater();

    --m_activeRequests;
    if (m_activeRequests <= 0) {
        m_activeRequests = 0;
        emit busyChanged(false);
    }
}
