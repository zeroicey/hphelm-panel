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
| `src/theme.h` | Catppuccin Mocha 配色 + 布局常量（逻辑像素） |
| `src/metrics.[ch]` | 数据采集：只读 `/proc`、`/sys`，**不 fork 子进程** |
| `src/view.[ch]` | 绘制基元（文字/圆角框/进度条/sparkline），面板与登录页共用 |
| `src/view_draw.c` | 两个界面：运维面板、登录页 |
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

## 待做

- ⬜ 「立即锁屏」快捷键（现在只能等空闲到阈值；可加一个 DBus 或信号触发的入口）
- ⬜ 密码错误次数限制 / 锁定（现在只有 2 秒冷却）
- ⬜ 面板内容补齐：服务概览、进程榜可选按 CPU 排序
- ⬜ PangoLayout 缓存 / 区域局部重绘（降 CPU）
