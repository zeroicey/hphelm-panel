#!/bin/sh
# deploy.sh —— 把本项目部署到 hphelm（含源码同步、构建、安装、重启、判据）
#
#   tools/deploy.sh --check    只打印将执行的动作（默认只读）
#   tools/deploy.sh            真的部署
#
# ⚠️ 重启面板会让桌面露出 1~2 秒（面板 unmapped），这是正常的。
# ⚠️ 这里只用 `pkill -x`（绝不用 `pkill -f`：会匹配到执行它的 shell 自己）。
# ⚠️ 绝不用 dm-tool / light-locker（会把 lightdm 的 greeter 招回来并抢走 VT）。
set -eu

HOST=${HPHELM_HOST:-hphelm}
DEST=${HPHELM_SRC:-projects/panel2}
CHECK=0
[ "${1:-}" = "--check" ] && CHECK=1

REPO=$(cd "$(dirname "$0")/.." && pwd)
say() { printf '\033[1m==> %s\033[0m\n' "$*"; }
run() { if [ "$CHECK" = 1 ]; then printf '    [dry] %s\n' "$*"; else sh -c "$*"; fi; }

say "源码：$REPO  →  $HOST:~/$DEST"
say "1/4 同步源码"
run "rsync -az --delete \
  --exclude '.git/' --exclude '*.o' --exclude 'hphelm-panel' --exclude 'hphelm-auth' \
  --exclude 'assets/' --exclude '*.bundle' --exclude '.venv/' \
  '$REPO/' '$HOST:$DEST/'"

say "2/4 构建 + 安装（make && sudo make install）"
run "ssh '$HOST' 'cd ~/$DEST && make 2>&1 | tail -5 && sudo make install 2>&1 | tail -2'"

say "3/4 重启面板（守护循环会自动用新二进制拉起）"
run "ssh '$HOST' 'pkill -x hphelm-panel || true'"
run "sleep 4"

say "4/4 判据"
if [ "$CHECK" = 1 ]; then
    printf '    [dry] 检查 tty / greeter / 单实例 / 二进制一致 / 不闪\n'
else
    ssh "$HOST" '
      echo "  VT       : $(cat /sys/class/tty/tty0/active)"
      echo "  greeter  : $(pgrep -cx slick-greeter || true)  (期望 0)"
      echo "  panel    : $(pgrep -cx hphelm-panel || true)  (期望 1)"
      echo "  二进制   : $(md5sum ~/projects/panel2/hphelm-panel /usr/local/bin/hphelm-panel | awk "{print \$1}" | uniq | wc -l) 个不同哈希（期望 1）"
      echo "  最近日志 :"; tail -3 /tmp/hphelm-panel.log | sed "s/^/    /"
    '
    if command -v xdotool >/dev/null 2>&1 && [ -n "${DISPLAY:-}" ]; then
        say "帧差分（判有没有闪）"
        "$REPO/tools/probe-frames" 40 30 | tail -1
    else
        printf '    （无 DISPLAY，跳过帧差分；在 hphelm 本机跑 tools/probe-frames 40）\n'
    fi
fi

say "完成。别忘了：改了行为/版式 ⇒ 同步 docs/ + HANDOFF.md，然后 git commit。"
