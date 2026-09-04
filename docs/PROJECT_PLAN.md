# 项目规划：手语桥（SignBridge）端侧手语识别终端

> 主要目标板：ESP32-P4-Function-EV-Board
> 状态口径与 README 第 6 节一致：编译/静态验证已通过的项标"已实现"，
> 需要上电的项标"待烧录复验"，未做的项如实标"预留/未实现"。

---

## 1. 项目目标

- **赛道**：新硬件适配 + AI 硬件产品创新
- **目标**：在 ESP32-P4-Function-EV-Board 上交付一个离线手语双向翻译终端：
  摄像头采集手部画面 → 端侧关键点提取与时序分类 → 屏幕显示 + 语音播报；
  反向通路为麦克风采集、VAD 与手语动画回放
- **新硬件适配部分**：ESP32-P4 在 openvela 上无任何板级支持，芯片架构、
  板级外设、显示/摄像头链路、CMake 构建支持均从零移植

## 2. 功能清单与状态

### 功能 1：平台移植（新硬件适配，全部已交付）

| 任务 | 状态 | 交付位置 |
| --- | --- | --- |
| ESP32-P4 芯片支持（arch + espressif HAL 80+ 外设） | 已完成，CMake 全链路出 bin | open-vela/nuttx PR #364 |
| 三块板级目录（function-ev / tab5 / pico-wifi） | 已完成，35 个外设配置 | open-vela/nuttx PR #364 |
| 自研 MIPI-CSI 驱动（DW-GDMA 双缓冲 + 帧回调） | 已实现，编译通过 | open-vela/nuttx PR #364 |
| MIPI-DSI 显示链路（EK79007 1024×600） | 已实现，已验证 | open-vela/nuttx PR #364 |
| gt9xx 触摸驱动增强（I2C 重试/总线恢复/统计） | 已完成 | open-vela/nuttx PR #364 |
| CMake 构建支持（多链接脚本/HAL 补丁/mkimage） | 已验证：nsh 配置出 nuttx.bin 281KB | open-vela/nuttx PR #364 |

### 功能 2：SignBridge 推理应用（专属仓 app/signbridge/）

| 任务 | 状态 |
| --- | --- |
| 应用状态机（IDLE→DETECTING→RECOGNIZING→RESULT） | 已实现，checkpatch 通过 |
| 摄像头帧源抽象（测试图案 / MIPI-CSI 切换） | 已实现 |
| TFLite Micro 推理链路（hand landmark + INT8 MLP） | 已实现，编译入链 |
| INT8 MLP 分类器权重加载（全量边界校验） | 已实现 |
| LVGL 主界面 / 手语骨架动画 / 50 词语音播报 | 已实现 |
| 麦克风采集 + 能量 VAD / 离线语义纠错 / 背光功耗管理 | 已实现 |
| **整机链路（烧录后 UI/语音/推理联调）** | **基本完成，待烧录复验** |

### 功能 3：LVGL 摄像头演示（nuttx-apps lvgldemo）

| 任务 | 状态 |
| --- | --- |
| 开机画面（logo + 进度条）→ SignBridge 主界面 | 已实现 |
| SC2336 RAW8 1280×720@30fps 经 CSI 驱动取帧 | 已实现 |
| LVGL 定时器轮询帧 → 裁剪缩放 → 灰度 RGB565 预览 | 已实现 |
| 中文字库（16/40px 全量符号集）+ 音频提示音接口 | 已实现 |
| 位置：open-vela/nuttx-apps PR #123 | 已提交 |

### 功能 4：语音双向通路

| 任务 | 状态 |
| --- | --- |
| 识别结果 → 语音播报（nxplayer + ROMFS 词库） | 已实现，待烧录复验 |
| 麦克风 → VAD 唤醒兜底 | 已实现，待烧录复验 |
| 唤醒词模型训练 | 未实现（VAD 兜底） |

### 功能 5：扩展能力（预留）

| 任务 | 状态 |
| --- | --- |
| 云端语义纠错（ESP32-C6 WiFi6 通路） | 接口预留，应用层未实现 |
| 深睡低功耗 | 未实现（GT911 INT/RESET 未接主控，无可靠唤醒源，见 README 3.3） |

## 3. 里程碑回顾

| 阶段 | 交付 |
| --- | --- |
| 1. 平台移植 | esp32p4 架构接入、HAL 适配、板级目录、35 个外设配置 |
| 2. 显示/触摸 | MIPI-DSI 点亮 1024×600、GT911 轮询、LVGL 框架 |
| 3. 摄像头 | 自研 MIPI-CSI 驱动、SC2336 接入、双缓冲取帧 |
| 4. 应用 | signbridge 推理应用（专属仓）、lvgldemo UI/预览（nuttx-apps） |
| 5. 构建链 | CMake 全链路修复并出 bin（HAL 补丁/多脚本链接/mkimage） |
| 6. 提交 | nuttx#364、nuttx-apps#123、专属仓 PR #15–#21（文档与状态更新） |

## 4. 代码归属

| 内容 | 归属 |
| --- | --- |
| ESP32-P4 芯片支持、板级目录、gt9xx 驱动、CMake 构建支持 | open-vela/nuttx PR #364 |
| lvgldemo SignBridge UI + 摄像头预览 | open-vela/nuttx-apps PR #123 |
| signbridge 推理应用、模型、词库、板级文档、技术报告 | 本仓库 |

## 5. 剩余工作

1. **烧录复验**：整机链路（UI/语音/推理）上电联调——当前唯一的关键路径
2. 唤醒词模型训练（替代 VAD 兜底）
3. 云端纠错应用层（依赖 C6 WiFi 通路）
4. 硬件改线（GT911 INT/RESET 接主控）后补深睡低功耗
