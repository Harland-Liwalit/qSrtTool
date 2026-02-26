#ifndef SUBTITLETRANSLATION_H
#define SUBTITLETRANSLATION_H

#include <QJsonObject>
#include <QStringList>
#include <QWidget>

struct LlmServiceConfig;
class LlmServiceClient;

namespace Ui {
class SubtitleTranslation;
}

class SubtitleTranslation : public QWidget
{
    Q_OBJECT

public:
    explicit SubtitleTranslation(QWidget *parent = nullptr);
    ~SubtitleTranslation();

private:
    void initializePresetStorage();
    void refreshPresetList(const QString &preferredPath = QString());
    QString selectedPresetPath() const;
    LlmServiceConfig collectServiceConfig();
    void applyProviderDefaults(bool forceResetHost = false);
    void loadStoredSecrets();
    void updateSecretInputState();
    void persistSecret(const QString &storageKey, const QString &plainSecret);
    QString resolveSecretForRequest(const QString &storageKey,
                                    const QString &inputText,
                                    QString *cachedSecret);
    void appendOutputMessage(const QString &message);

    void importPresetToStorage();
    void openPromptEditingDialog();
    void importSrtFile();
    void refreshRemoteModels();
    void sendCommunicationProbe();

private slots:
    void onModelsReady(const QStringList &models);
    void onChatCompleted(const QString &content, const QJsonObject &rawResponse);
    void onRequestFailed(const QString &stage, const QString &message);
    void onBusyChanged(bool busy);

private:

    Ui::SubtitleTranslation *ui;
    QString m_presetDirectory;
    LlmServiceClient *m_llmClient = nullptr;
    QString m_savedApiKey;
    QString m_savedServerPassword;
};

#endif // SUBTITLETRANSLATION_H
