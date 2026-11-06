# Current task

<!-- The single live surface. Keep it under ~60 lines: it is read on every session start. -->

**Status:** 🟢 第十五轮已真机验收并部署（容器块 → Tailscale 尾网块），待提交 · **Updated:** 2026-11-06 傍晚

## Goal

`hpcmd` 下线、全场 all-in-one 到 `hpcore`，面板**只画一台机**：主机区左右两个子列（各 2 张曲线，
1080p 下曲线高 135 逻辑像素）＋ 下方** Tailscale 尾网块**（两个子列各 5 台）＋ **一整列**进程榜
（按 CPU%，25 行，同值比 RSS ⇒ 空载时自动退化成内存榜）。
两幕（`panel` / `trending`）仍在，kiosk 默认 30s 轮换；桌面/锁屏模式不轮换。

## Checklist

- [x] **第十三轮真机验收 + 部署**（0 warning、装二进制 + 探针、`systemctl restart`、取证数字见 Code state）
- [x] 提交 `17b8a4e` 并推送 `origin/main`
- [x] **第十四轮**：一条进程榜（25 行）+ 容器块 + 曲线 260→150 + 主机区 0.56→0.66；探针新增 `D` 行；提交 `001cc1e`
- [x] **第十五轮**：容器块整块**删除**，同位置换成 **Tailscale 尾网块**（探针读 tailscaled localapi，
      `T` 行 10 台；`D`/`docker.sock`/`ctr_*`/`draw_ctr_block`/`L_CTR_*` 全删）；真机验收并部署
- [ ] 第十五轮提交（文档已同步：README / design / architecture / AGENTS / HANDOFF / ADR）
- [ ] `--rotate` 最终值仍没定（候选 25/30/45）
- [ ] `tools/deploy.sh` 支持 hpopi5b 落点（现在仍指向 hphelm 的 `/usr/local/bin`）
- [ ] CPU 预算收口（见 Blockers —— 数字口径已经变了，要先重定「预算怎么量」）
- [ ] 尾网块上限 10 台（`L_TS_ROWS 5 × 2`）；超过 10 台时标题的「N 台」会大于画出来的行数（现在 9 台，不冲突）
- [ ] 尾网没画出口节点（`ExitNodeStatus`）、没画 DERP 延迟（`netcheck` 很贵）、没画本机自身流量
- [ ] `tools/render-ascii.py` / `measure.py` 依赖 ImageMagick，hpopi5b 上没有 ⇒ 整屏 ASCII 与 `--bands` 仍不可用
      （已补 `tools/xshot.c` 截图 + `tools/pngcrop.py` 裁切/单行游程，够量分隔线与局部；整屏视图仍缺）

## Code state

`main` @ `001cc1e` + 第十五轮未提交改动 · 第十五轮已部署到 hpopi5b（2026-11-06 17:1x）。
现场：`hpopi5b-kiosk` active · `pgrep -cx hpopi5b-panel` = 1 · **tty1** active ·
slick-greeter 0 · 无 X 错误 · 二进制与探针都与 `/tmp/panelb` 构建产物 sha256 一致。
判据：`probe-frames 60` ⇒ **>30% 变化帧 0**；`LAYOUT_DEBUG=1` ⇒ `wa=399.2 ch=135.4 FIX_A=187 FIX_B=187 余=0.0`；
单榜 25 行、尾网块 9 台（7 在线 / 2 离线、`直连 4 · 中继 2`）、`bottom = 712`；CPU 90s ⇒ **3.43%**、`VmRSS 20.4 MB`。
**CPU A/B（真屏 :0，`performance` 模式 1.8 GHz，各 60s）**：旧三列 **3.47%** → 新单机 **3.68%**。
⚠️ 同一份二进制在 `ondemand` 空载（408 MHz）下量到 **5.8%** —— 频率让 CPU-秒差 5 倍，**只做 A/B 比**。
第七轮到第十二轮的逐轮记录在 `.ai/sessions/2026-11-05-rounds-7-to-12-handoff.md`。

## Constraints

边界，不是阻塞项：决定改法，但没人在等它们。

- **CPU 预算 `<1% of one core`**：这个数在 hpopi5b 上量不出来（见 Blockers），当作「不许变慢」的相对门槛。
  再降的正解：PangoLayout 缓存（每秒 ~50 段文字）+ 「值没变就不重画那一行」的区域局部重绘。
