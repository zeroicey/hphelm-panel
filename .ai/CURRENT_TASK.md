# Current task

<!-- The single live surface. Keep it under ~60 lines: it is read on every session start. -->

**Status:** 🟡 active · **Updated:** 2026-10-21（第七轮）

## Goal

面板同屏显示**本机 + 生产服务器（hpcore）**，版面为 `本机 │ 生产 │ 进程榜` 三列。
第七轮已完成并上真机；下一轮待定（见 Checklist 未勾项）。

## Checklist

- [x] `src/remote.[ch]`：ssh 长连接 + 非阻塞行解析 + 半帧不提交 + 退避重连
- [x] `tools/probe-remote.pl`：远端只读探针（从 stdin 喂入，**不在 hpcore 装任何东西**）
- [x] 三列版式（`theme.h` 绝对逻辑像素 + `draw_host()` 画两台 + 进程榜一列两段）
- [x] `/etc/hphelm-panel.conf` 的 `remote=`（改主机名不用编辑 root 的会话包装器）
- [x] `tools/deploy.sh` 安装并校验探针 + `HPHELM_REMOTE=` 时顺带验链路
- [x] 性能：右对齐复用排版、背景渐变只渲染一次、等宽字体按字符宽截断
- [x] 真机判据全绿（tty7 / greeter=0 / panel=1 / 帧差分 0 / 日志无兜底）
- [x] 版面精细化（圆点对齐墨迹中线 / 每块一行 / 曲线均分剩余高度 / 新增 7 项指标）
- [x] 修复「远程采集连上但没数据 ⇒ 永久离线」（真机踩到，见 #20/#21 坑档）
- [x] 远程采集加**状态变迁日志**（拉起/已连上/EOF/退出/超时重连）
- [ ] **CPU 仍略超预算**：只本机 ~1.0%、含生产 ~1.4%（预算 <1%）— 见 Blockers
- [ ] 容器**健康**（重启循环/备份/证书）还没做，现在只有「容器 N 个」
- [ ] GitHub 推送通路（用户已裁决「先不管」，提交留本地）

## Code state

`main` · 第七轮改动已提交。已装二进制与源码逐字节一致。
现场：`tty7` · `slick-greeter=0` · `hphelm-panel=1` · `sleep_off=off` · 帧差分 `0` 帧 >30% ·
面板子进程里有 1 个 `ssh … hpcore perl -`。

## Blockers

- **CPU 1.0%（1 台）/ 1.4%（2 台）of one core，预算 <1%**。
  已定位到「单帧绘制 5~10ms × 1 Hz」，并修掉三个大头（pango 整形次数、整屏渐变、逐字符测量）。
  要进一步降只有两条路：**降低重绘频率**（内容签名已经是「没变就不画」，但时钟每秒变 ⇒ 每秒必画一次）
  或**区域局部重绘**（只重画变化的区块）。⚠️ 再优化前先读
  `.ai/pitfalls/cases/2026-10-21-benchmarking-cpu-under-foreign-load.md`：
  上一次量 CPU 时机器 load 23，数字全部作废。
- `git push origin main` 在本机失败（hphelm 的 GitHub 身份只够推 `dotfiles`）—
  用户 2026-10-21 裁决「先不管，提交留本地」⇒ 别为此改动仓库配置。

## Pitfall reminders for the current branch

- #15 绝不 `dm-tool` / `light-locker`（会把 lightdm greeter 招回来并抢走 VT）
- #16 `sleep_off=0` = 永不关屏；裸比较会让 0 变成「立即关屏」
- #17 **量 CPU 必须同时报 load average 与窗口长度**，优化前后要同窗口交替对比
- #18 加宽/加列后必须重跑 `--bands` 量竖带起点；`-1` 不要兼职两种语义；UTF-8 截断别切半
- #19 外部脚本读 `--dump` 一律按前缀 grep，不要依赖行序（`tail -1` 已经坑过一次）
- 测试实例与真机实例同名 ⇒ **杀进程按 `/proc/<pid>/environ` 里的 `DISPLAY` 挑 pid**，永不 `pkill -f`

## Layout invariants（改版面时必看）

- 主机列纵向是**流式**的：`L_H_*` 是各行高/间距，`fixed` 表必须与绘制顺序逐段对应
- 判据：`LAYOUT_DEBUG=1 ./hphelm-panel --state panel 2>&1 | grep HOST` ⇒ `余=0.0`
- 圆点用 `v_text_right_ex()` 返回的墨迹中线画，**不要用常数偏移**

## Next action

先决定 CPU 这一项怎么收：**（a）接受 1.0~1.4% 并改文档预算**（面板已经比 conky 便宜得多），
或**（b）做区域局部重绘**（只重画时钟/数字所在的行，预计能砍掉一半）。
动手前先在**空闲的机器上**（`uptime` 的 load < nproc/2）量一次基线，把数字记进 HANDOFF。
