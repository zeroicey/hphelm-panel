# 架构：状态机 · 双缓冲 · 认证 · 数据采集

> 这份文件回答「它怎么跑起来的」。改交互、改渲染、加指标之前读它。
> 相关：[`design.md`](design.md)（版式与配色）· [`verify.md`](verify.md)（怎么验）· [`pitfalls.md`](pitfalls.md)（踩过的坑）

---

## 一、状态机（`src/panel.c`）

```
DESKTOP ──idle≥panel_idle(180s)──▶ PANEL ──input(≥input_grace 1.2s)──▶ LOGIN
   ▲                                                                    │
   └────────────────── 认证成功（PAM，见第四节）────────────────────────┘
LOGIN ──idle≥login_back(120s)──▶ PANEL（密码框清空、清错误）
PANEL ──idle≥sleep_off(0=永不)──▶ DPMS 关屏；任意输入唤醒（并进 LOGIN）
```

> **`sleep_off` 的 0 语义（2026-10-19）**：默认改成 **0 = 永不关屏**（用户要求看板 7×24 一直亮）。
> 实现上必须写 `CFG_SLEEP_OFF > 0 && idle >= CFG_SLEEP_OFF` —— **裸写 `idle >= CFG_SLEEP_OFF`
> 会让 0 变成「立即关屏」，语义正好反过来**（这是本轮改之前就埋着的坑）。

**空闲时间怎么来**：`XScreenSaverQueryInfo(dpy, root, info)` 的 `info->idle`（毫秒）。
**不要自己数鼠标事件** —— 面板只在自己显示时收到事件，桌面态收不到，`XScreenSaver` 是唯一的全局口径。

**三个状态的显隐语义**：

| 状态 | 窗口 | 键盘 | 焦点 | 备注 |
| --- | --- | --- | --- | --- |
| DESKTOP | unmapped | **必须 `XUngrabKeyboard`** | 还给 `PointerRoot` | 忘了放 grab ⇒ 整个桌面收不到按键（踩过） |
| PANEL | mapped + raised | grabbed | 面板窗口 | 每 2 tick `XRaiseWindow`（防 polybar 后映射压上来） |
| LOGIN | mapped + raised | grabbed | 面板窗口 | 同上 |

**信号入口**（运维用）：`kill -USR1` = 立即进 LOGIN；`kill -USR2` = 立即回 PANEL。

**开机必须 `--state panel`**：开机 idle=0，若从 DESKTOP 起步就会直接暴露桌面、丢掉「要密码」。

**单实例锁**：`flock /tmp/hphelm-panel.lock`（`--lock PATH` 可改）；已有实例则 `exit 3`，
会话包装器见到 `rc=3` 会 `sleep 30` 避免空转。
（两个实例会互相抢抬窗 ⇒ 屏幕变成两张界面互盖，而日志一切正常 —— 踩过。）

---

## 二、渲染：双缓冲 + 内容签名

```
每帧：
  content_sig()  ── FNV-1a over metrics_t + 界面态（state/pwlen/busy/cursor/err/倒计时）
      │ 没变 ⇒ return（不画、不上屏）
      ▼
  render()       ── 全部绘制进离屏 Pixmap（back）──▶ cairo_surface_flush
      ▼
  blit()         ── 一次 XCopyArea(back → win)    ──▶ XFlush
```

### 为什么必须离屏（否则会闪）

最初是 `cairo_xlib_surface_create(dpy, win, …)` 直接画窗口。每帧顺序是「先 `cairo_paint(C_BASE)`
把整屏涂底色 → 再逐块画卡片/文字」，cairo 分批把请求发给 X 服务器，**中间态是真实可见的**
（picom 开了 `use-damage=true`，随时可能抓到「刚清完底、还没画完」的那一帧）⇒ 每秒闪一下。

实测判据：抓窗口连续帧逐像素差分 ⇒ 每秒两次「整屏 63% 像素变化」，中间帧 PNG 只有 67KB
（前后 177KB）且只有底色没有盒子/文字。

### 三个实现细节（都别回退）

1. **GC 必须关 `graphics_exposures`**：
   ```c
   XGCValues gcv; gcv.graphics_exposures = False;
   blitgc = XCreateGC(dpy, win, GCGraphicsExposures, &gcv);
   ```
   默认 `True` ⇒ 每次 `XCopyArea` 回一个 `NoExpose` ⇒ `poll()` 立刻返回 ⇒ **主循环自激空转**
   （实测 35 it/s、CPU 0.8%→33~49%、采样窗口压到 ~28ms ⇒ 面板上的 CPU% 跳成 KTV）。

2. **窗口背景 = 后备 pixmap**（`XSetWindowBackgroundPixmap`）⇒ 暴露/映射时服务器自己回填，不闪黑。

3. **`win_show()` 顺序**：先 `render()` → `XMapRaised` → `XSync` → `blit()` → `XSetInputFocus` → `XGrabKeyboard`。
   ⚠️ 顺序不能换：对**未映射**窗口调 `XSetInputFocus` 会 `BadMatch` 并**直接终止进程**（`Major opcode 42`）。

