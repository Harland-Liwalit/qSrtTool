#ifndef TRANSLATIONFLOWSTATE_H
#define TRANSLATIONFLOWSTATE_H

#include <QString>

class TranslationFlowState
{
public:
    struct RequestInfo
    {
        bool valid = false;
        int segmentIndex = -1;
        int estimatedTotalSegments = 0;
        int startIndex = -1;
        int count = 0;
    };

    // 重置全部运行态（续译游标、停止点、上下文、序号）。
    void reset();
    // 初始化一次全量翻译任务。
    void begin(int totalEntries);
    // 初始化一次“部分重译”任务。
    void restartWithPartialEntries(int totalEntries);

    // 按当前 chunk 大小计算下一次请求区间与段序信息。
    RequestInfo prepareNextRequest(int chunkSize);
    // 标记当前段完成，并记录该段译文用于下一段文风上下文。
    void markSegmentCompleted(const QString &cleanPreview);
    // 导出当前段后推进游标；返回值表示是否还有下一段。
    bool advanceAfterExport();
    // 标记任务已完成（进入可部分重译状态）。
    void markTaskCompleted();

    // 标记用户请求停止（供失败回调消费）。
    void markStopRequested();
    // 消费“停止请求”标记，避免重复处理。
    bool consumeStopRequested();
    // 停止当前任务并记录可重译起点。
    void stopActiveTask();
    // 从停止点恢复重译，chunkSize 允许按最新 UI 值生效。
    bool restartFromStopped(int chunkSize);

    // 获取中间文件递增序号（segment_XXX.srt）。
    int takeIntermediateSerial();

    int currentSegment() const;
    int lastRequestStartIndex() const;
    int lastRequestCount() const;
    int stoppedEntryIndex() const;
    int totalEntries() const;

    bool isWaitingExport() const;
    bool isTaskCompleted() const;
    bool hasRunningOrPendingTask() const;
    bool hasStoppedRetryPoint() const;

    const QString &previousSegmentContext() const;

private:
    int m_totalEntries = 0;
    int m_currentSegment = -1;
    int m_nextEntryIndex = 0;
    int m_lastRequestStartIndex = -1;
    int m_lastRequestCount = 0;
    int m_stoppedEntryIndex = -1;
    int m_intermediateSerial = 0;

    bool m_waitingExport = false;
    bool m_userStopped = false;
    bool m_taskCompleted = false;

    QString m_previousSegmentContext;
};

#endif // TRANSLATIONFLOWSTATE_H
