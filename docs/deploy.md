# 部署：怎么把改完的代码变成「系统屏保」

> **这是本项目最重要的一份操作文档。** 它回答一件事：
> **改完代码，怎样安全地把它变成此刻正在工位屏幕上跑的那个东西，并且知道它真的好了。**
>
> 适用机：`hphelm`（GMKtec G10，别名就是 `hphelm`）· 最后实测：**2026-10-21**（三条判据链全绿，
> 只有 CPU 一条是黄字，见第七节末尾的「已知偏差」）。
> 相关：[`architecture.md`](architecture.md) · [`verify.md`](verify.md) · [`pitfalls.md`](pitfalls.md) ·
> [`../AGENTS.md`](../AGENTS.md) · 机队视角的权威在 `kb/runbooks/hphelm.md`

---

## 零、30 秒版

```sh
cd /data/dev/hphelm-panel        # 源码目录 = 部署目录，同一份（2026-10-21 起）
tools/deploy.sh --check          # 看它要做什么（只读，不落地）
tools/deploy.sh                  # 构建 → 备份旧二进制 → 安装 → 重启面板 → 跑判据
```

判据全绿就结束了。**黄字提醒**（CPU 预算那种）不阻断，但要记在 `HANDOFF.md` 里。**红字失败**就停下按第七节查，
或按第八节回滚。

⚠️ **`tools/deploy.sh` 必须在 hphelm 本机跑**（不是 ssh 到别处）。它没有 rsync，也不 ssh 自己连自己 ——
源码目录就是部署目录。

---

## 一、先说清「屏保」是什么（不然会找错东西）

这台机上**没有** `xscreensaver` / `light-locker` / `dm-tool`。所谓的「屏保」就是这个项目自己：

| 状态 | 屏幕上看到什么 | 谁在画 |
| --- | --- | --- |
| `PANEL` | 运维看板（CPU/内存/网络/每核柱/进程榜 40 条/时钟），**7×24 常亮** | `hphelm-panel --state panel` |
| `LOGIN` | 动键盘/鼠标 ⇒ 自绘登录页（PAM 认证） | 同一个进程，切状态 |
| 桌面 | 认证通过 ⇒ 面板取消映射，露出 bspwm 桌面 | 面板让位 |
| 再空闲 180s | 面板覆盖回来 | 同上 |

所以：

- **「更新系统屏保」= 用新的 `/usr/local/bin/hphelm-panel` 替换旧的，并让它重新跑起来。** 就这一件事。

### 生产服务器（可选）

面板的右列显示一台远程主机（默认 `hpcore`）。配置只有一个文件：

```sh
sudo tee /etc/hphelm-panel.conf <<'EOF'
# 生产服务器：ssh 目标名（~/.ssh/config 里的 Host 别名）。留空/删掉这行 = 只显示本机。
remote=hpcore
EOF
```

优先级：`--remote <host>` > `$HPHELM_REMOTE` > `/etc/hphelm-panel.conf`。
**改完 `pkill -x hphelm-panel`**（守护循环会用新配置拉起）。

⚠️ 前提：本机到该目标**免密 ssh 可用**（`ssh -T -o BatchMode=yes hpcore true`），
且对方有 `perl`（Debian/Arch 都自带，**不需要装任何东西**）。
链路自检：`./hphelm-panel --dump | grep '^remote host='`。
- 它由 **lightdm 的会话入口 `/usr/local/bin/hphelm-session`** 拉起（在 `while :; do … done` 守护循环里），
  ⇒ `pkill -x hphelm-panel` 之后约 1 秒会自己用**新二进制**回来。**不需要重启 lightdm，也不需要退出登录。**
- 它**替代了 lightdm 的 greeter** ⇒ 它挂了就是「用户看不到登录页」⇒ 所以下面有三条红线。

---

## 二、机器上到底有哪些文件（谁装、谁管）