尺寸变化（`ConfigureNotify`）⇒ `surface_init()` 重建 pixmap + `redraw_force()`。

---

## 三、采样：为什么一切都按「时间」节流

`metrics_sample(&M, want_procs)` 只读 `/proc`、`/sys`，**不 fork 子进程**。约定：

| 项 | 频率 | 为什么 |
| --- | --- | --- |
| 全量采样 | **1 Hz**（主循环按 `now_s()` 节流） | 采样周期 = CPU/网络速率的计算窗口 |
| 进程榜（`read_procs`，遍历 `/proc`） | 每 3 秒（`nsamp % 3`） | 面板里最贵的一项；实测 3 秒一刷无可测影响 |
| `getifaddrs`（IP） | 每 10 秒 | 最贵的一次系统调用（netlink 全量转储），IP 几乎不变 |
| 显示刷新 | 内容变了才画（时钟每秒变 ⇒ 稳定 1 Hz） | 见上一节的内容签名 |

**`dt` 用单调时钟（亚秒）**，不要用 `time_t` 的整秒差：采样快于 1 Hz 时 `difftime` 恒为 0
⇒ 网络速率永远算不出来（踩过，当时是自激空转把循环带到 35 Hz）。

**性能细节**：
- 别「每核打开一次 `/proc/stat`」——单次遍历拿汇总 + 所有核（8 核省 8 次 fopen）。
- 进程榜只读一次 `/proc/<pid>/stat`（`(comm)` 与 rss 都在里面），比 comm+statm 少一半 fopen。
- `read_procs` 的 `all[512]` 必须是 `static`（别把 20KB 放栈上，这函数每秒都调）。

---

## 四、认证：唯一的特权代码

```
面板（普通用户） ──写密码到 stdin──▶ hphelm-auth（setuid root, ~120 行）
                                        │ pam_start("hphelm-auth") → pam_authenticate → pam_acct_mgmt
                                        ▼
                                   stdout: OK / FAIL 原因
```

| 项 | 内容 |
| --- | --- |
| 位置 | `/usr/local/libexec/hphelm-auth`（`-rwsr-xr-x root:root`） |
| 做什么 | **只做一件事**：从 stdin 读一行密码 → PAM 验一次 → 输出 `OK`/`FAIL 原因`。**不开 shell、不执行命令、不写文件** |
| 限制 | `argv[1]` **必须等于 `getuid()` 对应的用户名** ⇒ 就算被滥用也不能横向验证别人 |
| 密码安全 | **只走 stdin**：不进 argv（`ps` 可见）、不进 env（`/proc/pid/environ` 可见） |
| PAM 服务 | ⚠️ **专用** `/etc/pam.d/hphelm-auth`，只留 `pam_unix`。**绝不复用 `login` 栈** |
| 防爆破 | 面板自己做指数退避（2/4/8/16/32 秒），与 DM 的 faillock 完全解耦 |

**为什么绝不复用 `login` 栈**：`login → system-local-login → system-auth` 带 `pam_faillock`，
而 `lightdm-autologin` 的第一行也是 `pam_faillock.so preauth` ⇒ 在锁屏上输错 3 次会把账号锁上、
**连坐 autologin 失败** ⇒ lightdm 退回 greeter 并把 VT 切走（踩过，见 `pitfalls.md`）。

**自检（不需要密码）**：
```sh
/usr/local/libexec/hphelm-auth                  # FAIL usage
/usr/local/libexec/hphelm-auth root             # FAIL 只允许验证调用者本人
echo wrong | /usr/local/libexec/hphelm-auth $USER   # FAIL Authentication failure（说明 PAM 真的跑了）
```

---

## 五、数据来源一览

| 指标 | 来源 | 注意 |
| --- | --- | --- |
| CPU 总量/每核 | `/proc/stat` 一次遍历 | 差值算法，窗口 = 采样周期 |
| load | `/proc/loadavg` | |
| 温度 | `/sys/class/hwmon/*/name == k10temp` → `temp1_input` | **本机硬件**；换机器要改 |
| governor / 频率 | `/sys/devices/system/cpu/cpu0/cpufreq/` | |
| 内存 / swap(zram) | `/proc/meminfo`（`MemAvailable`、`SwapTotal/Free`） | zram 也走 SwapTotal |
| 磁盘 | `statvfs("/")` + `/sys/block/nvme0n*/stat`（扇区×512） | **设备名是本机的** |
| 网络速率 | `/sys/class/net/<iface>/statistics/{rx,tx}_bytes` | 差值 / dt |
| 网卡选择 | 遍历 `/sys/class/net`，**判据是「有全局 IPv4」** | ⚠️ 不能靠 `readdir` 顺序（实测先返回没接线的 `wlp1s0`） |
| IP / tailscale | `getifaddrs` | 最贵 ⇒ 10 秒一次 |
| 进程榜 | `/proc/<pid>/stat`（comm + rss）| RSS 口径，与 htop 一致；不是 PSS（PSS 要扫 smaps，太贵）|
