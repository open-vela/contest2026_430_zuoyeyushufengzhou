# 手语桥（SignBridge）端侧手语识别终端

| 项目 | 内容 |
|---|---|
| 作品名称 | 手语桥（SignBridge） |
| 参赛赛道 | 新硬件适配 + AI 硬件产品创新 |
| 目标硬件 | ESP32-P4-Function-EV-Board |
| 系统底座 | openvela（Apache NuttX） |

## 1. 概述

手语桥是运行在 ESP32-P4-Function-EV-Board 上的离线手语双向翻译终端：
通过 MIPI-CSI 摄像头采集手部画面，在本地完成 21 关键点提取与时序分类，
识别结果以文字显示于 7 寸触摸屏并同步播放语音；反向通路为麦克风采集、
语音活动检测与手语动画回放。全部推理与媒体处理在端侧完成，无云端依赖。

ESP32-P4-Function-EV-Board 在 openvela 上此前无板级支持。本项目的工作
分两部分：板级移植（arch 接线、外设驱动、显示/音频/摄像头链路、CMake
构建支持）与上层应用（识别、播报、界面、低功耗）。板级代码位于 nuttx
公共仓，按大赛指南以 PR 形式提交（open-vela/nuttx PR #364，接续已关闭
的 #347）；应用界面/摄像头演示位于 nuttx-apps（PR #123）；作品应用与
文档位于本仓库。

## 2. 硬件环境

| 部件 | 型号/规格 | 接口 | 备注 |
|---|---|---|---|
| 主控 | ESP32-P4，双核 RISC-V 400MHz，32MB PSRAM | — | — |
| 无线 | ESP32-C6-MINI-1（WiFi6/BLE5） | SDIO（GPIO14–19） | 云端纠错预留通路 |
| 显示 | 7 寸 1024×600，EK79007AD + EK73217BCGA | MIPI-DSI 2 lane | 专用物理引脚 |
| 触摸 | GT911 | I2C0（SCL=8/SDA=7） | INT/RESET 板上未接，轮询模式；驱动支持 0x5D/0x14 自动探测 |
| 音频 | ES8311 codec + NS4150 功放 | I2S0 + I2C0（地址 0x18） | 功放使能 GPIO53 |
| 摄像头 | SC2336 2MP | MIPI-CSI 2 lane，SCCB 复用 I2C0（地址 0x3c） | CSI 驱动+预览链路已实现（lvgldemo），整机联调待复验 |
| 控制台 | UART0 | TX=37 / RX=38，115200 | — |

I2S0 引脚：BCLK=12，MCLK=13，WS=10，DOUT=9，DIN=11。
背光 GPIO26，屏复位 GPIO27（均为官方默认杜邦线接法）。
完整引脚依据与出处见 `board/esp32p4_function_ev/pins.md`。

## 3. 软件架构

```
┌─────────────────────────────────────────────┐
│ 应用层  signbridge（状态机/推理/界面/音频） │
├─────────────────────────────────────────────┤
│ 中间件  LVGL · TFLite Micro · nxplayer ·    │
│         ROMFS（模型 7.5MB + 语音词库 2.3MB）│
├─────────────────────────────────────────────┤
│ 驱动层  EK79007 · GT911(gt9xx) · ES8311 ·   │
│         SC2336 · I2S/I2C/FB/input           │
├─────────────────────────────────────────────┤
│ 板级    esp32p4-function-ev-board           │
│ 底座    openvela / Apache NuttX             │
└─────────────────────────────────────────────┘
```

### 3.1 应用模块（app/signbridge/）

