# RK3588 智能视频流推理与推流系统

## 项目简介

本项目基于 RK3588 平台，集成了摄像头采集、硬件加速 H264 编码、NATS 实时推流、AI推理（YOLO）、UDP/NATS通信等功能，适用于边缘智能视频分析、流媒体分发等场景。

核心特性：

- **高性能视频采集**：支持 USB 摄像头（如 Microdia H65），MJPG 格式高帧率采集。
- **硬件加速 H264 编码**：基于 Rockchip MPP 和 RGA，充分利用 RK3588 硬件能力。
- **NATS 实时推流**：编码后的视频流可实时推送到 NATS 消息服务器。
- **AI推理与检测**：集成 YOLOv8 检测，可扩展更多 AI 功能。
- **多线程高效架构**：采集、编码、推流、推理等多线程并行，帧率统计实时输出。

---

## 目录结构

```
.
├── src/                # 主体源码
│   ├── v4l2_h264.cpp   # 摄像头采集+编码+NATS推流示例
│   ├── App.cpp         # 综合AI推理、推流主程序
│   ├── video/          # 编解码相关
│   ├── io/             # NATS/UDP通信
│   ├── utils/          # 工具类（如帧率统计等）
│   └── ...             # 其他功能模块
├── librknn_api/        # RKNN推理API
├── CMakeLists.txt      # 构建脚本
└── ...
```

---

## 依赖环境

- RK3588 平台（如 NanoPi R6C）
- OpenCV 4.x
- Rockchip MPP、RGA
- ffmpeg-rockchip
- NATS C 客户端
- librknn_api（如需AI推理）
- CMake 3.11+
- g++ 7.5+ (建议 aarch64 架构)

> 依赖库已部分集成在 `3rdparty/` 目录（文件数过多故未上传，需自行编译安装），部分需自行编译或安装。

---

## 编译部署步骤

1. **安装系统依赖**
   ```bash
   sudo apt update
   sudo apt install build-essential cmake pkg-config libopencv-dev libssl-dev
   ```

2. **准备第三方库**
   - `3rdparty/ffmpeg-rockchip`、`3rdparty/rga`、`3rdparty/nats.c` 已集成（未上传，需自行编译安装）。
   - 如需自定义 OpenCV，可自行编译或安装系统包。

3. **编译项目**
   ```bash
   mkdir -p build
   cd build
   cmake ..
   make -j$(nproc)
   ```

4. **连接摄像头**
   - 插入 USB 摄像头，确认设备节点（如 `/dev/video0`）。
   - 推荐使用支持 MJPG 格式的摄像头以获得高帧率。

5. **运行采集推流示例**
   ```bash
   ./v4l2_h264
   ```
   - 默认采集 `/dev/video0`，1280x720@30fps，编码后推送到 NATS `ai.streaming` 主题。
   - 可在代码中修改 `CAMERA_ID`、`NATS_URL` 等参数。

6. **运行综合AI推理主程序**
   ```bash
   ./Ai
   ```
   - 包含推理、推流、UDP/NATS通信等完整流程。

---

## 常见问题

- **采集帧率低？**
  - 请确保摄像头支持 MJPG 格式，并已在代码中设置 `cv::CAP_PROP_FOURCC` 为 MJPG。
  - 用 `v4l2-ctl --list-formats-ext -d /dev/video0` 查看支持格式和帧率。

- **NATS 连接失败？**
  - 请确认 NATS 服务器地址和端口（`NATS_URL`）正确，且网络可达。

- **依赖库找不到？**
  - 检查 `3rdparty/` 目录下相关库文件是否齐全，（文件过多故未上传，向您道歉）或根据实际平台自行编译。

---

## 参考命令

- 查看摄像头支持格式和帧率：
  ```bash
  v4l2-ctl --list-formats-ext -d /dev/video0
  ```
- 设置摄像头帧率（如有必要）：
  ```bash
  v4l2-ctl -d /dev/video0 --set-parm=30
  ```

---
