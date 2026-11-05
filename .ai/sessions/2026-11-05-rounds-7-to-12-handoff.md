# 第七轮 → 第十二轮轮次记录（原文归档）

2026-11-05 从 `.ai/CURRENT_TASK.md` 原样搬出：这些是「某一轮怎么做的、当时验了什么」的过程记录。
当前状态见 `.ai/CURRENT_TASK.md`。逐字保留。

**搬走时已过时的一条**（留在 `.ai/CURRENT_TASK.md` 会误导）：原文 Blockers 写「`git push origin main`
在本机失败」，但第十轮已在 hpcmd 上恢复 GitHub 推送通路并 push 成功——那条已删，不再列在这里。

## 第十二轮：第 4 格曲线 = 磁盘 I/O（2026-11-05）

- 用户报障（m00001）：hpcmd 列「磁盘上方那条折线」不显示，hpcore 有 —— 那不是磁盘曲线，是**温度**；
  两处读取端都只认 `hwmon name == k10temp`，hpcmd 是 KVM guest（`/sys/class/hwmon` 空）、Pi 是 `soc_thermal`
  ⇒ 三列里只有 hpcore 画得出。用户裁决「可以」（m00071）。
- 已改完（源码）：第 4 格 → 磁盘 I/O 双曲线（teal 读 / peach 写，共享量程）；磁盘行右侧显示当前速率，
  盘累计 GB 挪脚注；`read_disk()` 改扫 `/sys/block` 所有整盘（跳 loop/ram/zram/nbd/sr/fd/dm-/mtdblock）；
  探针尾部追加 `diskReadBps diskWriteBps`；`remote.c` 补全整个追加段解析（此前缓存/PSI/线程/累计/丢错**恒为 0**）；
  字段 `have_ssd/ssd_*` → `have_disk/disk_*`。
- 已验：`-Wall -Wextra` 零告警；离屏 PPM 量测 4 格/列、`ch=87.4`、余 0.0、I/O 格两色都在（日/夜主题）、
  磁盘行与脚注无重叠；`probe-remote.pl` 实跑 33 字段。
- **未验（下一步）**：真机 `make panel` + `tools/deploy.sh`（hpcmd 缺 `X11/extensions/scrnsaver.h`、无 make，
  编不了 `panel.c`）⇒ CPU / RSS / 无闪帧 / `pgrep -cx` 判据；`metrics.h` 改了结构，必须整体重建。
- 已知取舍：I/O 双曲线共享量程 ⇒「写≪读」时写曲线贴基线（与网络 ↓/↑ 同规矩）。若用户嫌不明显，
  可考虑写曲线单独量程（会失去同量纲对比）。

## 第十轮：改名与落点（2026-11-05）

- 用户裁决：项目 `hphelm-panel` → **`hpopi5b-panel`**，并归位到 **`/data/dev/project/`**（顶层只留分类目录）。
- **改名范围**：项目目录 / GitHub 仓库 / 二进制 / 安装路径 / 配置 / 日志 / 锁 / systemd 单元；
  纯历史 `hphelm`（旧工位机）称谓与已废弃的 `hphelm-session`/`hphelm-vt-guard` 保持原样，不改写成假历史。
- **生产端已同步**（hpopi5b）：`/usr/local/lib/hpopi5b-panel/` · `/usr/local/bin/hpopi5b-kiosk` ·
  `/usr/local/share/hpopi5b-panel/probe-remote.pl` · `/etc/hpopi5b-panel.conf` · `/var/log/hpopi5b-panel.log` ·
  `/var/cache/hpopi5b-panel/` · 单元 `hpopi5b-kiosk.service` + `hpopi5b-trending.{service,timer}`。
- **推送通路已恢复**（hpcmd 的 `gh auth` 可用，HTTPS push 成功）；旧的「本机推送失败、先不管」作废。

## 第十一轮：生产迁移与验收（2026-11-05 同日）

- 项目改名后**已在真机完成迁移**（hpopi5b）：`make panel` 重建 102272 B → `/usr/local/lib/hpopi5b-panel/`，
  探针、`hpopi5b-kiosk` 启动脚本（已收回仓库 `systemd/hpopi5b-kiosk.sh`）、conf、logrotate、`/var/cache/hpopi5b-panel/`、
  新单元全部就位；`disable --now` 旧 `hphelm-{kiosk,trending}` → `enable --now` 新单元。
