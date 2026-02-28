#include "translationflowstate.h"

#include <QtGlobal>

void TranslationFlowState::reset()
{
    m_totalEntries = 0;
    m_currentSegment = -1;
    m_nextEntryIndex = 0;
    m_lastRequestStartIndex = -1;
    m_lastRequestCount = 0;
    m_stoppedEntryIndex = -1;
    m_intermediateSerial = 0;
    m_waitingExport = false;
    m_userStopped = false;
    m_taskCompleted = false;
    m_previousSegmentContext.clear();
}

void TranslationFlowState::begin(int totalEntries)
{
    reset();
    m_totalEntries = qMax(0, totalEntries);
    if (m_totalEntries > 0) {
        m_currentSegment = 0;
    }
}

void TranslationFlowState::restartWithPartialEntries(int totalEntries)
{
    begin(totalEntries);
}

TranslationFlowState::RequestInfo TranslationFlowState::prepareNextRequest(int chunkSize)
{
    RequestInfo info;
    if (m_totalEntries <= 0 || m_nextEntryIndex < 0 || m_nextEntryIndex >= m_totalEntries) {
        return info;
    }

    const int chunk = qMax(1, chunkSize);
    const int count = qMin(chunk, m_totalEntries - m_nextEntryIndex);

    m_lastRequestStartIndex = m_nextEntryIndex;
    m_lastRequestCount = count;

    if (m_currentSegment < 0) {
        m_currentSegment = m_nextEntryIndex / chunk;
    }

    info.valid = true;
    info.segmentIndex = m_currentSegment;
    info.estimatedTotalSegments = qMax(m_currentSegment + 1,
                                       (m_totalEntries + chunk - 1) / chunk);
    info.startIndex = m_lastRequestStartIndex;
    info.count = m_lastRequestCount;
    return info;
}

void TranslationFlowState::markSegmentCompleted(const QString &cleanPreview)
{
    m_waitingExport = true;
    if (!cleanPreview.trimmed().isEmpty()) {
        m_previousSegmentContext = cleanPreview;
    }
}

bool TranslationFlowState::advanceAfterExport()
{
    if (!m_waitingExport || m_lastRequestStartIndex < 0 || m_lastRequestCount <= 0) {
        return false;
    }

    m_waitingExport = false;
    m_nextEntryIndex = m_lastRequestStartIndex + m_lastRequestCount;
    ++m_currentSegment;
    return m_nextEntryIndex < m_totalEntries;
}

void TranslationFlowState::markTaskCompleted()
{
    m_taskCompleted = true;
    m_waitingExport = false;
    m_currentSegment = -1;
    m_stoppedEntryIndex = -1;
}

void TranslationFlowState::markStopRequested()
{
    m_userStopped = true;
}

bool TranslationFlowState::consumeStopRequested()
{
    if (!m_userStopped) {
        return false;
    }
    m_userStopped = false;
    return true;
}

void TranslationFlowState::stopActiveTask()
{
    m_userStopped = true;
    m_stoppedEntryIndex = (m_lastRequestStartIndex >= 0) ? m_lastRequestStartIndex : m_nextEntryIndex;
    m_currentSegment = -1;
    m_waitingExport = false;
    m_taskCompleted = false;
}

bool TranslationFlowState::restartFromStopped(int chunkSize)
{
    if (m_stoppedEntryIndex < 0 || m_stoppedEntryIndex >= m_totalEntries) {
        return false;
    }

    const int chunk = qMax(1, chunkSize);
    m_nextEntryIndex = m_stoppedEntryIndex;
    m_currentSegment = m_stoppedEntryIndex / chunk;
    m_waitingExport = false;
    m_lastRequestStartIndex = -1;
    m_lastRequestCount = 0;
    m_userStopped = false;
    m_taskCompleted = false;
    return true;
}

int TranslationFlowState::takeIntermediateSerial()
{
    ++m_intermediateSerial;
    return m_intermediateSerial;
}

int TranslationFlowState::currentSegment() const
{
    return m_currentSegment;
}

int TranslationFlowState::lastRequestStartIndex() const
{
    return m_lastRequestStartIndex;
}

int TranslationFlowState::lastRequestCount() const
{
    return m_lastRequestCount;
}

int TranslationFlowState::stoppedEntryIndex() const
{
    return m_stoppedEntryIndex;
}

int TranslationFlowState::totalEntries() const
{
    return m_totalEntries;
}

bool TranslationFlowState::isWaitingExport() const
{
    return m_waitingExport;
}

bool TranslationFlowState::isTaskCompleted() const
{
    return m_taskCompleted;
}

bool TranslationFlowState::hasRunningOrPendingTask() const
{
    return m_currentSegment >= 0 || m_waitingExport;
}

bool TranslationFlowState::hasStoppedRetryPoint() const
{
    return m_stoppedEntryIndex >= 0 && m_stoppedEntryIndex < m_totalEntries;
}

const QString &TranslationFlowState::previousSegmentContext() const
{
    return m_previousSegmentContext;
}
