#!/bin/sh
# deploy.sh —— 把源码变成「系统屏保」：在 hphelm 本机构建 → 安装 → 重启 → 判据
#
#   tools/deploy.sh --check      只打印将执行的动作（只读，不落地）
#   tools/deploy.sh              真的部署（会重启面板 ⇒ 桌面露出 1~2 秒，正常）
#   tools/deploy.sh --no-restart 只构建 + 安装，不碰正在跑的面板（下次登录生效）
#
# 前提（2026-10-21 起）：**在 hphelm 本机跑**，源码目录就是部署目录
#   /data/dev/hphelm-panel —— 所以没有 rsync、没有 ssh 自己连自己。
#   （旧版 `ssh hphelm 'make && sudo make install'` 在 hphelm 上必失败：
#    ~/.ssh/config 里没有 hphelm 这个 Host，`Host key verification failed`。）
#
# ⚠️ 三条硬规矩（踩过，别改）：
#   · 只用 `pkill -x`，绝不用 `pkill -f`（-f 会匹配到执行它的 shell 自己）
#   · 绝不用 dm-tool / light-locker（会把 lightdm 的 greeter 招回来并抢走 VT）
#   · 重启是 SIGTERM ⇒ 退出码 143，守护循环按「运行时长」判失败，不会触发兜底
#
# 判据与回滚的完整说明：docs/deploy.md
set -eu

SRC=$(cd "$(dirname "$0")/.." && pwd)
CHECK=0
RESTART=1

for a in "$@"; do
    case "$a" in
        --check)      CHECK=1 ;;
        --no-restart) RESTART=0 ;;
        -h|--help)    sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) printf '未知参数：%s（试用 --check / --no-restart）\n' "$a" >&2; exit 2 ;;
    esac
done

say()  { printf '\033[1m==> %s\033[0m\n' "$*"; }
NOTES=0
warn() { printf '\033[33m    ! %s\033[0m\n' "$*"; NOTES=$((NOTES+1)); }
dry()  { printf '    [dry] %s\n' "$*"; }
die()  { printf '\033[31m失败：%s\033[0m\n' "$*" >&2; exit 1; }
ok()   { printf '\033[32m    OK %s\033[0m\n' "$*"; }

INSTALLED=/usr/local/bin/hphelm-panel
PREV=/usr/local/bin/hphelm-panel.prev
AUTH_INSTALLED=/usr/local/libexec/hphelm-auth

# ---------------------------------------------------------------- 1/7 环境自检
say "1/7 环境自检"
printf '    源码目录 : %s\n' "$SRC"
printf '    本机     : %s\n' "$(hostname)"
printf '    DISPLAY  : %s\n' "${DISPLAY:-（未设，判据里的帧差分将被跳过）}"

[ -f "$SRC/Makefile" ] || die "$SRC/Makefile 不存在 —— 请在项目根或 tools/ 下运行"
pkg-config --exists x11 x11-xcb xscrnsaver cairo cairo-xlib pangocairo \
    || die "缺构建依赖。装：sudo pacman -S --needed base-devel libx11 libxcb libxss cairo pango"
command -v make >/dev/null || die "没有 make"
request() {
    # 这些是「部署环境」的前提，不是本脚本能替你装的东西
    [ "$(cat /sys/class/tty/tty0/active 2>/dev/null)" = "tty7" ] || warn "活跃 VT 不是 tty7（可能是别的原因，见 docs/deploy.md 判据）"
    # ⚠️ 别写 `pgrep -qx`：procps 的 pgrep 没有 -q 选项，它会报错并返回非 0 ⇒ 假告警
    pgrep -x lightdm >/dev/null 2>&1 || warn "lightdm 没在跑 —— 这不是 hphelm 的图形会话？"
}
if [ "$CHECK" = 1 ]; then
    printf '    [dry] 检查 pkg-config / make / sudo -n / tty / lightdm\n'
else
    request
    sudo -n true 2>/dev/null || die "sudo 需要密码：本流程要求免密 sudo（oicey 本来就有）；不要交互式跑部署"
fi

# ------------------------------------------------------------------- 2/7 构建
say "2/7 构建（make）"
if [ "$CHECK" = 1 ]; then
    dry "make -C $SRC"
