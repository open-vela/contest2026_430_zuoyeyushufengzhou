---
name: display-lvgl-pitfalls
description: ESP32-P4X + openvela（NuttX framebuffer + LVGL）显示链路排障——横屏原生决策依据、旋转带宽封顶、大缓冲 PSRAM 纪律、defconfig choice 删行陷阱、硬件加速软件回退、杜邦线漏接。当出现花屏、闪屏、黑屏、撕裂、显示起不来、fps 骤降、旋转方向不对、改 defconfig 不生效时使用。
---

# 显示链路排障（openvela × NuttX framebuffer + LVGL）

## 使用时机

- 显示起不来 / 黑屏 / 背光不亮 / 花屏 / 闪屏 / 撕裂
- fps 骤降、改了 defconfig 行为不变、动了显示相关 Kconfig 之后异常
- 任何涉及整屏旋转、软件旋转、PPA 加速的调参念头

## Step 1：症状 → 根因 → 处置

| 症状 | 根因 | 处置 |
|------|------|------|
| 背光不亮 / 屏不亮 | LCD 复位 **GPIO27**（杜邦线接 J1-38）与背光 PWM **GPIO26**（接 J1-31）为官方默认杜邦线接法，**漏接是最常见根因** | 先查两根杜邦线再查代码（`pins.md`） |
| 改 defconfig 行为不变 | choice 型配置「删行」≠ 关闭，回落 Kconfig 默认值（choice 组必有选中项兜底）；或改后未重新生成配置 | 显式写 `# CONFIG_XXX is not set` → 重新 olddefconfig/menuconfig → `grep` 逐项核对（Kconfig 通用语义，任何配置前端同理） |
| 整屏旋转后 fps 掉到个位数 | 横屏面板（EK79007AD+EK73217）整屏软件旋转受 PSRAM 带宽结构性封顶（LVGL 上游实测约 51ms/帧 ≈ 12fps，LVGL issue #9590），无成熟端到端方案 | **本板定案 1024×600 横屏原生零旋转**，UI 自设计起按横屏布局，绕开旋转问题域；确需竖屏属硬件选型问题，软件别硬扛 |
| 大旋转缓冲导致启动即崩 / 无限重启 | 整屏旋转缓冲（MB 量级）从内部 RAM 分配失败且未判空 | 渲染大缓冲一律挂 PSRAM 且**必须判空**再使用 |
| 回调注册被拒 / 显示服务初始化失败 | 要求回调驻留特殊内存段（cache-safe / IRAM 类）时，回调链上其他函数不满足段属性 | 核对**回调链上全部函数**的实际内存段属性——单点标记不等于全链安全 |
| PPA 搬运出现整屏细密竖向列噪声（UI 层干净） | PPA 硬件加速与 LVGL 叠加渲染存在上游已知缺陷（LVGL issue「ESP32-P4: PPA acceleration causes tearing and incorrect redraw」） | 叠加场景回退 CPU 拷贝；加速路径**必须有编译开关与软件回退** |
| 显示配置与文档不符 | 驱动头注释与实际行为不符（移植期实证：注释写 800×1280 旋转扫描，实为 1024×600 直接扫描，#347） | 以源码实测行为为准修正文档；**注释失真同样是坑** |

## Step 2：本板显示配置锚点（defconfig.inc 阶段 1）

| 配置 / 参数 | 值 |
|-------------|-----|
| MIPI-DSI | `CONFIG_ESPRESSIF_MIPI_DSI=y`，2 lane，1000 Mbps/lane，1024×600 |
| 帧缓冲 | `CONFIG_FB=y`、`CONFIG_VIDEO_FB=y`、`CONFIG_ESP32P4_FUNCTION_EV_LCD=y` |
| LVGL | `CONFIG_GRAPHICS_LVGL=y` + `LV_USE_NUTTX_FBDEV`（走 `/dev/fb0`）+ `LV_USE_NUTTX_TOUCHSCREEN` |
| 色深 | `CONFIG_LV_COLOR_DEPTH=16` |
| 触摸 | `CONFIG_INPUT=y`、`CONFIG_INPUT_GT9XX=y`、I2C0（SCL=8/SDA=7） |

DSI 时序基准：HPW=10 HBP=160 HFP=160 / VPW=1 VBP=23 VFP=12，DPI CLK≈52MHz（`pins.md`）。EK79007 DSI 时序的真机微调属遗留验证项（PR #347 残余清单），实测为准。

## Step 3：fps 纪律

- 采样须在系统空闲期，外设 / 网络活动期的样本作废。
- 对照基线前先确认未踩上述结构性问题（旋转封顶、禁止叠加组合）。
- 实时取舍原则：**丢帧优于撕裂**。

## 注意事项

- 动显示驱动配置前，**必读**驱动 Kconfig + 源码注释并与实物核对——移植期实证 Kconfig 接线描述可与实物不符（触摸 INT/RESET 描述 vs 实物 NC，#347）。
- MIPI-DSI 数据线为专用物理引脚，无 GPIO 矩阵映射（`pins.md`），不存在「换引脚」选项。
- 任何显示问题的最终判定以实体机目测为准，串口无报错不构成通过证据。
