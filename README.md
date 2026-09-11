# VelaGoGoGo — 小家灵犀 (HomeSense Agent)

## 一、作品简介

VelaGoGoGo 小家灵犀 (HomeSense Agent) 是一款运行在 OpenVela（R528/Gemini-S1 开发板）上的 **AI 全屋智能语音助手**。它将豆包端到端实时语音对话、免提唤醒词检测、Claude Code 实时状态联动、人脸识别自动触发、环境传感器、LED 灯控、红外空调遥控等功能集成在一块 320×240 彩色触屏上，实现了从"唤醒 → 语音交互 → 智能控制 → 状态反馈"的完整闭环。

**核心亮点：**

- **豆包实时语音对话**：基于豆包 RealtimeAPI 的 WebSocket 全双工语音链路，按键说话（PTT）或免提唤醒后即说即答，支持 TTS 播放和文本展示。
- **免提唤醒词**：纯 C 实现的 CNN 唤醒词检测（关键词"你好，OpenVela"），后台常驻流式检测，唤醒后自动开启豆包对话，无需触屏操作。
- **Claude Code 状态联动**：通过 MQTT 桥接，电脑端 Claude Code 的工作状态（思考中 / 执行中 / 空闲）实时同步到开发板表情显示，打造硬件+AI 编程伴侣。
- **人脸识别自动触发**：基于 BlazeFace + TFLite Micro 的 UVC 摄像头人脸识别，检测到人脸自动开启语音对话，离开后自动关闭。
- **智能家居控制**：温湿度/近距离传感器数据实时显示、WS2812 RGB LED 亮度控制、红外空调遥控。
- **表情动画系统**：设备空闲时显示情绪数字表情轮播，语音交互时切换为聆听/说话/思考等动画状态。

## 二、选题方向

**AI 硬件产品创新**

基于 OpenVela RTOS 和 R528/Gemini-S1 开发板，集成豆包大模型实时语音、CNN 唤醒词、人脸识别、MQTT IoT 联动等多项 AI 能力，打造了一款可交互、可感知、可联动的全屋智能语音助手原型。

## 三、目录结构

```text
app/home_scense/              — 主应用（LVGL 智能家居 UI + 全部功能模块）
  ├── main.c / main.h         — 程序入口、全局变量、libuv 事件循环
  ├── ui_main.c               — 主屏 UI（时钟、日期、天气、传感器）
  ├── ui_voice.c / ui_voice.h — 语音交互面板（录音/识别/回答/播放）
  ├── ui_light.c              — LED 灯控面板（开关 + 亮度滑条）
  ├── ui_settings.c           — 设置面板
  ├── ui_about.c              — 关于页面
  ├── ui_status_bar.c/h       — 顶部状态栏（WiFi/电量/时间）
  ├── ui_emoji_idle.c         — 空闲表情动画轮播
  ├── home_scense_emoji_ipc.c — 表情 IPC 通信
  ├── claude_mqtt.c           — Claude Code MQTT 状态桥接客户端
  ├── claude_mqtt.h           — MQTT 状态桥接接口
  ├── wifi_status.c/h         — WiFi 状态监控
  ├── sensor.c / sensor.h     — 温湿度/近距离传感器读取
  ├── led_control.c/h         — LED 适配层（WS2812 + PWM）
  ├── ac_ir_control.c         — 红外空调遥控
  ├── face_detect.cc/h        — 人脸识别（BlazeFace + TFLite Micro）
  ├── doubao/                 — 豆包实时语音模块
  │   ├── doubao_voice.c/h    — 会话协调层
  │   ├── doubao_protocol.c/h — 豆包二进制协议封包/解析
  │   ├── voice_transport.c/h — TLS WebSocket 连接与帧收发
  │   ├── voice_capture.c/h   — PCM 麦克风采集（16kHz/mono）
  │   ├── voice_player.c/h    — TTS PCM 播放（24kHz）
  │   ├── wake_reply.c/h      — 唤醒应答音播放
  │   ├── wake_reply_assets.S — 应答音二进制嵌入
  │   ├── doubao_secret.h.example — 凭证模板（本地填写，不提交 Git）
  │   ├── doubao_config.h     — 模型/音色/协议常量
  │   ├── certs/              — TLS CA 证书
  │   └── README.md           — 豆包模块详细文档
  ├── wakeup/                 — 免提唤醒词模块
  │   ├── wakeup.c/h          — 后台线程：麦克风仲裁 + CNN 推理 + 触发豆包
  │   ├── mel_features.c/h    — log-mel 特征提取前端
  │   ├── mic_capture.c/h     — NuttX audio 流式采集
  │   ├── wav_player.c/h      — 唤醒应答音播放
  │   ├── wake_model_weights.h — CNN 模型权重
  │   ├── wakeup_wozai.wav    — 唤醒应答语音
  │   └── README.md           — 唤醒模块详细文档
  ├── res/                    — UI 资源（表情 webp 图片）
  ├── emoji_blob.S            — 表情资源二进制嵌入
  ├── Kconfig                 — 构建开关定义
  ├── CMakeLists.txt          — CMake 构建脚本
  └── Make.dep                — 依赖声明

board/contest_board/          — 板级适配
  ├── configs/nsh/            — board config（defconfig）
  ├── overlay/                — 构建时覆盖到 openvela 公共树的文件
  ├── src/                    — 板级初始化代码
  └── Kconfig / CMakeLists.txt

configs/                      — 构建配置
  ├── defconfig               — 完整 defconfig
  ├── rcS.nsh                 — 启动脚本
  └── sys_partition.fex       — 分区表

claude-mqtt-bridge/           — Claude Code MQTT 状态桥接
  ├── skill/                  — 电脑端 Claude Code Skill（hook 脚本）
  │   ├── SKILL.md            — Skill 说明文档
  │   ├── scripts/            — Python 脚本（hook.py / publish_status.py / dependency.py）
  │   └── settings.example.json
  └── README.md               — MQTT 桥接完整文档

scripts/                      — 辅助脚本
  └── wifi.sh                 — WiFi 连接/状态管理脚本

tools/                        — 开发工具
  ├── wav_to_raw.py           — WAV → raw PCM 转换
  ├── webp_to_blob.py         — WebP → 二进制嵌入
  └── claude_webp_to_blob.py  — Claude 状态专用 WebP 转换

build.sh                      — 一键构建/烧录脚本
logs/                         — AI Coding 日志
```

