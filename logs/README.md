# logs/ — AI Coding 日志目录

本目录存放本项目开发过程中与 AI 工具的对话日志，与作品代码一并提交，
由组委会提供的日志归集工具导出。

## 当前收录

| 会话 | 工具 | 日期 | 事件数 | 说明 |
|---|---|---|---|---|
| qoder-signbridge-20260821 | opencode | 2026-08-21 | 124 | SignBridge 应用开发会话（backfill-sqlite 导出） |

- 归集账号：`IsXiaoXiaoZhou`（manifest.json 中 github_login）
- 会话日志持续归集中，后续日期的导出文件按同一目录约定追加。

## 目录结构

```text
logs/
├── README.md                  # 本说明
└── IsXiaoXiaoZhou/            # GitHub 用户名，一人一目录
    ├── manifest.json          # 会话清单
    └── 2026-08-21/            # 日期 YYYY-MM-DD
        └── opencode__qoder-signbridge-20260821.jsonl
```

- `<tool>`：`claude-code` / `opencode` / `codex` / `kiro`
- 每个 `.jsonl` 每行一个事件，**只提交 JSONL 本身**，不提交导出工具的二进制缓存。
- 清单字段（session_id / tool / 日期 / event_count / file_path）见 `IsXiaoXiaoZhou/manifest.json`。

导出与提交的完整步骤、字段定义见[《AI Coding 日志归集与提交手册》](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/ai_coding_log_guide.md)。
