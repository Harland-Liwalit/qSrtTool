# Executable Capabilities Detection System

## 概述

本系统提供**统一的版本检测和能力判断机制**，用于自动适配不同版本的第三方软件（FFmpeg、Whisper.cpp、yt-dlp）的 CLI 参数。

## 架构目标

- **版本兼容性**：自动检测软件版本，并根据版本动态调整 CLI 参数
- **优雅降级**：当软件版本过旧时，自动禁用不支持的功能标志
- **集中管理**：所有版本检测逻辑集中在 `ExecutableCapabilitiesDetector` 类
- **缓存机制**：初次检测后缓存版本能力信息，避免重复执行

## 核心组件

### 1. ExecutableCapabilities 结构体

```cpp
struct ExecutableCapabilities {
    QString name;                    // 软件名称（如 "whisper.cpp"）
    QString executablePath;          // 可执行文件路径
    QString version;                 // 检测到的版本号（如 "1.5.4"）
    bool isAvailable;                // 可执行文件是否可用
    bool isSupported;                // 版本是否被支持
    QString unsupportedReason;       // 不支持的原因描述
    
    // Whisper 特定标志
    bool whisperSupportsGpu;         // 支持 -ng（GPU 控制）标志
    bool whisperSupportsThreads;     // 支持 -t（线程）标志
    bool whisperSupportsLanguage;    // 支持 -l（语言）标志
    
    // FFmpeg 特定标志
    bool ffmpegHasRtmp;              // 支持 RTMP 协议
    bool ffmpegHasHardwareAccel;     // 支持硬件加速（CUDA/NVENC）
    
    // yt-dlp 特定标志
    bool ytDlpSupportsPlaylist;      // 支持播放列表下载
    bool ytDlpSupportsFragments;     // 支持分片下载
};
```

### 2. ExecutableCapabilitiesDetector 类

提供静态方法用于检测不同软件的能力：

```cpp
class ExecutableCapabilitiesDetector {
public:
    // 检测 Whisper.cpp
    static ExecutableCapabilities detectWhisper(const QString &execPath);
    
    // 检测 FFmpeg
    static ExecutableCapabilities detectFfmpeg(const QString &execPath);
    
    // 检测 yt-dlp
    static ExecutableCapabilities detectYtDlp(const QString &execPath);
    
    // 辅助函数
    static QString executeCommandWithTimeout(const QString &program, 
                                             const QStringList &args, 
                                             int timeoutMs = 5000);
    
    static QString extractVersionNumber(const QString &versionOutput);
};
```

## 版本支持表

### Whisper.cpp 版本支持

| 版本范围 | -ng (GPU) | -t (Threads) | -l (Language) | 状态 |
|---------|-----------|--------------|---------------|------|
| < 1.4   | ❌        | ❌           | ❌            | 不支持 |
| 1.4.x   | ❌        | ✅           | ✅            | 有限支持 |
| 1.5.x   | ✅        | ✅           | ✅            | 完全支持 |
| ≥ 2.0   | ✅        | ✅           | ✅            | 完全支持 |

**说明**：
- `-ng`：禁用 GPU 加速标志，v1.5 引入
- `-t`：线程数控制标志，v1.4 引入
- `-l`：语言指定标志，所有现代版本都支持

### FFmpeg 版本支持

| 版本范围 | RTMP | 硬件加速 | 状态 |
|---------|------|--------|------|
| < 5.0   | ❌   | ❌     | 不支持 |
| ≥ 5.0   | ✅   | ✅     | 完全支持 |

### yt-dlp 版本支持

| 版本范围 | 播放列表 | 分片下载 | 状态 |
|---------|---------|--------|------|
| < 2022.01 | ❌    | ❌     | 不支持 |
| ≥ 2022.01 | ✅    | ✅     | 完全支持 |

## 集成示例

### 示例 1：Whisper 转录（已集成）

**位置**：`src/Modules/Whisper/subtitleextraction.cpp:transcribeSegment()`

```cpp
// 检测 Whisper 可执行文件的能力
ExecutableCapabilities whisperCaps = ExecutableCapabilitiesDetector::detectWhisper(whisperPath);

// 根据检测到的能力构建命令行
const QStringList args = WhisperCommandBuilder::buildWhisperTranscribeArgs(
    modelPath, segmentAudioPath, segmentOutputBasePath, languageCode,
    useGpu, whisperThreadCount, &whisperCaps);  // 传递能力结构体
```

**优雅降级**：
- 如果 `whisperCaps.whisperSupportsGpu == false`，则不添加 `-ng` 标志
- 如果 `whisperCaps.whisperSupportsThreads == false`，则不添加 `-t` 标志
- 如果 `whisperCaps.whisperSupportsLanguage == false`，则不添加 `-l` 标志

### 示例 2：WhisperCommandBuilder 适配（已实现）

**位置**：`src/Modules/Whisper/whispercommandbuilder.cpp`

```cpp
QStringList WhisperCommandBuilder::buildWhisperTranscribeArgs(
    const QString &modelPath,
    const QString &audioPath,
    const QString &outputBasePath,
    const QString &languageCode,
    bool useGpu,
    int threadCountHint,
    const ExecutableCapabilities *capabilities)  // 可选参数
{
    QStringList args;
    
    // 条件性地添加线程参数
    if (capabilities) {
        if (capabilities->whisperSupportsThreads) {
            args << "-t" << QString::number(threadCount);
        }
    } else {
        // 未提供能力信息时，使用默认行为
        args << "-t" << QString::number(threadCount);
    }
    
    // 类似地处理语言和 GPU 标志...
}
```

