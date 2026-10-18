# 部署与回滚（hphelm）

> 部署目标：`hphelm`（GMKtec G10，ssh 别名 `hphelm`）。源码权威在 `hpcore:/data/dev/hphelm-panel`。
> 一条命令：`tools/deploy.sh`（先 `--check` 看会做什么）。
> 相关：[`architecture.md`](architecture.md) · [`verify.md`](verify.md) · [`pitfalls.md`](pitfalls.md)

---

## 一、装在哪（`hphelm` 上的文件）

| 路径 | 内容 | 谁管 |
| --- | --- | --- |
| `/usr/local/bin/hphelm-panel` | 主程序（~70KB，`ldd` 里 GUI 工具包数量 = 0） | `make install` |
| `/usr/local/libexec/hphelm-auth` | setuid 助手（`-rwsr-xr-x root:root`） | `make install` |
| `/etc/pam.d/hphelm-auth` | **专用** PAM 服务（只留 `pam_unix`） | 手工（一次性） |
| `/usr/local/bin/hphelm-session` | lightdm 会话入口（起面板 + 守护循环 + 日志轮转） | 手工（一次性） |
| `/usr/local/sbin/hphelm-vt-guard` + `.service` | 独立防线：被抢的 VT 拽回 tty7 | 手工（一次性） |
| `/tmp/hphelm-panel.log` | 运行日志（会话启动时按 >1MiB 轮转） | 自动 |

## 二、日常部署（改了代码）

```sh
cd /data/dev/hphelm-panel
tools/deploy.sh            # rsync 源码 → hphelm:~/projects/panel2 → ssh 上 make → sudo make install → 重启面板
tools/deploy.sh --check    # 只打印将执行的动作，不落地
```

脚本做的事等价于：

```sh
rsync -az --delete src tools Makefile README.md AGENTS.md HANDOFF.md docs/ hphelm:~/projects/panel2/
ssh hphelm 'cd ~/projects/panel2 && make && sudo make install'
ssh hphelm 'pkill -x hphelm-panel'      # 守护循环会在 ~1s 内用新二进制拉起
```

⚠️ **重启会露出桌面 1~2 秒**（面板 unmapped）—— 这是正常的，用户会看到；不要为此改架构。
⚠️ **`pkill -x` 而不是 `pkill -f`**（`-f` 会匹配到执行它的那个 ssh shell 自己，我们踩过四次）。

## 三、部署后必跑的判据

```sh
ssh hphelm 'cat /sys/class/tty/tty0/active'        # tty7
ssh hphelm 'pgrep -cx slick-greeter'               # 0
ssh hphelm 'pgrep -cx hphelm-panel'                # 1
ssh hphelm 'md5sum ~/projects/panel2/hphelm-panel /usr/local/bin/hphelm-panel'   # 一致
ssh hphelm 'tail -3 /tmp/hphelm-panel.log'
DISPLAY=:0 tools/probe-frames 60                   # >30% 像素变化帧 = 0
```

日志里出现下面任何一条 ⇒ **立刻停下来查**：

| 日志 | 含义 |
| --- | --- |
| `hphelm-panel 退出 rc=143 运行 Ns` | 正常（我们主动重启，SIGTERM） |
| `连续 3 次「起来就死」⇒ 关屏兜底` | 兜底开火了 ⇒ 面板在反复崩；**它只关屏，不会招回 greeter**（红线保护） |
| `状态 … → …` | 状态机变迁（正常） |

## 四、会话入口（`/usr/local/bin/hphelm-session`）关键片段

```sh
# ⛔ 永不出现 dm-tool / light-locker
xset s off
xset dpms 0 0 0          # DPMS 定时交给面板自己管

# 日志轮转（>1MiB 就 tail 到 256K；直接 rm 会删掉正在写的 inode）
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

## 五、PAM（一次性安装，别搞错）

```sh
# /etc/pam.d/hphelm-auth —— 只留 pam_unix，绝不 include login/system-auth
cat >/etc/pam.d/hphelm-auth <<'EOF'
auth     required pam_unix.so
account  required pam_unix.so
EOF
```

⛔ **绝不要写成 `@include login` 或 `system-local-login`**：那条链带 `pam_faillock`，
而 `lightdm-autologin` 的第一行也是 `pam_faillock.so preauth` ⇒ 锁屏输错 3 次会把账号锁上、
连坐 autologin 失败 ⇒ lightdm 起 greeter 并抢走 VT（**用户就会看到那个本该消失的旧登录页**）。

## 六、回滚

```sh
# ① 先停 VT 守卫（否则它会一直把 VT 往 tty7 拽）
sudo systemctl disable --now hphelm-vt-guard.service

# ② 回到「lightdm greeter 当锁屏」
sudo cp -a /usr/local/bin/hphelm-session.bak-prepanel /usr/local/bin/hphelm-session
cp -a ~/.config/sxhkd/sxhkdrc.bak-prepanel ~/.config/sxhkd/sxhkdrc
sudo /usr/local/bin/hphelm-lockd >/dev/null 2>&1 &     # 旧的空闲锁屏守护（二进制仍在）
pkill -x hphelm-panel
sudo systemctl restart lightdm
```

**整机级归档**：`/data/backups/hphelm-panel-20261018/`（panel2 完整 git 历史 + 四处系统配置 + `RESTORE.md`）。

## 七、改「会话入口」这类系统文件的纪律

1. **先备份**：`sudo cp -a <file> <file>.bak-$(date +%Y%m%d)`。
2. **改完立即验证红线**：`cat /sys/class/tty/tty0/active`（tty7）+ `pgrep -cx slick-greeter`（0）。
3. **不要 `systemctl restart lightdm` 来「试一下」** —— 那会杀掉当前图形会话（用户正在用的桌面）。
4. 任何「让面板消失」的改动都要先问：**用户会不会看到别的登录页？**
