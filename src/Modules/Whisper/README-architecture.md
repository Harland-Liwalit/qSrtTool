# Whisper 模块架构更新（最新）

## 架构演进

从单体设计 (v1) 演进到三层分离设计 (v2)：

### v1 - 单体设计（已弃用部分）
- SubtitleExtraction 包含所有逻辑：命令构建、分段合并、线程管理
- 代码复用性差，难以测试，维护困难

### v2 - 三层分离设计（当前）
- **界面层**：SubtitleExtraction （UI 与工作流编排）
- **处理层**：WhisperCommandBuilder（命令构建） + WhisperSegmentMerger（段落合并）
- **执行层**：TranscribeWorker（并行转录）

---

## 核心组件详解

### 1. WhisperSegmentMerger（分段合并器）

**文件**：`whispersegmentmerger.h/cpp`

**职责**：所有 SRT 字幕处理的单一职责组件

**关键 API**：
| 方法 | 作用 | 参数 | 返回值 |
|------|------|------|--------|
| `parseSrtTimestamp()` | 解析时间戳字符串 | "HH:MM:SS,mmm" | bool + 毫秒值 |
| `formatSrtTimestamp()` | 格式化毫秒值 | 毫秒数 | "HH:MM:SS,mmm" |
| `shiftedSrtContent()` | 时间轴偏移 | SRT内容 + 偏移ms | 偏移后SRT |
| `srtToPlainText()` | 提取纯文本 | SRT内容 | 纯文本 |
| `srtToTimestampedText()` | 带时间文本 | SRT内容 | [时间范围] 文本 |
| `srtToWebVtt()` | WebVTT转换 | SRT内容 | WebVTT内容 |
| `mergeSegmentSrtFiles()` | **合并主流程** | 文件列表+时长+格式 | 合并后内容 |

**关键改进**：
- 封装所有时间戳处理逻辑（解析、格式化、偏移）
- 单一方法 `mergeSegmentSrtFiles()` 处理整个合并流程
- 自动处理全局索引重编、时间轴偏移、格式转换
- 支持 4 种输出格式（SRT、TXT、TXT+时间、WebVTT）

**使用示例**：
```cpp
// 合并 3 个分段（每段 5 分钟）为 WebVTT 格式
QStringList segments = {"seg_0.srt", "seg_1.srt", "seg_2.srt"};
QString result = WhisperSegmentMerger::mergeSegmentSrtFiles(
    segments,                                    // 分段文件列表
    300.0,                                       // 每段 5 分钟（秒）
    WhisperSegmentMerger::Format_WebVTT          // 输出格式
);

// 写入文件
QFile out("final.vtt");
out.open(QIODevice::WriteOnly | QIODevice::Text);
out.write(result.toUtf8());
out.close();
```

---

### 2. WhisperCommandBuilder（命令构建器）

**文件**：`whispercommandbuilder.h/cpp`

**职责**：FFmpeg 和 Whisper 命令行参数的统一构建

**关键 API**：
| 方法 | 作用 | 参数 |
|------|------|------|
| `buildFfmpegExtractArgs()` | 音频提取命令 | 输入路径、起始秒数、时长、输出路径 |
| `buildWhisperTranscribeArgs()` | 转录命令 | 模型路径、音频文件、输出前缀、语言、GPU标志 |
| `languageCodeFromUiText()` | 语言代码转换 | UI文本（中文/English/...） |
| `outputFileExtensionFromUiText()` | 扩展名转换 | 格式文本（SRT/TXT/WebVTT/...） |

**关键改进**：
- 将硬编码的参数替换为统一的参数构建逻辑
- FFmpeg 参数自动设置为 16kHz、单声道、PCM 格式
- Whisper 参数自动检测 CPU 核心数，设置 `-t` 参数
- 支持条件参数（语言、GPU 标志、禁用语言检测）

**自动优化参数**：
```cpp
// FFmpeg 始终使用
"-y -hide_banner -loglevel error"     // 静默模式
"-ac 1 -ar 16000 -c:a pcm_s16le"      // 16kHz 单声道 PCM

// Whisper 自动设置
"-t " + QString::number(QThread::idealThreadCount())  // 多线程优化
"-np"  // （当指定语言时）跳过语言检测，加速转录
```

