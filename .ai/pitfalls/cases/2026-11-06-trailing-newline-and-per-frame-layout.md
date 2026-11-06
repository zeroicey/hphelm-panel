# 趋势幕「排不满」的两个连环坑：行尾 `\n` 混进描述、以及 1 Hz 重画下每帧重新排版

**Severity:** 🟠 medium · **First hit:** 2026-11-06 · **Hits since:** 0

## Symptom

GitHub 趋势幕下半屏空一大块，15 条里有 5 条上不了屏；量出来 `H_DESC=48.0`（应为 24.0）。
修好行高后，趋势幕的**单帧绘制时间**从 19.5ms 涨到 29.0ms，整机 CPU +1.5% of one core。

## Root cause

1. `scene_trending.c` 的 `load()` 先按 `\t` 切列（写 NUL），**之后**才
   `line[strcspn(line, "\r\n")] = 0` —— `strcspn` 撞上第一个 NUL 就停，行尾 `\n` 永远没去掉。
   它挂在最后一个字段（描述）上，pango 把它当**硬换行**：每条描述变成「文字 + 空行」，
   高度翻倍、空行不可见 ⇒ 屏幕上「看着挺正常，就是排不满」。
2. 面板闲时**每秒重画**（`content_sig()` 里 `FEED.age_s` 每秒变）。`draw()` 里
   15 次 `v_text_measure` + 每行仓库名二分截断（最多 8 次整形）= 每帧 100+ 次多余整形。

## Fix

- `load()`：把「去行尾 `\r\n`」提到**切列之前**。
- `scene_trending.c`：加排版计划缓存 `plan`（键 = 列宽 + 缓存文件 `mtime`/`size` + 条数），
  缓存行高、截断名、语言/涨星串及其宽度、语言分布行。绘制 29.0 → 23.1ms。

## Guard

- `LAYOUT_DEBUG=1 … --scene trending` 打印 `H_DESC`：单行描述必须是 24.0 而不是 48.0。
- `tools/drawcost.sh`（hpopi5b `/tmp/drawcost.sh` 有副本）：新二进制由自身重画、旧二进制用
  `SIGUSR2` 强制 1 次/秒重画，比较 `HPOPI5B_PANEL_DEBUG=1` 的「绘制 Xms」中位数，差 >2ms 即视为回归。
- 纪律（已写进 `docs/pitfalls.md` §23）：`draw()` 里只允许做「不缓存就没法画」的排版。