### 示例 3：FFmpeg 集成（待实现）

**建议位置**：`src/Modules/Loader/videoloader.cpp`

```cpp
// 检测 FFmpeg 能力
ExecutableCapabilities ffmpegCaps = ExecutableCapabilitiesDetector::detectFfmpeg(ffmpegPath);

// 根据能力选择合适的提取命令
QStringList args = buildFfmpegExtractArgs(inputPath, startSeconds, durationSeconds, outputPath);

if (ffmpegCaps.ffmpegHasHardwareAccel) {
    // 使用硬件加速
    args << "-hwaccel" << "cuda";
}
```

### 示例 4：yt-dlp 集成（待实现）

**建议位置**：`src/Modules/Downloder/videodownloader.cpp`

```cpp
// 检测 yt-dlp 能力
ExecutableCapabilities ytDlpCaps = ExecutableCapabilitiesDetector::detectYtDlp(ytDlpPath);

QStringList args;
args << videoUrl;

if (ytDlpCaps.ytDlpSupportsPlaylist) {
    args << "--yes-playlist";
}

if (ytDlpCaps.ytDlpSupportsFragments) {
    args << "-f" << "best";
}
```

## 版本检测算法

### 通用流程

1. **执行版本命令**
   - Whisper：`whisper.exe --version`
   - FFmpeg：`ffmpeg.exe -version`
   - yt-dlp：`yt-dlp.exe --version`

2. **版本号提取**
   - 使用正则表达式：`v?(\d+)\.(\d+)(?:\.(\d+))?`
   - 支持格式：`v1.5.4`、`1.5.4`、`version 1.5.4` 等

3. **能力判断**
   - 根据主版本号和次版本号判断功能支持
   - 示例：Whisper `< 1.4` 不支持任何标志

4. **缓存更新**
   - 初次检测后，版本号和能力信息可被缓存（通过 DependencyManager）
   - 避免每次调用都重新检测

## 超时机制

所有版本检测命令都使用 **5 秒超时**（可配置）：

```cpp
const QString versionOutput = ExecutableCapabilitiesDetector::executeCommandWithTimeout(
    execPath, QStringList() << "--version", 5000);  // 5000ms 超时
```

如果版本检测超时，将设置 `isAvailable = false` 并记录原因。

## 文件位置

- **核心实现**：
  - `src/Core/executablecapabilities.h` - 结构体和类声明
  - `src/Core/executablecapabilities.cpp` - 版本检测实现

- **集成点**：
  - `src/Modules/Whisper/subtitleextraction.cpp` - Whisper 集成（已完成）
  - `src/Modules/Whisper/whispercommandbuilder.cpp` - 参数生成（已完成）
  - `src/Modules/Loader/videoloader.cpp` - FFmpeg 集成（待实现）
  - `src/Modules/Downloder/videodownloader.cpp` - yt-dlp 集成（待实现）

## 向后兼容性

- 如果不提供 `ExecutableCapabilities` 参数，系统使用**默认行为**（假设最新版本支持所有标志）
- 这确保了旧代码在升级后仍能正常工作，但可能无法正确处理旧版本软件

## 故障排查

### 问题：某个命令行参数未生效

**可能原因**：
1. 版本检测失败（超时或版本过旧）
2. 能力检测不准确

**调试建议**：
1. 检查 `ExecutableCapabilities.isAvailable` 和 `isSupported` 状态
2. 查看 `unsupportedReason` 字段获取诊断信息
3. 手动运行版本命令：`whisper.exe --version`

### 问题：版本检测超时

**可能原因**：
1. 可执行文件不在 PATH 中
2. 磁盘 I/O 缓慢
3. 网络驱动器访问延迟

**解决方案**：
1. 确保可执行文件路径正确
2. 增加超时时间（在 `executeCommandWithTimeout` 中）
3. 考虑预先缓存版本信息

## 扩展指南

### 添加新软件的版本检测

以添加 "mytool" 为例：

1. **扩展结构体**
   ```cpp
   struct ExecutableCapabilities {
       // ... 现有字段
       bool myToolSupportsFeatureX;
       bool myToolSupportsFeatureY;
   };
   ```

2. **添加检测方法**
   ```cpp
   static ExecutableCapabilities detectMyTool(const QString &execPath) {
       // 实现版本检测逻辑
   }
   ```

3. **集成到调用点**
   ```cpp
   ExecutableCapabilities caps = ExecutableCapabilitiesDetector::detectMyTool(toolPath);
   // 根据 caps 调整 CLI 参数
   ```

## 性能注意事项

- **版本检测成本**：~100ms（执行外部进程 + 版本号解析）
- **推荐**：在应用启动时一次性检测，而不是在每个操作前检测
- **可选**：缓存检测结果到 `.qsrottool_dep_cache` JSON 文件（由 DependencyManager 管理）

## 依赖关系

- **Qt 模块**：Core、Concurrent
- **外部进程**：检测到的第三方软件可执行文件
- **文件系统**：读取可执行文件路径

## 相关文档

- [Whisper 模块架构](../Modules/Whisper/README-architecture.md)
- [依赖管理系统](../../REFACTORING_SUMMARY.md)
- [验证清单](../../VERIFICATION_CHECKLIST.md)
