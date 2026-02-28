#ifndef SUBTITLETRANSLATION_H
#define SUBTITLETRANSLATION_H

#include "llmserviceclient.h"
#include "promptrequestcomposer.h"
#include "translationflowstate.h"

#include <QJsonObject>
#include <QMap>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QTimer;

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
    enum class RetryMode {
        None,
        RetryCurrentSegment,
        RetryPartialRange
    };

    // 初始化预设目录并确保可读写。
    void initializePresetStorage();
    // 刷新预设下拉框，并尽量恢复目标选中项。
    void refreshPresetList(const QString &preferredPath = QString());
    // 获取当前选中的预设文件路径。
    QString selectedPresetPath() const;
    // 读取预设 JSON。
    QJsonObject loadPresetObject(const QString &presetPath) const;
    // 保存预设 JSON。
    bool savePresetObject(const QString &presetPath, const QJsonObject &presetObject) const;
    void applySharedPresetParameters();
    void syncSharedParametersToPreset();
    // 收集当前 UI 上的服务连接参数。
    LlmServiceConfig collectServiceConfig();
    // 根据 provider 填充默认地址与提示。
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

    // 解析 SRT 文本为结构化条目。
    QVector<SubtitleEntry> parseSrtEntries(const QString &srtText) const;
    // 将条目序列化为 SRT 文本（可选择重编号）。
    QString serializeSrtEntries(const QVector<SubtitleEntry> &entries, bool reindex) const;
    QString normalizeTimelineToken(const QString &timelineToken) const;
    qint64 timelineToMs(const QString &timelineToken) const;
    QString msToTimeline(qint64 ms) const;

    // 启动一次新的分段翻译任务。
    void startSegmentedTranslation();
    // 发送当前段请求（按当前“每次翻译条数”动态切分）。
    void sendCurrentSegmentRequest();
    // 获取当前请求对应的源字幕条目区间。
    QVector<SubtitleEntry> currentSegmentSourceEntries() const;
    // 构造单段的 SRT 提示文本。
    QString buildSegmentPromptSrt(const QVector<SubtitleEntry> &entries) const;
    // 对原始响应做正则裁剪，用于预览展示。
    QString cleanSrtPreviewText(const QString &rawText) const;
    // 处理单段返回结果并写入全局合并映射。
    void applySegmentTranslationResult(const QString &rawResponse);
    void updateLivePreview(const QString &rawResponse);
    void flushPendingStreamPreview();

    // 计算并准备最终导出文件路径。
    bool prepareExportTargetPath();
    // 写出当前段中间文件（segment_xxx.srt）。
    void writeCurrentSegmentIntermediateFile();
    // 导出按时间戳合并后的最终 SRT。
    void exportFinalMergedSrt();
    // 按时间顺序合并条目，同时间戳仅保留一条。
    QVector<SubtitleEntry> mergedTranslatedEntriesByTimestamp() const;

    // 重置当前翻译会话的运行态。
    void resetTranslationSessionState();
    // 从 UI 刷新活动请求上下文（供重译/续译复用）。
    bool refreshActiveRequestContextFromUi();
    void setRetryButtonState(RetryMode mode, bool enabled);

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
    void onStopTaskClicked();
    void onRetryActionClicked();
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
    bool m_outputAutoFollow = true;
    bool m_restoringOutputScroll = false;

    QVector<SubtitleEntry> m_sourceEntries;
    QVector<SubtitleEntry> m_runtimeEntries;
    QMap<qint64, SubtitleEntry> m_translatedByStartMs;

    TranslationFlowState m_flowState;
    RetryMode m_retryMode = RetryMode::None;
    QString m_currentSegmentRawResponse;
    QString m_currentSegmentCleanPreview;
    QString m_lastFinalMergedSrt;
    QString m_exportTargetPath;
    LlmServiceConfig m_activeConfig;
    QJsonObject m_activeOptions;
    PromptComposeInput m_activeComposeInput;
    QTimer *m_streamPreviewTimer = nullptr;
    QString m_pendingStreamRawContent;
};

#endif // SUBTITLETRANSLATION_H
