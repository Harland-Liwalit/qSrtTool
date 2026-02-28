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

    void reset();
    void begin(int totalEntries);
    void restartWithPartialEntries(int totalEntries);

    RequestInfo prepareNextRequest(int chunkSize);
    void markSegmentCompleted(const QString &cleanPreview);
    bool advanceAfterExport();
    void markTaskCompleted();

    void markStopRequested();
    bool consumeStopRequested();
    void stopActiveTask();
    bool restartFromStopped(int chunkSize);

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
