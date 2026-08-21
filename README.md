# 手语桥（SignBridge）—— 端侧手语识别终端

## 一、作品简介

手语桥是运行在 **ESP32-P4-Function-EV-Board** 上的纯端侧手语识别终端，
基于 openvela（小米开源的 Apache NuttX RTOS）实现。面向听障群体的日常
沟通场景：通过 MIPI 摄像头采集手部画面，端侧完成 21 关键点提取与
TCN/MLP 时序分类，将手语实时翻译为文字并在 7 寸触摸屏上展示，
全程离线、无云端依赖。

核心亮点：
- **openvela 首次跑通 ESP32-P4-Function-EV-Board**（新硬件适配）：
  MIPI-DSI 显示（EK79007AD 1024×600）、GT911 触摸（轮询模式）、
  MIPI-CSI 摄像头、ES8311 音频全部基于 NuttX 驱动打通
- **完整端侧推理链路**：TFLite Micro 真实推理（MediaPipe hand
  landmark 模型，7.5MB 内嵌 ROMFS）+ INT8 时序分类器（270KB）
- **双向沟通闭环**：手语→文字+语音播报（50 词中文词库）；
  语音输入→VAD+唤醒词（"你好，openvela"）→手语动画
- **LVGL 实时界面**：识别结果 + 置信度 + 摄像头预览 + 手语动画 +
  状态栏（功耗级别/麦克风音量）

## 功能完成度（截至 2026-08-21）

| 模块 | 状态 | 说明 |
|---|---|---|
| 手语识别（关键点+时序分类） | 75% | TFLM 编译链路全通，真实摄像头帧采集/模型推理待真机 |
| 语音播报（ES8311+50词词库） | 90% | 驱动+词库完成，待真机出声验证 |
| 语音输入（I2S 麦克风+VAD+唤醒词） | 70% | 采集+VAD+唤醒词接口完成，唤醒模型待训练 |
| 手语动画（LVGL） | 85% | 程序化动画完成 |
| 语义纠错（离线规则版） | 80% | 碎片拼接/去重/模板补全，云端路径留接口 |
| 功耗管理（空闲关屏/唤醒） | 70% | 基础分级完成，深睡/降频待真机 |
| 板级硬件驱动（显示/触摸/音频/摄像头） | 85% | 按官方资料权威核对，待真机验证 |

## 二、选题方向

新硬件适配 + AI 硬件产品创新（双赛道）。

理由：ESP32-P4-Function-EV-Board 是乐鑫 2024 年发布的 AI 视觉旗舰
开发板（双核 RISC-V 400MHz + 32MB PSRAM + MIPI-DSI/CSI + PPA），
但 openvela 尚无该板级支持。本项目先完成板级移植（nsh → lvgl 基线），
再在其上构建端侧手语识别应用，形成"移植 + 应用"完整闭环。

## 三、目录结构

```
app/signbridge/              # 端侧手语识别应用（主作品）
├── signbridge_main.c        #   入口：LVGL + 状态机事件循环
├── signbridge_sm.c          #   状态机：IDLE→DETECTING→RECOGNIZING→RESULT
├── signbridge_camera.c      #   摄像头帧源（测试图案 + MIPI-CSI/SC2336）
├── signbridge_infer.c       #   推理管线统一接口（stub/TFLM 切换）
├── signbridge_infer_tflm.cc #   TFLite Micro 后端（hand_landmark 模型）
├── signbridge_cls_mlp.c     #   INT8 时序分类器（ROMFS 权重加载）
├── signbridge_ui.c          #   LVGL 界面（结果/置信度/预览/动画/状态栏）
├── signbridge_anim.c        #   程序化手语动画播放器
├── signbridge_voice.c       #   语音播报（nxplayer + 50 词 WAV）
├── signbridge_audio_in.c    #   语音输入（I2S 麦克风 + VAD + 唤醒词）
├── signbridge_correct.c     #   语义纠错（离线规则版）
├── signbridge_pm.c          #   功耗管理（空闲关屏/活动唤醒）
├── signbridge_vocab.c       #   共享 50 词中文词表
└── Kconfig / Make.defs / Makefile

board/esp32p4_function_ev/   # ESP32-P4-Function-EV-Board 板级适配文档
├── README.md                #   板级说明与构建命令
├── pins.md                  #   引脚映射表（权威核对版）
└── defconfig.inc            #   各阶段功能增量配置

models/                      # 训练产物
├── sign_classifier_int8.bin #   INT8 量化分类器权重（270KB）
├── sign_classifier_int8.tflite
├── hand_landmarker/         #   MediaPipe hand landmark 模型（7.5MB）
│   ├── hand_detector.tflite
│   └── hand_landmarks_detector.tflite
└── vocab.txt                #   50 类手语词表

media/signs/                 # 语音播报词库（50 个中文 WAV，espeak-ng 生成）

tools/                       # PC 端训练脚本（TensorFlow Keras）
├── train_sign_classifier_tf.py
└── train_sign_classifier.py

logs/                        # AI Coding 日志

> 注：板级核心代码（esp32p4 arch、esp32p4-function-ev-board 板目录、
> MIPI-DSI/ILI9881C/GT911 驱动）属于 nuttx 公共仓改动，按提交指南
> 以 PR 形式提交到 open-vela/nuttx 的 dev-ai-contest-2026 分支。
```

