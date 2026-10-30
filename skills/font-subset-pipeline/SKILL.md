---
name: font-subset-pipeline
description: LVGL 中文字库子集生成管线与排障——Noto Sans SC 可变字体静态化、lv_font_conv 子集化、界面字符自动提取、图标字体分通道、豆腐块/乱码证据链排查、字体许可红线。当新增或修改界面文案、更换字体、出现豆腐块乱码、需要重生成中文字库时使用。
---

# 字体子集管线（Noto Sans SC + lv_font_conv × openvela LVGL）

## 使用时机

- 新增 / 修改任何界面文案——**改文案必重跑子集**，否则板上豆腐块
- 中文显示豆腐块 / 乱码，要定位是字体数据、运行时查找还是子集缺字
- 更换字体源或新增字号档位

## Step 1：资产与产物

| 项 | 说明 |
|----|------|
| 字体源 | Noto Sans SC（OFL 1.1，明确允许子集化嵌入）：`NotoSansSC-Regular.ttf` 静态版 + `NotoSansSC_VF.ttf` 可变字体 |
| 图标字体源 | Material Symbols Outlined `.ttf` + `.codepoints` 码点表 |
| 产物（入库） | `ui_font_noto_<size>.c`、`ui_font_mat_<size>.c` 等生成 C 源文件，**直接纳入应用源列表随固件编译**（nuttx-apps PR #123 的 SignBridge UI 中文字库即此形态） |
| 集成点 | 应用侧 CMakeLists.txt / Makefile 登记源文件；生成文件默认 `#include "lvgl/lvgl.h"`，按应用包含路径适配编译定义（如 `LV_LVGL_H_INCLUDE_SIMPLE`） |

## Step 2：生成流程

1. **提取字符集**：grep 应用全部源码中的 CJK 字符，与 ASCII 可打印区、中文标点（含「」等全角符号）合并——**漏字即豆腐块**，字符集提取必须覆盖所有界面源码文件。
2. **可变字体静态化**（Noto VF → 静态 Regular）：

   ```bash
   fonttools varLib.instancer NotoSansSC_VF.ttf wght=400 -o NotoSansSC-Regular.ttf
   ```

3. **lv_font_conv 子集化**（工程实证参数：v1.5.3、bpp 4、无压缩；完整参数以 `npx lv_font_conv --help` 为准）：

   ```bash
   npx lv_font_conv@1.5.3 --font NotoSansSC-Regular.ttf --size 24 --bpp 4 \
     --no-compress --format lvgl --symbols "<提取的字符集>" -o ui_font_noto_24.c
   ```

4. 生成后：登记源列表 → 重新构建（`./build.sh ... --cmake`）→ 板上目测。

## Step 3：豆腐块 / 乱码排查（证据链，按序）

| 步骤 | 手段 | 结论指向 |
|------|------|----------|
| ① 字体数据 | 脚本解析生成的 `.c` 文件，把目标字 4bpp 位图逐像素渲染 | 位图错 = 生成问题；位图对 → 下一步 |
| ② 运行时查找 | 板上字形探针 `lv_font_get_glyph_dsc`（正常输出形如 `ok=1 box=19x19 adv=20`） | 查不到 = 加载 / 注册问题；查得到 → 下一步 |
| ③ 子集缺字 | ①② 都正常而屏上仍豆腐块 | 字符不在子集内 → 补字重跑管线 |

- **严禁盲开 `LV_FONT_FMT_TXT_LARGE`**：先核最大 `bitmap_index` 是否超出非 LARGE 位域上限 2^20（`lv_font_fmt_txt.h:29-37`，LVGL 9.x 源码）。
- LVGL 9.x 探针两个坑（以实际集成的 LVGL 版本为准）：`lv_font_get_glyph_dsc` 第 3 参传**当前字符**（非 NULL）；`lv_font_t` 无 `glyph_cnt` 成员。
- 方法论：乱码类问题**先证数据、再证检索、最后才怀疑配置**。

## Step 4：字体选型与许可红线

| 字体 | 许可 / 工具结论 | 判定 |
|------|-----------------|------|
| Noto Sans SC | OFL 1.1，明确允许子集化嵌入 | ✅ 现行 |
| 等线 Deng.ttf | 微软版权字体，嵌入固件分发不合规 | ❌ |
| MiSans | 协议条款「禁改编」 | ❌ |
| 阿里巴巴普惠体 | 协议条款「禁转换 / 拆分」（子集化即拆分） | ❌ |
| msyh.ttc | lv_font_conv 不支持 `.ttc` 集合 | ❌ 工具不收 |

SignBridge 的中文词库与字库分发沿用同一条合规红线：**只有许可明确允许子集化嵌入的字体才进固件**。

## 注意事项

- 图标必须用**图标字体通道**（Material Symbols 码点 + 对应图标字体渲染）；**严禁拿拉丁文本字体渲染 Material 码点**——这是 Dock 类图标豆腐块的典型根因，图标与文本必须分通道。
- 换字号 / 换字体后核对行高差（如 Noto 24px 行高与替代字体的行高不同），相关控件 y 偏移需定向修正。
- 运行期用户自输汉字不在静态子集内：要么扩子集，要么设计运行时加载方案，二选一须在立项时定。
