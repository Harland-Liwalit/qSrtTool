# qSrtTool

一个基于 Qt Widgets 的本地化视频字幕工具，覆盖从视频下载、预览、字幕提取（Whisper）、字幕翻译（LLM）、字幕烧录到输出管理的完整流程。

> 当前仓库以 Windows + qmake 开发流程为主。

## 功能概览

- 视频下载：基于 `yt-dlp` 执行下载任务，支持队列与并发调度。
- 视频预览：导入/拖拽视频并在应用内播放，支持一键进入下一步。
- 字幕提取：基于 `whisper.cpp` + `ffmpeg` 进行分段转写、并行处理与结果合并。
- 字幕翻译：对接 LLM 服务，支持分段翻译、流式预览、停止/续译/重译。
- 字幕烧录：通过 FFmpeg 将字幕烧录进视频，支持容器与输出路径联动。
- 输出管理：统一浏览与筛选输出文件，支持导出清单。
- 依赖管理：启动后可检测 `ffmpeg / yt-dlp / whisper.cpp` 版本与更新。

## 工作流

1. 视频下载（可选）
2. 视频预览与导入
3. Whisper 字幕提取
4. LLM 字幕翻译
5. 字幕烧录（可选）
6. 输出管理

应用主界面支持跨页面切换保护：当存在运行任务时，会做停止确认，避免误切换导致任务中断。

## 技术栈

- C++11
- Qt（Core / Gui / Widgets / Network / Multimedia / Concurrent）
- qmake
- FFmpeg（外部依赖）
- whisper.cpp（外部依赖）
- yt-dlp（外部依赖）

## 项目结构

```text
qSrtTool/
├─ src/
│  ├─ Core/                 # 依赖管理、可执行能力探测
│  ├─ Modules/
│  │  ├─ Downloder/         # 视频下载（目录名保持历史拼写）
│  │  ├─ Loader/            # 视频预览/导入
│  │  ├─ Whisper/           # 字幕提取
│  │  ├─ Translator/        # 字幕翻译
│  │  ├─ Burner/            # 字幕烧录
│  │  └─ OutputMgr/         # 输出管理
│  ├─ Widgets/              # 通用小组件
│  ├─ mainwindow.*          # 主窗口与页面编排
│  └─ main.cpp
├─ resources/
│  ├─ dependencies.json     # 外部工具依赖清单
│  └─ style.qrc             # 主题与资源
├─ deps/                    # 本地依赖与工具目录（默认不提交）
└─ qSrtTool.pro
```

## 环境要求

- Windows 10/11（当前代码中含 Windows 性能计数相关逻辑）
- Qt 5.12+ 或 Qt 6.x（需包含 Widgets / Network / Multimedia / Concurrent 模块）
- 可用 C++ 编译工具链（MinGW 或 MSVC）

## 快速开始

### 方式 A：Qt Creator（推荐）

1. 使用 Qt Creator 打开 `qSrtTool.pro`。
2. 选择 Qt Kit（MinGW 或 MSVC）。
3. 构建并运行。

### 方式 B：命令行（qmake）

在项目根目录执行：

```bash
qmake qSrtTool.pro
```

随后根据工具链选择：

- MinGW：

```bash
mingw32-make -j
```

- MSVC（Developer Command Prompt）：

```bash
nmake
```

## 依赖与资源说明

- 应用会读取 `resources/dependencies.json`，用于识别并检查外部工具版本。
- 运行期会按需使用以下目录：
  - `models/whisper`
  - `models/LLM`
  - `temp/`
  - `output/`
- 以上目录可在运行时自动创建。

## 关键模块文档

- 翻译模块：`src/Modules/Translator/README-translator.md`
- Whisper 架构：`src/Modules/Whisper/README-architecture.md`
- 可执行能力探测：`src/Core/EXECUTABLECAPABILITIES-README.md`

## 常见问题

### 1) 启动后提示依赖缺失或版本过低

检查外部工具是否可用，并确认 `resources/dependencies.json` 可被读取。

### 2) qmake 生成文件出现在仓库根目录

这是 qmake 默认行为。仓库已通过 `.gitignore` 忽略常见构建产物；提交前建议清理 `Makefile*`、`object_script.*`、`debug/`、`release/` 等目录/文件。

### 3) Whisper 转写速度慢

优先检查模型体积、CPU 线程数与输入视频时长；本项目已实现分段并行与线程预算分配，速度仍受硬件影响。

## 参与开发

欢迎提交 Issue / PR。建议提交前完成：

- 可编译
- 关键流程可运行（下载/提取/翻译至少覆盖一个）
- 不引入构建产物文件

## 许可

- 本项目源码采用 **GNU General Public License v3.0**（见 `LICENSE`）。
- 第三方组件许可证与下载来源见 `THIRD_PARTY_NOTICES.md`。