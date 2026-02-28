#include "llmserviceclient.h"
#include "apiformatmanager.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonValue>
#include <QIODevice>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>
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

QString persistRequestPayloadForDebug(const QByteArray &payload,
                                     const QString &requestUrl,
                                     int statusCode)
{
    if (payload.isEmpty()) {
        return QString();
    }

    const QString debugDirPath = QDir::currentPath() + "/temp/translator_http_debug";
    QDir().mkpath(debugDirPath);

    const QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz");
    const QString fileName = QStringLiteral("request_%1_%2.json")
                                 .arg(timestamp)
                                 .arg(statusCode > 0 ? QString::number(statusCode) : QStringLiteral("error"));
    const QString filePath = QDir(debugDirPath).filePath(fileName);

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return QString();
    }

    QByteArray output = payload;
    QJsonParseError parseError;
    const QJsonDocument requestDocument = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error == QJsonParseError::NoError && requestDocument.isObject()) {
        output = requestDocument.toJson(QJsonDocument::Indented);
    }

    file.write(output);
    file.write("\n\n");
    if (!requestUrl.trimmed().isEmpty()) {
        file.write(QStringLiteral("# URL: %1\n").arg(requestUrl).toUtf8());
    }
    if (statusCode > 0) {
        file.write(QStringLiteral("# HTTP Status: %1\n").arg(statusCode).toUtf8());
    }
    file.close();

    return filePath;
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

    const QString normalizedProviderName = provider.trimmed().toLower();
    if (normalizedProviderName.contains("deepseek") && base.endsWith("/v1", Qt::CaseInsensitive)) {
        base.chop(3);
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
    if (normalized.contains("deepseek")) {
        return QStringLiteral("https://api.deepseek.com");
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

    const QString provider = ApiFormatManager::providerId(config.provider, config.normalizedBaseUrl());
    const QString endpoint = ApiFormatManager::modelListEndpoint(provider);
    const QNetworkRequest request = buildRequest(config, endpoint);
    const int timeoutMs = config.timeoutMs > 0 ? config.timeoutMs : 30000;
    sendRequest(request, QByteArray(), ReplyKind::ModelList, timeoutMs);
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

    const QString provider = ApiFormatManager::providerId(config.provider, config.normalizedBaseUrl());
    const QString endpoint = ApiFormatManager::chatEndpoint(provider);

    const QJsonObject body = buildChatBody(config, messages, options);
    QNetworkRequest request = buildRequest(config, endpoint);
    request.setRawHeader("X-QSrtTool-Stream", config.stream ? "1" : "0");
    sendRequest(request,
                QJsonDocument(body).toJson(QJsonDocument::Compact),
                ReplyKind::ChatCompletion,
                config.timeoutMs);
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

    attachReply(reply, kind, timeoutMs, payload);
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
    const QString provider = ApiFormatManager::providerId(config.provider, config.normalizedBaseUrl());
    return ApiFormatManager::buildChatBody(provider,
                                           config.model,
                                           config.stream,
                                           messages,
                                           options);
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

QString LlmServiceClient::extractStreamDelta(const QJsonObject &object, bool *done) const
{
    if (done) {
        *done = false;
    }

    if (object.contains("done") && done) {
        *done = object.value("done").toBool(false);
    }

    const QJsonArray choices = object.value("choices").toArray();
    if (!choices.isEmpty()) {
        const QJsonObject choiceObject = choices.first().toObject();

        if (choiceObject.value("finish_reason").isString()) {
            const QString finishReason = choiceObject.value("finish_reason").toString().trimmed();
            if (!finishReason.isEmpty() && done) {
                *done = true;
            }
        }

        const QJsonObject deltaObject = choiceObject.value("delta").toObject();
        const QString deltaText = deltaObject.value("content").toString();
        if (!deltaText.isEmpty()) {
            return deltaText;
        }

        const QJsonObject messageObject = choiceObject.value("message").toObject();
        const QString messageText = messageObject.value("content").toString();
        if (!messageText.isEmpty()) {
            return messageText;
        }

        const QString textFallback = choiceObject.value("text").toString();
        if (!textFallback.isEmpty()) {
            return textFallback;
        }
    }

    const QJsonObject messageObject = object.value("message").toObject();
    const QString nestedContent = messageObject.value("content").toString();
    if (!nestedContent.isEmpty()) {
        return nestedContent;
    }

    const QString responseText = object.value("response").toString();
    if (!responseText.isEmpty()) {
        return responseText;
    }

    const QString outputText = object.value("output_text").toString();
    if (!outputText.isEmpty()) {
        return outputText;
    }

    return QString();
}

QString LlmServiceClient::normalizeErrorMessage(QNetworkReply *reply, const QByteArray &responseBody) const
{
    if (!reply) {
        return tr("未知网络错误");
    }

    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString bodyText = QString::fromUtf8(responseBody).trimmed();
    const QString requestUrl = m_replyRequestUrl.value(reply);
    const QByteArray requestPayloadRaw = m_replyRequestPayload.value(reply);

    const QString payloadDumpPath = persistRequestPayloadForDebug(requestPayloadRaw,
                                                                  requestUrl,
                                                                  statusCode);

    auto withRequestContext = [&](const QString &base) -> QString {
        QStringList details;
        if (!requestUrl.isEmpty()) {
            details << tr("请求地址：%1").arg(requestUrl);
        }

        if (!payloadDumpPath.isEmpty()) {
            details << tr("完整请求体已写入：%1").arg(payloadDumpPath);
        } else if (!requestPayloadRaw.isEmpty()) {
            details << tr("完整请求体写入调试文件失败");
        }

        if (details.isEmpty()) {
            return base;
        }
        return base + QStringLiteral("\n") + details.join(QStringLiteral("\n"));
    };

    if (reply->error() == QNetworkReply::OperationCanceledError && m_replyTimedOut.value(reply, false)) {
        const int timeoutMs = m_replyTimeoutMs.value(reply, 0);
        const QString timeoutText = timeoutMs > 0
                                    ? tr("请求超时（%1 ms）后被客户端中止").arg(timeoutMs)
                                    : tr("请求被客户端中止");
        if (!bodyText.isEmpty()) {
            return withRequestContext(tr("%1\nHTTP %2\n完整响应：\n%3")
                .arg(timeoutText)
                .arg(statusCode)
                .arg(bodyText));
        }
        return withRequestContext(timeoutText);
    }

    if (!responseBody.isEmpty()) {
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(responseBody, &parseError);
        if (parseError.error == QJsonParseError::NoError && document.isObject()) {
            const QJsonObject root = document.object();
            const QJsonObject errorObject = root.value("error").toObject();
            const QString errorMessage = errorObject.value("message").toString().trimmed();
            if (!errorMessage.isEmpty()) {
                return withRequestContext(tr("HTTP %1\n错误消息：%2\n完整响应：\n%3")
                    .arg(statusCode)
                    .arg(errorMessage)
                    .arg(QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented)).trimmed()));
            }

            const QString fallbackMessage = root.value("message").toString().trimmed();
            if (!fallbackMessage.isEmpty()) {
                return withRequestContext(tr("HTTP %1\n错误消息：%2\n完整响应：\n%3")
                    .arg(statusCode)
                    .arg(fallbackMessage)
                    .arg(QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented)).trimmed()));
            }

            return withRequestContext(tr("HTTP %1\n完整响应：\n%2")
                .arg(statusCode)
                .arg(QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented)).trimmed()));
        }

        return withRequestContext(tr("HTTP %1\n完整响应：\n%2").arg(statusCode).arg(bodyText));
    }

    const QString qtError = reply->errorString().trimmed();
    if (statusCode > 0) {
        return withRequestContext(qtError.isEmpty() ? tr("HTTP %1 请求失败").arg(statusCode)
                                                    : tr("HTTP %1 %2").arg(statusCode).arg(qtError));
    }
    return withRequestContext(qtError.isEmpty() ? tr("请求失败") : qtError);
}

