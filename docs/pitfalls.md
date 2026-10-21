# 已踩的坑（16 条，每条带判据）

> 这个项目最贵的资产是这张表 —— 每一条都是**真机实测**踩出来的，而且大部分**不报错、不崩溃**。
> 改代码前扫一眼；踩到新坑请追加（带判据），并在 `HANDOFF.md` 记一行。
> 机队视角的同类记录在 `kb`（主副本在 `hphelm:/data/kb`）：`pitfalls/2026-10-18-*.md` 若干篇。

---

## A. 渲染与 X11

### 1. 不要直接往可见窗口上画（没有双缓冲就会每秒闪一下）

`cairo_xlib_surface_create(dpy, win, …)` 直接画窗口时，每帧「先 `cairo_paint` 清底 → 再逐块画」
的中间态会被 picom/屏幕看到。
**判据**：抓窗口连续帧差分 ⇒ 单帧 >30% 像素变化、连续两帧都变、中间帧 PNG 明显更小（只有底色）。
**正解**：绘制全进离屏 `Pixmap`，每帧**一次 `XCopyArea`** 上屏；窗口背景 = 那块 pixmap。

### 2. `XCopyArea` 的 GC 必须关掉 `graphics_exposures`

默认 `True` ⇒ 每次上屏回一个 `NoExpose` ⇒ `poll()` 立刻返回 ⇒ **主循环自激空转**：
实测 35 it/s、CPU **0.8% → 33~49%**、采样窗口被压到 ~28ms ⇒ 面板上 CPU% 跳成 KTV；
连带 `getifaddrs` 变成每 0.3s 一次、网络速率因 `difftime` 恒 0 永远算不出来。
**判据**：仪表里 `poll[可读 N/超时 0]` 且事件类型全是 `NoExpose`。
**正解**：`gcv.graphics_exposures = False; XCreateGC(dpy, win, GCGraphicsExposures, &gcv);`

### 3. 对**未映射**窗口 `XSetInputFocus` 会 `BadMatch` 并直接终止进程

`Major opcode 42`。**正解**：`XMapRaised` → `XSync` → 再设焦点/抓键。
（`win_show()` 现在的顺序就是为此定的：先 `render()` → `XMapRaised` → `XSync` → `blit()` → 焦点 → grab。）

### 4. `XGrabKeyboard` 只在显示时抓、隐藏时必须放

第一版只在启动抓了一次 ⇒ **回桌面后整个桌面收不到任何按键**。
**判据**：隐藏后另起进程 `XGrabKeyboard` 应返回 `GrabSuccess`（面板显示时应为 `AlreadyGrabbed`）。

### 5. `override-redirect` 窗口的堆叠 = 映射顺序，WM 不仲裁

会话包装器里 `(sleep 2; exec polybar)` 比面板晚映射 ⇒ polybar 压在面板上。
**正解**：锁定态每 2 tick `XRaiseWindow`。
⚠️ 查堆叠顺序时两个工具**方向相反**：`XQueryTree` 是 bottom→top、`xwininfo -root -children` 是 top→bottom。
**别背方向，用「裁顶部 48px 看谁有墨迹」这个与顺序无关的判据。**

### 6. 两个实例会互相抢抬窗

屏幕变成两张界面互盖，而**日志一切正常**。
**正解**：`flock /tmp/hphelm-panel.lock` 单实例锁（`--lock` 可换路径）；包装器遇 `rc=3` 要 `sleep 30`。

### 7. `const double w[3] = {a, b, 0}` 会静默产生「零宽度栏」

修「const 数组不能赋值」时顺手把第三栏写成 `0` ⇒ 该栏宽度 = 0 ⇒ sparkline 的 300 个采样点
全叠在同一个 x 上（画成一根竖线）、右对齐文字挤到左边缘，而**编译零告警、程序不崩**。
**判据**：按行取色块游程量实际几何（`tools/measure.py --runs`）。

---

## B. 数据与显示

### 8. 不要用「启动瞬间的采样值」预填曲线

为治「曲线起步为空」而 `ring_prefill(ring, 当前值)` ⇒ 那个值来自启动时 120ms 的窗口，
实测常是 **0**、偶尔是**突发尖峰**（实测预填 ~530B/s 而真实只有 ~130B/s）⇒
曲线要么贴底、要么被假尖峰把量程顶上去把真数据压扁。**这是伪造历史。**
**正解**：不预填；空态用**零基线** + 曲线从右侧诚实生长；真要铺满就缩短窗口。

### 9. 曲线量程必须稳定，否则整条曲线会「整体跳」

按「历史最大值」线性缩放时，最大值一变整条曲线就上下弹一下（用户看成「在抖」）。
**正解**：`v_nice_ceil()` 吸附到 1/2/5×10ⁿ；CPU/内存固定 0~100。

### 10. 采样必须按**时间**节流，速率必须用**单调时钟**

