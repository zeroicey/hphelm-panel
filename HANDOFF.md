# 交接：hphelm-panel（运维面板 + 自研锁屏）

> **本轮状态**：功能已全部跑通并在真机验收（用户确认「整个流程我已经看到跑通了」）。
> **下一轮目标**：UI 与细节优化（用户口径：「优化一下你的这个应用资源面板，UI 还有一些其他的地方」）。
> 最后更新：2026-10-18。

---

## 一、30 秒上手

```bash
ssh hphelm
cd ~/projects/panel2          # 源码（git 仓库，HEAD = f754cf1）
make                          # 编译（需 pkg-config: x11 x11-xcb xscrnsaver cairo cairo-xlib pangocairo）
sudo make install             # → /usr/local/bin/hphelm-panel + /usr/local/libexec/hphelm-auth(4755)
```

**看效果不需要重启**：面板已经常驻在跑，改动只要 `make && sudo make install` 然后
`kill -USR2 $(pgrep -x hphelm-panel)`（回面板）就能看到新界面。

**先用隔离环境试**（不动真屏，推荐每次都先这么干）：

```bash
Xvfb :9 -screen 0 1920x1080x24 -ac &
DISPLAY=:9 ./hphelm-panel --state panel     # 或 --state login
DISPLAY=:9 import -window "$(DISPLAY=:9 xdotool search --name hphelm-panel | tail -1)" /tmp/p.png
```

**不需要 X 的自检**：`./hphelm-panel --dump`（打印采集层数据）。

**验证渲染（我看不到图，所以走像素判据）**：

```bash
# 粗看布局（12x14 px 一格，亮度字符 + C=彩色）—— 脚本已在仓库里：tools/
python3 tools/render-ascii.py /tmp/p.png | head -50
# 细看区域（x0 y0 x1 y1 每格像素）
python3 tools/render-region.py /tmp/p.png 700 560 1230 780 4
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
12. **CPU 0.8%**：主要花在每秒约 50 段 pango 文字整形。两个杠杆：PangoLayout 缓存、区域局部重绘。
13. **面板只在 :0 上**：换接口（DP-2→HDMI）后尺寸变化由 `ConfigureNotify` 处理，但没实测过。

---

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

**渲染验证脚本已沉淀进仓库**：`tools/render-ascii.py`（整屏→ASCII）
与 `tools/render-region.py`（局部放大）。这是本项目开发的基本功 ——
**看不到图的时候，布局核对只能靠像素统计**，别凭想象改 UI。

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

---

## 七、下一轮开工前请先读

- `hphelm:~/projects/panel2/README.md`（本项目自述：行为、选型实测表、安全模型、6 个坑）
- kb `runbooks/hphelm.md` 的「运维面板（hphelm-panel）」节（机队视角 + 验收判据）
- kb 两个坑档：`2026-10-18-lockscreen-auth-reusing-login-pam-stack-*`、
  `2026-10-18-fullscreen-override-redirect-overlay-traps`
