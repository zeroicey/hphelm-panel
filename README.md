# hphelm-panel —— hphelm 运维面板 + 自研锁屏（状态机）

把 `hphelm` 的显示屏当「运维看板 + 锁屏」：不操作时显示资源面板，动一下键鼠进登录页，
认证通过回到桌面，再次空闲又回面板 —— **像屏保一样循环**。

**不再依赖 lightdm 的 greeter**：登录界面与认证都由本项目负责（认证走自建 setuid 助手 + PAM）。

## 行为（用户口径的四条）

| # | 行为 |
| --- | --- |
| ① | 长时间未操作 → 进入自己画的资源面板 |
| ② | 晃鼠标 / 点一下 / 按任意键 → 进入登录页 |
| ③ | 登录成功 → 回到桌面正常操作 |
| ④ | 再次长时间未操作 → 重新进入面板（循环） |

对应状态机（`src/panel.c`）：

```
DESKTOP ──idle≥panel_idle──▶ PANEL ──input(≥input_grace)──▶ LOGIN
   ▲                                                          │
   └──────────────────── 认证成功（PAM）──────────────────────┤
LOGIN ──idle≥login_back──▶ PANEL（密码框清空、清错误）
PANEL ──idle≥sleep_off──▶ DPMS 关屏；任意输入唤醒
```

面板态会 `XGrabKeyboard` 把键盘抓过来；**回桌面时必须放开**（`XUngrabKeyboard`），
否则桌面收不到任何按键 —— 这是实测踩到的坑，见下。

## 为什么不用 GUI 工具包（实测，同一台机 / 真桌面 / 同一份 1 Hz 工作量）

| 路线 | 实现 | PSS | CPU | 线程 |
| --- | --- | --- | --- | --- |
| **本项目** | **X11 + cairo + pango（C，无工具包）** | **7.4 MiB**（最小程序） | 0.08% | **2** |
| 轻量包 | FLTK | 8.4 MiB | 0.08% | 2 |
| 现成 | conky | 13.4 MiB | 1.33% | 6 |
| 工具包 | GTK3 + C | 17.6 MiB | 0.08% | 5 |
| 工具包 | Qt6 Widgets | 27.4 MiB | 0.42% | 3 |
| 工具包 | GTK3 + Python | 37.1 MiB | 0.08% | 5 |
| 工具包 | **GTK4 + C** | **119.3 MiB** | 0.50% | **22** |

语言维度（同一份 cairo+pango 工作量）：**C 14.1 MiB vs Rust 14.5 MiB（只差 0.4 MiB）**，
空转地板 0.1 vs 0.4 MiB。⇒ **内存的大头是工具包，不是语言。**

文字质量不吃亏：cairo + pango 就是 GTK 底下那同一套（cairo/pango/fontconfig/harfbuzz）。
实测：只给一个「只有拉丁字形」的字体族（DejaVu Sans）画「中文测试 汉字」，再换 Noto CJK 重画，
**只差 112 像素（0.001%）** ⇒ 字体回退生效，中文与 Nerd Font 图标正常。
另外自绘能精确 `cairo_scale(1.48)` 对上本机 141.8 DPI（GTK 在 X11 只能整数倍）。

**本程序实测**：`PSS 22.8 MiB · Private 13.7 MiB · 2 线程`（比最小程序多的是字体缓存与六块界面）。

## 文件与构建

| 文件 | 作用 |
| --- | --- |
| `src/theme.h` | Catppuccin Mocha 配色 + **版式常量**（逻辑像素） |
| `src/metrics.[ch]` | 数据采集：只读 `/proc`、`/sys`，**不 fork 子进程** |
| `src/view.[ch]` | 绘制基元（文字/背景渐变/细线/进度条/每核柱/sparkline），两个界面共用 |
| `src/view_panel.c` | **运维面板版面**（无卡片、三栏 + 进程榜） |
| `src/view_login.c` | **登录页版面**（无卡片、居中） |
| `src/auth.[ch]` | 与 setuid 助手通信（**密码只走 stdin，绝不进 argv/env**） |
| `src/hphelm-auth.c` | **唯一的特权代码**：setuid root，调 PAM `login` 栈验密码 |
| `src/panel.c` | X11 窗口、事件、状态机、主循环 |

```sh
make                 # 需要 pkg-config: x11 x11-xcb xscrnsaver cairo cairo-xlib pangocairo
sudo make install    # → /usr/local/bin/hphelm-panel
                     # → /usr/local/libexec/hphelm-auth（4755 root:root，setuid）

# 常用参数（默认值见 src/panel.c 顶部）
hphelm-panel --panel-idle 180     # 桌面空闲多久进面板（秒）
hphelm-panel --login-back 120     # 登录页空闲多久回面板（秒）
hphelm-panel --input-grace 1.2    # 进面板后多久才接受唤醒（防误触）
hphelm-panel --sleep-off 3600     # 面板空闲多久关屏（DPMS）
hphelm-panel --scale 1.48         # 缩放（物理/逻辑像素比）
hphelm-panel --lock /tmp/x.lock   # 单实例锁路径（默认 /tmp/hphelm-panel.lock）
                                  #   ⇒ 在 Xvfb 里测时用它，不必杀真机实例
hphelm-panel --state panel|login  # 强制初始状态（验证界面用）
hphelm-panel --dump               # 只打采集层数据后退出（不需要 X）
```

