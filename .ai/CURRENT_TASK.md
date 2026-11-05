# Current task

<!-- The single live surface. Keep it under ~60 lines: it is read on every session start. -->

**Status:** 🟢 第十二轮已真机验收并部署（第 4 格曲线温度 → 磁盘 I/O）· **Updated:** 2026-11-05

## Goal

把面板从「一个写死的版面」改成**可插拔的多幕（scene）**，目前**只保留两幕**：`panel`（运维面板）+
`trending`（GitHub 趋势，两列 12 条），kiosk 下默认 30 s 轮换 + 四种转场。桌面/锁屏模式默认不轮换。
两幕已在真机跑通（两幕轮换、0 闪帧、trending 两列实拍、HUD 两点）；本轮改的是第 4 格的曲线来源。

## Checklist

- [x] **第十二轮真机验收**（hpopi5b `make panel` 0 warning + 安装 + 重启 + 取证，数字见 Code state）
- [ ] 定 `--rotate` 最终值（候选 25/30/45）并写进 `/etc/hpopi5b-panel.conf`
- [ ] `tools/deploy.sh` 支持 hpopi5b 落点（现在仍指向 hphelm 的 `/usr/local/bin`）
- [ ] CPU 预算收口（见 Blockers，二选一）
- [ ] 容器**健康**（重启循环/备份/证书）还没做，现在只有「容器 N 个」

## Code state

`main` · **第十二轮（磁盘 I/O 曲线）已提交、部署到 hpopi5b**（`addaa03`）。
现场（2026-11-05 16:00）：`hpopi5b-kiosk` active · `pgrep -cx hpopi5b-panel` = 1 · `tty1` active ·
两幕 30s 轮换 · 日志「已连上 hpcmd 4 核 / hpcore 16 核」· X Error 0。
本轮判据：`make panel` **0 warning**；截图取证第 4 格满宽双曲线（peach 写横跨 x 26..659）+ 磁盘行
teal「读」/peach「写」+ 脚注「盘 读/写」；**CPU A/B（Xvfb :9，各 25s，交替两轮）旧 5.12/5.08% → 新 5.52/5.40%
⇒ +0.35 pp**；RSS 17940 → 18008 kB（噪声内）。
注意：`probe-frames` 在 kiosk 下会数到换幕的 2 帧 100%（不是闪）——
见 `.ai/pitfalls/cases/2026-11-05-probe-frames-vs-scene-rotation.md`，干净数用 `--rotate 0`。
第七轮到第十二轮的逐轮过程记录在 `.ai/sessions/2026-11-05-rounds-7-to-12-handoff.md`。

## Constraints

边界，不是阻塞项：决定改法，但没人在等它们。

- **CPU 预算 <1% of one core**，现 1.0%（1 台）/ 1.4%（2 台）。内容签名已做到「没变就不画」，
  但时钟每秒变 ⇒ 每秒必画一帧；再降只有「降频重绘」或「区域局部重绘」两条路，其余是白费。
- **I/O 双曲线共享量程**：「写 ≪ 读」时写曲线贴基线（与网络 ↓/↑ 同规矩）。要单独量程就会失去同量纲对比。
- **hpcmd 编不了 `panel.c`**（KVM guest：`/sys/class/hwmon` 空、无 make、缺 scrnsaver.h）⇒
  编译与验收都在 hpopi5b 上做，hpcmd 只做源码编辑与 ssh 远端探针。
- **测试实例与真机实例同名** ⇒ 杀进程按 `/proc/<pid>/environ` 的 `DISPLAY` 挑 pid，永不 `pkill -f`。

## Blockers

- CPU 1.0~1.4% 略超预算 — waiting on owner 二选一：接受并改文档预算，或做区域局部重绘
  （动手前必须在**空闲**机器上量基线，先读
  `.ai/pitfalls/cases/2026-10-21-benchmarking-cpu-under-foreign-load.md`）

## Pitfall reminders for the current branch

- #15 绝不 `dm-tool` / `light-locker`（会把 lightdm greeter 招回来并抢走 VT）
- #16 `sleep_off=0` = 永不关屏；裸比较会让 0 变成「立即关屏」
- #17 **量 CPU 必须同时报 load average 与窗口长度**，优化前后要同窗口交替对比
- #18 加宽/加列后必须重跑 `--bands` 量竖带起点；`-1` 不要兼职两种语义；UTF-8 截断别切半
- #19 外部脚本读 `--dump` 一律按前缀 grep，不要依赖行序（`tail -1` 已经坑过一次）

## Layout invariants（改版面时必看）

- 主机列纵向是**流式**的：`L_H_*` 是各行高/间距，`fixed` 表必须与绘制顺序逐段对应
- 判据：`LAYOUT_DEBUG=1 ./hpopi5b-panel --state panel 2>&1 | grep HOST` ⇒ `余=0.0`
- 圆点用 `v_text_right_ex()` 返回的墨迹中线画，**不要用常数偏移**

## Next action

1. 把「接受 1.4~1.5% 并改预算」与「区域局部重绘」两个选项连同本轮 A/B 数字（+0.35 pp）交给 owner 二选一。
2. 定 `--rotate` 最终值；`tools/deploy.sh` 改成支持 hpopi5b 落点（现在仍指向 hphelm 的 `/usr/local/bin`）。
