# 交接：hphelm-panel（运维面板 + 自研锁屏）

> **本轮状态（第六轮：交接与部署链本土化，2026-10-21）**：`tools/deploy.sh` 从「rsync 到 hphelm
> 再 ssh 上去」改成**本机原地部署**（旧路径在本机必失败）+ 七阶段判据；`docs/deploy.md`
> 重写成「更新系统屏保」runbook；项目记忆库 **`.ai/`** 由 engram v0.5.0 初始化并填入内容。
>
> **项目现在的形态**：源码目录 = 部署目录 = **`hphelm:/data/dev/hphelm-panel`**（2026-10-19 整仓迁入），
> 公开镜像 `github.com/zeroicey/hphelm-panel`。
>
> **前五轮成果一句话**：功能跑通（桌面↔面板↔登录）→ 消闪（离屏 pixmap）→ 修掉自激空转
> （CPU 33~49% → 0.60%）→ 无卡片紧凑版式 + 进程榜 40 条 → 立为独立项目（仓库 + 交接文档）。
>
> **最后更新：2026-10-21（第六轮）。**

---

## 零、先读 `AGENTS.md`

如果你（人或 AI）是来**接手维护**这个项目的：**先读 [`AGENTS.md`](AGENTS.md)** —— 那里有
三条红线、硬约束（性能预算/不许引工具包）、工作流、每轮必跑的验收判据、以及「不要做什么」。
本文（HANDOFF）只记「逐轮做了什么 + 当前待办」。

---

## 一、30 秒上手

**源码目录 = 部署目录 = `hphelm:/data/dev/hphelm-panel`**（2026-10-19 整仓迁入；旧的
`hpcore:/data/dev/hphelm-panel` 与 `hphelm:~/projects/panel2` 都已不存在）：

```bash
cd /data/dev/hphelm-panel
make                     # 需 pkg-config: x11 x11-xcb xscrnsaver cairo cairo-xlib pangocairo
tools/deploy.sh --check  # 看部署会做什么（只读）
tools/deploy.sh          # 构建 → 备份旧二进制 → 安装 → 重启面板 → 跑判据（本机原地）
```

⚠️ **必须在本机跑**：没有 rsync、不 ssh 自己连自己（本机 `~/.ssh/config` 没有 `hphelm` 这个 Host，
`ssh hphelm` 直接 `Host key verification failed`）。屏幕更新怎么带走、怎么回滚、六件绝不能做的事，
全在 **`docs/deploy.md`**。

**先在隔离环境看效果**（不要一上来动真屏）：

```bash
Xvfb :9 -screen 0 1920x1080x24 -ac &
DISPLAY=:9 ./hphelm-panel --state panel --lock /tmp/xv9.lock   # ⚠️ --lock 必须换，否则和真机实例抢锁
DISPLAY=:9 import -window $(DISPLAY=:9 xdotool search --name hphelm-panel | tail -1) /tmp/shot.png
python3 tools/render-ascii.py /tmp/shot.png 22 22              # 看版式
```

**看效果需要重启进程**（`kill -USR2` 只切状态、**不会重载二进制**）：

```bash
pkill -x hphelm-panel        # 会话包装器的守护循环会在 ~1s 内用新二进制把它拉回来
# ⚠️ 别用 `pkill -f`：它会匹配到执行它的那个 ssh shell 自己（已踩四次，见 docs/pitfalls.md）
# ⚠️ 重启会让桌面露出 1~2 秒（面板 unmapped）；`rc=143` 不计失败，红线 2 安全
# ⚠️ 在 Xvfb 里测时不要 `pkill -x`：会连真机实例一起杀（同名），请按 DISPLAY 挑 pid
```

**先用隔离环境试**（不动真屏，推荐每次都先这么干）：

```bash
Xvfb :9 -screen 0 1920x1080x24 -ac &
DISPLAY=:9 ./hphelm-panel --state panel     # 或 --state login
DISPLAY=:9 import -window "$(DISPLAY=:9 xdotool search --name hphelm-panel | tail -1)" /tmp/p.png
```

**不需要 X 的自检**：`./hphelm-panel --dump`（打印采集层数据）。

**验证渲染（我看不到图，所以走像素判据）**：

