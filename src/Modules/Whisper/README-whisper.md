# Whisper 模块文档

## 模块概述

Whisper 模块负责音视频转写、模型选择、分段识别、字幕合并、多格式导出，以及中间文件管理。

### 主要职责

1. 导入音视频文件（文件对话框 / 由 Loader 传入）
2. 扫描 `models/whisper` 并提供模型选择
3. 使用 `ffmpeg` + `whisper` 执行分段识别（每 20 分钟一段）
4. 将分段字幕按时间轴偏移后合并为完整字幕
5. 支持导出 `TXT`、`TXT（带时间）`、`SRT`、`WebVTT`
6. 管理中间目录与最终输出目录（可选清理中间文件）

---

## 核心类

### 1) SubtitleExtraction

文件：`subtitleextraction.h` / `subtitleextraction.cpp`

- 负责页面层交互：输入文件、模型目录、输出目录、输出格式、开始/停止
- 负责路径解析：`deps` 下可执行文件与 `models/whisper` 下模型
- 负责工作流编排：探测时长、20 分钟切段、逐段识别、字幕合并与格式转换
- 负责任务日志：按阶段分行输出进度与状态

---

## 重要调用链（已按当前实现更新）

### A. 页面初始化与默认目录

```text
SubtitleExtraction::SubtitleExtraction
  -> ensureModelDirectories()
       -> 创建 models/whisper 与 models/LLM
  -> refreshWhisperModelList()
       -> 扫描 models/whisper 填充 modelComboBox
  -> setupWorkflowUi()
       -> 默认中间目录: temp/whisper_work
       -> 默认输出目录: output/whisper
       -> 初始化日志栏（只读 + 实时滚动）
```

说明：模型下拉框只显示 `models/whisper` 中实际存在的文件/目录。

### B. 点击“开始转写”后的主流程

```text
transcribeButton(clicked)
  -> startTranscriptionWorkflow()
       -> 校验输入/模型/目录/依赖
       -> resolveFfmpegPath(), resolveWhisperPath(), resolve ffprobe
       -> probeDurationSeconds()
       -> 输出日志：任务开始、模型、格式、分段策略
       -> for 每个 20 分钟分段:
            extractSegmentAudio()   // ffmpeg 提取 16k 单声道 wav
            transcribeSegment()     // whisper 输出分段 srt（GPU 勾选时追加 -ng 0）
            输出日志：分段开始 / 识别进度 / 分段完成
       -> 合并分段字幕（统一时间轴）
       -> 按输出格式转换内容并写出最终文件
       -> 按选项清理中间目录
       -> 输出日志：合并进度 / 全部完成
```

说明：分段长度固定为 20 分钟（1200 秒），最后一段按剩余时长处理。

### C. 停止任务

```text
transcribeButton(clicked, running=true)
  -> requestStopWorkflow()
       -> m_cancelRequested = true
       -> 若外部进程运行中：terminate -> kill(必要时)
       -> 输出日志：正在停止任务
```

说明：停止为“软中断 + 必要时强杀子进程”，流程会在当前步骤安全退出。

### D. 字幕合并与格式导出

```text
读取每段 srt
  -> shiftedSrtContent(offsetMs = segmentIndex * 20min)
       -> parseSrtTimestamp()
       -> formatSrtTimestamp()
  -> 重新编号合并为标准 SRT 内容
  -> 根据 outputFormatComboBox 转换并输出:
       TXT / TXT（带时间）/ SRT / WebVTT
```

说明：最终文件扩展名会自动匹配所选格式（`.txt` / `.srt` / `.vtt`）。

### E. 依赖解析

```text
resolveExecutableInDeps(candidateNames)
  -> 先查 deps 根目录
  -> 再递归 deps 子目录
```

说明：当前识别流程依赖：
- `ffmpeg.exe`（切段）
- `ffprobe.exe`（探测时长）
- `whisper.exe` 或 `whisper-cli.exe`（分段识别）

### F. GPU 选项

```text
gpuCheckBox(checked)
  -> transcribeSegment()
       -> args << "-ng" << "0"
```

说明：勾选 GPU 时向 whisper 追加 `-ng 0`；未勾选时不追加。

---

## 当前关键行为约定

1. 模型目录固定为 `models/whisper`，模型列表随页面显示刷新。
2. 中间目录与最终输出目录分离，避免清理动作误删最终产物。
3. 输出格式支持 `TXT`、`TXT（带时间）`、`SRT`、`WebVTT`，默认 `SRT`。
4. 勾选“完成后清理中间文件”时，会删除本次任务的 `job_*` 中间目录。
5. 运行态会禁用目录/模型相关控件，并将按钮文案切换为“停止”。
6. 勾选 GPU 时会向 whisper 传递 `-ng 0` 参数；实际是否启用取决于当前 whisper 构建能力。
7. 日志区以“分行阶段日志”展示任务进度：开始 → 分段 → 合并 → 完成/失败。

---

## 错误处理与提示

常见处理路径：

- 输入文件无效：提示选择可访问的音视频文件
- 缺少依赖：分别提示 `ffmpeg` / `ffprobe` / `whisper` 缺失
- 模型不可用：提示选择有效 `.bin` 模型
- 分段失败：提示音频分段失败（FFmpeg）
- 识别失败：提示 Whisper 识别失败或未产出分段字幕
- 合并失败：提示片段读取失败
- 写入失败：提示输出目录权限问题

---

## 输入与输出说明

- 输入：音视频文件（如 `mp4/mkv/avi/mov/mp3/wav/flac`）
- 中间输出：`temp/whisper_work/job_时间戳/segment_*.wav/.srt`
- 最终输出：`output/whisper/<输入文件名>_whisper.<ext>`
  - `TXT` / `TXT（带时间）` -> `.txt`
  - `SRT` -> `.srt`
  - `WebVTT` -> `.vtt`

---

## 文件清单

- `subtitleextraction.h` / `subtitleextraction.cpp`：Whisper 页面与转写工作流核心
- `subtitleextraction.ui`：界面文件（模型、目录、输出格式、开始/停止、日志）
- `README-whisper.md`：本说明文档