- 备份 `/root/backup-rename-20261105/`；**旧二进制目录 + 旧单元文件保留未删**（已 disable、`is-active` inactive），回滚可用。
- 验收集：
  - `pgrep -cx hpopi5b-panel` = 1（旧名 0）· 两远程 `已连上（hpcore 16 核/4581 进程、hpcmd 4 核/238 进程）`
  - `xrandr` `DP-1 1920x1080+0+0 *`（HDMI-1 disconnected）
  - `probe-frames 40 20` ⇒ 变化 >30% 的帧 **0**
  - 日志 `X Error|Segmentation` **0** · CPU 5.7% / RSS 17956 KB / 1 线程
  - `hpopi5b-trending.timer` 14:56 首次跑通：`16 条 → /var/cache/hpopi5b-panel/trending.tsv (daily)`，下次 17:56
- 真机源码实测 **4008 行 C/H**（旧文档的 3054 是两幕之前的数）。
- 同日收尾（用户「生产也更新 2 根治」）：
  - 生产旧名残留已清（旧目录/conf/logrotate/cache/lock/旧单元），旧日志并入备份；`grep -rl hphelm /etc /usr/local` 0 命中，kiosk 仍 active。
  - `ssh hpopi5b` 根治：真因是 `~/.ssh/hpopi5b` 的**公钥没进 Pi `authorized_keys`**（钥文件一直在）；追加后裸 ssh 通，config 未改。
  - ⚠️ 回滚现只靠 `/root/backup-rename-20261105/`（旧产物已删）。


## Checklist（第九轮）

- [x] `src/scene.h` + `src/scenes.c`（注册表/轮换/转场/内容签名）
- [x] 两幕：panel / trending（hero/clock/ambient 已按用户要求撤掉）
- [x] `panel.c` 接幕（render/签名/poll 30 ms/懒分配 altpix/HUD）
- [x] `--scene --rotate --transition --list-scenes` + 配置文件键
- [x] `v_text_h()`：版式不再拿字号猜行高
- [x] `trending` 幕 + `tools/fetch-trending.py` + `hpopi5b-trending.timer`（3h）
- [x] 真机 enabled，列表/涨星/总数/HUD 实拍确认
- [x] trending 两列 12 条 + 描述 13pt / 仓库名 14pt（`F_DESC`），离屏与真机都看过
- [x] 撤幕：删除 scene_hero/clock/ambient，`--scene` 只剩 panel|trending
- [x] 白天/夜间主题（07:00/19:00 自动切，淡出→换色→淡入 0.9s；`theme=`/`theme_day=`/`theme_night=` + `--theme*`）
- [x] 两幕 × 两主题离屏与真机实拍；自动切换用近时刻实测亮度曲线（237→6→21）
- [x] 真机构建 + `probe-frames`（>30% 变化帧=0）+ CPU 口径
- [x] 转场视觉实测（push 中间帧确认；四特效无 X 错误）
- [ ] 定 `--rotate` 最终值（候选 25/30/45）并写进 `/etc/hpopi5b-panel.conf`
- [ ] `tools/deploy.sh` 支持 hpopi5b 落点（现在指向 hphelm 的 `/usr/local/bin`）

## Checklist（第七轮）

- [x] `src/remote.[ch]`：ssh 长连接 + 非阻塞行解析 + 半帧不提交 + 退避重连
- [x] `tools/probe-remote.pl`：远端只读探针（从 stdin 喂入，**不在 hpcore 装任何东西**）
- [x] 三列版式（`theme.h` 绝对逻辑像素 + `draw_host()` 画两台 + 进程榜一列两段）
- [x] `/etc/hpopi5b-panel.conf` 的 `remote=`（改主机名不用编辑 root 的会话包装器）
- [x] `tools/deploy.sh` 安装并校验探针 + `HPOPI5B_REMOTE=` 时顺带验链路
- [x] 性能：右对齐复用排版、背景渐变只渲染一次、等宽字体按字符宽截断
- [x] 真机判据全绿（tty7 / greeter=0 / panel=1 / 帧差分 0 / 日志无兜底）
- [x] 版面精细化（圆点对齐墨迹中线 / 每块一行 / 曲线均分剩余高度 / 新增 7 项指标）
- [x] 修复「远程采集连上但没数据 ⇒ 永久离线」（真机踩到，见 #20/#21 坑档）
- [x] 远程采集加**状态变迁日志**（拉起/已连上/EOF/退出/超时重连）
- [ ] **CPU 仍略超预算**：只本机 ~1.0%、含生产 ~1.4%（预算 <1%）— 见 Blockers
- [ ] 容器**健康**（重启循环/备份/证书）还没做，现在只有「容器 N 个」
- [x] GitHub 推送通路（2026-11-05 在 hpcmd 上恢复，push 成功）
