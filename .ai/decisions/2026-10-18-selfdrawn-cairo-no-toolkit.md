# 自绘 UI（X11 + cairo + pango）且不引任何 GUI 工具包

**Status:** ✅ ACCEPTED · **Date:** 2026-10-18（实测当天定选型） · **Deciders:** 用户 · **Supersedes:** 早先「GTK4 已在盘上可优先用」的判断（被实测否掉）

## Context

面板要在工位机上 7×24 常驻，用户明确不要「卡片一个卡片」的重量感，也问过「除了 GTK 没有别的东西吗」。
选型必须在**同一台机、同一个真桌面 :0、同一份 1 Hz 工作量**下量出来，而不是凭印象。

实测 PSS（真桌面 :0）：X11+cairo+pango(C) **7.4 MiB / 2 线程** · FLTK 8.4 · conky 13.4 ·
GTK3+C 17.6 · Qt6 27.4 · GTK3+Python 37.1 · **GTK4+C 119.3 MiB / 22 线程**（有 Vega 8 GPU 时复测仍 116.6）。

## Options

| Option | Upside | Downside | Reversibility |
| --- | --- | --- | --- |
| GTK4 + C | 控件现成 | 119 MiB / 22 线程，最重；X11 只能整数倍缩放 | hard（版式全部重写） |
| GTK3 + Python | 开发快 | 37.1 MiB，工具包拖累大于语言 | easy |
| Rust + 自绘 | 内存安全 | 只比 C 省 0.4 MiB ⇒ 不值一套工具链 | easy |
| **C + X11 + cairo + pango** | 7.4 MiB、2 线程、141.8 DPI 可精确 `cairo_scale(1.48)` | 自己要写绘制基元 | — |

## Decision

自绘，**不许引 GUI 工具包**（GTK/Qt/FLTK 一个都不许进 `ldd`）。文字用 pango（与 GTK 同一套渲染栈 ⇒
中文 + Nerd Font 回退免费；实测只给拉丁字体画中文再换 CJK，仅差 112/98000 像素）。
`ldd` 里工具包数量 = 0 是**验收判据**，也是项目存在的理由。

## Consequences

- 版式、控件、动画都得自己写（`view.c` 是基元层，`theme.h` 是常量层）。
- 「为了省内存选 Rust」这个论证不成立；语言按维护性选，工具包才是数量级差异。
- 任何引入工具包的改动 = 推翻立项理由，必须带新的实测对比表才能提。

## Revisit when

屏幕/DPI/桌面栈变化导致自绘成本失控（例如需要复杂文本编辑控件），或者实测证明某个轻量工具包能 <10 MiB。
