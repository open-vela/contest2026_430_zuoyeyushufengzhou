# ESP32-P4-Function-EV-Board 关键引脚

依据（交叉核对）：
- 主板原理图 SCH_ESP32-P4X_FUNCTION_EV_BOARD_V1.8（用户提供）
- Espressif 官方用户指南 ESP32-P4X-Function-EV-Board user_guide（V1.8，权威）
- 显示屏/摄像头子板原理图（用户提供）
- 显示屏规格书 AML070JGI50（EK79007AD + EK73217BCGA，1024×600）

## 串口控制台

| 功能 | 信号 | GPIO |
|---|---|---|
| 控制台 TX | U0TXD | 37 |
| 控制台 RX | U0RXD | 38 |

## 显示（MIPI-DSI，固定物理引脚）

| 项 | 值 |
|---|---|
| 模组 | AML070JGI50-07403L，7.0 寸 |
| 分辨率 | **1024×600**（EK79007AD 源极驱动 + EK73217BCGA 栅极驱动） |
| 接口 | MIPI-DSI 视频模式，2 data lane |
| DSI 时序 | HPW=10 HBP=160 HFP=160 / VPW=1 VBP=23 VFP=12，DPI CLK≈52MHz |
| DSI lane 速率 | 1000 Mbps/lane |
| 复位 | **RST_LCD = GPIO27**（官方默认，杜邦线接 J1-38） |
| 背光 | **PWM = GPIO26**（官方默认，杜邦线接 J1-31，驱动 AP3012K boost） |

> 注意：DSI/CSI 数据线为 ESP32-P4 专用 MIPI 物理引脚，无 GPIO 矩阵映射。
> LCD 复位与背光在本板为杜邦线连接（官方默认 GPIO27/GPIO26），可软件重配。

## 音频（ES8311 codec + NS4150 功放 + I2S0）

| 功能 | 信号 | GPIO |
|---|---|---|
| I2S 位时钟 | I2S_SCLK / BCLK | 12 |
| I2S 主时钟 | I2S_MCLK | 13 |
| I2S 字选择 | I2S_LRCK / WS | 10 |
| I2S 数据输出（P4 TX → codec DSDIN） | **I2S_DSDIN** | **9** |
| I2S 数据输入（P4 RX ← codec ASDOUT） | **I2S_ASDOUT** | **11** |
| I2C（共用 I2C0） | SCL=8 SDA=7 | 8 / 7 |
| ES8311 I2C 地址 | — | 0x18 |
| 功放 | NS4150（3W 单声道 D 类） | codec 直驱 |

## 触摸（GT911，位于显示模组 FPC）

| 功能 | 信号 | GPIO |
|---|---|---|
| I2C | 与音频/codec 共用 I2C0 | SCL=8 SDA=7 |
| GT911 I2C 地址 | — | **0x5D**（INT 悬空；0x14 需 INT 拉高） |
| 触摸中断 | INT_TP | **不接（NC）**：仅引出到适配板 J6-pin5，需飞线；esp-bsp: BSP_LCD_TOUCH_INT=NC，轮询模式 |
| 触摸复位 | RESET_TP | **不接（NC）**：适配板 R10 上拉 3V3；esp-bsp: BSP_LCD_TOUCH_RST=NC |

## 摄像头（MIPI-CSI，固定物理引脚）

| 项 | 值 |
|---|---|
| 传感器 | SC2336（2MP，RAW Bayer） |
| 接口 | MIPI-CSI 2 data lane |
| 传感器 I2C（SCCB） | ESP_I2C_SCL/SDA → SENSOR_SCL/SDA（与显示/音频共用 I2C0，SCL=8/SDA=7） |
| 时钟 | 子板 24MHz 晶振（Y1）提供 XVCLK |
| 传感器复位 | RST（子板 NMOS 电平转换） |

## 其他

| 功能 | GPIO |
|---|---|
| 用户按键 BOOT | 0（按住 BOOT + 按 Reset 进下载模式） |
| ESP32-C6（WiFi6/BLE，SDIO） | 另见子板 |

## 待真机核对项

- 触摸 GT911 的 INT/RESET 已确认不接（官方 NC，轮询模式）；如需中断驱动，从适配板 J6-pin5(pin8) 飞线到空闲 GPIO 并改 esp32p4_touch.c 回调
- 摄像头 SCCB 是否与显示/音频确为同一 I2C 物理总线