| 路径 | 是什么 | 谁负责写 |
| --- | --- | --- |
| `/data/dev/hphelm-panel/` | **源码 = 部署目录**（同一个目录，git 仓，`main` 分支） | 你 |
| `/usr/local/bin/hphelm-panel` | 主程序（~71 KB；`ldd` 里 GUI 工具包 **0** 个） | `sudo make install` |
| `/usr/local/libexec/hphelm-auth` | setuid 认证助手，**必须是 `4755 root:root`** | `sudo make install` |
| `/usr/local/share/hphelm-panel/probe-remote.pl` | 生产服务器探针（**644，不是可执行文件**；由 ssh 喂到对方 stdin） | `sudo make install` |
| `/etc/hphelm-panel.conf` | `remote=<ssh 目标>`（改主机名只改这里，不用动会话包装器） | **手工创建**（见下） |
| `/usr/local/bin/hphelm-panel.prev` | **上一版二进制**（deploy.sh 在内容不同时自动留） | `deploy.sh` |
| `/usr/local/bin/hphelm-session` | lightdm 会话入口：`xset`、日志轮转、守护循环、兜底 | **手工，一次性**（见第五节） |
| `/etc/pam.d/hphelm-auth` | **专用** PAM 服务（只留 `pam_unix`） | **手工，一次性**（见第五节） |
| `/usr/local/sbin/hphelm-vt-guard` + `hphelm-vt-guard.service` | 独立防线：被抢的 VT 拽回 tty7 | **手工，一次性**（见第五节） |
| `/tmp/hphelm-panel.log` | 运行日志（会话启动时 >1 MiB 就轮转） | 运行时自动 |
| `/data/backups/hphelm-panel-20261018/` | 整机级归档（git 历史 + 四处系统配置 + `RESTORE.md`） | 别删 |

**读这张表的方式**：只有前两行是「你每天会动的」；其余是「装了一次就基本不动」的系统文件，
动它们之前先读第五节和第八节。

---

## 三、日常开发循环（改代码之前先看这个）

```sh
# ① 构建
make

# ② 在隔离的 X 里看效果 —— 不要一上来动真屏
Xvfb :9 -screen 0 1920x1080x24 -ac &
DISPLAY=:9 ./hphelm-panel --state panel --lock /tmp/xv9.lock   # ⚠️ --lock 必须换，否则和真机实例抢锁
DISPLAY=:9 import -window "$(DISPLAY=:9 xdotool search --name hphelm-panel | tail -1)" /tmp/p.png
python3 tools/render-ascii.py /tmp/p.png 22 22                 # 粗看版式
python3 tools/measure.py /tmp/p.png --runs 240 --bands         # 量几何

# ③ 看不到图也要能量（这是本项目的核心方法）
#    见 docs/verify.md：帧差分判闪 / 几何判版式 / 仪表判空转 / /proc 判性能
```

⚠️ **在 Xvfb 里测的时候不要 `pkill -x hphelm-panel`** —— 那会**连真机实例一起杀**（同名）。
按 pid 挑（认 `/proc/<pid>/environ` 里的 `DISPLAY`），或者用完 `kill <pid>` 指定 pid。

⚠️ **`kill -USR2` 只切状态，不会重载二进制**：换了代码就必须重启进程。

---

## 四、部署（工具做的事，逐条可核对）

`tools/deploy.sh` 有七个阶段，每一阶段都会打一行 `==> n/7`。`--check` 只打印、不落地。

| 阶段 | 做什么 | 失败会怎样 |
| --- | --- | --- |
| 1/7 环境自检 | 源码目录、`pkg-config` 六个包、`make`、免密 `sudo`、VT=tty7、lightdm 在跑 | 直接退出，不动系统 |
| 2/7 构建 | `make`；**要求 0 警告**（有警告只黄字提醒）；打印字节数与 `ldd` 里 GUI 工具包数量（期望 0） | 构建失败 ⇒ 原样保留旧二进制，屏幕不受影响 |
| 3/7 备份 | 新构建与已装**内容不同**时，`cp -a` 到 `/usr/local/bin/hphelm-panel.prev` | 这一步是回滚的凭据 |
| 4/7 安装 | `sudo make install`；随后校验「源码 ⇄ 已装」逐字节一致、助手权限 `4755 root:root` | 任一不符 ⇒ 红字退出 |
| 5/7 语义自检 | `./hphelm-panel --dump \| tail -1`（**不需要 X，不抢单实例锁**），断言含 `sleep_off=off` | 不符 ⇒ 停下（说明默认值或判据被改回去了，`docs/pitfalls.md` #16） |
| 6/7 重启 | `pkill -x hphelm-panel`，等 ≤15 秒让守护循环用新二进制拉起，并确认 **pid 变了** | 15 秒没起来 ⇒ 守护循环没在跑，看日志 |
| 7/7 判据 | VT / greeter / 单实例 / 帧差分 / CPU / 当前实例日志 | 红线失败 ⇒ exit 1 |

