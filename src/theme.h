/* theme.h —— Catppuccin Mocha + 版式常量（全部是「逻辑像素」，绘制时统一乘 SCALE）
 *
 * 版式原则（2026-10-18 第三轮定，用户口径）：
 *   ① **不用卡片**：内容直接铺在整屏渐变底上，只用「细线 + 字号层级 + 留白」分区；
 *   ② **紧凑**：区块标题不单独占行（label 与值同排、靠右对齐），字号/行距压到最小可读；
 *   ③ 版面自顶向下三带：顶栏 → 系统三栏（CPU|内存|网络）→ 进程榜（占满剩余高度）。
 */
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

/* ---- 版式（逻辑像素；屏 141.8dpi ⇒ SCALE≈1.48，逻辑屏 ≈1297×729） ---- */
#define L_MARGIN      18.0
#define L_COL_GAP     26.0   /* 系统三栏之间的水平间距 */

#define L_TOP_Y       12.0   /* 顶栏文字顶部 */
#define L_RULE1_Y     46.0   /* 顶栏下分隔线 */

#define L_SYS_Y       58.0   /* 系统三栏顶部 */
#define L_SYS_H       210.0  /* 系统三栏高度（底部要给曲线留出零基线，别压到分隔线） */
#define L_RULE2_Y     (L_SYS_Y + L_SYS_H + 12.0)   /* = 280 */

#define L_PROC_HDR_Y  (L_RULE2_Y + 16.0)  /* 进程榜标题行 */
#define L_PROC_LIST_Y (L_PROC_HDR_Y + 28.0)

#define L_PROC_COLS   4      /* 进程榜列数（按内存降序、逐列向下填） */
#define L_PROC_ROWS   10     /* 每列行数 ⇒ 共 40 条 */

#define L_CPU_FRAC    0.42   /* 三栏宽度占比 */
#define L_MEM_FRAC    0.29

#define L_CORE_BAR_GAP 4.0   /* 每核柱间距 */

/* ---- 字号 ---- */
#define F_TITLE  "JetBrainsMono Nerd Font 11"
#define F_LABEL  "JetBrainsMono Nerd Font 10"
#define F_SMALL  "JetBrainsMono Nerd Font 9"
#define F_PROC   "JetBrainsMono Nerd Font 14"
#define F_VALUE  "JetBrainsMono Nerd Font 17"
#define F_BIG    "JetBrainsMono Nerd Font 26"
#define F_HUGE   "JetBrainsMono Nerd Font 44"

#endif
