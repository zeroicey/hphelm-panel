# 更新系统屏保（部署 hphelm-panel）

**Applies to:** `hphelm` 本机（GMKtec G10，Arch + X11 + lightdm(autologin) + bspwm）· **Owner:** 本项目的专职 AI · **Last verified:** 2026-10-21（三次真跑全绿）

## When to use this

- 你改了 `src/` 下的任何东西，想让它出现在工位屏幕上；
- 用户说「屏幕上还是老样子」/「面板没变」；
- 你要验证「装到机器上的东西」与「仓库里的东西」是否一致。

⚠️ **不需要**退出登录、不需要 `systemctl restart lightdm`、不需要重启机器。
「屏保」就是这个项目本身 —— 更新它 = 换掉 `/usr/local/bin/hphelm-panel` 并让守护循环重新拉起。

## Preconditions

- 你**在 hphelm 本机上**（`hostname` = hphelm），工作目录 `/data/dev/hphelm-panel`（源码目录 = 部署目录）。
- `pkg-config --exists x11 xscrnsaver cairo pangocairo` 通过；`sudo -n true` 通过（免密 sudo）。
- 你**没在** Xvfb 里跑着同名实例（在跑的话 `pgrep -cx hphelm-panel` 会 >1，先按 pid 收拾掉）。

## Procedure

```sh
cd /data/dev/hphelm-panel
tools/deploy.sh --check     # ① 只读：它会做什么
tools/deploy.sh             # ② 真的做：自检 → 构建 → 备份旧二进制 → 安装+校验 → --dump → 重启 → 判据
```

七阶段与每阶段的失败后果见 `docs/deploy.md` 第四节。手工等价（脚本坏了才用）：

```sh
make && sudo make install
./hphelm-panel --dump | tail -1        # 应含 sleep_off=off（不需要 X，不抢锁）
pkill -x hphelm-panel                  # 守护循环 ~1s 后用新二进制拉起；别用 pkill -f
```

## Verification

| 判据 | 期望 |
| --- | --- |
| `cat /sys/class/tty/tty0/active` | `tty7` |
| `pgrep -cx slick-greeter` | `0` |
| `pgrep -cx hphelm-panel` | `1` |
| `md5sum ./hphelm-panel /usr/local/bin/hphelm-panel` | 一致 |
| `./hphelm-panel --dump \| tail -1` | 含 `sleep_off=off` |
| `DISPLAY=:0 tools/probe-frames 40` | `>30% 变化帧 = 0` |
| `/proc/<pid>/stat` 14/15 字段，20 秒窗口 | < 1% of one core（现测 1.1~1.6%，见 CURRENT_TASK） |
| `tail -3 /tmp/hphelm-panel.log` | 只有 `rc=143` 与 `启动：`；当前实例**没有**「连续 3 次」 |

判据全绿 ⇒ 在 `HANDOFF.md` 记一行本轮结果（含 CPU 数字）；黄字提醒不阻断但要记。

## Rollback

```sh
# 只回滚二进制（最常见）
sudo install -m755 /usr/local/bin/hphelm-panel.prev /usr/local/bin/hphelm-panel
pkill -x hphelm-panel

# 系统文件 / 完全退回 lightdm greeter：见 docs/deploy.md 第七节（先停 hphelm-vt-guard.service）
```

`.prev` 只在「新构建 ≠ 已装」时由 `deploy.sh` 写入（保留上一版一份）。
