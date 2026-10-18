# CLAUDE.md

@AGENTS.md

# Claude-only notes (only what differs from the shared file)

- 技能唯一源在 `.agents/skills/`：`/build-stm32`、`/flash-stm32`、`/reset-stm32`、`/serial-debug`。
- Claude Code 只读 `.claude/skills/`；首次 clone 后运行 `python tools/sync_skills.py` 生成本地链接，该目录不入库。
