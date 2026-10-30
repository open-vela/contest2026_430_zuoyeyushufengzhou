---
name: hardware-docs-lookup
description: ESP32-P4-Function-EV-Board 硬件资料速查——需求到出处文件的定位表，以及本板已验证的硬件事实清单（杜邦线接法、排线线序、I2C 地址表、音频链路、DSI 时序、待核对项）。当需要确认引脚定义、连接器位置、排线接法、装配方式、时序参数时使用。
---

# 硬件资料速查（板级文档路由 + 已验证事实）

## 使用时机

- 要确认某个引脚 / 连接器 / 排线的接法与出处
- 要查装配方式、子板连接、DSI / CSI 时序参数
- 写代码或文档引用硬件结论前，需要找到可注明的出处文件

## Step 1：需求 → 资料定位表

| 需求 | 出处（本仓优先） | 备注 |
|------|------------------|------|
| 引脚映射与依据链 | `board/esp32p4_function_ev/pins.md` | **第一站**；文首列有原理图 / 官方指南 / 规格书交叉核对清单 |
| 阶段化外设配置 | `board/esp32p4_function_ev/defconfig.inc` | 显示 / 音频 / 摄像头 / 推理分阶段 Kconfig 增量 |
| 外设参数与接口汇总 | `README.md` §2 硬件环境表 | 型号 / 接口 / 地址一页速查 |
| 板级适配背景与验证状态 | `docs/TECH_REPORT.md` 硬件设计与适配节、`docs/PROJECT_PLAN.md` | — |
| 官方装配方式 / 跳线 / 入门 | Espressif 官方用户指南《ESP32-P4X-Function-EV-Board》（docs.espressif.com） | 主板叠装与杜邦线接法的权威出处 |
| 屏 / 驱动芯片参数 | 显示规格书 AML070JGI50（EK79007AD + EK73217BCGA，1024×600）；显示 / 摄像头子板原理图 | 引用于 `pins.md` 文首依据链 |

## Step 2：已验证硬件事实速查（引用时注明出处）

| 事实 | 出处 |
|------|------|
| LCD 排线为**反向线序**（触点异面），接 MIPI-DSI；摄像头排线为同向线序，接 MIPI-CSI，**两者不可混用** | 官方用户指南「硬件设置」 |
| **GPIO27→RST_LCD（J1-38）、GPIO26→背光 PWM（J1-31）两条杜邦线必接**，漏接背光线是「屏不亮」最常见根因；背光经 AP3012K boost 驱动 | `pins.md` + 官方用户指南 |
| 触摸 INT_TP / RESET_TP **均不接（NC）**，仅引出至适配板 J6（INT=pin5 / RESET=pin8，中等置信度，飞线前万用表复核）；RESET 侧 R10 上拉 3V3 | `pins.md` |
| I2C0 单总线三设备：GT911（0x5D，INT 悬空锁存；0x14 需 INT 拉高）、ES8311（0x18）、SC2336 SCCB（0x3C）；SCL=8 / SDA=7，400kHz | `pins.md` + README §2 |
| 音频链路：**本板无独立采集芯片**，模拟麦克风直入 ES8311 ADC；扬声器经 NS4150 3W D 类功放；I2S0 引脚 BCLK=12 / MCLK=13 / WS=10 / DOUT=9 / DIN=11 | `pins.md` |
| DSI 时序：HPW=10 HBP=160 HFP=160 / VPW=1 VBP=23 VFP=12，DPI CLK≈52MHz，2 lane 1000Mbps/lane | `pins.md` |
| MIPI-DSI / CSI 数据线为 P4 专用物理引脚，**无 GPIO 矩阵映射** | `pins.md` |
| 摄像头 XVCLK 由子板 24MHz 晶振（Y1）提供；传感器复位经子板 NMOS 电平转换 | `pins.md` |
| 控制台 UART0：TX=GPIO37 / RX=GPIO38，115200；进下载模式：BOOT(GPIO0) + Reset | `pins.md` |
| 官方叠装方式：主板叠于 LCD 子板中心 4 螺母柱（孔距 58×49 为实测值） | 官方用户指南（叠装方式）+ 子板钻孔文件实测 |

## 注意事项

- 引用硬件结论必须注明出处文件路径，**严禁凭记忆**。
- `pins.md` 中的「待真机核对项」（J6 引脚号、触摸坐标方向、SCCB 复用最终确认）在真机核对前**不得当作已验证结论**引用。
- MIPI 时序参数以屏规格书为基准，驱动侧微调以实体机实测为准（PR #347 残余清单）。