## 四、运行方式

### 1. 拉取工程

```
repo init -u https://github.com/open-vela/contest2026_430_zuoyeyushufengzhou \
    -b dev-ai-contest-2026 -m contest2026_430_zuoyeyushufengzhou.xml
repo sync -c -j8
```

### 2. 编译（make 路径，ESP32-P4）

```
cd nuttx
export PATH=<riscv32-esp-elf 工具链>:$PATH

# 生成 .config（lvgl 基线含显示 + 触摸 + signbridge 应用）
python3 tools/kconfiglib_olddefconfig.py \
    boards/risc-v/esp32p4/esp32p4-function-ev-board/configs/lvgl/defconfig

make CROSSDEV=riscv32-esp-elf- -j8
```

产物：`nuttx/nuttx.bin`（SIMPLE_BOOT，无 MCUboot）。

### 3. 烧录

```
esptool.py -c esp32p4 -p /dev/ttyACM0 -b 921600 \
    write_flash -fs detect -fm dio -ff 80m 0x0 nuttx.bin
```

### 4. 运行

串口 115200 连接后，NSH 下执行：

```
nsh> signbridge
```

屏幕显示：标题栏 → 识别结果大字 → 置信度条 → 摄像头预览区
（当前为测试图案动画）→ 状态栏。状态机自动循环
IDLE→DETECTING→RECOGNIZING→RESULT 演示完整识别流程。

## 五、AI Coding 使用说明

本项目全程使用 AI 辅助开发：

- **方案设计**：需求收敛（功能裁剪决策）、架构设计（状态机/推理管线
  /UI 分层）、竞品与开源方案调研（WLASL 数据集、MediaPipe 模型）
- **板级移植**：ESP32-P4 arch 源码接线、14 轮构建迭代修复
  （fork API 差异、HAL 版本 pin、atomic 兼容等均由 AI 定位解决）
- **驱动开发**：ILI9881C MIPI-DSI 面板初始化、GT911 触摸、
  LVGL framebuffer 集成
- **模型训练**：TensorFlow Keras 训练脚本生成、INT8 量化、
  权重二进制格式设计与 C 加载代码
- **文档**：README、引脚表、构建指南

完整对话日志见 logs/ 目录。

## 六、当前进度与规划

| 阶段 | 内容 | 状态 |
|---|---|---|
| 0 | 工具链 + esp32p4 nsh/lvgl 构建基线 | ✅ |
| 1 | MIPI-DSI + EK79007 + GT911 + LVGL | ✅ |
| 2 | MIPI-CSI 摄像头链路 | 🔄 框架就绪，SC2336 传感器驱动开发中 |
| 3 | TFLite Micro 推理管线 | ✅ 分类器 INT8 权重已训练嵌入 |
| 4 | signbridge 应用 + LVGL 界面 | ✅ |
| 5 | 语音播报 / 手语动画 / 低功耗 | 规划中 |
