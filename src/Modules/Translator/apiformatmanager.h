#ifndef APIFORMATMANAGER_H
#define APIFORMATMANAGER_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

class ApiFormatManager
{
public:
    static QString providerId(const QString &provider, const QString &baseUrl);
    static QString modelListEndpoint(const QString &providerId);
    static QString chatEndpoint(const QString &providerId);

    static QJsonObject buildChatBody(const QString &providerId,
                                     const QString &model,
                                     bool stream,
                                     const QJsonArray &messages,
                                     const QJsonObject &options);
};

#endif // APIFORMATMANAGER_H