**使用示例**：
```cpp
// 构建 FFmpeg 提取命令（提取 5-10 分钟的音频段）
QStringList ffmpegCmd = WhisperCommandBuilder::buildFfmpegExtractArgs(
    "input.mp4",        // 输入视频
    300.0,              // 起始 5 分钟
    300.0,              // 时长 5 分钟
    "segment_01.wav"    // 输出音频文件
);
// 结果：["-y", "-hide_banner", ..., "-ss", "300.000", "-t", "300.000", ...]

// 构建 Whisper 转录命令（中文、自动线程数、跳过语言检测）
QStringList whisperCmd = WhisperCommandBuilder::buildWhisperTranscribeArgs(
    "models/ggml-large-v2.bin",  // 模型路径
    "segment_01.wav",             // 输入音频
    "output/segment_01",          // 输出前缀
    "zh",                         // 语言代码
    false                         // 不启用 GPU（当前为 CPU-only）
);
// 结果：["-m", "...", "-f", "...", "-t", "24", "-l", "zh", "-np", ...]
```

---

### 3. SubtitleExtraction（UI 与工作流编排）

**文件**：`subtitleextraction.h/cpp`

**职责**：页面交互与工作流编排，已重构为使用新组件

**改进内容**：
1. 命令参数构建 → 委托给 `WhisperCommandBuilder`
2. 分段合并逻辑 → 委托给 `WhisperSegmentMerger`
3. 格式转换方法 → 改为调用 `WhisperSegmentMerger` 的对应方法
4. 时间戳处理 → 改为调用 `WhisperSegmentMerger` 的对应方法

**向后兼容性**：
- 保留原有的 public 方法（`languageCodeFromUiText()` 等）
- 这些方法现在是 `WhisperCommandBuilder` 的包装器
- 现有代码无需修改，可继续使用原有接口

**当前流程**（简化视图）：
```
开始转写按钮
  ↓
startTranscriptionWorkflow()
  ├─ 校验并解析依赖
  ├─ 探测视频时长
  ├─ 计算分段数（5分钟/段）
  ├─ 第一阶段：并行提取音频
  │   ├─ extractSegmentAudio()
  │   │   └─ 使用 WhisperCommandBuilder::buildFfmpegExtractArgs()
  │   └─ (4-6 个并行工作线程)
  ├─ 第二阶段：并行转录字幕
  │   ├─ TranscribeWorker::run()
  │   │   └─ 使用 WhisperCommandBuilder::buildWhisperTranscribeArgs()
  │   └─ (4-6 个并行工作线程)
  ├─ 第三阶段：合并与转换
  │   ├─ WhisperSegmentMerger::mergeSegmentSrtFiles()
  │   │   ├─ 遍历分段 SRT 文件
  │   │   ├─ 时间轴偏移（通过 shiftedSrtContent()）
  │   │   ├─ 全局索引重编
  │   │   └─ 格式转换
  │   └─ 写出最终文件
  └─ 清理中间文件（可选）
```

---

### 4. TranscribeWorker（并行转录工作者）

**文件**：`subtitleextraction.cpp` 内（QRunnable 子类）

**职责**：在线程池中执行单个分段的转录工作

**核心功能**：
- 保存分段的元信息（索引、起始时间、音频路径等）
- 调用 `SubtitleExtraction::transcribeSegment()` 执行转录
- 将成功/失败结果回写到共享的 `QVector<bool>`

**线程安全**：
- 使用 `QMutex *m_resultLock` 保护结果向量
- 每个 worker 分别报告自己的进度（避免竞争条件）

---

## 性能与并行化策略

### 分段策略演进
| 版本 | 分段长度 | 处理方式 | 预期 RTF |
|------|---------|---------|---------|
| v1.0 | 20 分钟 | 串行 | ≈ 2.0 |
| v1.1 | 20 分钟 | 串行，参数优化 | ≈ 1.5 |
| v2.0 | 5 分钟 | 并行，4-6 workers | ≈ 0.4-0.6 |

**关键优化**：
1. **分段缩小**：20 分钟 → 5 分钟（便于并行）
2. **线程池**：`QThreadPool` + 4-6 workers （CPU 核数 / 4）
3. **自动线程检测**：`WhisperCommandBuilder` 自动设置 `-t` 参数
4. **语言检测跳过**：指定语言时追加 `-np` 参数

### 进度报告机制
- **粒度**：每个分段 10% 进度间隔报告一次
- **保护**：所有进度更新受 `QMutex m_progressLock` 保护
- **显示**：主界面状态栏实时显示全局进度百分比

---

## 集成指南（如何使用新组件）

### 场景 1：合并 SRT 文件

```cpp
#include "whispersegmentmerger.h"

// 假设有 3 个分段文件，每段 5 分钟
QStringList files = {
    "outputs/segment_0.srt",
    "outputs/segment_1.srt",
    "outputs/segment_2.srt"
};

// 合并为 SRT（保持原格式）
QString mergedSrt = WhisperSegmentMerger::mergeSegmentSrtFiles(
    files, 300.0, WhisperSegmentMerger::Format_SRT);

// 或者转换为 WebVTT
QString mergedVtt = WhisperSegmentMerger::mergeSegmentSrtFiles(
    files, 300.0, WhisperSegmentMerger::Format_WebVTT);
```