void LlmServiceClient::attachReply(QNetworkReply *reply, ReplyKind kind, int timeoutMs, const QByteArray &payload)
{
    ++m_activeRequests;
    if (m_activeRequests == 1) {
        emit busyChanged(true);
    }

    m_replyKinds.insert(reply, kind);
    m_replyRequestPayload.insert(reply, payload);
    m_replyRequestUrl.insert(reply, reply->request().url().toString());
    const bool requestMarkedStreaming = reply->request().hasRawHeader("X-QSrtTool-Stream")
                                        && reply->request().rawHeader("X-QSrtTool-Stream") == "1";
    m_replyStreaming.insert(reply, kind == ReplyKind::ChatCompletion && requestMarkedStreaming);
    m_replyTimedOut.insert(reply, false);
    m_replyTimeoutMs.insert(reply, timeoutMs);

    if (timeoutMs > 0) {
        QTimer *timer = new QTimer(reply);
        timer->setSingleShot(true);
        connect(timer, &QTimer::timeout, this, [this, reply]() {
            if (reply) {
                m_replyTimedOut.insert(reply, true);
                reply->abort();
            }
        });
        timer->start(timeoutMs);
        m_replyTimers.insert(reply, timer);
    }

    connect(reply, &QIODevice::readyRead, this, [this, reply]() {
        if (!m_replyStreaming.value(reply, false)) {
            return;
        }

        const QByteArray payloadChunk = reply->readAll();
        if (!payloadChunk.isEmpty()) {
            processStreamingPayload(reply, payloadChunk);
        }
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const ReplyKind kind = m_replyKinds.value(reply, ReplyKind::ChatCompletion);
        const bool isStreaming = m_replyStreaming.value(reply, false);

        const bool success = (reply->error() == QNetworkReply::NoError);
        const QByteArray payload = isStreaming ? QByteArray() : reply->readAll();

        if (isStreaming) {
            const QByteArray tailChunk = reply->readAll();
            if (!tailChunk.isEmpty()) {
                processStreamingPayload(reply, tailChunk);
            }

            const QByteArray tailBuffer = m_streamBuffers.take(reply);
            if (!tailBuffer.trimmed().isEmpty()) {
                consumeStreamingLine(reply, tailBuffer);
            }
        }

        if (!success) {
            emit requestFailed(kind == ReplyKind::ModelList ? tr("模型列表") : tr("翻译请求"),
                              normalizeErrorMessage(reply, payload));
            finalizeReply(reply);
            return;
        }

        if (!isStreaming) {
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
        } else {
            const QString aggregated = m_streamAccumulated.take(reply).trimmed();
            if (aggregated.isEmpty()) {
                emit requestFailed(tr("翻译请求"), tr("流式响应结束，但未收到可用文本内容"));
            } else {
                emit chatCompleted(aggregated, QJsonObject());
            }
        }

        finalizeReply(reply);
    });
}