- **两列固定高度必须相等**（`FIX_A == FIX_B == 187`）：「余」为负时禁止再压曲线（会挤字）。
- **曲线量程由 `v_chart_scale()` 统一算**，`v_spark/v_spark2` 必须显式传它返回的 vmax（网格与曲线不能各算一套）。
- **探针与面板同版本**：`P`/`Q` 4 段 + `T` 行；老探针仍兼容（CPU 列画 `-`，不冒充 0.0%），
  没有 `T` 行时尾网块整块消失且 `ts_h = 0`（曲线自己长回去），不留空洞。
- **尾网数据只走 tailscaled 的 localapi**（`/var/run/tailscale/tailscaled.sock`，权限 0666 ⇒ 不要 root），
  **不 fork `tailscale status`**（起进程 + 几百毫秒，1 Hz 探针扛不住）。
  ⚠️ JSON 里对端列表的键是 **`Peer`（单数）**，写 `Peers` 会静默得到 0 台。
- 容器只剩「个数」（cgroup 目录计数，不碰 socket）；**不要再做容器清单**（用户已否：容器里的进程本来就在进程榜里）。
- **测试实例与真机实例同名** ⇒ 杀进程按 `/proc/<pid>/environ` 的 `DISPLAY` 挑 pid，永不 `pkill -f`。
- 换二进制先 `systemctl stop`，否则 `install`/`cp` 撞 `Text file busy`（本轮踩过）。

## Blockers

- CPU 数字口径 — waiting on owner：`ondemand` 408 MHz 空载下 3.5~5.8% 到底算不算超预算？
  选项：① 改文档预算为「A/B 不变慢」；② 固定 `performance` 模式（费电、Pi 烫）；
  ③ 真做 PangoLayout 缓存 + 区域局部重绘再谈绝对值。

## Pitfall reminders for the current branch

- #15 绝不 `dm-tool` / `light-locker`（会把 lightdm greeter 招回来并抢走 VT）
- #16 `sleep_off=0` = 永不关屏；裸比较会让 0 变成「立即关屏」
- #17 **量 CPU 必须同时报 load average、窗口长度**，并且**同一频率下 A/B**（`ondemand` 会让同一份代码差 5 倍，
  见 `.ai/pitfalls/cases/2026-11-06-cpu-measure-freq-confound.md`）
- #20 **量性能前先 `ps -eo pcpu,pid,comm --sort=-pcpu | head`**：本轮发现 5 个孤儿 `/tmp/render_tr`
  各烧 100% CPU 已 3 天（load 5.2），任何 CPU 测量都会被它带偏
- #18 加宽/加列后必须重跑 `--bands` 量竖带起点（当前 ImageMagick 缺失，这条暂时只能靠代码算）
- #19 外部脚本读 `--dump` 一律按前缀 grep，不要依赖行序

## Layout invariants（改版面时必看）

- 主机区两子列**各自流式**：`L_H_*` 是各行高/间距，`fixed` 表必须与绘制顺序逐段对应
- 判据：`LAYOUT_DEBUG=1` ⇒ `FIX_A=187 FIX_B=187 余=0.0`、`ch ≤ L_H_CHART_MAX(150)`
- 纵向顺序：主机块（`bottom = 712 - ts_h`）→ 主机块自己的说明行（`L_H_CAP_H 16`）→ 尾网块
  （`ts_h = 18 + 24 + 5×22 + 16 = 168`）；没有尾网数据时 `ts_h = 0`
- 尾网行内列位从右往左算（速率 100 → 连接方式 62 → 系统 52 → 名字吃剩下的）；「系统」列必须**右对齐**，
  `windows` 比槽位宽，左对齐会压到「中继 hpnet」上（第一版就这么叠了）
- **判版面不要看缩放截图**：先 `LAYOUT_DEBUG` 算，再 `tools/pngcrop.py` 数像素（见 pitfall 2026-11-06）
- 圆点用 `v_text_right_ex()` 返回的墨迹中线画，**不要用常数偏移**
- 右对齐数值（load / 缓存 / 丢错 / 量程文字）一律先用 `v_text_w` 量宽再决定左边能放什么，禁止硬叠

## Next action

1. 提交第十五轮（含文档）。把 Blockers 的「预算口径三选一」交给 owner。
2. 定 `--rotate`；`tools/deploy.sh` 改成支持 hpopi5b 落点。
3. 若 owner 要真降 CPU：先加 PangoLayout 缓存（`v_text` 里按 (字符串, 字号, 颜色) 缓存 layout），再量 A/B。
