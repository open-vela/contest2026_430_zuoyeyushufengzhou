# 手语桥（SignBridge）—— 端侧手语识别终端

**作品名称**：手语桥（SignBridge）
**所属赛道**：新硬件适配 + AI 硬件产品创新（双赛道）
**目标硬件**：ESP32-P4-Function-EV-Board（openvela 首次适配该板）

## 这是什么

一块能"看懂"手语的板子。

听障朋友在日常沟通里最大的困难，不是"不会表达"，而是对方看不懂手语。
手语桥想做的事很直接：摄像头拍下手势，设备自己在本地完成识别，
把结果用大字显示在 7 寸屏上，同时念给对方听；反过来，对方说话时，
设备识别语音并在屏幕上打出手语动画。整个过程不联网，不上传任何画面。

整机基于 openvela（小米开源的 Apache NuttX RTOS）。ESP32-P4 这块板子
发布时 openvela 还不支持它，所以我们做的第一件事是把板级移植跑通，
然后才在上面搭应用——这也是作品的双赛道属性：既交出一份新硬件适配，
也交出一个跑在它上面的完整产品。

## 它现在能做到什么

先说实话：代码链路已经全部打通并编译验证，但真机验证还没做完，
下面按"已经确定能跑"和"等真机说话"分开讲。

**已经确定能跑的：**

- **板级移植**。ESP32-P4 在 openvela 上从零到可运行：MIPI-DSI 显示
  （EK79007AD，1024×600）、GT911 触摸（这块板子的触摸中断和复位线
  硬件上没接，我们考证后改成了纯轮询方案）、ES8311 音频、
  SC2336 摄像头控制面，全部基于 NuttX 驱动。引脚分配逐项对照
  官方原理图和 esp-bsp 核实过，见 `board/esp32p4_function_ev/pins.md`。
- **识别链路**。摄像头帧 → MediaPipe hand landmark 模型提取 21 个手部
  关键点（TFLite Micro 推理，模型 7.5MB 内嵌 ROMFS）→ 关键点按帧
  累积成滑动窗口 → INT8 量化 MLP 时序分类器（270KB，PC 端训练、
  自定义二进制格式、板端加载）→ 输出词和置信度。这条链路的每一环
  都有真实实现，不是桩代码。
- **说出来**。识别结果触发语音播报：50 个中文词的预录词库
  （espeak-ng 生成，22050Hz），走 ES8311 + NS4150 功放，经
  nxplayer 播放。
- **界面**。LVGL 按 1024×600 横屏布局：左边是识别结果大字、
  置信度条和状态信息，右边是 500×500 的摄像头预览。
- **语音输入侧**。ES8311 麦克风采集（`/dev/audio/pcm_in0`）+
  基于帧能量的 VAD。唤醒词"你好，openvela"目前是 VAD 兜底方案：
  检测到说话即唤醒，真正的唤醒词模型还没训练，这是已知的欠账。
- **手语动画**。识别出的词会驱动一段程序化手部动画（21 关键点
  骨架插值），作为"语音转手语"方向的演示。
- **语义纠错**。识别出的词先进碎片队列，输出前做离线规则纠错：
  低置信度丢弃、连续重复去重、时间窗内合并。云端纠错（走
  ESP32-C6 的 WiFi6）留了接口没实现。
- **功耗管理**。空闲超时自动关背光，触摸/识别活动点亮。
  深睡和降频没做——触摸中断线没接，深睡后没有可靠的唤醒源。

**等真机验证的：**

- ES8311 实际出声效果、摄像头 CSI DMA 帧采集（控制面通了，
  数据面还靠测试图案顶着）、触摸坐标方向（esp-bsp 做了镜像，
  NuttX gt9xx 驱动不做，需要上屏对一次）。

## 目录结构