**手动等价（脚本坏了才用，别养成习惯）**：

```sh
cd /data/dev/hphelm-panel
make && sudo make install                       # 构建 + 安装
./hphelm-panel --dump | tail -1                 # 语义自检：应含 sleep_off=off
pkill -x hphelm-panel                           # 守护循环 ~1s 后用新二进制拉起
```

⚠️ `pkill -x` **不是** `pkill -f`：`-f` 会匹配到执行它的那个 shell 自己（我们踩过四次）。
⚠️ 重启会让桌面露出 **1~2 秒**（面板 unmapped），这是正常的，不要为了「无感更新」去改架构。

---

## 五、只做一次的三处系统配置（重建机器时才需要）

### 5.1 会话入口 `/usr/local/bin/hphelm-session` 的三段关键代码

```sh
# ⛔ 永不出现 dm-tool / light-locker
xset s off
xset dpms 0 0 0          # DPMS 定时交给面板自己管（默认 sleep_off=0 ⇒ 永不关屏）

# 日志轮转：>1MiB 就 tail 到 256K（直接 rm 会删掉正在写的 inode，日志从此「看起来是空的」）
[ -f /tmp/hphelm-panel.log ] && [ "$(stat -c %s /tmp/hphelm-panel.log)" -gt 1048576 ] && \
  { tail -c 262144 /tmp/hphelm-panel.log > /tmp/hphelm-panel.log.1; mv -f /tmp/hphelm-panel.log.1 /tmp/hphelm-panel.log; }

(
  fails=0
  while :; do
    start=$(date +%s)
    /usr/local/bin/hphelm-panel --state panel >>/tmp/hphelm-panel.log 2>&1
    rc=$?; dur=$(( $(date +%s) - start ))
    printf '[%s] hphelm-panel 退出 rc=%s 运行 %ss\n' "$(date +%T)" "$rc" "$dur" >>/tmp/hphelm-panel.log
    [ "$rc" -eq 3 ] && { sleep 30; continue; }        # 已有实例（单实例锁）
    [ "$dur" -lt 5 ] && fails=$((fails+1)) || fails=0 # ⚠️ 判据是「运行时长」，不是退出码
    if [ "$fails" -ge 3 ]; then
      printf '[%s] 连续 3 次「起来就死」⇒ 关屏兜底（不碰 lightdm）\n' "$(date +%T)" >>/tmp/hphelm-panel.log
      xset dpms force off >/dev/null 2>&1 || true     # ⛔ 绝不 dm-tool lock
      sleep 30; fails=0
    fi
    sleep 1
  done
) &
```

### 5.2 PAM：`/etc/pam.d/hphelm-auth` 只留 `pam_unix`

```sh
cat >/etc/pam.d/hphelm-auth <<'EOF'
auth     required pam_unix.so
account  required pam_unix.so
EOF
```

⛔ **绝不要写成 `@include login` / `system-local-login`**：那条链带 `pam_faillock`，而
`lightdm-autologin` 第一行也是 `pam_faillock.so preauth` ⇒ 锁屏输错 3 次会把账号锁上、
连坐 autologin 失败 ⇒ lightdm 起 greeter 抢走 VT（**用户就会看到那个本该消失的旧登录页**）。

### 5.3 VT 守卫

