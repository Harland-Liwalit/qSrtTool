# Translator 模块文档

## 模块概述

Translator 负责字幕翻译全流程：预设管理、提示词组装、模型通信、流式预览、分段续译/重译、中间导出与最终合并。

### 主要职责

1. 翻译任务配置（Provider / Host / Model / Prompt / 参数）
2. 与 LLM 服务通信（模型列表、Chat Completion、流式增量）
3. SRT 解析、预览清洗、分段请求与续译控制
4. 任务控制（停止、重译本段、按时间范围部分重译）
5. 中间文件输出与最终按时间戳合并导出

---

## 核心类

### 1) SubtitleTranslation

文件：`subtitletranslation.h` / `subtitletranslation.cpp` / `subtitletranslation.ui`

- 翻译页主控类，负责 UI 交互、任务调度、导出流程
- 持有 `LlmServiceClient` 与 `TranslationFlowState`

### 2) LlmServiceClient

文件：`llmserviceclient.h` / `llmserviceclient.cpp`

- 负责 HTTP 请求发送、超时/取消、错误归一化
- 支持非流式与流式响应解析

### 3) ApiFormatManager

文件：`apiformatmanager.h` / `apiformatmanager.cpp`

- 负责不同 Provider 的端点与请求体格式适配
- 统一处理 `model list` 与 `chat completion` 的构造差异

### 4) PromptRequestComposer

文件：`promptrequestcomposer.h` / `promptrequestcomposer.cpp`

- 把自然语言指令、语言对、预设内容组合成最终提示词
- 构造单轮消息数组供通信层发送

### 5) TranslationFlowState

文件：`translationflowstate.h` / `translationflowstate.cpp`

- 维护续译/重译状态机（游标、停止点、等待导出、上下文）
- 负责“每次翻译条数”动态切片下的请求推进

### 6) PromptEditing

文件：`promptediting.h` / `promptediting.cpp` / `promptediting.ui`

- 负责翻译预设的可视化编辑、导入导出与 JSON 预览

---

## 重要调用链（按当前实现更新）

### A. 开始翻译（动态分段）

```text
SubtitleTranslation::startSegmentedTranslation()
  -> 读取并解析 SRT（parseSrtEntries）
  -> 组装配置/提示词上下文（collectServiceConfig + PromptComposeInput）
  -> TranslationFlowState::begin(totalEntries)
  -> SubtitleTranslation::sendCurrentSegmentRequest()
       -> TranslationFlowState::prepareNextRequest(chunkSize)
       -> 构造 messages（含上一段文风上下文）
       -> LlmServiceClient::requestChatCompletion(...)
```

说明：`chunkSize` 每次发送前实时读取 UI 输入，不在任务开始时固化。

### B. 流式预览刷新

```text
LlmServiceClient::streamChunkReceived
  -> SubtitleTranslation::onStreamChunkReceived(...)
  -> 节流计时器触发 flushPendingStreamPreview()
  -> updateLivePreview(raw)
  -> cleanSrtPreviewText(raw)（仅正则裁剪，不改原消息内容）
```

说明：预览区显示“裁剪后的原文块”，不再重编序号或改写文本。

### C. 一段完成后导出并续译

```text
onChatCompleted -> applySegmentTranslationResult()
  -> m_translatedByStartMs[startMs] = entry（同时间戳覆盖）
  -> TranslationFlowState::markSegmentCompleted(cleanPreview)

用户点击导出：onExportSrtClicked()
  -> writeCurrentSegmentIntermediateFile()
  -> TranslationFlowState::advanceAfterExport()
     -> 若仍有剩余：sendCurrentSegmentRequest()
     -> 否则：exportFinalMergedSrt()
```

说明：每段完成后必须先导出，导出动作既生成中间文件，也驱动下一段发送。

### D. 停止与重译

```text
onStopTaskClicked()
  -> TranslationFlowState::stopActiveTask()
  -> LlmServiceClient::cancelAll()

onRetryActionClicked() [重译本段]
  -> 从停止点起清理已翻译映射
  -> TranslationFlowState::restartFromStopped(currentChunkSize)
  -> sendCurrentSegmentRequest()
```

说明：停止后修改“每次翻译条数”会在重译时生效。

### E. 最终合并规则

```text
mergedTranslatedEntriesByTimestamp()
  -> 按 startMs 升序（相同 startMs 再按 endMs）
  -> 同 startMs 仅保留一条（QMap 键覆盖）
exportFinalMergedSrt()
  -> serializeSrtEntries(..., reindex=true)
```

说明：最终输出保持时间顺序；时间戳重合时保留同时间的一条结果。

---

## 当前关键行为约定

1. 翻译请求默认不设置超时（`timeoutMs=0`）避免长任务被本地超时中断。
2. 流式与非流式均会经过同一预览清洗入口，但仅做“提取”不做“改写”。
3. 任务状态由 `TranslationFlowState` 统一维护，UI 层不再分散维护重译/续译游标。
4. Provider 格式差异必须通过 `ApiFormatManager` 统一处理，不在业务层硬编码。

---

## 信号与错误处理

- `modelsReady(QStringList)`：模型列表返回
- `chatCompleted(QString, QJsonObject)`：翻译响应完成
- `streamChunkReceived(QString, QString)`：流式增量
- `requestFailed(QString, QString)`：请求失败
- `busyChanged(bool)`：网络忙闲状态

常见处理路径：

- 配置无效：阻止发送并弹窗提示
- 网络/HTTP 错误：输出归一化错误 + 完整响应摘要
- 用户主动停止：请求 `cancelAll`，状态切换为可重译

---

## 文件清单

- `subtitletranslation.h/.cpp/.ui`：翻译页面主流程
- `llmserviceclient.h/.cpp`：模型服务通信层
- `apiformatmanager.h/.cpp`：多 Provider 格式适配
- `promptrequestcomposer.h/.cpp`：提示词组装
- `translationflowstate.h/.cpp`：续译/重译状态机
- `promptediting.h/.cpp/.ui`：预设编辑器