## 安装到会话

`/usr/local/bin/hphelm-session`（lightdm 的会话入口）里加一行：

```sh
/usr/local/bin/hphelm-panel >/tmp/hphelm-panel.log 2>&1 &
```

并且**不要**再在启动时 `dm-tool lock`（面板接手了锁屏）。

## 安全模型（重要）

- 特权面只有 `hphelm-auth`（~120 行），它**只做一件事**：从 stdin 读一行密码、PAM `login` 验一次、
  输出 `OK`/`FAIL 原因`。**不开 shell、不执行命令、不写文件**。
- **只允许验证调用者本人**（`argv[1]` 必须等于 `getuid()` 对应的用户名）⇒ 即使被滥用也不能横向验证别人。
- 密码**只走 stdin**：不进 argv（`ps` 可见）、不进环境（`/proc/pid/environ` 可见）。
  自检判据：`tr '\0' ' ' < /proc/$(pgrep -f hphelm-auth)/cmdline` 里不应出现密码。
- 认证与系统登录**同口径**（`pam_acct_mgmt` 会管账户过期/锁定）。
- 面板本身以普通用户跑；`setuid` 只在助手那 1 个文件上（`-rwsr-xr-x root:root`）。

## 已踩的坑

1. **未映射窗口上 `XSetInputFocus` 会 BadMatch 直接退出**（`Major opcode 42`）⇒ 必须先 `XMapRaised`。
2. **`XGrabKeyboard` 只在显示时抓、隐藏时必须 `XUngrabKeyboard` 并把焦点还给 `PointerRoot`**，
   否则回桌面后整个桌面收不到按键（本程序第一版就是这么写的，已修并加判据：隐藏后另起进程
   `XGrabKeyboard` 应返回 `GrabSuccess`）。
3. **`pkill -f <pattern>` 会匹配到执行它的那个 ssh shell 自己**（同一天踩了四次）⇒ kill 写进脚本文件。
4. **默认网卡不能靠 `readdir(/sys/class/net)` 的顺序**（实测先返回没接线的 `wlp1s0`）。判据是「有全局 IPv4」。
5. **进程榜只读一次 `/proc/<pid>/stat`** 就够（`(comm)` 与 rss 都在里面），比 comm+statm 少一半 fopen。
6. **`getifaddrs` 最贵** ⇒ IP 每 10 秒才刷一次。
7. ⭐ **不要直接往可见窗口上画 —— 没有双缓冲就会每秒闪一下。**
   原先 `cairo_xlib_surface_create(dpy, win, …)` 直接画窗口，每帧先 `cairo_paint` 清底再逐块画，
   cairo 分批下发 ⇒ **「刚清完底、还没画完」的中间态会被 picom/屏幕看到**。
   实测判据（抓窗口连续帧逐像素差分）：每秒两次「整屏 63% 像素变化」，中间帧 PNG 只有 67KB
   （前后 177KB）且只有底色没有盒子/文字。
   ⇒ 现在**所有绘制进离屏 `Pixmap`，每帧一次 `XCopyArea` 上屏**（见「渲染架构」）；
   顺带把这块 pixmap 设为窗口背景（暴露/映射不闪黑）。
8. ⭐ **`const double w[3] = {a, b, 0}` 会静默产生「零宽度栏」**（本轮踩到）：
   我为了修「const 数组不能赋值」把第三栏写成 `0`，于是网络栏宽度 = 0 ⇒ sparkline 被压成一根
   竖线、右对齐文字挤到左边缘，而**编译零告警、程序不崩**。
   判据：`magick <png> -crop <w>x1+<x>+<y> txt:-` 取一行色块游程，量实际几何 —— 比看图靠谱。
9. ⭐ **`XCopyArea` 用的 GC 必须关掉 `graphics_exposures`。**
   默认 `True` ⇒ 每次上屏回一个 `NoExpose` ⇒ `poll()` 立刻返回 ⇒ **主循环自激空转**：
   实测 35 it/s、CPU 0.8%→33~49%、采样窗口被压到 ~28ms ⇒ 面板上 CPU% 跳成 KTV；
   连带 `getifaddrs` 变成每 0.3s 一次、网络速率因 `difftime` 恒 0 永远算不出来。
   修法：`gcv.graphics_exposures = False; XCreateGC(dpy, win, GCGraphicsExposures, &gcv);`
10. ⭐ **不要用启动瞬间的采样值预填曲线**（等于伪造历史，且量程会被突发尖峰顶上去）——
    见上面「版式 · 曲线」。

## 版式（2026-10-18 第四轮：用户口径「不要卡片 / 更紧凑」）

**不用卡片**：内容直接铺在整屏渐变底上，分区只靠「一条 1px 细线 + 字号层级 + 留白」，
没有任何圆角框/底色块。区块标题不单独占行（label 与值同排、靠右对齐）。

