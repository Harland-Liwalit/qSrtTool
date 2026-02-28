#ifndef SUBTITLEEXTRACTION_H
#define SUBTITLEEXTRACTION_H

#include <QWidget>
#include <QIcon>
#include <QMutex>
#include <QMap>
#include <atomic>

class QTimer;
class QShowEvent;
class TranscribeWorker;
class WhisperSegmentMerger;
class WhisperCommandBuilder;
struct WhisperRuntimeSelection;

namespace Ui {
class SubtitleExtraction;
}

class SubtitleExtraction : public QWidget
{
    Q_OBJECT
    friend class TranscribeWorker;

public:
    explicit SubtitleExtraction(QWidget *parent = nullptr);
    ~SubtitleExtraction();

    /// @brief 加载视频文件到字幕提取界面
    /// @param videoPath 视频文件的绝对路径
    void loadVideoFile(const QString &videoPath);

signals:
    void statusMessage(const QString &message);
    void progressChanged(int percent);
    void requestNextStep(const QString &subtitlePath);

private:
    Ui::SubtitleExtraction *ui;
    QTimer *m_toolsSpinTimer = nullptr;
    QIcon m_toolsBaseIcon;
    int m_toolsSpinAngle = 0;
    bool m_toolsLoading = false;
    bool m_isRunning = false;
    std::atomic_bool m_cancelRequested{false};
    int m_lastProgressPercent = -1;
    QMutex m_progressLock;
    QMap<int, int> m_segmentProgress;
    QStringList m_workflowLogHistory;
    QMap<int, QString> m_activeSegmentLogLines;
    QString m_lastCompletedOutputFilePath;

    void setToolsLoading(bool loading);
    void updateToolsSpinner();

    /// @brief 获取 Whisper 模型目录（固定为构建目录下 models/whisper）
    QString whisperModelsDirPath() const;
    /// @brief 创建运行所需目录（models/whisper 与 models/LLM）
    void ensureModelDirectories();
    /// @brief 扫描并刷新模型下拉框（仅显示 models/whisper 内的模型）
    void refreshWhisperModelList();
    /// @brief 打开 Whisper 模型目录
    void openWhisperModelsDirectory();

    /// @brief 绑定页面按钮与工作流逻辑
    void setupWorkflowUi();
    /// @brief 设置运行态/空闲态 UI
    void updateRunningStateUi(bool running);
    /// @brief 执行完整识别流程：分段、识别、拼接、清理
    void startTranscriptionWorkflow();
    /// @brief 请求停止当前流程
    void requestStopWorkflow();

    /// @brief 解析依赖可执行文件路径
    QString resolveExecutableInDeps(const QStringList &candidateNames) const;
    QString resolveFfmpegPath() const;
    WhisperRuntimeSelection resolveWhisperRuntimeSelection(bool preferCuda) const;
    QString selectedModelPath() const;

    /// @brief 执行外部进程（支持停止）
    bool runProcessCancelable(const QString &program, const QStringList &arguments, QString *stdErrOutput = nullptr);
    /// @brief 获取输入媒体总时长（秒）
    bool probeDurationSeconds(const QString &ffprobePath, const QString &inputPath, double &durationSeconds);
    /// @brief 提取单个 20 分钟片段音频
    bool extractSegmentAudio(const QString &ffmpegPath,
                             const QString &inputPath,
                             double startSeconds,
                             double durationSeconds,
                             const QString &segmentAudioPath);
    /// @brief 调用 whisper 对单段进行识别并生成 SRT
    bool transcribeSegment(const QString &whisperPath,
                           const QString &modelPath,
                           const QString &segmentAudioPath,
                           const QString &segmentOutputBasePath,
                           const QString &languageCode,
                           bool useGpu,
                           int whisperThreadCount,
                           int segmentIndex,
                           int segmentCount,
                           double segmentDurationSeconds);

    /// @brief SRT 时间与拼接辅助
    static bool parseSrtTimestamp(const QString &text, qint64 &milliseconds);
    static QString formatSrtTimestamp(qint64 milliseconds);
    static QString shiftedSrtContent(const QString &srtContent, qint64 offsetMs);
    static QString languageCodeFromUiText(const QString &uiText);
        static QString outputFileExtensionFromUiText(const QString &uiText);
        static QString srtToPlainText(const QString &srtContent);
        static QString srtToTimestampedText(const QString &srtContent);
        static QString srtToWebVtt(const QString &srtContent);
    static QString segmentRangeLabel(double startSeconds, double durationSeconds);
        QString buildParallelStatusSummaryLocked(int segmentCount, int overallPercent) const;
        void renderWorkflowLogConsole();
        void updateSegmentProgressLog(int segmentIndex, int progressPercent, bool finished);

    /// @brief 追加一行日志到页面日志栏
    void appendWorkflowLog(const QString &message);
    /// @brief 设置日志栏初始状态
    void initializeLogConsole();

protected:
    void showEvent(QShowEvent *event) override;
};

#endif // SUBTITLEEXTRACTION_H
