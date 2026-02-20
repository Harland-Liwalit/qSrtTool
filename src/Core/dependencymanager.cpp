#include "dependencymanager.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QProcess>
#include <QRegularExpression>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QDir>
#include <QDateTime>
#include <QFileInfo>
#include <QUrl>
#include <QSslSocket>

DependencyManager::DependencyManager()
    : m_netManager(new QNetworkAccessManager(this)) {
}

DependencyManager::~DependencyManager() = default;

DependencyManager& DependencyManager::instance() {
    static DependencyManager instance;
    return instance;
}

void DependencyManager::initialize(const QString& dependenciesJsonPath) {
    m_dependenciesJsonPath = dependenciesJsonPath;

    QFile file(dependenciesJsonPath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit updateCheckFailed("无法打开依赖清单文件");
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject root = doc.object();
    QJsonArray deps = root["dependencies"].toArray();

    for (const QJsonValue& val : deps) {
        QJsonObject obj = val.toObject();
        DependencyInfo info;
        info.id = obj["id"].toString();
        info.name = obj["name"].toString();
        info.executable = obj["executable"].toString();
        info.versionArg = obj["versionArg"].toString();
        info.versionPattern = obj["versionPattern"].toString();
        info.latestVersionApi = obj["latestVersionApi"].toString();
        info.downloadUrlTemplate = obj["downloadUrlTemplate"].toString();
        info.minVersion = obj["minVersion"].toString();
        info.needsUpdate = false;
        info.isInstalled = false;

        m_dependencies[info.id] = info;
    }

    file.close();
}

QString DependencyManager::getLocalVersion(const QString& depId) {
    if (!m_dependencies.contains(depId))
        return QString();

    DependencyInfo& info = m_dependencies[depId];

    // 检查文件是否存在
    QString exePath = QDir::currentPath() + "/deps/" + info.executable;
    if (!QFile::exists(exePath)) {
        info.isInstalled = false;
        return QString();
    }
    info.isInstalled = true;

    // 执行版本命令
    QString output = executeVersionCommand(exePath, info.versionArg);

    // 正则提取版本号
    QRegularExpression re(info.versionPattern);
    QRegularExpressionMatch match = re.match(output);

    if (match.hasMatch()) {
        info.localVersion = match.captured(1);
        return info.localVersion;
    }

    return QString();
}

QString DependencyManager::executeVersionCommand(const QString& executable, const QString& arg) {
    QProcess process;
    process.start(executable, QStringList() << arg);
    process.waitForFinished(5000); // 5秒超时

    QString output = process.readAllStandardOutput();
    if (output.isEmpty())
        output = process.readAllStandardError();

    return output;
}

void DependencyManager::checkForUpdates() {
    if (m_busy) {
        return;
    }

    setBusy(true);

    if (!QSslSocket::supportsSsl()) {
        emit updateCheckFailed("TLS 初始化失败：缺少 OpenSSL 运行库或版本不匹配。");
        setBusy(false);
        return;
    }

    if (m_dependencies.isEmpty()) {
        emit updateCheckFailed("依赖清单未加载");
        setBusy(false);
        return;
    }

    // 先获取所有本地版本
    for (auto it = m_dependencies.begin(); it != m_dependencies.end(); ++it) {
        getLocalVersion(it.key());
    }

    // 逐个查询最新版本
    m_hadUpdateCheckError = false;
    m_pendingDownloads = 0;
    m_pendingVersionReplies = m_dependencies.size();
    if (m_pendingVersionReplies == 0) {
        emit updateCheckFinished();
        setBusy(false);
        return;
    }
    for (auto it = m_dependencies.begin(); it != m_dependencies.end(); ++it) {
        DependencyInfo& info = it.value();

        if (info.latestVersionApi.isEmpty()) {
            info.needsUpdate = false;
            if (--m_pendingVersionReplies == 0) {
                emit updateCheckFinished();
                startPendingDownloads();
            }
            continue;
        }

        QNetworkRequest request(QUrl(info.latestVersionApi));
        request.setHeader(QNetworkRequest::UserAgentHeader, "qSrtTool/1.0");

        QNetworkReply* reply = m_netManager->get(request);
        reply->setProperty("depId", info.id);

        connect(reply, &QNetworkReply::finished,
                this, &DependencyManager::onVersionReplyFinished);
    }
}

bool DependencyManager::isBusy() const
{
    return m_busy;
}

void DependencyManager::cancelAllOperations()
{
    const QList<QNetworkReply *> replies = m_netManager->findChildren<QNetworkReply *>();
    for (QNetworkReply *reply : replies) {
        if (!reply) {
            continue;
        }
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }

    m_pendingVersionReplies = 0;
    m_pendingDownloads = 0;
    setBusy(false);
}

void DependencyManager::onVersionReplyFinished() {
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply)
        return;

    QString depId = reply->property("depId").toString();

    if (reply->error() != QNetworkReply::NoError) {
        m_hadUpdateCheckError = true;
        if (m_dependencies.contains(depId)) {
            DependencyInfo& info = m_dependencies[depId];
            if (info.localVersion.isEmpty() && !info.downloadUrlTemplate.isEmpty()) {
                info.needsUpdate = true;
            }
        }
        emit updateCheckFailed(networkErrorToChinese(reply));
        reply->deleteLater();
        if (--m_pendingVersionReplies == 0) {
            emit updateCheckFinished();
            startPendingDownloads();
        }
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    QJsonObject json = doc.object();

    // 解析 GitHub API 响应
    QString latestVersion = parseLatestVersion(json, depId);

    if (m_dependencies.contains(depId)) {
        DependencyInfo& info = m_dependencies[depId];
        if (!latestVersion.isEmpty()) {
            info.latestVersion = latestVersion;
        }

        // 版本比较或缺失回退
        if (info.localVersion.isEmpty()) {
            info.needsUpdate = true; // 未安装，需要下载
        } else if (!info.latestVersion.isEmpty() && compareVersions(info.localVersion, info.latestVersion) < 0) {
            info.needsUpdate = true; // 有新版本
        } else {
            info.needsUpdate = false;
        }
    }

    reply->deleteLater();

    if (--m_pendingVersionReplies == 0) {
        emit updateCheckFinished();
        startPendingDownloads();
    }
}

QString DependencyManager::parseLatestVersion(const QJsonObject& json, const QString& depId) {
    // GitHub Releases API 返回 tag_name
    QString tagName = json["tag_name"].toString();

    // 清理版本号（去掉 v 前缀等）
    tagName.remove(QRegularExpression("^v"));

    return tagName;
}

int DependencyManager::compareVersions(const QString& v1, const QString& v2) {
    // 简单版本比较（支持 x.y.z 格式）
    QStringList parts1 = v1.split(".");
    QStringList parts2 = v2.split(".");

    int maxLen = qMax(parts1.size(), parts2.size());

    for (int i = 0; i < maxLen; ++i) {
        int num1 = (i < parts1.size()) ? parts1[i].toInt() : 0;
        int num2 = (i < parts2.size()) ? parts2[i].toInt() : 0;

        if (num1 < num2) return -1;
        if (num1 > num2) return 1;
    }

    return 0; // 相等
}

void DependencyManager::downloadUpdate(const QString& depId, const QString& savePath) {
    if (!m_dependencies.contains(depId))
        return;

    DependencyInfo& info = m_dependencies[depId];

    QUrl url(info.downloadUrlTemplate);
    if (!url.isValid() || url.isEmpty()) {
        emit downloadFailed(depId, "无效的下载地址");
        return;
    }

    if (!m_busy) {
        setBusy(true);
    }
    ++m_pendingDownloads;

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "qSrtTool/1.0");
#if QT_VERSION >= QT_VERSION_CHECK(5, 6, 0)
    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
#endif
    QNetworkReply* reply = m_netManager->get(request);
    reply->setProperty("depId", depId);
    reply->setProperty("savePath", savePath);
    reply->setProperty("redirectCount", 0);

    connect(reply, &QNetworkReply::downloadProgress,
            this, &DependencyManager::onDownloadProgress);
    connect(reply, &QNetworkReply::finished,
            this, &DependencyManager::onDownloadReplyFinished);
}

