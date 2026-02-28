#ifndef SUBTITLETRANSLATION_H
#define SUBTITLETRANSLATION_H

#include "llmserviceclient.h"
#include "promptrequestcomposer.h"

#include <QJsonObject>
#include <QMap>
#include <QStringList>
#include <QVector>
#include <QWidget>

class LlmServiceClient;

namespace Ui {
class SubtitleTranslation;
}

class SubtitleTranslation : public QWidget
{
    Q_OBJECT

    struct SubtitleEntry
    {
        int index = 0;
        qint64 startMs = 0;
        qint64 endMs = 0;
        QString startText;
        QString endText;
        QString text;
    };

public:
    explicit SubtitleTranslation(QWidget *parent = nullptr);
    ~SubtitleTranslation();

private:
    void initializePresetStorage();
    void refreshPresetList(const QString &preferredPath = QString());
    QString selectedPresetPath() const;
    QJsonObject loadPresetObject(const QString &presetPath) const;
    bool savePresetObject(const QString &presetPath, const QJsonObject &presetObject) const;
    void applySharedPresetParameters();
    void syncSharedParametersToPreset();
    LlmServiceConfig collectServiceConfig();
    void applyProviderDefaults(bool forceResetHost = false);
    void loadStoredSecrets();
    void loadStoredNaturalInstruction();
    void persistNaturalInstruction();
    void loadUiPreferences();
    void persistUiPreferences();
    QString buildAutoInstructionText() const;
    void updateSecretInputState();
    void persistSecret(const QString &storageKey, const QString &plainSecret);
    QString resolveSecretForRequest(const QString &storageKey,
                                    const QString &inputText,
                                    QString *cachedSecret);
    void appendOutputMessage(const QString &message);
    void renderOutputPanel();

    QVector<SubtitleEntry> parseSrtEntries(const QString &srtText) const;
    QString serializeSrtEntries(const QVector<SubtitleEntry> &entries, bool reindex) const;
    QString normalizeTimelineToken(const QString &timelineToken) const;
    qint64 timelineToMs(const QString &timelineToken) const;
    QString msToTimeline(qint64 ms) const;

    void startSegmentedTranslation();
    void sendCurrentSegmentRequest();
    QVector<SubtitleEntry> currentSegmentSourceEntries() const;
    QString buildSegmentPromptSrt(const QVector<SubtitleEntry> &entries) const;
    QString cleanSrtPreviewText(const QString &rawText) const;
    void applySegmentTranslationResult(const QString &rawResponse);
    void updateLivePreview(const QString &rawResponse);

    bool prepareExportTargetPath();
    void writeCurrentSegmentIntermediateFile();
    void exportFinalMergedSrt();
    QVector<SubtitleEntry> mergedTranslatedEntriesByTimestamp() const;

    void resetTranslationSessionState();

    void importPresetToStorage();
    void openPromptEditingDialog();
    void importSrtFile();
    void refreshRemoteModels();
    void sendCommunicationProbe();
    void onPresetSelectionChanged();
    void onNaturalInstructionChanged();

private slots:
    void onModelsReady(const QStringList &models);
    void onChatCompleted(const QString &content, const QJsonObject &rawResponse);
    void onStreamChunkReceived(const QString &chunk, const QString &aggregatedContent);
    void onRequestFailed(const QString &stage, const QString &message);
    void onBusyChanged(bool busy);
    void onExportSrtClicked();
    void onCopyResultClicked();
    void onClearOutputClicked();

private:

    Ui::SubtitleTranslation *ui;
    QString m_presetDirectory;
    LlmServiceClient *m_llmClient = nullptr;
    QString m_savedApiKey;
    QString m_savedServerPassword;
    bool m_syncingSharedParameters = false;
    bool m_loadingUiPreferences = false;

    QStringList m_outputLogLines;
    QString m_outputPreviewText;

    QVector<SubtitleEntry> m_sourceEntries;
    QVector<QVector<SubtitleEntry>> m_segments;
    QMap<qint64, SubtitleEntry> m_translatedByStartMs;

    int m_currentSegment = -1;
    bool m_waitingExportToContinue = false;
    QString m_currentSegmentRawResponse;
    QString m_currentSegmentCleanPreview;
    QString m_lastFinalMergedSrt;
    QString m_exportTargetPath;
    LlmServiceConfig m_activeConfig;
    QJsonObject m_activeOptions;
    PromptComposeInput m_activeComposeInput;
};

#endif // SUBTITLETRANSLATION_H
