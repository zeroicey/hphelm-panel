#!/bin/sh
# hpopi5b-kiosk —— 纯显示终端入口（X 由 xinit 起好；本脚本负责「别让它黑屏」+ 拉起面板）
#
# ⚠️ 必须关掉 X server 自己的屏保/DPMS（2026-11-03 实测：默认 Screen Saver timeout=600s、
#    DPMS Standby/Suspend/Off 全 600s ⇒ kiosk 没有键鼠 ⇒ 永远算「空闲」⇒ **10 分钟必黑屏**）。
#    面板的 sleep_off=0 只表示「它自己不去关屏」，完全不等于「X 不会关屏」—— 这两件事要分开看。
xset s off          # 关屏保
xset -dpms          # 关 DPMS 定时器
xset s noblank      # 别让 blank 机制插手
xset dpms force on  # 万一是从黑屏状态被拉起，先点亮
# 为什么不写「守护循环」：systemd 的 Restart=always 就是那个循环，且带限流与状态。
LOG=/var/log/hpopi5b-panel.log
exec /usr/local/lib/hpopi5b-panel/hpopi5b-panel --kiosk --scale 1.48 \
     --lock /var/lock/hpopi5b-panel.lock >>"$LOG" 2>&1