void DependencyManager::onDownloadProgress(qint64 received, qint64 total) {
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply)
        return;

    QString depId = reply->property("depId").toString();
    emit downloadProgress(depId, received, total);
}

void DependencyManager::onDownloadReplyFinished() {
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply)
        return;

    QString depId = reply->property("depId").toString();
    QString savePath = reply->property("savePath").toString();
    const int redirectCount = reply->property("redirectCount").toInt();

    const QVariant redirectAttr = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
    if (redirectAttr.isValid()) {
        const QUrl redirectUrl = reply->url().resolved(redirectAttr.toUrl());
        if (redirectUrl.isValid() && redirectCount < 5) {
            QNetworkRequest request(redirectUrl);
            request.setHeader(QNetworkRequest::UserAgentHeader, "qSrtTool/1.0");
#if QT_VERSION >= QT_VERSION_CHECK(5, 6, 0)
            request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
#endif
            QNetworkReply* newReply = m_netManager->get(request);
            newReply->setProperty("depId", depId);
            newReply->setProperty("savePath", savePath);
            newReply->setProperty("redirectCount", redirectCount + 1);

            connect(newReply, &QNetworkReply::downloadProgress,
                    this, &DependencyManager::onDownloadProgress);
            connect(newReply, &QNetworkReply::finished,
                    this, &DependencyManager::onDownloadReplyFinished);

            reply->deleteLater();
            return;
        }
    }

    if (reply->error() != QNetworkReply::NoError) {
        emit downloadFailed(depId, networkErrorToChinese(reply));
        reply->deleteLater();
        if (m_pendingDownloads > 0) {
            --m_pendingDownloads;
        }
        if (m_pendingDownloads == 0 && m_pendingVersionReplies == 0) {
            setBusy(false);
        }
        return;
    }

    // 保存文件
    QFile file(savePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(reply->readAll());
        file.close();

        QFileInfo savedInfo(savePath);
        if (savedInfo.suffix().compare("zip", Qt::CaseInsensitive) == 0) {
            QString extractError;
            if (!extractZipArchive(savePath, QFileInfo(savePath).absolutePath(), &extractError)) {
                emit downloadFailed(depId, extractError.isEmpty() ? "解压失败" : extractError);
                reply->deleteLater();
                if (m_pendingDownloads > 0) {
                    --m_pendingDownloads;
                }
                if (m_pendingDownloads == 0 && m_pendingVersionReplies == 0) {
                    setBusy(false);
                }
                return;
            }

            QFile::remove(savePath);
        }

        if (m_dependencies.contains(depId)) {
            m_dependencies[depId].needsUpdate = false;
        }
        emit downloadFinished(depId, savePath);
    } else {
        emit downloadFailed(depId, "无法保存文件");
    }

    reply->deleteLater();

    if (m_pendingDownloads > 0) {
        --m_pendingDownloads;
    }
    if (m_pendingDownloads == 0 && m_pendingVersionReplies == 0) {
        setBusy(false);
    }
}