### 场景 2：构建 FFmpeg 命令

```cpp
#include "whispercommandbuilder.h"

// 提取 10-15 分钟的音频
QStringList args = WhisperCommandBuilder::buildFfmpegExtractArgs(
    "movie.mp4", 600.0, 300.0, "audio_segment.wav");

QProcess ffmpeg;
ffmpeg.setProgram("ffmpeg");
ffmpeg.setArguments(args);
ffmpeg.start();
ffmpeg.waitForFinished();
```

### 场景 3：构建 Whisper 命令

```cpp
#include "whispercommandbuilder.h"

// 日文转录，启用自动线程优化，跳过语言检测
QStringList args = WhisperCommandBuilder::buildWhisperTranscribeArgs(
    "models/ggml-large-v2.bin",
    "audio_segment.wav",
    "output/segment",
    "ja",      // 日文
    false      // CPU-only，不需要 GPU
);

QProcess whisper;
whisper.setProgram("whisper");
whisper.setArguments(args);
whisper.start();
whisper.waitForFinished();
```

### 场景 4：时间戳操作

```cpp
#include "whispersegmentmerger.h"

// 解析 SRT 时间戳
qint64 ms = 0;
bool ok = WhisperSegmentMerger::parseSrtTimestamp("00:01:30,500", ms);
// ms = 90500

// 格式化回字符串
QString ts = WhisperSegmentMerger::formatSrtTimestamp(90500);
// ts = "00:01:30,500"

// 偏移整个 SRT 内容（例如下移 5 分钟）
QString shifted = WhisperSegmentMerger::shiftedSrtContent(
    originalSrtContent, 300000);  // 偏移 300 秒 = 300000 毫秒
```

---

## 文件清单（更新后）

| 文件 | 类型 | 职责 |
|------|------|------|
| `subtitleextraction.h/cpp` | 核心类 | UI 与工作流编排（已重构使用新组件） |
| `whispersegmentmerger.h/cpp` | 工具类 | **NEW** SRT 合并与格式转换 |
| `whispercommandbuilder.h/cpp` | 工具类 | **NEW** FFmpeg/Whisper 命令构建 |
| `subtitleextraction.ui` | UI 文件 | 界面定义（无变更） |

---

## 向后兼容性说明

### 保留接口
以下 `SubtitleExtraction` 的 public 方法已改为调用新组件，但保留原有签名：

- `languageCodeFromUiText()` → wrapper for `WhisperCommandBuilder::languageCodeFromUiText()`
- `outputFileExtensionFromUiText()` → wrapper for `WhisperCommandBuilder::outputFileExtensionFromUiText()`
- `parseSrtTimestamp()` → wrapper for `WhisperSegmentMerger::parseSrtTimestamp()`
- `formatSrtTimestamp()` → wrapper for `WhisperSegmentMerger::formatSrtTimestamp()`
- `shiftedSrtContent()` → wrapper for `WhisperSegmentMerger::shiftedSrtContent()`
- `srtToPlainText()` → wrapper for `WhisperSegmentMerger::srtToPlainText()`
- `srtToTimestampedText()` → wrapper for `WhisperSegmentMerger::srtToTimestampedText()`
- `srtToWebVtt()` → wrapper for `WhisperSegmentMerger::srtToWebVtt()`

### 推荐迁移路径
- **新代码**：直接使用 `WhisperCommandBuilder::*()` 和 `WhisperSegmentMerger::*()`
- **旧代码**：继续使用 `SubtitleExtraction::*()` 包装器，无需修改

---

## 测试建议

### 单元测试示例
```cpp
// 测试时间戳解析
{
    qint64 ms = 0;
    QVERIFY(WhisperSegmentMerger::parseSrtTimestamp("00:01:30,500", ms));
    QCOMPARE(ms, 90500);
}

// 测试命令构建
{
    auto args = WhisperCommandBuilder::buildFfmpegExtractArgs(
        "test.mp4", 100.0, 200.0, "out.wav");
    QVERIFY(args.contains("-ss"));
    QVERIFY(args.contains("100.000"));
    QVERIFY(args.contains("-t"));
    QVERIFY(args.contains("200.000"));
}
```

### 集成测试
1. 提供测试视频片段（30 秒）
2. 验证分段提取成功
3. 验证 Whisper 转录成功
4. 验证分段合并结果正确
5. 验证所有输出格式（SRT/TXT/WebVTT）