| 项 | 值 |
| --- | --- |
| 脚本 / 单元 | `/usr/local/sbin/hphelm-vt-guard` · `/etc/systemd/system/hphelm-vt-guard.service`（enabled） |
| 动作 | 活跃 VT ≥ 8 **且** `slick-greeter` 在跑 **且** `bspwm` 在跑 ⇒ `chvt 7` |
| 边界为什么这么窄 | 不干扰 `Ctrl+Alt+F2` 救援 TTY（tty2~tty6 永不介入） |
| 判据 | `sudo chvt 2; sleep 3; cat /sys/class/tty/tty0/active` ⇒ 仍 `tty2` |

---

## 六、判据表（**部署完必须逐条对上**）

| # | 命令 | 期望 | 不符意味着什么 |
| --- | --- | --- | --- |
| 1 | `cat /sys/class/tty/tty0/active` | `tty7` | 有东西抢了 VT（`tty8` = greeter 起来了 ⇒ 红线 1 被碰） |
| 2 | `pgrep -cx slick-greeter` | `0` | ≠0 ⇒ 面板没在当锁屏，用户在看着 DM 的旧登录页 |
| 3 | `pgrep -cx hphelm-panel` | `1` | `0` 守护循环死了；`2` 单实例锁失效（会互相抢抬窗） |
| 4 | `md5sum ./hphelm-panel /usr/local/bin/hphelm-panel` | 一致 | 装错版本（或装到别处） |
| 5 | `./hphelm-panel --dump \| tail -1` | 含 `sleep_off=off` | 默认值/判据被改回去了（#16） |
| 6 | `DISPLAY=:0 tools/probe-frames 40` | `>30% 变化帧 = 0` | 有闪 ⇒ 双缓冲被绕过了 |
| 7 | CPU（`/proc/<pid>/stat` 的 14/15 字段，20 秒窗口） | < 1% of one core | 见第七节末尾「已知偏差」 |
| 8 | `tail -3 /tmp/hphelm-panel.log` | `rc=143` = 我们主动重启（正常）；**不能出现**当前实例的「连续 3 次」 | 出现 ⇒ 面板在反复崩，兜底已经关屏 |

⚠️ **第 8 条的坑**：`/tmp/hphelm-panel.log` 是**跨会话追加**的，2026-10-18 那两次事故的
「连续 3 次异常退出 ⇒ 退回 dm-tool lock 兜底」还在文件里 ⇒ **对全文件 grep 会假告警**
（我写 `deploy.sh` 第一版就这么误报过）。正确做法：只取**最近一条 `启动：` 之后**的日志块。
`deploy.sh` 里已经这么实现了。

### 已知偏差（2026-10-21 实测，留给项目 AI 查）

| 项 | 记录值 | 2026-10-21 实测 | 备注 |
| --- | --- | --- | --- |
| CPU | 0.60% of one core（2026-10-18） | **1.13%–1.60%**（20/30 秒窗口，多次） | 不闪、不空转、几何都对；**机理未定**。查法：`HPHELM_PANEL_DEBUG=1` 看 it/s 与各阶段耗时，别先猜 |

---

## 七、回滚（三个层级，从轻到重）

### A. 只是新版不好 ⇒ 换回上一版二进制（最常见，10 秒）

```sh
sudo install -m755 /usr/local/bin/hphelm-panel.prev /usr/local/bin/hphelm-panel
pkill -x hphelm-panel        # 守护循环会把它拉起来
```

`deploy.sh` 在「新构建 ≠ 已装」时会自动写 `.prev`。**注意**：它只保留**上一版**。
想留更多版本就自己 `cp -a /usr/local/bin/hphelm-panel /data/backups/panel-$(date +%F-%H%M)`。

### B. 会话入口 / 快捷键被改坏了 ⇒ 恢复系统文件

```sh
sudo systemctl disable --now hphelm-vt-guard.service        # 先停守卫，否则它一直把 VT 拽回 tty7
sudo cp -a /usr/local/bin/hphelm-session.bak-prepanel /usr/local/bin/hphelm-session
cp -a ~/.config/sxhkd/sxhkdrc.bak-prepanel ~/.config/sxhkd/sxhkdrc
pkill -x hphelm-panel
sudo systemctl restart lightdm                              # ⚠️ 会杀掉当前图形会话
```