else
    log=$(mktemp)
    if ! make -C "$SRC" >"$log" 2>&1; then
        cat "$log"; rm -f "$log"; die "构建失败（输出见上）"
    fi
    n=$(grep -c 'warning:' "$log" || true)
    [ "$n" = 0 ] && ok "构建通过，0 警告" || { warn "构建有 $n 条警告（不阻断，但别把它当正常）"; grep 'warning:' "$log" | head -5; }
    rm -f "$log"
    printf '    产物     : %s（%s 字节）\n' "$SRC/hphelm-panel" "$(stat -c %s "$SRC/hphelm-panel")"
    printf '    GUI 工具包: %s 个（期望 0）\n' "$(ldd "$SRC/hphelm-panel" | grep -cE 'libgtk|libQt|libfltk' || true)"
fi

# ------------------------------------------------------------ 3/7 备份旧二进制
say "3/7 备份当前已装的二进制（回滚用）"
if [ -f "$INSTALLED" ]; then
    if [ "$CHECK" = 1 ]; then
        dry "cp -a $INSTALLED $PREV   （仅当内容不同）"
    else
        if cmp -s "$INSTALLED" "$SRC/hphelm-panel"; then
            printf '    已装的与新构建的相同，不覆盖 %s\n' "$PREV"
        else
            sudo cp -a "$INSTALLED" "$PREV"
            ok "旧二进制存到 $PREV（回滚见 docs/deploy.md 第六节）"
        fi
    fi
else
    printf '    当前没有已装版本（首次部署）\n'
fi

# ------------------------------------------------------------------- 4/7 安装
say "4/7 安装（sudo make install）"
if [ "$CHECK" = 1 ]; then
    dry "sudo make -C $SRC install"
else
    sudo make -C "$SRC" install >/dev/null
    ok "已安装 $INSTALLED + $AUTH_INSTALLED"
    cmp -s "$SRC/hphelm-panel" "$INSTALLED" || die "安装后二进制内容不一致（源码 %s vs 已装 %s）" "$SRC/hphelm-panel" "$INSTALLED"
    ok "源码与已装二进制逐字节一致"
    [ "$(stat -c '%a %U:%G' "$AUTH_INSTALLED")" = "4755 root:root" ] \
        && ok "认证助手权限 4755 root:root" \
        || die "认证助手权限不是 4755 root:root（PAM 认证会失败）：$(stat -c '%a %U:%G' "$AUTH_INSTALLED")"
fi

# ------------------------------------------------- 5/7 语义自检（不需要 X，不抢锁）
say "5/7 语义自检：--dump（不需要 X）"
if [ "$CHECK" = 1 ]; then
    dry "$SRC/hphelm-panel --dump | tail -1   ⇒ 期望含 sleep_off=off（永不关屏）"
else
    out=$("$SRC/hphelm-panel" --dump | tail -1)
    printf '    %s\n' "$out"
    case "$out" in
        *"sleep_off=off"*) ok "0 语义正确（永不关屏）" ;;
        *) die "sleep_off 不是 off —— 默认值或判据被改回去了，停下来查（docs/pitfalls.md #16）" ;;
    esac
fi

# ------------------------------------------------------------------- 6/7 重启
say "6/7 重启面板（守护循环会用新二进制拉起）"
if [ "$RESTART" = 0 ]; then
    printf '    --no-restart：不动正在跑的实例（新版本下次登录生效）\n'
elif [ "$CHECK" = 1 ]; then
    dry "pkill -x hphelm-panel；等 ≤15s 让 hphelm-session 的守护循环用新二进制拉起"
else
    before=$(pgrep -x hphelm-panel | head -1 || true)
    pkill -x hphelm-panel || true
    i=0
    while [ $i -lt 15 ]; do
        sleep 1; i=$((i+1))
        new=$(pgrep -x hphelm-panel | head -1 || true)
        [ -n "$new" ] && [ "$new" != "$before" ] && break
    done
    [ -n "${new:-}" ] || die "15 秒内没有新实例起来 —— 守护循环可能没在跑（看 /tmp/hphelm-panel.log）"
    ok "新实例 pid=$new（旧 pid=${before:-无}）"
fi

# ------------------------------------------------------------------- 7/7 判据
say "7/7 判据"
if [ "$CHECK" = 1 ]; then
    printf '    [dry] VT=tty7 · slick-greeter=0 · hphelm-panel=1 · 二进制一致 · sleep_off=off ·\n'
    printf '    [dry] 帧差分 >30%% 变化帧 = 0 · CPU <1%% of one core · 日志无「起来就死」\n'
    exit 0
