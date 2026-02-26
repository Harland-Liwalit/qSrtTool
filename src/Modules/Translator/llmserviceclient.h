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

    QString normalizedBaseUrl() const;
    bool isValid() const;

    static QString defaultBaseUrlForProvider(const QString &provider);
};

class LlmServiceClient : public QObject
{
    Q_OBJECT

public:
    explicit LlmServiceClient(QObject *parent = nullptr);
    ~LlmServiceClient() override;

    void requestModels(const LlmServiceConfig &config);
    void requestChatCompletion(const LlmServiceConfig &config,
                               const QJsonArray &messages,
                               const QJsonObject &options = QJsonObject());
    void cancelAll();

signals:
    void modelsReady(const QStringList &models);
    void chatCompleted(const QString &content, const QJsonObject &rawResponse);
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
    QString normalizeErrorMessage(QNetworkReply *reply, const QByteArray &responseBody) const;

    void attachReply(QNetworkReply *reply, ReplyKind kind, int timeoutMs);
    void finalizeReply(QNetworkReply *reply);

    QNetworkAccessManager *m_networkManager = nullptr;
    QHash<QNetworkReply *, ReplyKind> m_replyKinds;
    QHash<QNetworkReply *, QTimer *> m_replyTimers;
    int m_activeRequests = 0;
};

#endif // LLMSERVICECLIENT_H
