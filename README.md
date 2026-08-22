# 手语桥（SignBridge）

- 作品：手语桥，端侧手语识别终端
- 赛道：新硬件适配 + AI 硬件产品创新
- 硬件：ESP32-P4-Function-EV-Board（openvela 之前不支持这块板，移植是我们做的）

## 做的什么事

给听障人群做一个离线翻译设备：摄像头拍手势，板子在本地识别成文字，
显示在屏幕上并播放语音；对方说话时，设备做语音检测，屏幕上回放
手语动画。全程不联网。

选这块板子有两个原因。一是它带屏幕、摄像头、麦克风、功放，做这类
产品原型基本不用外扩；二是 openvela 还没有它的板级支持，我们想把
移植这件事一起做了。

## 现在的情况

板级部分：显示（EK79007AD，1024×600）、触摸、音频、摄像头控制面
都已经驱动通了。其中触摸值得说一下，这块板子的 GT911 中断和复位线
硬件上没有接到主控（只在屏幕适配板 J6 排针上引出），官方文档没写清楚，
我们对着原理图和 esp-bsp 源码核实之后改成了轮询驱动，I2C 地址 0x5D
（INT 悬空时的默认地址）。引脚分配整理在
`board/esp32p4_function_ev/pins.md`，每一项都写了出处。

识别部分：摄像头帧经过 MediaPipe hand landmark 模型（TFLite Micro，
模型 7.5MB 打进 ROMFS）提取 21 个手部关键点，按帧累积成滑动窗口，
送进一个自己训的 INT8 MLP 分类器（270KB，PC 端训练，自定义二进制
格式，板端运行时加载）。识别出的词一方面上屏，一方面触发对应的
语音播报（50 个中文词，espeak-ng 生成）。

语音输入部分：麦克风采集走 `/dev/audio/pcm_in0`，做了基于帧能量的
VAD。唤醒词"你好，openvela"目前是用 VAD 兜底的，检测到有人说话就
唤醒，真正的唤醒词模型还没有训练，这个在代码里有 TODO。

界面：LVGL，按 1024×600 横屏布局，左边结果显示和置信度条，
右边 500×500 摄像头预览。

其他：离线语义纠错（低置信度丢弃、连续重复去重、窗口内合并，
云端接口预留了没实现）；空闲自动关背光。

还没做完的，按顺序：

1. 摄像头 CSI 的 DMA 数据面没打通，预览目前用测试图案代替；
2. 唤醒词模型没训练；
3. 云端纠错只有接口；
4. 深睡没做，触摸中断没接，深睡后没有可靠唤醒源。

真机上还要再确认三件事：扬声器出声效果、触摸坐标方向
（esp-bsp 做了镜像，NuttX 的 gt9xx 驱动不做）、摄像头出图。

## 目录

```
app/signbridge/          应用，状态机 + 推理 + 界面 + 音频
  signbridge_sm.c          状态机
  signbridge_infer.c       关键点窗口和分类调度
  signbridge_infer_tflm.cc TFLM 后端
  signbridge_cls_mlp.c     INT8 MLP 分类器
  signbridge_ui.c          LVGL 界面
  signbridge_anim.c        手语动画
  signbridge_voice.c       语音播报
  signbridge_audio_in.c    采集 + VAD
  signbridge_correct.c     离线纠错
  signbridge_pm.c          关屏/唤醒
  signbridge_vocab.c       50 词表

board/esp32p4_function_ev/  板级文档（引脚表、增量配置）
models/                     分类器权重和 hand landmark 模型
media/signs/                50 个播报 WAV
tools/                      PC 端训练脚本
logs/                       AI Coding 会话日志
```

板级代码本身（esp32p4 arch、板目录、外设驱动）在 nuttx 公共仓，
按指南以 PR 提到 open-vela/nuttx 的 dev-ai-contest-2026 分支，
对应 PR #347。

## 怎么跑

```
repo init -u https://github.com/open-vela/contest2026_430_zuoyeyushufengzhou \
    -b dev-ai-contest-2026 -m contest2026_430_zuoyeyushufengzhou.xml
repo sync -c -j8

cd nuttx
export PATH=<riscv-none-elf 工具链>/bin:$PATH
python3 tools/kconfiglib_olddefconfig.py \
    boards/risc-v/esp32p4/esp32p4-function-ev-board/configs/lvgl/defconfig
make -j$(nproc)
```

产物 `nuttx/nuttx.bin`，大约 10.7MB（应用、模型、语音词库都在里面）。

烧录：

```
esptool.py -c esp32p4 -p /dev/ttyACM0 -b 921600 \
    write_flash -fs detect -fm dio -ff 80m 0x0 nuttx.bin
```

串口 115200。应用是系统入口，开机自己就起来了，也可以在 NSH 里
手动 `signbridge`。

## 关于 AI 辅助

这个项目基本是 AI 辅助完成的，完整会话在 `logs/`。
说几个实际占用时间最多的地方：板级移植期间的构建错误排查
（fork 和上游 API 差异、esp-hal 版本锁定，前后修了一百多轮构建）；
触摸接线和 I2C 地址的考证（直接决定了驱动怎么写）；模型量化格式
和板端加载代码的设计。规范类的问题（checkpatch、提交信息、目录
约定）也是靠 AI 扫描修复的。
