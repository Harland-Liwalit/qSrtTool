# Loader 模块文档

## 模块概述

Loader 负责视频导入、预览播放、进度控制，以及 FFmpeg 依赖检查与提示。

### 主要职责
1. 导入视频（文件对话框 / 拖拽）
2. 使用 FFmpeg 子进程解码并渲染画面
3. 播放控制（播放/暂停、快进/快退、跳转）
4. 进度与时间显示（拖动/点击进度条）

---

## 核心类

### 1) VideoLoader
文件：`videoloader.h` / `videoloader.cpp`

- 负责页面层交互：导入、拖拽、状态转发
- 持有 `EmbeddedFfmpegPlayer` 实例并驱动播放

### 2) EmbeddedFfmpegPlayer
文件：`embeddedffmpegplayer.h` / `embeddedffmpegplayer.cpp`

- 负责播放器 UI、键盘/鼠标控制
- 负责 FFmpeg/FFprobe 路径解析、子进程管理、帧解析渲染

---

## 重要调用链（已按当前实现更新）

### A. 导入视频并播放

```text
VideoLoader::onImportVideoClicked / dropEvent
  -> VideoLoader::loadVideo(filePath)
  -> EmbeddedFfmpegPlayer::loadVideo(filePath)
       -> stopPlayback()
       -> destroyDecoderProcess()
       -> setupDecoderProcess()
       -> refreshVideoMeta()
       -> updateProgressUi()
  -> EmbeddedFfmpegPlayer::playPause()
       -> beginPlaybackFromCurrentPosition()
       -> startPlaybackAt(m_positionMs)
```

说明：`loadVideo` 在重新导入时会重建解码进程，避免旧进程残留状态导致崩溃。

### B. 进度条拖动/点击跳转

```text
(拖动) onSliderPressed -> onSliderReleased -> seekTo(target)
(点击) eventFilter(MousePress) 仅设置 sliderPosition
      -> 由 QSlider 正常释放流程进入 onSliderReleased -> seekTo(target)
```

说明：点击逻辑已改为“只改位置，不在 MousePress 里做重操作”，减少事件重入风险。

### C. seek 跳转恢复播放

```text
seekTo(target)
  -> 若原本在播放：stopPlayback()
  -> 更新 m_positionMs + updateProgressUi()
  -> 若原本在播放：beginPlaybackFromCurrentPosition()
```

说明：恢复播放走确定性函数 `beginPlaybackFromCurrentPosition`，不再依赖 toggle 式切换。

### D. 帧解码与渲染

```text
QProcess::readyReadStandardOutput
  -> onDecoderOutputReady()
  -> 从 stdout 追加到 m_frameBuffer
  -> 按 m_frameBytes 切帧
  -> QImage(..., bytesPerLine = m_outputWidth * 3, Format_RGB888)
  -> QLabel::setPixmap
```

说明：`bytesPerLine` 明确使用 `m_outputWidth * 3`，修复了画面斜切/分块问题。

---

## 当前关键行为约定

1. `stopPlayback()` 终止当前 FFmpeg 进程并清理缓冲，但不销毁 `QProcess` 对象。
2. `loadVideo()` 在替换导入新视频时会显式重建解码进程对象。
3. 仅当 `m_durationMs > 0` 时允许进度条跳转。
4. 所有解码回调都会校验 `sender() == m_ffmpegProcess`，并检查进程可读状态。

---

## 信号与错误处理

- `statusMessage(QString)`：状态提示
- `playbackError(QString)`：播放失败信息
- `ffmpegMissing()`：未发现 `ffmpeg.exe`

常见处理路径：

- 找不到 FFmpeg：提示下载依赖
- 解码异常退出：读取 stderr 并上报 `playbackError`
- 无效视频文件：直接拒绝加载

---

## 支持格式

`mp4, mkv, avi, mov, wmv, flv, webm, m4v`

---

## 快捷键

- `Space` / `K`：播放 / 暂停
- `Left` / `J`：快退 10 秒
- `Right` / `L`：快进 10 秒

---

## 文件清单

- `embeddedffmpegplayer.h` / `embeddedffmpegplayer.cpp`：播放器核心
- `videoloader.h` / `videoloader.cpp`：导入页面与交互
- `videoloader.ui`：界面文件
- `README-Loader.md`：本说明文档
