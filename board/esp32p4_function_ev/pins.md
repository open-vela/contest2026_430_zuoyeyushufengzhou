# ESP32-P4-Function-EV-Board 关键引脚（源自 Espressif esp-dev-kits 原理图）

| 功能 | 信号 | GPIO |
|---|---|---|
| 串口控制台 TX | U0TXD | 37 |
| 串口控制台 RX | U0RXD | 38 |
| 显示 DSI 时钟/数据 | MIPI-DSI（固定引脚，不可映射） | — |
| 触摸 I2C SCL | I2C0_SCL | 8 |
| 触摸 I2C SDA | I2C0_SDA | 7 |
| 触摸中断 | GT911_INT | 9 |
| 显示/触摸复位 | LCD_RST | 16 |
| 背光控制 | LCD_BL_PWM | 26 |
| 摄像头 I2C SCL | I2C1_SCL | 10 |
| 摄像头 I2C SDA | I2C1_SDA | 11 |
| 摄像头数据 | MIPI-CSI 2 lane（固定引脚） | — |
| 用户按键 | BOOT | 35 |

> 说明：MIPI-DSI/CSI 为固定物理引脚，无 GPIO 矩阵映射；上表为
> bringup 阶段实测/原理图核对前的初值，阶段 1 点亮屏幕前以
> esp-dev-kits 最新原理图复核。