```bash
# 粗看布局（12x14 px 一格，亮度字符 + C=彩色）
python3 /tmp/ascii2.py /tmp/p.png | head -50      # 注意 /tmp 是易失的，见「四」
# 细看区域（x0 y0 x1 y1 每格像素）
python3 /tmp/fine.py /tmp/p.png 700 560 1230 780 4
# 取某个颜色的 bbox（例如输入框底色 #313244）
magick /tmp/p.png -crop ... ; # 见 README 的方法
```

---

## 二、代码地图（1634 行 C，无 GUI 工具包）

| 文件 | 行数级 | 作用 | 改 UI 常动它吗 |
| --- | --- | --- | --- |
| `src/theme.h` | 50 | **Catppuccin Mocha 配色 + 布局常量（逻辑像素）** | ⭐ 最常动 |
| `src/view.c` | 250 | 绘制基元：文字（pango）/背景渐变/细线/进度条/每核柱/sparkline | 偶尔 |
| `src/view_panel.c` | 190 | **面板版面**（无卡片、三栏 + 进程榜 40 条） | ⭐⭐ 最常动 |
| `src/view_login.c` | 100 | **登录页版面**（无卡片、居中） | ⭐ 改登录页时 |
| `src/metrics.[ch]` | 480 | 数据采集（只读 `/proc`、`/sys`，不 fork） | 加新指标时 |
| `src/panel.c` | 420 | X11 窗口、事件、**状态机**、主循环 | 改交互时 |
| `src/auth.[ch]` | 90 | 与助手通信（密码只走 stdin） | 一般不动 |
| `src/hphelm-auth.c` | 120 | setuid PAM 助手 | 一般不动 |

**坐标体系**：所有绘制都在「逻辑像素」下进行，主循环里 `cairo_scale(SCALE, SCALE)`，
`SCALE=1.48`（屏 141.8 DPI ÷ 96）。所以 `theme.h` 里的 `L_MARGIN=18` 实际是 18×1.48≈27 物理像素。
**改布局优先改 `theme.h` 的常量，不要硬编码在 `view_draw.c` 里。**

---

## 三、本轮已定的关键决策（别推翻，除非有实测理由）

| 决策 | 为什么 | 证据 |
| --- | --- | --- |
| **不用任何 GUI 工具包** | 工具包的影响比语言大一个数量级 | 自绘 7.4 MiB vs GTK3 17.6 vs Qt6 27.4 vs **GTK4 119.3** MiB |
| 用 C 而不是 Rust/Python | Rust 只多 0.4 MiB（不值得多一套工具链）；Python 多 19.5 MiB | C 14.1 vs Rust 14.5 MiB |
| 文字用 pango（不是 cairo 自带 toy API） | 与 GTK 同一套渲染栈 ⇒ 中文/图标/字体回退免费 | 只给拉丁字体族画中文，换 CJK 字体仅差 112/98000 像素 |
| 认证用一个 **setuid 助手**而不是整个程序 setuid | 特权面最小（120 行，只验密码） | — |
| 认证用**专用 PAM 服务**，不复用 `login` | `login` 带 faillock ⇒ 会连坐 autologin（实测把机器锁在 greeter） | 坑档 `2026-10-18-lockscreen-auth-reusing-login-pam-stack-*` |
| 面板 `override-redirect` + 定期 `XRaiseWindow` | WM 不仲裁这类窗口，polybar 后映射会压在上面 | 坑档 `2026-10-18-fullscreen-override-redirect-overlay-traps` |
| 键盘 grab「显示时抓、隐藏时放」 | 否则回桌面后桌面收不到按键 | 同上 |

---

## 三·五、第四轮做完的事：**无卡片紧凑版式 + 进程榜重做**

用户口径（原话）：「**不要卡片式设计**：从视觉角度来讲，我不想让它是一个卡片一个卡片这样组成的。
你要什么内容就直接显示在上面」「**布局更紧凑**：不要让太多的标题占用太多空间」
「**优化进程展示**：(a) 增加展示数量（十几二十个，甚至二三十个）(b) 一列一列的样子，
按内存从高到低排列下来 (c) 增加实时刷新……权衡一下体验感和资源占用」
「我鼓励你分多个文件去写，去装载不同的 UI 样式」。

