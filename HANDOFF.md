# 交接：hphelm-panel（运维面板 + 自研锁屏）

> **本轮状态**：① 功能已全部跑通并在真机验收（用户确认「整个流程我已经看到跑通了」）；
> ② **UI 优化的第一刀已落地 = 消除「每秒闪一下」**（真机验收：`>30% 像素变化` 帧 4/40 → 0）；
> 期间我自己引入过一次 **`XCopyArea` 的 `NoExpose` 自激空转**（CPU 0.83%→4.9%、面板 CPU% 跳成 KTV），
> 同轮已根治（现 **0.08%**）。
> **下一轮目标**：继续 UI 观感优化（第四节清单，**以用户实际观感为准**）。
> 最后更新：2026-10-18（第二轮）。

---

## 一、30 秒上手

```bash
ssh hphelm
cd ~/projects/panel2          # 源码（git 仓库，HEAD = f754cf1）
make                          # 编译（需 pkg-config: x11 x11-xcb xscrnsaver cairo cairo-xlib pangocairo）
sudo make install             # → /usr/local/bin/hphelm-panel + /usr/local/libexec/hphelm-auth(4755)
```

**看效果需要重启进程**（`kill -USR2` 只切状态、**不会重载二进制**，上一版这里写错了）：

```bash
make && sudo make install
pkill -x hphelm-panel        # 会话包装器的守护循环会在 ~1s 内用新二进制把它拉回来
# ⚠️ 别用 `pkill -f`：它会匹配到执行它的那个 ssh shell 自己（已踩四次，见 README 坑 3）
# ⚠️ 重启会让桌面露出 1~2 秒（面板 unmapped）；`rc=143` 不计失败，红线 2 安全
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
| `src/view.c` | 230 | 绘制基元：文字（pango）/圆角框/进度条/sparkline | 偶尔 |
| `src/view_draw.c` | 230 | **两个界面的实际布局**：`view_panel_draw` / `view_login_draw` | ⭐⭐ 最常动 |
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

## 四、已知粗糙点（下一轮的候选）

**UI / 视觉**
1. **面板顶部 48px 的空白**：面板全屏覆盖了 polybar，所以锁屏时看不到工作区/时间。
   要么在面板顶栏里自己画这两样，要么把面板区域改成从 48px 开始（但那样 polybar 会露出来）。
2. **每核小柱看不清**：机器空闲时柱高≈0，只有 1px。可以改成「最近 1 分钟峰值」或加上基线色。
3. **sparkline 起步为空**：按 1 Hz 记录、5 分钟才铺满。可先预填一段假数据或缩短窗口。
4. **进程榜 RSS 与 PSS 口径不一致**：榜单是 RSS（和 htop 一致），而模块标题写「按内存排序」。
   可以标注口径，或改用 PSS（贵）。
5. **登录页卡片位置**：现在卡片中心在 `cy+40`，时钟在上方；整体偏「上下分布」，可以再收拢。
6. **没有错误态以外的反馈**：认证中只有「正在验证…」，可以加一个转圈或进度条。
7. **颜色**：`C_BASE` 大面积填充 `#1e1e2e`，与登录页的渐变风格不统一（面板是平涂，登录页是渐变）。

**功能 / 其他**
8. **「立即锁屏」只有信号**：`kill -USR1`。可以在 `sxhkd` 里包一个快捷键（`pkill -USR1 -x hphelm-panel`）。
9. **DPMS 关屏后唤不醒**：`sleep_off=3600` 后关屏，理论上任意输入会 `dpms(1)`；
   但实测未验证（需要等 1 小时）。可以调小 `--sleep-off` 测一次。
10. **面板内容还没「服务概览」**：mockup 里的「26 容器全绿 / 磁盘 / 备份」还没做（需要探测 hpcore 的容器）。
11. **多显示器未处理**：现在固定用 `DefaultScreen` 的整屏尺寸。
12. ~~**CPU 0.8%**：主要花在每秒约 50 段 pango 文字整形。两个杠杆：PangoLayout 缓存、区域局部重绘。~~
    **2026-10-18 重测：CPU 0.08%**（1 Hz 采样；绘制 ~3.5ms/帧、上屏 ~0.05ms、内容签名 ~0.05ms）
    ⇒ 这一条优先级已降。真要再降就先看 `HPHELM_PANEL_DEBUG=1` 的耗时分布。
13. **面板只在 :0 上**：换接口（DP-2→HDMI）后尺寸变化由 `ConfigureNotify` 处理，但没实测过。

---

## 四·五、本轮（第二轮）做完的事：**消闪 + 消除自激空转**

| # | 问题 | 根因 | 修法 | 判据 |
| --- | --- | --- | --- | --- |
| 1 | **面板每秒闪一下**（用户：「我这个屏幕现在会有一点闪……就是内存、网络、CPU 那个界面」） | 直接往窗口上画 = **没有双缓冲**：每帧先 `cairo_paint(C_BASE)` 清底、再逐块画，cairo 分批下发 ⇒ 中间态被 picom/屏幕看到 | 绘制全进**离屏 `Pixmap`**，每帧**一次 `XCopyArea`** 上屏；`XSetWindowBackgroundPixmap` 让服务器回填；`win_show()` 先画后映射；`ConfigureNotify` 重建 pixmap | 抓窗口连续帧差分：`>30% 像素变化` 帧 **4/40 → 0**；静止时 37/39 帧 `diffpx=0` |
| 2 | **面板 CPU% 跳成 KTV**（用户：「哥们我 cpu 在唱 k 啊 疯狂跳」） | ⚠️ **我自己引入的**：`XCreateGC(dpy,win,0,NULL)` 的 `graphics_exposures` 默认 True ⇒ 每次 `XCopyArea` 回一个 `NoExpose` ⇒ `poll()` 立刻返回 ⇒ 主循环自激空转 35 it/s ⇒ 采样窗口被压到 ~28ms | `gcv.graphics_exposures=False` + `XCreateGC(..., GCGraphicsExposures, &gcv)`；**另加两道与循环速率解耦的保险**：采样按时间节流 1 Hz、`dt` 改单调时钟亚秒、IP/进程榜按时间刷新 | 仪表 `poll[可读 0/超时 2]`、事件 0、内容 1 次/秒、CPU **4.91% → 0.08%** |

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

## 九、下一轮开工前请先读

- `hphelm:~/projects/panel2/README.md`（本项目自述：行为、选型实测表、安全模型、6 个坑）
- kb `runbooks/hphelm.md` 的「运维面板（hphelm-panel）」节（机队视角 + 验收判据）
- kb 两个坑档：`2026-10-18-lockscreen-auth-reusing-login-pam-stack-*`、
  `2026-10-18-fullscreen-override-redirect-overlay-traps`