```
┌ 顶栏（一行）───────────────────────────────────────────────────────┐
│ hphelm 192.168.5.50 up 8h06m 8 核 · eno1        2026-10-18 22:45 ● 24×7
├────────────────────────── 细线 ───────────────────────────────────┤
│ CPU            57.6°C │ 内存     1.0G/15.4G │ 网络      tailscale │
│ 1%  schedutil·1.2GHz  │ 7%                  │ ↓131B/s ↑80B/s      │
│ [█░░░░░░░░░░░░░░░░░░] │ [█░░░░░░░░░░░░░░░]  │                     │
│ ▁▂▃▅▂▁▂▃（每核带轨道）│ swap/SSD/磁盘        │ [↓↑ 双曲线 5min]    │
│ [CPU 5min 曲线]       │ [内存 5min 曲线]     │                     │
├────────────────────────── 细线 ───────────────────────────────────┤
│ 进程榜 共 219 个              按内存 (RSS) · 每 3 秒刷新            │
│ 1 Xorg  181M  11 fcitx5 47M  21 sshd 14M  31 systemd 12M          │
│ …（4 列 × 10 行 = 40 条，逐列向下，按内存降序）                      │
└───────────────────────────────────────────────────────────────────┘
```

**进程榜**（用户要的「更多、分列、几秒一刷」）：

- 条数 **40**（`TOPN`，4 列 × 10 行，逐列向下填、按 RSS 降序）；机器实测约 220 个进程，
  `read_procs` 本来就会遍历 `/proc` 后 `qsort`，**取前 6 还是前 40 的扫描成本一样**。
- 刷新 **每 3 秒**（采样 1 Hz，`nsamp % 3`）；`read_procs` 是面板里最贵的一项（遍历 `/proc`），
  实测 3 秒一刷对 CPU 无可测影响（整机 **0.60% of one core**，20s 内 utime+stime = 120ms）。
- 每行 `名次 · 进程名 · pid · 内存`，内存右对齐到本列右边界；口径明确标注 **RSS**（与 htop 一致）。

**曲线（sparkline）**：

- 量程用 `v_nice_ceil` 量化到 1/2/5×10ⁿ ⇒ **不会因为来了一个新高就整条曲线整体跳**；
  CPU/内存固定 0~100。
- ⚠️ **不要用「启动瞬间的采样值」预填曲线**（本轮踩到）：那个值来自 120ms 的采样窗口，
  实测常是 0、偶尔是突发尖峰 ⇒ 曲线要么贴底、要么被假尖峰把量程顶上去把真数据压扁，
  等于伪造 5 分钟历史。现在**不预填**，曲线从右侧诚实生长，并画一条**零基线**让空曲线区
  看起来是「0」而不是「坏了」。

## 渲染架构（双缓冲）

```
每帧： render()  ──全部绘制进离屏 Pixmap（back）──▶  cairo_surface_flush
         │                                              │
         └── 内容签名（FNV-1a over metrics+界面态）没变就直接 return（稳定 1 Hz）
                                                        ▼
                              blit(): 一次 XCopyArea(back → win)  ──▶  XFlush
```

- 窗口背景 = 后备 pixmap（`XSetWindowBackgroundPixmap`）⇒ 暴露/映射时服务器自己回填。
- `win_show()` 顺序：先 `render()` → `XMapRaised` → `XSync` → `blit()` → 设焦点/抓键。
- 尺寸变化（`ConfigureNotify`）⇒ 重建 pixmap + 强制重绘。
- 验收判据：抓窗口连续帧差分，`>30% 像素变化` 的帧应为 **0**。

## 排障：主循环仪表

```sh
HPHELM_PANEL_DEBUG=1 ./hphelm-panel --state panel     # 每秒一行
# 主循环 2 it/s · 事件 0 · poll[可读 0/超时 2/其他 0] · 重绘 1 · 采样 1 ·
#   类型: · 签名 0.1ms 绘制 3.5ms 上屏 0.1ms
```

「谁在叫醒主循环」看 `poll` 那一段与「类型」直方图 —— 本轮就是靠它发现 `NoExpose` 自激空转的。

## 待做

- ⬜ 「立即锁屏」快捷键（现在只能等空闲到阈值；可加一个 DBus 或信号触发的入口）
- ⬜ 密码错误次数限制 / 锁定（现在只有 2 秒冷却）
- ⬜ 面板内容补齐：服务概览、进程榜可选按 CPU 排序
- ⬜ UI 观感优化（候选见 `HANDOFF.md` 第四节：顶部 48px 空白、每核小柱、sparkline 自动缩放导致的
  视觉跳动、进程榜内存口径、登录页卡片位置、面板平涂 vs 登录页渐变、认证中缺进度反馈…）
- ⬜ ~~PangoLayout 缓存 / 区域局部重绘（降 CPU）~~ —— 优先级已降：实测 CPU **0.60% of one core**
  （绘制 ~3.5ms/帧、上屏 ~0.05ms、签名 ~0.05ms）
