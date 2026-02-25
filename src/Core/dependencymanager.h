#ifndef DEPENDENCYMANAGER_H
#define DEPENDENCYMANAGER_H

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QHash>
#include <QNetworkAccessManager>

class QFile;
class QNetworkReply;

struct DependencyInfo {
    QString id;
    QString name;
    QString executable;
    QString versionArg;
    QString versionPattern;
    QString latestVersionApi;
    QString downloadUrlTemplate;
    QString minVersion;
    QString installSubDir;

    // 运行时信息
    QString localVersion;
    QString latestVersion;
    bool needsUpdate;
    bool isInstalled;
};

class DependencyManager : public QObject {
    Q_OBJECT
public:
    static DependencyManager& instance();

    // 初始化（加载依赖清单）
    void initialize(const QString& dependenciesJsonPath);

    // 获取所有依赖信息
    QList<DependencyInfo> getAllDependencies() const;

    // 检查单个依赖是否已安装
    bool isInstalled(const QString& depId) const;

    // 获取本地版本号（通过命令行）
    QString getLocalVersion(const QString& depId);

    // 检查更新（联网）
    void checkForUpdates();

    // 当前是否有正在执行的依赖任务
    bool isBusy() const;

    // 取消当前所有依赖相关网络任务
    void cancelAllOperations();

    // 下载更新
    void downloadUpdate(const QString& depId, const QString& savePath);

    // 是否需要更新
    bool needsUpdate(const QString& depId) const;

    // 保存版本缓存
    void saveVersionCache(const QString& cachePath);

    // 加载版本缓存
    void loadVersionCache(const QString& cachePath);

signals:
    void busyChanged(bool busy);
    void updateCheckFinished();
    void updateCheckFailed(const QString& error);
    void downloadProgress(const QString& depId, qint64 received, qint64 total);
    void downloadFinished(const QString& depId, const QString& savePath);
    void downloadFailed(const QString& depId, const QString& error);

private slots:
    void onVersionReplyFinished();
    void onDownloadReplyFinished();
    void onDownloadProgress(qint64 received, qint64 total);
    void onDownloadReadyRead();

private:
    DependencyManager();
    ~DependencyManager();

    // 解析 GitHub API 响应
    QString parseLatestVersion(const QJsonObject& json, const QString& depId);

    // 版本比较（semver）
    int compareVersions(const QString& v1, const QString& v2);

    // 执行命令获取版本
    QString executeVersionCommand(const QString& executable, const QString& arg);

    bool extractZipArchive(const QString& zipPath, const QString& destDir, QString* errorMessage);
    QString networkErrorToChinese(QNetworkReply* reply) const;

    void setBusy(bool busy);
    void startPendingDownloads();

    QMap<QString, DependencyInfo> m_dependencies;
    QNetworkAccessManager* m_netManager;
    QString m_dependenciesJsonPath;
    int m_pendingVersionReplies = 0;
    int m_pendingDownloads = 0;
    bool m_busy = false;
    bool m_hadUpdateCheckError = false;
    QHash<QNetworkReply*, QFile*> m_activeDownloadFiles;
};

#endif // DEPENDENCYMANAGER_H