## 四、运行方式

### 4.1 环境准备

```bash
# 1. 拉取完整工程
repo init -u https://github.com/open-vela/contest2026_106_VelaGoGoGo \
  -b dev-ai-contest-2026 -m contest2026_106_VelaGoGoGo.xml
repo sync -c -j8

# 2. 配置豆包凭证（本地文件，不提交 Git）
cp app/home_scense/doubao/doubao_secret.h.example \
  app/home_scense/doubao/doubao_secret.h
# 编辑 doubao_secret.h，填入火山引擎控制台的 APP ID、Access Token、模型、音色

# 3. 连接 WiFi
./scripts/wifi.sh connect "<SSID>" "<PASSWORD>"
```

### 4.2 编译

```bash
# 进入 openvela 工作区根目录（仓的上一级）
cd ..

# 首次编译（完整构建）
./contest2026_106_VelaGoGoGo/build.sh full

# 日常开发增量编译（~10s）
./contest2026_106_VelaGoGoGo/build.sh
```

### 4.3 烧录与运行

```bash
# 增量编译 + 烧录到设备
./contest2026_106_VelaGoGoGo/build.sh flash

# 完整编译 + 烧录
./contest2026_106_VelaGoGoGo/build.sh full-flash
```

烧录完成后设备自动重启，`home_scense` 应用随系统自动启动（见 `configs/rcS.nsh`）。

### 4.4 真机前置检查

```bash
adb shell "date"                      # 确认时间正确（TLS 依赖）
adb shell "ls -l /dev/audio/pcm*"     # 音频节点存在
adb shell "ping -c 1 openspeech.bytedance.com"  # 网络/DNS 正常
```

### 4.5 功能使用

| 功能 | 操作方式 |
|------|---------|
| 豆包语音对话 | 触屏点击 AI 入口 → 开始录音 → 再次点击结束 → 等待回答播放 |
| 免提唤醒 | 说出唤醒词"你好，OpenVela" → 自动开始对话（无需触屏） |
| Claude Code 状态联动 | 电脑端安装 `claude-mqtt-bridge/skill/`，开发板自动显示 AI 工作状态 |
| LED 灯控 | 触屏进入灯光面板，开关 + 亮度滑条 |
| 环境数据 | 主屏实时显示温度、湿度、近距离传感器数据 |
| 人脸识别 | UVC 摄像头检测到人脸自动开启语音对话 |

## 五、AI Coding 使用说明

### 开发工具链

本项目全程使用 **Claude Code** 作为 AI 辅助开发工具，覆盖了从架构设计到调试优化的完整开发周期。

### AI 协作方式

- **架构设计**：使用 Claude Code 进行模块拆分设计，将 luncher_mini 单体应用重构为 sensor/LED/UI/voice/mqtt 等独立模块，确定了 Kconfig 条件编译策略和 CMake 构建组织。
- **编码实现**：通过 Claude Code 实现了豆包 RealtimeAPI 的二进制协议解析、WebSocket TLS 传输、PCM 录音/播放等底层模块；MQTT 桥接服务的设备端脚本和电脑端 Python Skill 全部由 AI 生成。
- **调试排错**：利用 Claude Code 分析串口日志、网络抓包、音频格式问题，定位并修复了 TTS 播放无声（recv_exact ms=0 偏移错误）、语音中断（上传期间暂停播放）等关键 bug。
- **文档与工具**：各子模块 README、Kconfig 注释、build.sh 脚本、WiFi 工具脚本、资源转换工具等均由 AI 辅助编写。
- **跨平台适配**：Claude Code MQTT 桥接的电脑端 Skill 实现了 Linux/macOS/Windows 三平台兼容，hook 脚本和依赖检查均由 AI 一次性生成并测试通过。

### AI 带来的效率提升

- **协议开发**：豆包二进制协议的封包/解析代码，AI 根据协议文档直接生成，减少约 80% 的手动编码时间。
- **嵌入式调试**：AI 辅助分析 NuttX 系统调用、音频驱动行为、网络栈配置，大幅缩短了嵌入式环境下的调试周期。
- **全栈覆盖**：从 C 嵌入式固件到 Python 桌面 Skill，AI 在不同语言和技术栈间无缝切换，一人完成了硬件+固件+桌面工具的全栈开发。

> 完整对话日志见 `logs/` 目录。