bool DependencyManager::extractZipArchive(const QString& zipPath, const QString& destDir, QString* errorMessage)
{
#ifdef Q_OS_WIN
    QProcess process;
    QStringList args;
    args << "-NoProfile" << "-Command";
    const QString command = QString("Expand-Archive -LiteralPath \"%1\" -DestinationPath \"%2\" -Force")
                                .arg(QDir::toNativeSeparators(zipPath), QDir::toNativeSeparators(destDir));
    args << command;
    process.start("powershell", args);
    if (!process.waitForFinished(60000)) {
        if (errorMessage) {
            *errorMessage = "解压超时";
        }
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (errorMessage) {
            *errorMessage = "解压失败";
        }
        return false;
    }
    return true;
#else
    QProcess process;
    QStringList args;
    args << "-o" << zipPath << "-d" << destDir;
    process.start("unzip", args);
    if (!process.waitForFinished(60000)) {
        if (errorMessage) {
            *errorMessage = "解压超时";
        }
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (errorMessage) {
            *errorMessage = "解压失败";
        }
        return false;
    }
    return true;
#endif
}

QString DependencyManager::networkErrorToChinese(QNetworkReply* reply) const
{
    if (!reply) {
        return "网络错误";
    }

    const auto error = reply->error();
    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    switch (error) {
    case QNetworkReply::NoError:
        return "";
    case QNetworkReply::HostNotFoundError:
        return "无法解析服务器地址";
    case QNetworkReply::TimeoutError:
        return "网络请求超时";
    case QNetworkReply::ConnectionRefusedError:
        return "服务器拒绝连接";
    case QNetworkReply::RemoteHostClosedError:
        return "服务器主动断开连接";
    case QNetworkReply::ContentNotFoundError:
        return statusCode > 0 ? QString("资源不存在（HTTP %1）").arg(statusCode) : "资源不存在";
    case QNetworkReply::AuthenticationRequiredError:
        return "需要身份验证";
    case QNetworkReply::ProxyAuthenticationRequiredError:
        return "代理服务器需要身份验证";
    case QNetworkReply::SslHandshakeFailedError:
        return "TLS 初始化失败：SSL 握手失败，请检查 OpenSSL 运行库。";
    case QNetworkReply::TemporaryNetworkFailureError:
        return "临时网络故障，请稍后重试";
    case QNetworkReply::NetworkSessionFailedError:
        return "网络会话不可用";
    case QNetworkReply::OperationCanceledError:
        return "操作已取消";
    default:
        return statusCode > 0 ? QString("网络错误（HTTP %1）").arg(statusCode) : "网络错误";
    }
}

