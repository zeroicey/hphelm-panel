# 「`probe-frames` >30% 帧必须 0」在 kiosk 双幕轮换下必然假报警

**Severity:** 🟠 medium（会把「正常换幕」判成「闪」，也可能让人为了迁就判据去改代码）· **First hit:** 2026-11-05 · **Hits since:** 1

## Symptom

部署完新二进制跑验收：

```
$ DISPLAY=:0 tools/probe-frames 40 20
---- frames with >30% pixels changed: 2        ← 判据要求 0
```

同一条命令连跑三次得到 `3` / `0` / `2` —— 同一份代码、同一块屏，数不稳定。

## Root cause

`hpopi5b` 的现役 kiosk **每 30 秒换一次幕**（`src/scenes.c` 的 `rotate_s` 默认 30，
`SCENES[] = {&SCENE_PANEL, &SCENE_TRENDING}`），换幕是整屏淡出淡入 ⇒ 那 2 帧**必然 100% 像素变化**，
bbox 就是整屏 `(0,0)-(1919,1079)`，且 `dt` 比标称 0.157 长（实测 0.195~0.200）。
`probe-frames` 的判据「>30% 帧必须 0」是为**单幕内双缓冲失效（闪）**设计的，
从没把换幕排除在外 ⇒ 探针数到的是换幕，不是闪。

判据本身没错，错的是**判据前提没写下来**。

## Fix

不改代码，改判据口径（三处同步）：

- `docs/verify.md` §二 判据下加 ⚠️ 段：kiosk 30s 换幕的 2 帧不算闪；要干净数就用
  `DISPLAY=:9 ./hpopi5b-panel --state panel --rotate 0 --lock /tmp/xv9.lock` 的隔离实例；
  并记「同一条命令连跑三次得到 `2 / 0 / 3`」。
- `docs/deploy.md:36`（三十秒版）与 `docs/deploy.md:218`（九条判据表第 6 条）加括注。
- `AGENTS.md:115` 同款括注。

## Guard

- 看到 `>30% 变化帧 = N (N≤3)` 时，先按 `docs/verify.md` 的**三条「闪」指纹**判：① bbox 横跨整屏；
  ② 连续两帧都变；③ 中间帧 PNG 明显更小。**换幕只满足 ①**（单调的全屏切换，中间帧是完整的新幕）。
- 要可复现的干净数字：隔离实例加 `--rotate 0`。
- 换幕周期是 `rotate_s`（默认 30s），窗口取 `40s` 时几乎必然跨越一次换幕 —— 想避开就取 < 30s，
  或接受并逐帧解释。