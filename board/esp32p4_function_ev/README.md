# board/esp32p4_function_ev

ESP32-P4-Function-EV-Board 板级适配增量目录（openvela 新硬件适配赛道）。

> 主板级代码位于 fork 的 nuttx 仓
> `boards/risc-v/esp32p4/esp32p4-function-ev-board`（由 apache/nuttx master
> 移植，按竞赛规则以 PR 回合 open-vela/nuttx dev-ai-contest-2026）。
> 本目录存放本地增量：引脚表、外设配置说明、自定义 defconfig 片段。

## 硬件概要

| 部件 | 型号/参数 | 接入方式 |
|---|---|---|
| 主芯片 | ESP32-P4（双核 RISC-V 400MHz + LP 核） | — |
| 显示屏 | 7 寸 1024×600，EK79007AD+EK73217 | MIPI-DSI 2 lane |
| 触摸 | GT911 | I2C |
| 摄像头 | SC2336 2MP | MIPI-CSI 2 lane，RAW 拜耳 |
| 像素加速 | PPA（S2M/SRM/Blender） | 帧缩放/OSD |
| PSRAM | 32MB | 帧缓冲/模型权重 |
| 串口控制台 | UART0（GPIO37/38） | 115200 8N1 |

## 目录约定

- `pins.md` —— 关键外设引脚映射（DSI/CSI/I2C/背光/触摸中断复位）
- `defconfig.inc` —— 在 nsh defconfig 之上的功能增量（显示/摄像头/推理）

## 构建

```
cd nuttx
python3 tools/kconfiglib_olddefconfig.py \
    boards/risc-v/esp32p4/esp32p4-function-ev-board/configs/nsh/defconfig
make CROSSDEV=riscv32-esp-elf- -j8
```
