#ifndef LLMSERVICECLIENT_H
#define LLMSERVICECLIENT_H

#include <QObject>
#include <QHash>
#include <QStringList>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkRequest>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

struct LlmServiceConfig
{
    QString provider;
    QString baseUrl;
    QString apiKey;
    QString serverPassword;
    QString model;
    bool stream = false;
    int timeoutMs = 60000;

    // 规范化基础地址（去尾斜杠、按 provider 做兼容修正）。
    QString normalizedBaseUrl() const;
    // 判断配置是否可用于发起请求。
    bool isValid() const;

    // 根据 provider 返回默认基础地址。
    static QString defaultBaseUrlForProvider(const QString &provider);
};

class LlmServiceClient : public QObject
{
    Q_OBJECT

public:
    explicit LlmServiceClient(QObject *parent = nullptr);
    ~LlmServiceClient() override;

    // 请求远端模型列表。
    void requestModels(const LlmServiceConfig &config);
    // 发送聊天补全请求（支持流式与非流式）。
    void requestChatCompletion(const LlmServiceConfig &config,
                               const QJsonArray &messages,
                               const QJsonObject &options = QJsonObject());
    // 取消当前所有进行中的网络请求。
    void cancelAll();

signals:
    void modelsReady(const QStringList &models);
    void chatCompleted(const QString &content, const QJsonObject &rawResponse);
    void streamChunkReceived(const QString &chunk, const QString &aggregatedContent);
    void requestFailed(const QString &stage, const QString &message);
    void busyChanged(bool busy);

private:
    enum class ReplyKind {
        ModelList,
        ChatCompletion
    };

    void sendRequest(const QNetworkRequest &request,
                     const QByteArray &payload,
                     ReplyKind kind,
                     int timeoutMs);

    QNetworkRequest buildRequest(const LlmServiceConfig &config, const QString &endpointPath) const;
    QJsonObject buildChatBody(const LlmServiceConfig &config,
                              const QJsonArray &messages,
                              const QJsonObject &options) const;

    QString extractChatContent(const QJsonObject &responseObject) const;
    QStringList extractModelList(const QJsonObject &responseObject) const;
    QString extractStreamDelta(const QJsonObject &object, bool *done = nullptr) const;
    QString normalizeErrorMessage(QNetworkReply *reply, const QByteArray &responseBody) const;

    void processStreamingPayload(QNetworkReply *reply, const QByteArray &payloadChunk);
    void consumeStreamingLine(QNetworkReply *reply, const QByteArray &line);

    void attachReply(QNetworkReply *reply, ReplyKind kind, int timeoutMs, const QByteArray &payload);
    void finalizeReply(QNetworkReply *reply);

    QNetworkAccessManager *m_networkManager = nullptr;
    QHash<QNetworkReply *, ReplyKind> m_replyKinds;
    QHash<QNetworkReply *, QTimer *> m_replyTimers;
    QHash<QNetworkReply *, bool> m_replyTimedOut;
    QHash<QNetworkReply *, int> m_replyTimeoutMs;
    QHash<QNetworkReply *, QByteArray> m_replyRequestPayload;
    QHash<QNetworkReply *, QString> m_replyRequestUrl;
    QHash<QNetworkReply *, bool> m_replyStreaming;
    QHash<QNetworkReply *, QByteArray> m_streamBuffers;
    QHash<QNetworkReply *, QString> m_streamAccumulated;
    int m_activeRequests = 0;
};

#endif // LLMSERVICECLIENT_H