### C. 完全回到「lightdm greeter 当锁屏」（放弃自研面板）

```sh
sudo systemctl disable --now hphelm-vt-guard.service
sudo /usr/local/bin/hphelm-lockd >/dev/null 2>&1 &   # v1 的空闲锁屏守护（二进制可能已不在，见下）
pkill -x hphelm-panel
sudo systemctl restart lightdm
```

⚠️ `hphelm-lockd` 是 **v1** 留下的脚本，`/usr/local/bin/` 里现在**可能没有**（它随 v1 退休）。
真要走 C 层，改用 lightdm 自带锁（`light-locker`）**会重新引入「登录两遍」的两把锁 bug** ——
先读 `docs/pitfalls.md` #15 再决定。

### 整机级归档

`/data/backups/hphelm-panel-20261018/`：`panel2.bundle`（完整 git 历史）+ 四处系统配置 + `RESTORE.md`。

---

## 八、⛔ 绝对不要做的六件事

1. **不给 lightdm 发任何锁请求**（`dm-tool lock` / `light-locker` / 让 autologin 失败）⇒ 会起 greeter 抢 VT，用户看到旧登录页。红线 1。
2. **不用退出码判守护循环失败**（SIGTERM ⇒ 143）⇒ 用「运行时长 <5 秒」。否则每次调试重启都在攒失败计数。红线 2。
3. **回滚/改会话入口前不停 VT 守卫** ⇒ 它会把 VT 一直拽回 tty7，让你误判「改了没生效」。红线 3。
4. **不用 `pkill -f <pattern>`** ⇒ 会匹配到执行它的 shell 自己（踩过四次）。用 `pkill -x` 或写进脚本文件。
5. **不 `rm -f /tmp/hphelm-panel.log`** ⇒ 进程仍持 fd，继续往已删除 inode 写：日志「看起来空了」、空间还不释放。要清就轮转（`tail -c` + `mv`）。
6. **不 `systemctl restart lightdm` 来「试一下」** ⇒ 那会杀掉用户正在用的图形会话。

---

## 九、交付清单（一轮改动的「完成」定义）

- [ ] `make` 0 警告；`deploy.sh` 判据全绿（黄字提醒要在 `HANDOFF.md` 记一笔）
- [ ] 改的是 UI ⇒ 在 Xvfb 里量过几何（柱宽/间距/对齐/行距），与设计值逐位对过
- [ ] 改的是主循环/渲染 ⇒ 跑过帧差分（0 帧 >30%）与仪表（2 it/s、poll 全超时、事件 0）
- [ ] `HANDOFF.md` 追加本轮小结（做了什么 / 判据数字 / 踩了什么）
- [ ] 行为、版式、接口有变 ⇒ 同步 `README.md` 与对应 `docs/*.md`
- [ ] `.ai/CURRENT_TASK.md` 更新；新决策 → `.ai/decisions/`；新坑 → `.ai/pitfalls/cases/`
- [ ] **机队事实有变**（部署路径、服务名、红线、PAM、VT 守卫）⇒ 提醒维护 kb 的人更新
      `kb/runbooks/hphelm.md` 的「运维面板（hphelm-panel）」节
- [ ] `git commit`（`main` 分支直接提交，不 force-push、不改历史）

---

## 十、历史说明（为什么文档里还留着 hpcore / rsync）

2026-10-18～10-19 期间，源码权威在 `hpcore:/data/dev/hphelm-panel`，部署靠
`tools/deploy.sh` 用 rsync 推到 `hphelm:~/projects/panel2`（后来改成 `hphelm:/data/dev/hphelm-panel`），
再 `ssh hphelm 'make && sudo make install'`。

**2026-10-19 起整仓迁到 hphelm，源码与部署目录合一** ⇒ rsync 与「ssh 自己连自己」都不需要了。
那条老路现在**必然失败**：hphelm 的 `~/.ssh/config` 里没有 `hphelm` 这个 Host，
`ssh hphelm` 直接 `Host key verification failed`。⇒ `tools/deploy.sh` 已改成**原地部署**（2026-10-21）。