void LlmServiceClient::processStreamingPayload(QNetworkReply *reply, const QByteArray &payloadChunk)
{
    if (!reply || payloadChunk.isEmpty()) {
        return;
    }

    QByteArray &buffer = m_streamBuffers[reply];
    buffer.append(payloadChunk);

    int newlinePos = buffer.indexOf('\n');
    while (newlinePos >= 0) {
        const QByteArray line = buffer.left(newlinePos);
        buffer.remove(0, newlinePos + 1);
        consumeStreamingLine(reply, line);
        newlinePos = buffer.indexOf('\n');
    }
}

void LlmServiceClient::consumeStreamingLine(QNetworkReply *reply, const QByteArray &line)
{
    if (!reply) {
        return;
    }

    QByteArray trimmed = line.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    if (trimmed.startsWith("data:")) {
        trimmed = trimmed.mid(5).trimmed();
    }

    if (trimmed == "[DONE]") {
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(trimmed, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return;
    }

    bool done = false;
    const QString delta = extractStreamDelta(document.object(), &done);
    Q_UNUSED(done)
    if (delta.isEmpty()) {
        return;
    }

    QString &aggregated = m_streamAccumulated[reply];
    aggregated += delta;
    emit streamChunkReceived(delta, aggregated);
}

void LlmServiceClient::finalizeReply(QNetworkReply *reply)
{
    if (!reply) {
        return;
    }

    m_replyKinds.remove(reply);
    m_replyTimedOut.remove(reply);
    m_replyTimeoutMs.remove(reply);
    m_replyRequestPayload.remove(reply);
    m_replyRequestUrl.remove(reply);
    m_replyStreaming.remove(reply);
    m_streamBuffers.remove(reply);
    m_streamAccumulated.remove(reply);

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