- 「每轮循环采样一次」⇒ 循环一被事件带快，采样窗口就变成几十毫秒 ⇒ CPU% 变成噪声。
- 用 `time_t` 的整秒差算速率 ⇒ 采样快于 1 Hz 时 `difftime` 恒为 0 ⇒ 速率永远算不出来。
**正解**：`if (now_s() - t_sample >= 1.0)` + `CLOCK_MONOTONIC` 亚秒 dt。

### 11. 默认网卡不能靠 `readdir(/sys/class/net)` 的顺序

实测先返回没接线的 `wlp1s0` ⇒ IP 空、面板一片 down。**判据是「有全局 IPv4」。**

### 12. 性能细节三则

- 别「每核打开一次 `/proc/stat`」——单次遍历拿汇总 + 所有核。
- 进程榜只读一次 `/proc/<pid>/stat`（`(comm)` 与 rss 都在里面）。
- `getifaddrs` 最贵（netlink 全量转储）⇒ IP 每 10 秒刷一次；`read_procs` 的 `all[512]` 放 `static`。

---

## C. 运维与自伤（**这部分最危险**）

### 13. 认证绝不能复用 `login` PAM 栈

`login → system-local-login → system-auth` 带 `pam_faillock`；而 `lightdm-autologin` 第一行也是
`pam_faillock.so preauth` ⇒ 锁屏上输错 3 次把账号锁上 ⇒ **连坐 autologin 失败** ⇒
lightdm 退回 greeter 并把 VT 切到 tty8 ⇒ 自研面板被藏在后面（用户看到旧登录页）。
**正解**：专用 `/etc/pam.d/hphelm-auth`（只留 `pam_unix`）；防爆破由面板自己做指数退避。
**判据**：`tty0/active` = 会话那个 VT + `pgrep -c slick-greeter` = 0。

### 14. 守护/兜底逻辑的三个自伤（**一天内让用户两次看到不该出现的旧登录页**）

① 把「我主动 SIGTERM 重启」计成失败（`pkill` ⇒ rc=143），攒到 3 次触发兜底；
② 兜底选了 `dm-tool lock` ⇒ lightdm 起 greeter 并切 VT ⇒ 面板被藏起来；
③ 批量清理 `rm /tmp/*.log` 命中运行中程序正在写的日志（不报错，只是日志从此不可见）。

**正解**：
- 守护循环**只把「运行 <5 秒即退出」当失败**（判据用运行时长，不用退出码）；
- 兜底**只关屏 + 大声记日志，绝不把控制权交给 DM**；
- 日志按大小轮转（`tail -c` 再 `mv`），**不要 `rm`**；
- 独立防线 `hphelm-vt-guard`（活跃 VT≥8 且 greeter 在跑且 bspwm 在跑 ⇒ `chvt 7`；
  边界这么窄是为了不干扰 `Ctrl+Alt+F2` 救援 TTY）。
- ⚠️ **回滚/改会话入口前先停 VT 守卫。**

### 15. 绝不用 `light-locker` / `dm-tool lock` —— 与 lightdm 构成「两把锁」，且有两个静默陷阱

> **来源**：v1（`feat: hphelm 运维面板首版` 那条血缘）的 `tools/hphelm-lockd`（Python 空闲锁屏守护）。
> v2 把锁屏收进面板自己（`--lock` + 自研 PAM），那份 Python 实现已废；但下面三条**实测结论**与实现无关，删掉就会重踩。
> 归档于 2026-10-19 删除 `hphelm-panel-legacy-20261018` 之前（取「抢救知识再删」口径）。

**① `light-locker` 与 lightdm 是「两把锁」**：lightdm 解锁时会自己 `Unlock`（日志 `Unlocking login1 session`），
但 light-locker 的状态没被同步 ⇒ 它立刻又要求上锁 ⇒ **用户必须登录两遍**。实测证据链：

```
[2067s] Authentication complete → Unlocking login1 session 418 → Activating VT 7
[2068s] Creating greeter session        ← 1 秒后又建 greeter
[2069s] Locking login1 session 418      ← 立刻又锁上 ⇒ 第二次登录
```

**② `dm-tool lock` 缺 `XDG_SEAT_PATH` 时会静默失败**：该变量由 DM 注入，手工从 ssh 起守护时没有 ⇒
报 `Not running inside a display manager, XDG_SEAT_PATH not defined` 后**什么也不做**（不报错、不锁屏）。

**③ 座位已锁时再发一次锁请求 ⇒ lightdm 会再新建一个 greeter X 服务器**（实测 VT 8→9→10 累积，**状态泄漏**）。

**正解**：锁屏**完全由面板自己负责**（`hphelm-panel --lock`），**绝不把控制权交给 DM** ——
这同时是第 13、14 条的同一条结论。若要自写空闲守护，必须①按**时间**判空闲（解锁后 idle 立刻归零 ⇒
不会刚解锁又锁）②发锁前先判「是否已锁」，已锁就跳过。