| 文件 | 职责 |
|---|---|
| signbridge_main.c | 入口，状态机与 LVGL 事件循环 |
| signbridge_sm.c | 状态机 IDLE→DETECTING→RECOGNIZING→RESULT |
| signbridge_camera.c | 帧源抽象（测试图案 / MIPI-CSI） |
| signbridge_infer.c / _tflm.cc | 推理管线：关键点窗口管理与 TFLM 后端 |
| signbridge_cls_mlp.c | INT8 MLP 时序分类器，运行时从 ROMFS 加载权重 |
| signbridge_ui.c | LVGL 界面，1024×600 横屏布局 |
| signbridge_anim.c | 程序化手语动画（21 关键点骨架插值） |
| signbridge_voice.c | 语音播报，nxplayer 播放 ROMFS 内 50 词 WAV |
| signbridge_audio_in.c | 麦克风采集（/dev/audio/pcm_in0）+ 能量 VAD |
| signbridge_correct.c | 离线语义纠错：置信度过滤、去重、窗口合并 |
| signbridge_pm.c | 空闲关背光 / 活动恢复 |
| signbridge_vocab.c | 50 词共享词表（识别/播报/动画单一数据源） |

### 3.2 识别链路

摄像头帧 → hand landmark 模型（MediaPipe，TFLite Micro）→ 21×3 关键点
→ 32 帧滑动窗口（带回绕解绕）→ INT8 MLP 分类器（2016→128→64→50）→
类别与置信度 → 上屏 / 语音播报 / 纠错队列。

### 3.3 关键技术决策

1. **触摸采用轮询模式**。该板 GT911 的 INT/RESET 未连接至主控（仅引出
   至屏幕适配板 J6 排针），经原理图与 esp-bsp 源码交叉核实。驱动将
   irq 回调置空，read 路径直接 I2C 读状态寄存器；地址支持 0x5D/0x14
   自动探测（Kconfig 可选），兼容 INT 悬空锁存 0x5D 与外部拉高改 0x14
   两种硬件形态。
2. **I2C0 单总线挂三设备**（触摸/音频/摄像头），为硬件设计既定，
   驱动按共享总线实现，400kHz。
3. **分类器权重自定义二进制格式**（magic + 版本 + 层参数 + INT8 权重
   /float32 偏置），板端加载时做全量边界校验，避免文件损坏导致越界读。
4. **深睡暂不实现**。触摸中断缺失导致深睡后无可靠唤醒源，当前仅做
   背光级功耗管理。

## 4. 模型与数据

| 资产 | 规格 | 挂载位置 |
|---|---|---|
| hand_detector / hand_landmarks_detector | MediaPipe TFLite，合计 7.5MB | /etc/models |
| sign_classifier_int8.bin | INT8 MLP，2016→128→64→50，270KB | /etc/models |
| 语音词库 | 50 词，espeak-ng（cmn），22050Hz 单声道 | /media/signs |

训练脚本见 `tools/`（TensorFlow/Keras），词表 `models/vocab.txt`。

## 5. 构建与运行

### 5.1 拉取

```
repo init -u https://github.com/open-vela/contest2026_430_zuoyeyushufengzhou \
    -b dev-ai-contest-2026 -m contest2026_430_zuoyeyushufengzhou.xml
repo sync -c -j8
```

### 5.2 编译

CMake 方式（openvela 标准构建入口，本仓已全流程验证至 nuttx.bin 产出）：

```
export PATH=<riscv32-esp-elf 工具链>/bin:$PATH   # esp-14.2.0 验证通过
./build.sh boards/risc-v/esp32p4/esp32p4-function-ev-board/configs/nsh \
    --cmake -j8
```

或传统 Make 方式：

```
cd nuttx
export PATH=<riscv-none-elf 工具链>/bin:$PATH   # 验证版本：GCC 13.4.0
python3 tools/kconfiglib_olddefconfig.py \
    boards/risc-v/esp32p4/esp32p4-function-ev-board/configs/nsh/defconfig
make -j$(nproc)
```

SignBridge 完整固件在 nsh 基础上按
`board/esp32p4_function_ev/defconfig.inc` 的阶段增量放开显示/触摸/
音频/摄像头/推理开关（menuconfig 或合并 defconfig），产物
`nuttx/nuttx.bin` 约 10.7MB（含应用、模型与词库 ROMFS 镜像）。