| 项 | 做法 |
| --- | --- |
| 无卡片 | 删掉全部 `v_panel_box`；分区只靠 **1px 细线 + 字号层级 + 留白**；面板与登录页共用同一套 `v_bg()` 渐变底（顺带解决「面板平涂 vs 登录页渐变不统一」） |
| 紧凑 | 顶栏压成一行；区块标题不单独占行（label 与值同排、右对齐）；系统区由「卡片式 3 行」改成**三栏并排**（CPU 42% / 内存 29% / 网络 29%），总高 210 逻辑像素 |
| 进程榜 | `TOPN` 6 → **40**（4 列 × 10 行，逐列向下、按 RSS 降序）；每 3 秒刷新（采样 1 Hz）；行内 `名次·名·pid·内存`，口径标注 RSS |
| 拆文件 | `view.c`（基元）/ `view_panel.c`（面板）/ `view_login.c`（登录页），删掉 `view_draw.c` |
| 曲线 | 量程量化（`v_nice_ceil`）⇒ 不再整体跳；**撤掉「预填」**（那是伪造历史）；加零基线 |
| 每核柱 | 带轨道 + 最低 2px 亮条 ⇒ 空闲核也看得见（原来只有 1px 亮条） |
| 登录页 | 去掉外层卡片；认证中加不确定态进度条 |

**本轮验收**（真机 `:0`）：`>30% 像素变化` 帧 **0/50**（不闪）；CPU **0.60% of one core**；
进程榜 40 条、列对齐逐列量过（名次/名称/pid/内存右边界误差 ≤2px）；tty7 / `slick-greeter`=0 / 单实例。

⚠️ **本轮两个自伤（都已归档 kb）**：
① `const double cw2[3] = {a, b, 0}` ⇒ 网络栏宽度 0 ⇒ sparkline 压成一根竖线、右对齐文字挤到左边缘，
**编译零告警**（判据：按行取色块游程量几何）；
② 用「启动瞬间的采样值」预填曲线 ⇒ 那个值常是 0 / 偶尔是突发尖峰 ⇒ 曲线贴底或被假尖峰压扁真数据。

## 三·六、第五轮做完的事：**立为独立项目（搬进 `/data/dev` + 建 GitHub 仓库 + 交接文档）**

用户口径：「接下来把这个变成一个项目去维护：① 新建一个项目，新建 github 的仓库，然后推送上去。
② 把该交接的文档都交接给专门负责这个项目的 AI…… 但肯定还是要体现我们系统（运维中心主机）的偏好。
就是告诉外界，这是我的方案，并不是说你能直接拿过去就能用的。」