fi

fail=0
vt=$(cat /sys/class/tty/tty0/active)
greeter=$(pgrep -cx slick-greeter || true)
panels=$(pgrep -cx hphelm-panel || true)
[ "$vt" = "tty7" ]        && ok "VT=$vt"        || { warn "VT=$vt（期望 tty7 —— 有东西抢走了 VT）"; fail=1; }
[ "$greeter" = "0" ]      && ok "slick-greeter=0" || { warn "slick-greeter=$greeter（期望 0 —— 红线 1 被碰了）"; fail=1; }
[ "$panels" = "1" ]       && ok "hphelm-panel=1"  || { warn "hphelm-panel=$panels（期望 1 —— 单实例锁或守护循环有问题）"; fail=1; }

if [ "$RESTART" = 0 ]; then
    printf '    （--no-restart：跳过运行态/CPU 判据）\n'
else
    # 帧差分：看得见的「闪」只有这一条判据能抓住
    if [ -n "${DISPLAY:-}" ] && command -v xdotool >/dev/null 2>&1; then
        line=$("$SRC/tools/probe-frames" 40 20 2>/dev/null | tail -1 || true)
        [ -n "$line" ] || line="(no output)"
        case "$line" in
            *"frames with >30% pixels changed: 0"*) ok "不闪：$line" ;;
            *"changed: "*) warn "有闪：$line（>30% 变化的帧必须是 0）"; fail=1 ;;
            *) warn "帧差分跑不起来（DISPLAY/XAUTHORITY？）—— 手工跑 docs/verify.md 第二节" ;;
        esac
    else
        printf '    跳过帧差分（需要 DISPLAY 与 xdotool）：DISPLAY=:0 tools/probe-frames 40\n'
    fi

    # CPU：jiffies × 10ms / 窗口 ms（单位别写错，docs/verify.md 有 10 倍陷阱）
    # ⚠️ 必须跳过刚启动的几秒：启动帧会全额重绘 + 首次扫进程，窗口包含它就会读出 1.2%
    sleep 3
    pid=$(pgrep -x hphelm-panel | head -1)
    set -- $(awk '{print $14, $15}' "/proc/$pid/stat"); u1=$1; s1=$2
    sleep 15
    set -- $(awk '{print $14, $15}' "/proc/$pid/stat"); u2=$1; s2=$2
    cpu=$(awk -v a=$u1 -v b=$s1 -v c=$u2 -v d=$s2 'BEGIN{printf "%.2f", (c+d-a-b)*10/15000*100}')
    awk -v c="$cpu" 'BEGIN{exit !(c<1)}' && ok "CPU=${cpu}% of one core（预算 <1%）" || warn "CPU=${cpu}% of one core（预算 <1%）"
fi

# 日志：只认**当前实例**的「起来就死」与兜底开火（/tmp 的日志是跨会话追加的，
# 历史事故的字符串会一直在里面 ⇒ 全文件 grep 会假告警，我第一版就这么误报过）
current=$(awk '/启动：/{buf=""} {buf=buf $0 "\n"} END{printf "%s", buf}' /tmp/hphelm-panel.log 2>/dev/null || true)
if printf '%s' "$current" | grep -qE '连续 3 次|关屏兜底'; then
    warn "日志里出现「起来就死 / 关屏兜底」—— 面板在反复崩，看 tail -20 /tmp/hphelm-panel.log"; fail=1
else
    ok "日志无崩溃兜底记录"
fi
tail -3 /tmp/hphelm-panel.log 2>/dev/null | sed 's/^/    /'

echo
if [ "$fail" = 0 ] && [ "$NOTES" = 0 ]; then
    printf '\033[32m部署完成，判据全绿。\033[0m\n'
elif [ "$fail" = 0 ]; then
    printf '\033[32m部署完成：红线与运行态判据全过，但有 %s 条提醒（黄字）没达标。\033[0m\n' "$NOTES"
else
    printf '\033[31m部署完成，但上面的判据没全过 —— 先别走开，按 docs/deploy.md 排查或回滚。\033[0m\n'
    exit 1
fi
printf '回滚到上一版：sudo install -m755 %s %s && pkill -x hphelm-panel\n' "$PREV" "$INSTALLED"
printf '别忘了：改了行为/版式 ⇒ 同步 docs/ + HANDOFF.md + .ai/ ⇒ git commit。\n'