**判据**：`pgrep -c light-locker` = 0、`pgrep -c dm-tool` = 0；锁屏/解锁一轮后 `loginctl` 里
**不得**出现新的 greeter session，且活跃 VT 保持为会话那一个。

---

## D. 与「我们机队」相关的坑（别当成通用知识）

### 16. `sleep_off = 0` 曾被写成「立即关屏」（默认值一改就翻车）

原实现是裸比较 `if (idle >= CFG_SLEEP_OFF && !screen_off)`。想关掉这个功能的人自然会写
`--sleep-off 0`，但 `idle >= 0` **恒真** ⇒ 进 PANEL 立刻 `xset dpms force off`，语义正好反过来。
（本轮之前默认值是 3600，所以没人踩到；2026-10-19 把默认改成「永不关屏」时才必须同时改判据。）

**正解**：`if (CFG_SLEEP_OFF > 0 && idle >= CFG_SLEEP_OFF && !screen_off)`，即 **0 或负数 = 永不关屏**。
**判据**：`hphelm-panel --dump`（或启动日志首行）打印 `sleep_off=off（永不关屏）`；
再 `--sleep-off 1 --state panel` 在 Xvfb 里跑 ⇒ 日志应出现 `面板空闲 Ns ⇒ 关屏（DPMS）`
（证明正数路径仍可用）；而 `--sleep-off 0` 跑 10 秒日志里**不得**出现该行。

- 面板**只在活跃 VT 上可见**：`loginctl activate` 被 polkit 拒 ⇒ 只能靠「不让 DM 起 greeter」。
- `xset dpms 0 0 0` 必须由会话设：DPMS 定时关屏交给面板自己管，否则 X 会在 11 分钟把面板涂黑。
- 用户口径就是验收口径：「长时间未操作→面板，动一下→登录页」——**面板必须能自己画登录页**，
  否则「回到 DM greeter」等于用户看到两个不同的登录界面（这就是那两次事故的本质）。

---

## E. 2026-10-21 加第二台主机（生产服务器）那一轮

> 这三条的共同点：**代码跑得很正常、日志很干净、帧差分 0、判据全绿，但屏幕上/数字上就是错的**。
> 完整案例在 `.ai/pitfalls/cases/` 的两个新文件里，这里只留判据。

### 17. 在别人满载的机器上量 CPU，数字全部作废

`tools/deploy.sh` 报 `CPU=1.33%`（预算 <1%）之后我做了三轮「优化」，还写了 A/B 对照脚本 ——
对照表长这样：旧版 1.15% / 新版 1.90%，下一轮旧版 2.20% / 新版 1.35%。
**同一版本两轮差一倍、旧版比新版还慢，这张表本身就在报警。**

真相是当时这台机上正在跑无关的 `go install`（编译整棵 nuclei/katana 依赖树）：
`load average: 22.99, 10.19, 4.54`（8 核机）。

**判据（以后一律这么量）**：
- 报 CPU 必须同时写 **窗口长度 + load average + 是否含 `--remote`**
- 优化前后对比必须**同一窗口内交替**（A10s B10s A10s B10s，取中位数），不许串行两轮
- 同一份代码数字波动 > 50% ⇒ **先怀疑环境**，别先怀疑代码

### 18. 三列版式的三个静默错版

① **纵向坐标按「相对」写、实际当「绝对」用** ⇒ CPU 标题跑到主机名上面、整列下移 100px、
最底部被推出屏幕；② **`-1` 兼职两种语义**（本机 / 远端未连上）⇒ 本机列被早退分支截断，
只画两行；③ **进程名截断切裂 UTF-8** ⇒ 中文名画豆腐块。

**判据**：
- 改版式后必须重跑 `--bands` 量竖带起点，和 `theme.h` 的纵向预算表**逐行对**
- 加一个「故意配错主机名」的截图对照：`--remote no-such-host-xyz` 时**本机列必须与在线时一模一样**
- 截断函数要有单测：中文名 + 三个宽度，输出必须能正常渲染

> **加宽/加列这类改动不能复用上一版的判据。** 编译零告警 + 帧差分 0 + 进程不崩，
> 完全不能说明版面是对的。

### 19. 配置行被新日志挤出 `tail -1`

`--dump` 原来把配置行放在最后，部署脚本用 `| tail -1` 取它判 `sleep_off=off`。
本轮给 `--dump` 加了「真的拉一帧生产服务器」的输出 ⇒ 配置行不再是最后一行 ⇒
**部署脚本立刻假失败**（`sleep_off 不是 off`，其实一切正常）。

**判据**：外部脚本取 `--dump` 的某个字段一律**按前缀 grep**，不要依赖行序：
`printf '%s\n' "$dump" | grep '^panel_idle=' | tail -1`。