| 项 | 做法 |
| --- | --- |
| **实体** | `hpcore:/data/dev/hphelm-panel`（开发域实体根；git bundle 搬全历史，非 squash）· 部署副本 `hphelm:/data/dev/hphelm-panel`（2026-10-19 起） |
| **仓库** | **`https://github.com/zeroicey/hphelm-panel`**（public、无 license，与其余 40 个仓库一致）；描述里就写明「不是通用产品」 |
| **README** | 重写：公开定位 + 实测选型表 + **「这不是给谁用的」适配前提表**（屏幕/桌面栈/认证/会话入口/红线/字体/数据源/机队口径）+ 文档地图 + 状态与边界 |
| **AGENTS.md** | **给专职维护 AI 的工作指南**：三条红线 · 硬约束（性能预算/不引工具包）· 工作流 · 每轮验收判据 · 设计系统要点 · 代码地图 · 不要做什么 · 交付纪律 · 环境速查 |
| **docs/** | `architecture.md`（状态机/双缓冲/采样/认证/数据源）· `design.md`（无卡片+紧凑+坐标归 theme.h）· `verify.md`（**看不到图怎么验**）· `deploy.md`（部署/判据/回滚）· `pitfalls.md`（14 条坑，每条带判据） |
| **tools/** | `measure.py`（量几何：色块游程/墨迹带/颜色 bbox）· `probe-frames`（帧差分判闪）· `deploy.sh`（部署 + 判据，`--check` 只读） |
| **文档分工** | **代码知识归本仓库，机队事实归 kb**（`kb/runbooks/hphelm.md` 是机队视角的权威；本仓库只负责代码与设计） |

⚠️ **「体现我们系统的偏好」是怎么落地的**：README 里那张适配表把 8 项前提（1920×1080/1.48 缩放、
lightdm+bspwm+picom、专用 PAM 服务、会话包装器、三条运维红线、Nerd Font、k10temp/nvme 设备路径、机队看板口径）
逐项列出来，并在开头用 TL;DR 写明 **“This is not a drop-in product.”**
⇒ 外界能看懂「自绘、无工具包、低占用」这套**方法**可拿走，但「这份代码」要付适配成本。

## 三·七、第六轮做完的事：**交接 + 部署链本土化（2026-10-21）**

用户口径：「给 hphelm panel 交接如何部署更新项目屏保」「他们开发完也知道如何进行更新系统屏保不用麻烦你了」。

| 项 | 做法 | 判据 |
| --- | --- | --- |
| **部署链本土化** | `tools/deploy.sh` 重写：**去掉 rsync 与 `ssh hphelm`**，改成七阶段原地部署（自检 → 构建 → 备份旧二进制 → 安装+校验 → `--dump` 语义自检 → 重启并确认 pid 变化 → 判据）。新增 `--check` / `--no-restart` | 本机三次真跑全过：VT=tty7 · greeter=0 · 面板=1 · 源码⇄已装逐字节一致 · 助手 `4755 root:root` · `sleep_off=off` · 帧差分 `0` 帧 >30% |
| **回滚有凭据了** | 安装前若内容不同 ⇒ 自动留 `/usr/local/bin/hphelm-panel.prev` | 文档给出「三层回滚」（二进制 / 系统文件 / 退回 lightdm greeter） |
| **`docs/deploy.md` 重写** | 从「rsync 到另一台机」改成 **「更新系统屏保」runbook**：屏保到底是什么 · 文件清单 · 开发循环 · 七阶段 · 判据表 · 三层回滚 · 六件绝不能做的事 · 交付清单 | 全文 10 节，命令均可直接粘贴 |
| **项目记忆库 `.ai/`** | engram v0.5.0（本地构建 `/data/dev/engram/build/src/cli.js`）`init --root /data/dev/hphelm-panel --notes "…"` ⇒ `.ai/` + `.engram/` + `.agents/skills`，契约追加进 `AGENTS.md`（原有正文保留） | `engram doctor` / `engram dump` 通过；`.ai/CURRENT_TASK.md` · `ARCHITECTURE.md` · `runbooks/update-screensaver.md` · `decisions/` · `sessions/` 已填真实内容 |
| **文档纠偏** | `AGENTS.md` §0/§4/§8/§9 与本文 §一：删掉「源码在 hpcore、部署到 hphelm」的旧事实；新增「接手第一小时读什么」 | — |

**本轮我自己在 deploy.sh 第一版里踩的三个坑（都不报错，都已修）：**

1. **`pgrep -qx` 不存在** —— procps 的 pgrep 没有 `-q` 选项，它报错并返回非 0 ⇒ 脚本却当成
   「lightdm 没在跑」发了假告警。修法：`pgrep -x lightdm >/dev/null 2>&1`。
2. **对 `/tmp/hphelm-panel.log` 全文件 grep 「连续 3 次」** ⇒ 2026-10-18 那两次事故的字符串一直
   躺在日志里 ⇒ **每次部署都报「面板在反复崩」**。修法：只取**最近一条 `启动：` 之后**的日志块。
3. **CPU 窗口包含了刚启动的几秒**（启动帧全额重绘 + 首次扫进程）⇒ 读出 1.60%，而稳态是 1.1~1.4%。
   修法：先热身 3 秒再开 15 秒窗口。

**⚠️ 一个待查的真问题**：CPU 实测 **1.13%–1.60%**，高于文档记录 0.60%（2026-10-18）。
其余判据全绿（不闪、不空转、几何对、单实例）。清楚到代码里的假设**不要凭猜** ——
正解是用 `HPHELM_PANEL_DEBUG=1` 看 it/s 与各阶段耗时，先把「重绘次数」与「单帧耗时」分开。

**⚠️ 一个未解的交付缺口（用户裁决「先不管」）**：hphelm 的 GitHub 身份**只够推 `dotfiles`**（deploy key 一仓一把），
所以本机 `git push origin main` 会权限失败。本轮提交只落在本地；**用户 2026-10-21 裁决：先不管，提交留本地** ——
不要为了这个去改仓库配置或另建密钥。哪天需要推时，要么另找有账号级密钥的机器，要么在本仓加 write deploy key。

## 四、已知粗糙点（下一轮的候选）

**UI / 视觉**（第四轮已解决 1/2/3/7 的一部分：顶栏自己画了时间与状态、每核柱带轨道、
曲线不预填但有零基线、两界面统一渐变底）
1. ~~面板顶部 48px 的空白~~ ⇒ 顶栏自己画了主机/IP/uptime/核数 + 时间 + 24×7 状态点。
2. ~~每核小柱看不清~~ ⇒ 带轨道 + 最低 2px 亮条。
3. ~~sparkline 起步为空~~ ⇒ 不预填（诚实生长）+ 零基线；若要「一开机就铺满」，正解是**缩短窗口**
   （比如只画最近 120 个样本 = 2 分钟），而不是伪造数据。
4. **进程榜 RSS 与 PSS 口径不一致**：榜单是 RSS（和 htop 一致），而模块标题写「按内存排序」。
   可以标注口径，或改用 PSS（贵）。
5. **登录页卡片位置**：现在卡片中心在 `cy+40`，时钟在上方；整体偏「上下分布」，可以再收拢。
6. **没有错误态以外的反馈**：认证中只有「正在验证…」，可以加一个转圈或进度条。
7. **颜色**：`C_BASE` 大面积填充 `#1e1e2e`，与登录页的渐变风格不统一（面板是平涂，登录页是渐变）。

**功能 / 其他**
8. **「立即锁屏」只有信号**：`kill -USR1`。可以在 `sxhkd` 里包一个快捷键（`pkill -USR1 -x hphelm-panel`）。
9. ~~**DPMS 关屏后唤不醒**：`sleep_off=3600` 后关屏，理论上任意输入会 `dpms(1)`；
   但实测未验证（需要等 1 小时）。可以调小 `--sleep-off` 测一次。~~
   **2026-10-19 关闭**：用户裁决定位是「看板要 7×24 一直显示」⇒ 默认 `sleep_off=0`（永不关屏），
   并且把「0 语义」写对了（`CFG_SLEEP_OFF > 0 && idle >= ...`；旧代码裸比较会让 0 变成「立即关屏」）。
   唤醒路径（`on_key/on_motion/on_button` 里的 `if (screen_off) dpms(1)`）保留不动，以备日后调回正数。
10. **面板内容还没「服务概览」**：mockup 里的「26 容器全绿 / 磁盘 / 备份」还没做（需要探测 hpcore 的容器）。
11. **多显示器未处理**：现在固定用 `DefaultScreen` 的整屏尺寸。
12. ~~**CPU 0.8%**：主要花在每秒约 50 段 pango 文字整形。两个杠杆：PangoLayout 缓存、区域局部重绘。~~
    **2026-10-18 重测：CPU 0.60% of one core**（1 Hz 采样；绘制 ~3.5ms/帧、上屏 ~0.05ms、签名 ~0.05ms）
    ⇒ 这一条优先级已降。真要再降就先看 `HPHELM_PANEL_DEBUG=1` 的耗时分布。
13. **面板只在 :0 上**：换接口（DP-2→HDMI）后尺寸变化由 `ConfigureNotify` 处理，但没实测过。

---

## 四·五、本轮（第二轮）做完的事：**消闪 + 消除自激空转**

| # | 问题 | 根因 | 修法 | 判据 |
| --- | --- | --- | --- | --- |
| 1 | **面板每秒闪一下**（用户：「我这个屏幕现在会有一点闪……就是内存、网络、CPU 那个界面」） | 直接往窗口上画 = **没有双缓冲**：每帧先 `cairo_paint(C_BASE)` 清底、再逐块画，cairo 分批下发 ⇒ 中间态被 picom/屏幕看到 | 绘制全进**离屏 `Pixmap`**，每帧**一次 `XCopyArea`** 上屏；`XSetWindowBackgroundPixmap` 让服务器回填；`win_show()` 先画后映射；`ConfigureNotify` 重建 pixmap | 抓窗口连续帧差分：`>30% 像素变化` 帧 **4/40 → 0**；静止时 37/39 帧 `diffpx=0` |
| 2 | **面板 CPU% 跳成 KTV**（用户：「哥们我 cpu 在唱 k 啊 疯狂跳」） | ⚠️ **我自己引入的**：`XCreateGC(dpy,win,0,NULL)` 的 `graphics_exposures` 默认 True ⇒ 每次 `XCopyArea` 回一个 `NoExpose` ⇒ `poll()` 立刻返回 ⇒ 主循环自激空转 35 it/s ⇒ 采样窗口被压到 ~28ms | `gcv.graphics_exposures=False` + `XCreateGC(..., GCGraphicsExposures, &gcv)`；**另加两道与循环速率解耦的保险**：采样按时间节流 1 Hz、`dt` 改单调时钟亚秒、IP/进程榜按时间刷新 | 仪表 `poll[可读 0/超时 2]`、事件 0、内容 1 次/秒、CPU **49% → 0.60%** |

**新增的两个能力**（下一轮直接用）：

```bash
HPHELM_PANEL_DEBUG=1 ./hphelm-panel --state panel   # 主循环仪表：it/s · poll 结果 · 事件类型 · 重绘/采样 · 各阶段耗时
./hphelm-panel --lock /tmp/x.lock ...               # 单实例锁可换 ⇒ Xvfb 测试不必杀真机实例
```

**本轮踩到的两条新坑（已归档 kb）**：

- `pitfalls/2026-10-18-panel-flicker-direct-draw-no-double-buffer.md`
- `pitfalls/2026-10-18-xcopyarea-noexpose-poll-self-sustaining-loop.md`

⚠️ **本轮操作失误（记下来）**：我在 Xvfb 测试时用 `pkill -x hphelm-panel`，
它把**真机上正在跑的那个实例也杀了**（同名进程）⇒ 用户屏幕上的面板重启了两次
（守护循环按「运行时长」判定 ⇒ `fails` 没被误触发、greeter 也没被招回来，红线 2 生效）。
⇒ **测试实例与真机实例要能区分**：现在用 `--lock` 分开锁，杀的时候要挑 pid（按 `/proc/<pid>/environ` 里的 `DISPLAY` 认）。

## 五、诊断入口（下一轮排障直接用）

> `AGENTS.md` 第 4 节是精简版验收清单；这里是与 hphelm 这台机器绑定的具体命令。

```bash
# 状态机日志（每次变迁都有时间戳）
tail -20 /tmp/hphelm-panel.log

# 关键判据
cat /sys/class/tty/tty0/active                  # tty7=会话 / tty8=lightdm greeter（说明被抢了）
pgrep -cx slick-greeter                         # 应为 0
pgrep -c -f /usr/local/bin/hphelm-panel         # 应为 1（多了说明单实例锁失效）
xdotool search --name hphelm-panel | wc -l      # 应为 1

# 顶部 48px 是否有墨迹（0 = 面板盖住了 polybar）
import -window root /tmp/w.png
magick /tmp/w.png -crop 1920x48+0+0 +repage -colorspace gray -threshold 40% -format '%[fx:mean*w*h]' info:

# grab 是否已释放（回桌面后应 GrabSuccess）
cat > /tmp/tg.c <<'C'
#include <X11/Xlib.h>
#include <stdio.h>
int main(void){Display*d=XOpenDisplay(NULL);Window w=DefaultRootWindow(d);
int r=XGrabKeyboard(d,w,True,GrabModeAsync,GrabModeAsync,CurrentTime);
printf("trygrab=%d (%s)\n",r,r==GrabSuccess?"成功=桌面能收按键":"AlreadyGrabbed=被抓着");
XUngrabKeyboard(d,CurrentTime);XCloseDisplay(d);return 0;}
C
gcc -O2 -o /tmp/tg /tmp/tg.c -lX11 && DISPLAY=:0 XAUTHORITY=~/.Xauthority /tmp/tg

# 账号有没有被 faillock 锁（会让 autologin 失败 ⇒ VT 被 greeter 抢）
sudo faillock --user oicey
sudo grep -aE "autologin|Switching to greeter" /var/log/lightdm/lightdm.log | tail -3
```

⚠️ **注意 `/tmp` 是易失的**：我用来验证渲染的 `ascii2.py` / `fine.py` 现在只在 hphelm 的 `/tmp` 里。
下一轮如果要用，先确认还在；不在的话照「一」里的方法重写（各 ~20 行 python）。

---

## 六、回滚

```bash
# 只回滚会话入口（回到「lightdm greeter 当锁屏」）
sudo cp -a /usr/local/bin/hphelm-session.bak-prepanel /usr/local/bin/hphelm-session
cp -a ~/.config/sxhkd/sxhkdrc.bak-prepanel ~/.config/sxhkd/sxhkdrc
sudo /usr/local/bin/hphelm-lockd >/dev/null 2>&1 &   # 旧的空闲锁屏守护（二进制仍在）
pkill -f /usr/local/bin/hphelm-panel
sudo systemctl restart lightdm
```

**整机级归档**：`/data/backups/hphelm-panel-20261018/`
（`panel2.bundle` 完整 git 历史 + 四处系统配置 + `RESTORE.md` 恢复清单）

⚠️ 回滚时**也要停掉 VT 守卫**，否则它会一直把 VT 往 tty7 拽：
`sudo systemctl disable --now hphelm-vt-guard.service`

---

---

## 八、⛔ 两条红线（下一轮务必先读，我在这一轮**踩了两次**，都是让用户看到本该消失的旧登录页）

### 红线 1：永远不要给 lightdm 发锁请求

面板是全屏覆盖层，**只在活跃 VT 上可见**。只要有人把锁请求发给 lightdm
（`dm-tool lock` / `light-locker` / autologin 失败），lightdm 就会**起 greeter 并把 VT 切到 tty8**
⇒ 用户看到 DM 的旧登录页，我们的面板被藏在后面。

⇒ **`hphelm-session` 里永远不出现 `dm-tool`；守护循环的兜底只允许 `xset dpms force off` + 记日志。**

### 红线 2：守护循环只把「运行 <5 秒即退出」当失败

`pkill` 的默认信号是 SIGTERM ⇒ 退出码 **143**。用「非 0 即失败」判定的话，
**每次调试重启都会给失败计数 +1**，攒到 3 次就会触发兜底 —— 我这次就是这么把 greeter 招回来的。
**判据用运行时长，不要用退出码。**

### 独立防线：VT 守卫（2026-10-18 新增）

| 项 | 值 |
| --- | --- |
| 脚本 | `/usr/local/sbin/hphelm-vt-guard` |
| 单元 | `/etc/systemd/system/hphelm-vt-guard.service`（enabled，常驻 ~1.2 MiB） |
| 动作 | **活跃 VT ≥ 8 且 `slick-greeter` 在跑 且 `bspwm` 在跑 ⇒ `chvt 7`** |
| 边界为何这么窄 | 为了**不干扰 `Ctrl+Alt+F2` 进救援 TTY**（tty2~tty6 永不介入） |

**它的实测判据**：

```sh
sudo chvt 2; sleep 3; cat /sys/class/tty/tty0/active   # 应仍是 tty2（守卫不动它）
sudo chvt 7
dm-tool lock; sleep 3; cat /sys/class/tty/tty0/active  # 应已回到 tty7
journalctl -t hphelm-vt-guard -n 3                     # 应看到「抢到 tty8 ⇒ 切回 tty7」
```

（实测：模拟 `dm-tool lock` 后 **3 秒内 VT 已回 tty7**。）

### 另外两个同类自伤（都已修）

- **日志按大小轮转**：包装器在会话启动前检查 `/tmp/hphelm-panel.log` > 1 MiB 就 `tail -c 256K`。
  （背景：我用 `rm -f /tmp/*.log` 清理时**把面板正在写的日志也删了** —— 进程仍持 fd，
  继续往已删除 inode 写，日志从此不可见且空间不释放。**删正在写的日志不报错，只会让日志「看起来空了」**。）
- **`xset dpms 0 0 0`**：DPMS 定时关屏交给面板自己管（它才是「空闲显示」的唯一负责人），
  否则 X 会在 11 分钟把面板涂黑。

### 相关坑档（kb）

- `2026-10-18-guard-fallback-self-inflicted-traps.md` ← **本轮最重要的教训**
- `2026-10-18-lockscreen-auth-reusing-login-pam-stack-locks-out-autologin.md`
- `2026-10-18-fullscreen-override-redirect-overlay-traps.md`

---

## 九、下一轮开工前请先读（2026-10-21 更新）

- `.ai/CURRENT_TASK.md`（此刻在做什么 / 卡在哪 / 下一步）——`engram dump` 也能一行拿到指纹
- **`docs/deploy.md`**（更新系统屏保：七阶段、判据表、三层回滚、六件绝不能做的事）
- `README.md`（本项目自述：行为、选型实测表、安全模型、六条适配前提）
- kb `runbooks/hphelm.md` 的「运维面板（hphelm-panel）」节（机队视角 + 验收判据；kb 在 `hphelm:/data/kb`）
- kb 两个坑档：`2026-10-18-lockscreen-auth-reusing-login-pam-stack-*`、
  `2026-10-18-fullscreen-override-redirect-overlay-traps`
