# 看不到图时怎么改 UI（本地开发循环）

**Applies to:** 本机 `hphelm`（真屏 `:0`）+ 隔离的 `Xvfb :9` · **Owner:** 本项目的专职 AI · **Last verified:** 2026-10-21

## When to use this

你要动版式/配色/曲线，但你（AI）看不到屏幕 —— 这是**常态**，不是例外。

## Preconditions

- `make` 通过；`Xvfb`、`xdotool`、`import`（ImageMagick）、`python3` 可用。
- 你有一个**新锁文件**（`--lock /tmp/xv9.lock`），否则会和真机实例抢单实例锁。

## Procedure

```sh
# ① 隔离环境里跑（绝不动真屏）
Xvfb :9 -screen 0 1920x1080x24 -ac &
DISPLAY=:9 ./hphelm-panel --state panel --lock /tmp/xv9.lock &

# ② 抓图
DISPLAY=:9 import -window "$(DISPLAY=:9 xdotool search --name hphelm-panel | tail -1)" /tmp/p.png

# ③ 三个探针，按「粗 → 细 → 数值」
python3 tools/render-ascii.py /tmp/p.png 22 22            # 版式对不对（亮度字符 + C = 彩色）
python3 tools/render-region.py /tmp/p.png 700 560 1230 780 4   # 局部长什么样
python3 tools/measure.py /tmp/p.png --runs 240 --bands     # 几何：色块游程 / 墨迹带 / 颜色 bbox

# ④ 静止性 / 空转（这次改的是主循环或渲染时必跑）
DISPLAY=:9 HPHELM_PANEL_DEBUG=1 ./hphelm-panel --state panel --lock /tmp/xv9b.lock

# ⑤ 收拾隔离实例：按 pid 杀，绝不用 pkill -x（会连真机实例一起杀）
```

## Verification

- 几何：柱宽/间距/行距/右对齐边界与设计值逐位对（`docs/design.md` 的常量表）。
- 静止：`>30% 像素变化` 的帧 = 0；仪表：2 it/s、poll 全超时、事件 0、内容 1 次/秒。
- 空态：曲线不许用「当前值预填」（伪造历史 + 顶高量程压扁真数据）。

## Rollback

隔离环境里的东西不落地 ⇒ 直接 `kill <pid>` + `pkill Xvfb` 即可，真屏不受影响。
**唯一的危险动作是误杀真机实例**（同名进程）：用 `pgrep -ax hphelm-panel` 看 `DISPLAY`/参数，按 pid 杀。
