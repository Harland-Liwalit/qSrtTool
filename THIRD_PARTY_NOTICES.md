# Third-Party Notices

本项目主体代码采用 `GPL-3.0`（见根目录 `LICENSE`）。

项目运行时会按 `resources/dependencies.json` 下载并调用以下第三方组件；其版权与许可证归原作者所有，使用时需遵守各自协议。

## 组件清单

| 组件 | 上游仓库 | 许可证 | 项目内下载来源 |
| --- | --- | --- | --- |
| FFmpeg（BtbN 构建） | https://github.com/BtbN/FFmpeg-Builds | GPL-3.0（该构建为 `win64-gpl`） | https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl.zip |
| yt-dlp | https://github.com/yt-dlp/yt-dlp | Unlicense | https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe |
| whisper.cpp（CPU/CUDA） | https://github.com/ggml-org/whisper.cpp | MIT | https://github.com/ggml-org/whisper.cpp/releases/latest/download/whisper-bin-x64.zip / https://github.com/ggml-org/whisper.cpp/releases/latest/download/whisper-cublas-12.4.0-bin-x64.zip |

## 许可证文本来源

- FFmpeg（本地已包含）：`deps/ffmpeg-master-latest-win64-gpl/LICENSE.txt`
- yt-dlp：上游仓库 `LICENSE` 文件（Unlicense）
- whisper.cpp：上游仓库 `LICENSE` 文件（MIT）

## 分发与合规说明

- 本仓库发布的是源码；第三方二进制通常不随源码仓库提交（见 `.gitignore`）。
- 若你对外分发包含第三方二进制的打包产物，请同时提供对应许可证与必要声明。
- 对于 FFmpeg 的 GPL 构建，分发时请确保满足 GPL 的相关义务。