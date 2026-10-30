# skills — 板级 bring-up 技能沉淀

把同一块板（ESP32-P4-Function-EV-Board）在 openvela 适配与 SignBridge 开发中重复验证过的操作流程，固化为 AI 可直接执行的技能文档。本目录**跨 AI 工具通用**：SKILL.md + frontmatter（`name` / `description`）为开放格式，不注册到任何工具专属目录，由本 README 路由表索引。

## 技能路由表

| 技能 | 触发场景 | 来源 |
|------|----------|------|
| [board-flash-monitor](board-flash-monitor/SKILL.md) | 构建 / 烧录 / 串口监控 / 端口排障 / 抓日志 | openvela CMake 构建链路验证期与烧录沉淀 |
| [display-lvgl-pitfalls](display-lvgl-pitfalls/SKILL.md) | 花屏 / 黑屏 / 撕裂 / fps 骤降 / 显示配置不生效 | 显示链路适配与旋转问题域定案 |
| [touch-gt911-debug](touch-gt911-debug/SKILL.md) | 触摸失效 / 不挂载 / 吞点击 / 落点异常 | gt9xx 轮询驱动（nuttx PR #364）+ LVGL 应用层 |
| [sc2336-video-preview](sc2336-video-preview/SKILL.md) | 摄像头无画面 / 花屏 / 颜色异常 / 分辨率被拒 / 预览撕裂 | CSI 采集链路（nuttx PR #347、nuttx-apps PR #123） |
| [font-subset-pipeline](font-subset-pipeline/SKILL.md) | 改文案 / 换字体 / 豆腐块乱码 | 中文字库子集管线（nuttx-apps PR #123） |
| [hardware-docs-lookup](hardware-docs-lookup/SKILL.md) | 引脚 / 连接器 / 排线 / 装配 / DSI 时序参数定位 | `board/esp32p4_function_ev/` 与官方资料交叉核对 |
| [milestone-bringup-plan](milestone-bringup-plan/SKILL.md) | 新里程碑 / 大功能立项 / 修复路线分叉 / 上游 PR | 板级移植全流程纪律（16 条移植期踩坑复盘） |

## 内容来源与真实性约定

- 全部条目来自本队在同一块板上的实测与移植过程，按「现象 → 根因 → 处置 → 出处」归档；出处为 PR / commit（nuttx #347、#364，nuttx-apps #123 及本仓提交）、板级文档（`pins.md` / `defconfig.inc`）、上游 issue 或官方文档，**严禁无出处推断**。
- 板级硬件层结论（GT911 总线行为、CSI 模式白名单、杜邦线接法等）与固件栈无关，但跨环境复用前仍应按目标环境复核。
- 整机联调尚未收尾的链路（如 CSI 整机取流复验），技能内均**如实标注验证状态**，不得当作已验收结论引用。

## 沉淀判定标准（满足其一才建新技能，严禁预建空壳）

| 满足其一 | 示例 |
|----------|------|
| 同一多步流程重复执行 ≥3 次 | 每次都要重查的构建 / 烧录步骤 |
| 踩坑后总结出可复用的稳定做法 | GT911 断电恢复纪律、defconfig 关闭项显式写法 |

## SKILL.md 骨架（严格执行）

```markdown
---
name: <kebab-case-name>
description: <一句话能力边界>。当用户提到<触发词>时使用。
---

# <技能标题>

## 使用时机
- ...

## Step N：<流程>
| 情形 | 动作 / 出处 |
|------|------------|

## 注意事项
- 严禁 / 必须类硬规则
```

frontmatter 的 `description` 决定各 AI 工具的触发与索引质量，必须内嵌触发条件。细节多时下沉 `reference/` 子目录，主文件只留路由表。