```
app/signbridge/              # 应用主体
├── signbridge_main.c        #   入口：状态机 + LVGL 事件循环
├── signbridge_sm.c          #   状态机：IDLE→DETECTING→RECOGNIZING→RESULT
├── signbridge_camera.c      #   摄像头帧源（测试图案 / MIPI-CSI）
├── signbridge_infer.c       #   推理管线：关键点窗口管理 + 分类调度
├── signbridge_infer_tflm.cc #   TFLite Micro 后端（hand landmark）
├── signbridge_cls_mlp.c     #   INT8 MLP 时序分类器（加载 ROMFS 权重）
├── signbridge_ui.c          #   LVGL 界面（1024×600 横屏布局）
├── signbridge_anim.c        #   程序化手语动画
├── signbridge_voice.c       #   语音播报（nxplayer + 50 词 WAV）
├── signbridge_audio_in.c    #   麦克风采集 + VAD + 唤醒接口
├── signbridge_correct.c     #   离线语义纠错
├── signbridge_pm.c          #   空闲关屏 / 活动唤醒
└── signbridge_vocab.c       #   50 词共享词表（识别/播报/动画共用）

board/esp32p4_function_ev/   # 板级适配文档
├── pins.md                  #   引脚速查（按原理图逐项核实）
└── defconfig.inc            #   功能增量配置

models/                      # 训练产物
├── sign_classifier_int8.bin #   INT8 分类器权重（自定义格式，270KB）
├── hand_landmarker/         #   MediaPipe hand landmark 模型（7.5MB）
└── vocab.txt                #   50 类词表

media/signs/                 #   50 个播报用中文 WAV
tools/                       #   PC 端训练脚本（TensorFlow Keras）
logs/                        #   AI Coding 会话日志
```

板级核心代码（esp32p4 arch、板目录、各外设驱动）属于 nuttx 公共仓改动，
按大赛指南以 PR 提交到 open-vela/nuttx 的 dev-ai-contest-2026 分支
（PR #347），不在本仓库内。

## 运行方式

### 拉取工程

```
repo init -u https://github.com/open-vela/contest2026_430_zuoyeyushufengzhou \
    -b dev-ai-contest-2026 -m contest2026_430_zuoyeyushufengzhou.xml
repo sync -c -j8
```

### 编译

```
cd nuttx
export PATH=<riscv-none-elf 工具链>/bin:$PATH

python3 tools/kconfiglib_olddefconfig.py \
    boards/risc-v/esp32p4/esp32p4-function-ev-board/configs/lvgl/defconfig

make -j$(nproc)
```

产物为 `nuttx/nuttx.bin`（约 10.7MB，含应用、模型与语音词库的
ROMFS 镜像）。

### 烧录与运行

```
esptool.py -c esp32p4 -p /dev/ttyACM0 -b 921600 \
    write_flash -fs detect -fm dio -ff 80m 0x0 nuttx.bin
```

串口 115200 接入后，应用作为系统入口自动启动（也可在 NSH 下手动
执行 `signbridge`）。开机后屏幕左侧显示识别结果与置信度，右侧显示
摄像头预览；状态机循环演示完整识别流程，识别出结果时自动播报对应语音。

## AI Coding 使用说明

这个项目基本是在 AI 辅助下完成的，`logs/` 里保留了完整的会话记录。
几个比较有代表性的环节：

- **板级移植**是最大的一块硬骨头。ESP32-P4 的 arch 接线、
  esp-hal-3rdparty 版本锁定、fork 与上游的 API 差异，前后迭代了
  上百轮构建才稳定，绝大多数编译错误由 AI 直接定位修复。
- **硬件考证**。屏幕适配板的触摸中断/复位到底接没接、I2C 地址怎么
  锁存，官方文档没说透——AI 用原理图坐标提取加 esp-bsp 源码交叉
  验证，最终确认了轮询方案，这个结论直接决定了驱动写法。
- **模型链路**。训练脚本、INT8 量化、自定义权重二进制格式和板端
  C 加载代码，都是在对话里设计并验证的。
- **规范返工**。checkpatch 不合规、提交信息语言、目录约定这类问题，
  基本靠 AI 按 .claude 规范批量扫描修复。

## 已知欠账

按重要性排：

1. 唤醒词模型没训练，目前用 VAD 兜底（任何说话声都会唤醒）；
2. 摄像头 CSI DMA 数据面未打通，预览暂用测试图案；
3. 云端语义纠错只留了接口；
4. 深睡/降频未实现（受限于触摸中断未接的硬件现实）。

这些在代码里都有对应的 TODO 标记，不是遗漏，是排期。