void DependencyManager::saveVersionCache(const QString& cachePath) {
    QJsonObject root;
    root["lastCheckTime"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QJsonObject depsObj;
    for (auto it = m_dependencies.begin(); it != m_dependencies.end(); ++it) {
        const DependencyInfo& info = it.value();
        QJsonObject depObj;
        depObj["localVersion"] = info.localVersion;
        depObj["latestVersion"] = info.latestVersion;
        depObj["lastChecked"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        depObj["needsUpdate"] = info.needsUpdate;
        depsObj[it.key()] = depObj;
    }

    root["dependencies"] = depsObj;

    QFile file(cachePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson());
        file.close();
    }
}

void DependencyManager::loadVersionCache(const QString& cachePath) {
    QFile file(cachePath);
    if (!file.open(QIODevice::ReadOnly))
        return;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject root = doc.object();
    QJsonObject depsObj = root["dependencies"].toObject();

    for (auto it = depsObj.begin(); it != depsObj.end(); ++it) {
        if (m_dependencies.contains(it.key())) {
            QJsonObject depObj = it.value().toObject();
            DependencyInfo& info = m_dependencies[it.key()];
            info.localVersion = depObj["localVersion"].toString();
            info.latestVersion = depObj["latestVersion"].toString();
            info.needsUpdate = depObj["needsUpdate"].toBool();
        }
    }

    file.close();
}

bool DependencyManager::needsUpdate(const QString& depId) const {
    if (!m_dependencies.contains(depId))
        return false;
    return m_dependencies[depId].needsUpdate;
}

QList<DependencyInfo> DependencyManager::getAllDependencies() const {
    return m_dependencies.values();
}

bool DependencyManager::isInstalled(const QString& depId) const {
    if (!m_dependencies.contains(depId))
        return false;
    return m_dependencies[depId].isInstalled;
}

void DependencyManager::setBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }

    m_busy = busy;
    emit busyChanged(m_busy);
}

void DependencyManager::startPendingDownloads()
{
    if (m_pendingVersionReplies > 0) {
        return;
    }

    const QString depsDir = QDir::currentPath() + "/deps";
    QDir().mkpath(depsDir);

    for (auto it = m_dependencies.begin(); it != m_dependencies.end(); ++it) {
        DependencyInfo& info = it.value();
        if (!info.needsUpdate) {
            continue;
        }

        const QUrl url(info.downloadUrlTemplate);
        QString fileName = QFileInfo(url.path()).fileName();
        if (fileName.isEmpty()) {
            fileName = info.id + ".bin";
        }

        const QString savePath = QDir(depsDir).filePath(fileName);
        downloadUpdate(info.id, savePath);
    }

    if (m_pendingDownloads == 0) {
        setBusy(false);
    }
}
