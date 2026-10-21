# Architecture

> Single file by design. Detail lives in `docs/architecture.md`（状态机/双缓冲/采样/认证/数据源）——
> 那份是权威；本文件只回答「东西在哪、什么不许坏」。

## 1. Purpose

`hphelm`（GMKtec G10 工位机）的**运维看板 + 自研锁屏**，一个进程同时扮演两者：
空闲显示资源面板，动键鼠切成自绘登录页，PAM 认证通过后让位给桌面，再空闲又覆盖回来。
**它替代了 lightdm 的 greeter** ⇒ 它是用户直接可见的组件（挂了就是「看不到登录页」）。
它**不是**产品、不做通用化：README 的「这不是给谁用的」适配前提表就是设计边界。

## 2. Stack

| Layer | Technology | Version | Why this one |
| --- | --- | --- | --- |
| 语言 | C（+ pango 做文字整形） | gcc，`-O2 -Wall -Wextra` | 实测：GTK4 119 MiB vs 自绘 7.4 MiB；Rust 只省 0.4 MiB，不值一套工具链 |
| 绘图 | X11 + cairo + pango，**无 GUI 工具包** | `x11 x11-xcb xscrnsaver cairo cairo-xlib pangocairo` | 「不引工具包」是立项理由；cairo+pango 就是 GTK 底下那同一套 |
| 认证 | setuid 助手 `hphelm-auth`（~120 行）+ 专用 PAM 服务 | `pam_unix` only | 特权面最小；复用 `login` 栈会带 `pam_faillock` 连坐 autologin |
| 数据 | 只读 `/proc`、`/sys`（不 fork、不起子进程） | — | 1 Hz 采样、2 线程、Private 内存 ~5 MiB |
| 运行 | lightdm(autologin) → `/usr/local/bin/hphelm-session` → 本程序 | — | 会话入口负责 `xset`、日志轮转、守护循环、兜底 |

## 3. Repository map

| Path | Responsibility | Touches |
| --- | --- | --- |
| `src/theme.h` | 配色 + 版式常量（逻辑像素）+ 字号 | ⭐ 最常动 |
| `src/view_panel.c` / `src/view_login.c` | 两个界面的版面 | ⭐⭐ / ⭐ |
| `src/view.c` | 绘制基元（文字/渐变/细线/条/柱/sparkline/量程） | 偶尔 |
| `src/metrics.c` | `/proc` `/sys` 采集，按时间节流 | 加指标时 |
| `src/panel.c` | X11 窗口、事件、状态机、主循环、双缓冲、仪表 | 改交互时 |
| `src/auth.c` + `src/hphelm-auth.c` | 认证通道与 setuid 助手 | 一般不动 |
| `tools/deploy.sh` | **原地部署 + 判据**（构建 → 备份 → 安装 → 重启 → 判据） | 每次上真屏 |
| `tools/{measure.py,render-ascii.py,render-region.py,probe-frames}` | 看不到图时的像素判据 | 改 UI 时 |
| `docs/` | 架构/设计/验证/部署/坑（权威细节） | 每轮同步 |

## 4. Core topology

```text
lightdm autologin → /usr/local/bin/hphelm-session
      ├─ xset s off / xset dpms 0 0 0        （DPMS 交给面板自己管）
      ├─ 日志轮转（>1MiB ⇒ tail -c 256K）
      └─ while :; do hphelm-panel --state panel; … done   （失败判据 = 运行时长 <5s）

hphelm-panel 主循环（poll 超时驱动，1 Hz 采样）
      PANEL ──键盘/鼠标──▶ LOGIN ──PAM 通过──▶ 桌面（取消映射、放键盘）
        ▲                                            │
        └────────── 空闲 180s ◀───────────────────────┘
```

四条关键流：

1. **绘制**：所有绘制进离屏 `Pixmap`，每帧**一次** `XCopyArea` 上屏；`content_sig()` 无变化不重绘。
2. **采样**：1 Hz 按**时间**节流（不是每轮循环一次）；速率用单调时钟；IP/进程榜另按 10s/3s 刷。
3. **认证**：面板通过 stdio 把密码交给 setuid 助手，助手只用 `pam_unix` 验，回一个结果；密码不落盘、不进 argv。
4. **部署**：本机 `tools/deploy.sh`（源码目录 = 部署目录 `/data/dev/hphelm-panel`）。

## 5. Invariants

1. `ldd /usr/local/bin/hphelm-panel` 里 GUI 工具包数量 = **0**（gtk/qt/fltk 一个都不许出现）。
2. 安装后「源码二进制 ⇄ `/usr/local/bin/hphelm-panel`」**逐字节一致**；助手权限 `4755 root:root`。
3. `/etc/pam.d/hphelm-auth` 只含 `pam_unix` 的 `auth`+`account` 两行（不得 `@include login`）。
4. 会话入口与守护循环里**没有任何** `dm-tool` / `light-locker` 调用；兜底只允许 `xset dpms force off`。
5. 守护循环的失败判据是**运行时长 <5 秒**，不是退出码（SIGTERM = 143）。
6. 现场稳态：`tty7` · `slick-greeter=0` · `hphelm-panel=1` · `sleep_off=off` · 帧差分 0 帧 >30%。
7. 预算：CPU < 1% of one core（现状 1.1~1.6%，**待查**）· 线程 2 · Private 内存 ~5 MiB · 采样 1 Hz。
8. 版式坐标只写在 `src/theme.h` 的 `L_*` 常量里（逻辑像素，主循环统一 `cairo_scale`）。

## 6. Boundaries and known debts

- **单机方案**：硬编码假设 1920×1080 / `SCALE=1.48` / lightdm+bspwm+picom / 专用 PAM 服务。多显示器未处理。
- **CPU 预算有偏差**：0.60%（2026-10-18 记录）vs 1.13–1.60%（2026-10-21 实测），机理未定 —— 见 `.ai/CURRENT_TASK.md`。
- **推送通路缺口**：本机 GitHub 身份只能推 `dotfiles`，所以提交停在本地；加 write deploy key 之前，
  这个仓库的远端不是「提交即上线」。
- **不回退到 light-locker**：那条路会重新引入「两把锁 ⇒ 登录两遍」（`docs/pitfalls.md` #15）。
