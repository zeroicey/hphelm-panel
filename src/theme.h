/* theme.h —— Catppuccin Mocha + 布局常量（全部是「逻辑像素」，绘制时统一乘 SCALE） */
#ifndef THEME_H
#define THEME_H

#include <stdint.h>

/* ---- Catppuccin Mocha ---- */
#define C_BASE      0x1e1e2e
#define C_MANTLE    0x181825
#define C_CRUST     0x11111b
#define C_SURFACE0  0x313244
#define C_SURFACE1  0x45475a
#define C_SURFACE2  0x585b70
#define C_TEXT      0xcdd6f4
#define C_SUBTEXT1  0xbac2de
#define C_SUBTEXT0  0xa6adc8
#define C_OVERLAY1  0x7f849c
#define C_OVERLAY0  0x6c7086
#define C_MAUVE     0xcba6f7
#define C_BLUE      0x89b4fa
#define C_GREEN     0xa6e3a1
#define C_YELLOW    0xf9e2af
#define C_PEACH     0xfab387
#define C_RED       0xf38ba8
#define C_TEAL      0x94e2d5
#define C_LAVENDER  0xb4befe

/* ---- 布局（逻辑像素；屏 141.8dpi ⇒ SCALE≈1.48） ---- */
#define L_MARGIN     18.0
#define L_GAP        12.0
#define L_HEADER_H   46.0
#define L_ROW1_Y     70.0
#define L_ROW1_H     230.0
#define L_ROW2_Y     (L_ROW1_Y + L_ROW1_H + L_GAP)
#define L_ROW2_H     86.0
#define L_ROW3_Y     (L_ROW2_Y + L_ROW2_H + L_GAP)
#define L_ROW3_H     200.0
#define L_FOOT_Y     (L_ROW3_Y + L_ROW3_H + L_GAP)
#define L_FOOT_H     70.0
#define L_CPU_W_FRAC 0.545

/* ---- 字号 ---- */
#define F_TITLE  "JetBrainsMono Nerd Font 11"
#define F_LABEL  "JetBrainsMono Nerd Font 10"
#define F_VALUE  "JetBrainsMono Nerd Font 17"
#define F_BIG    "JetBrainsMono Nerd Font 26"
#define F_SMALL  "JetBrainsMono Nerd Font 9"
#define F_HUGE   "JetBrainsMono Nerd Font 44"

#endif
