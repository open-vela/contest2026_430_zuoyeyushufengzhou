---
name: board-flash-monitor
description: ESP32-P4-Function-EV-Board 在 openvela 下的构建、烧录、串口监控与端口排障完整流程，含 build.sh CMake 链路、defconfig 变更核对纪律、esptool 烧录参数、串口占用清理与已知好固件归档。当用户提到构建、编译、烧录、flash、串口、monitor、抓日志、刷机、板子连不上、下载失败时使用。
---

# 板卡构建与串口监控（openvela × ESP32-P4 Function EV Board）

## 使用时机

- 构建 / 烧录 / 串口监控任一步失败
- 设备管理器（或 `dmesg`）看不到串口、烧录报找不到端口、串口被占用
- 要抓启动日志但不想长驻串口终端；或分不清串口里的报错是不是真问题

## Step 1：构建链路（openvela 标准入口）

| 项 | 值 / 命令 | 出处 |
|----|-----------|------|
| CMake 方式（已全流程验证：HAL 拉取→补丁→编译→链接→mkimage 出 bin） | `./build.sh boards/risc-v/esp32p4/esp32p4-function-ev-board/configs/nsh --cmake -j8` | `board/esp32p4_function_ev/README.md` |
| 工具链 | `riscv32-esp-elf`（esp-14.2.0 验证通过），先 `export PATH=<工具链>/bin:$PATH` | 同上 |
| 传统 Make 方式 | `python3 tools/kconfiglib_olddefconfig.py <板级>/configs/nsh/defconfig` → `make CROSSDEV=riscv32-esp-elf- -j8` | 同上 |
| 产物 | CMake：`cmake_out/esp32p4-function-ev-board_nsh/nuttx.bin`；nsh 基线约 281KB，SignBridge 全量约 10.7MB（含 ROMFS 模型与词库） | README §5.2 |

**HAL 版本纪律**：esp-hal-3rdparty 必须钉在与 Make 构建相同的版本并应用 openvela 兼容补丁——版本漂移会**静默**破坏构建，无报错直接产物不可用（nuttx PR #364 实证）。

## Step 2：defconfig 变更核对纪律

1. 功能增量按 `board/esp32p4_function_ev/defconfig.inc` 的阶段注释逐段放开（阶段 1 显示 → 1b 音频 → 2 摄像头 → 3 推理 → 4 应用），合并方式：`cat configs/nsh/defconfig defconfig.inc | sort -u > 新 defconfig`。
2. **choice 型配置「删行」≠ 关闭**——删行后回落 Kconfig 默认值（choice 组必有选中项兜底）。关闭必须显式写 `# CONFIG_XXX is not set` 或改选同组其他项。
3. 任何 defconfig 变更后必须重新生成配置并 `grep <关键项>` **逐项核对生效**，严禁只看编译是否通过。

## Step 3：串口探测与验身

| 情形 | 判定 / 动作 |
|------|------------|
| 板载 USB-Serial-JTAG（USB 2.0 OTG 口） | 乐鑫设备 VID **303A**（Win 设备管理器 / `lsusb` 核对），Win10/11 免驱，Linux 枚举为 `/dev/ttyACM*` |
| UART 桥接芯片口 | VID **10C4**（CP210x）；无端口时按板上丝印装 CP210x / CH34x 驱动 |
| 只探测到无关虚拟串口 | 板不在位或未枚举——**止步构建交付，不烧录、不视为验收** |
| 烧录中偶发掉枚举 | 重插 USB 即恢复；先查设备管理器 / `dmesg`，再怀疑代码 |
| 验身 | `esptool.py -c esp32p4 read_mac`（或 `flash_id`）确认芯片应答；进下载模式：按住 BOOT(GPIO0) + 按 Reset（`pins.md`） |

## Step 4：烧录与监控——单任务串行纪律

- 烧录命令（README §5.3）：`esptool.py -c esp32p4 -p <端口> -b 921600 write_flash 0x2000 nuttx.bin`
- 烧录与串口监控**必须单任务串行**：并行会互抢端口。烧录结束再开监控。
- 监控参数：UART0 控制台，TX=GPIO37 / RX=GPIO38，**115200 8N1**（`pins.md`）；串口终端任选（minicom / picocom / Tera Term 等）。
- 烧录前清残留占用进程（历次端口锁死的根因）：

```powershell
Get-CimInstance Win32_Process -Filter "Name='python.exe'" |
  Where-Object { $_.CommandLine -match 'esptool|serial|monitor' } |
  ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
```

## Step 5：免监控抓日志（推荐做法）

长驻串口终端容易残留进程占口。抓启动日志改用「复位 + 定时读取自退出」：

1. pyserial（或等价脚本）打开串口；
2. 对 RTS 打脉冲复位板子；
3. 读固定时长（20–30 s，覆盖完整启动链路）后自动退出，不占口。

## Step 6：串口健康判读

- 启动链路（按序）：BootROM → bootloader → NuttX 内核横幅 → NSH 提示符；SignBridge 经 `CONFIG_INIT_ENTRYPOINT=signbridge_main` 开机自启（README §5.4），亦可在 NSH 手动执行 `signbridge`。
- **验证红线**：串口无报错 ≠ 问题解决；目测项（花屏 / 撕裂 / 触摸落点 / 帧率）必须实体机确认。
- 首次出现的告警逐条定性后再决定是否处理；确认良性的记入清单，避免每轮重复排查。

## 注意事项

- 已知好固件按阶段归档（nsh 基线 / 全量两个参照点），供回退烧录与二分定位。
- Windows 传统 260 字符路径上限对深层构建树（含 HAL 三方库克隆）同样构成风险：系统开启 `LongPathsEnabled=1` + `git config core.longpaths true`。
- 上游 CI 提交纪律（CLA / nxstyle / 注释语言）见 `milestone-bringup-plan` 技能，本技能不重复。