### 5.3 烧录

```
esptool.py -c esp32p4 -p /dev/ttyACM0 -b 921600 \
    write_flash 0x2000 nuttx.bin
```

### 5.4 运行

串口 115200。应用为系统入口（CONFIG_INIT_ENTRYPOINT=signbridge_main），
开机自动启动；亦可在 NSH 下手动执行 `signbridge`。

## 6. 验证状态（整体基本完成）

| 项 | 状态 |
|---|---|
| nsh 配置 CMake 全链路构建（HAL 拉取/补丁/编译/链接/出 bin） | 通过，nuttx.bin 281KB，0 error |
| SignBridge 全量固件编译（LVGL/CSI/触摸增量，见 defconfig.inc） | 通过，产物 10.7MB，0 error |
| 板级外设配置集（35 个：adc/pwm/i2c/spi/ethernet/twai/watchdog 等） | 已验证 |
| 应用源码 checkpatch | 通过（0 error / 0 warning） |
| 关键符号入链（入口/状态机/窗口/音频/触摸） | 已核对 |
| 引脚分配与官方原理图、esp-bsp 一致性 | 已逐项核对（见 pins.md） |
| CSI 采集与 LVGL 预览链路 | 已实现并编译通过（nuttx-apps lvgldemo camera 模块；signbridge_camera 帧源） |
| 应用整机链路（烧录后 UI/语音/推理联调） | 基本完成，待烧录复验 |

## 7. 已知问题与约束

1. CSI 采集与预览链路已在 lvgldemo camera 模块实现（SC2336 RAW8 1280×720@30fps，
   DMA→PSRAM 双缓冲），signbridge 端帧源接入待烧录复验。
2. 唤醒词"你好，openvela"当前由 VAD 兜底，唤醒词模型未训练。
3. 云端语义纠错仅预留接口（依赖 C6 WiFi 链路）。
4. 深睡/降频未实现，原因见 3.3 节第 4 条。
5. 采集 16kHz 与播放 22050Hz 共用 I2S0，双采样率共存行为待真机确认。

## 8. 目录结构

```
app/signbridge/                 应用（见 3.1）
board/esp32p4_function_ev/      板级文档：引脚表、增量配置
models/                         分类器权重、hand landmark 模型、词表
media/signs/                    50 词播报 WAV
tools/                          PC 端训练脚本
logs/                           AI Coding 会话日志
```

## 9. 相关提交

| 仓库 | 内容 |
|---|---|
| open-vela/nuttx，PR #364（dev-ai-contest-2026） | esp32p4 arch 移植、板级目录、外设驱动（gt9xx）、CMake 构建支持（接续 #347） |
| open-vela/nuttx-apps，PR #123（dev-ai-contest-2026） | lvgldemo SignBridge UI：开机画面、摄像头预览（SC2336/CSI）、中文字库 |
| 本仓库，PR #16（dev-ai-contest-2026） | 作品应用（signbridge）、文档与状态更新（整体基本完成）、CMake 构建记录、配置增量 |
| 本仓库 | 模型、词库、板级文档、AI 日志（随 PR #16 一并提交） |

## 10. AI 辅助说明

开发全程使用 AI 辅助，会话记录见 `logs/`。主要投入：板级移植期的
构建错误定位（fork 与上游 API 差异、esp-hal 版本锁定）；触摸接线与
I2C 地址考证；模型量化格式与板端加载设计；checkpatch 与提交规范的
批量合规修复；CMake 构建链路修复与全流程验证（HAL 拉取/补丁、
多脚本 LD_SCRIPT、mkimage 出 bin）；gt9xx 驱动 I2C 健壮性增强
（实测驱动的重试/恢复策略与事件统计）；以及跨仓 PR 的 CLA 合规
与提交规范梳理。
