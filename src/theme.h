/* theme.h —— Catppuccin Mocha + 版式常量（全部是「逻辑像素」，绘制时统一乘 SCALE）
 *
 * 版式原则（2026-10-18 第三轮定，用户口径）：
 *   ① **不用卡片**：内容直接铺在整屏渐变底上，只用「细线 + 字号层级 + 留白」分区；
 *   ② **紧凑**：区块标题不单独占行（label 与值同排、靠右对齐），字号/行距压到最小可读；
 *   ③ 版面自左向右三列（2026-10-21 第四版，用户口径「进程榜只占一列」）：
 *        本机 hphelm │ 生产 hpcore │ 进程榜（整列高）
 *      两台主机用**同一个 draw_host()** 画 —— 版面/口径必然对称，不会「一台多一行」。
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
#define L_COL_GAP     26.0   /* 列之间的水平间距 */

#define L_TOP_Y       12.0   /* 顶栏文字顶部 */
#define L_RULE1_Y     46.0   /* 顶栏下分隔线 */
#define L_BODY_Y      58.0   /* 主体（三列）顶部 */

/* ---- 三列的宽度占比（进程榜在右且固定；两台主机对称平分剩下的）----
 * ⚠️ 两个占比之和必须 ≤ 1；进程榜宽度用「剩余量」算出来 ⇒ 不会出现「宽度 0」那种静默错版。
 */
#define L_HOST_FRAC   0.348  /* 每台主机列的宽度占比（≈452 逻辑像素） */

/* ---- 主机列内部（两个主机列共用这套常量；全部是**绝对逻辑像素**）----
 * 预算：逻辑屏高 729.7，页边 18 ⇒ 内容区 58..711。纵向节奏如下（括号里是行高）：
 *   58 主机名(24) · 82 角色/ip/uptime/核数(18) · 104 CPU 标题(20)
 *   126 大百分比(36) · 166 CPU 条(12) · 184 每核柱(50) · 246 “5 分钟”(13) · 262 CPU 曲线(46)
 *   318 内存标题(20) · 340 大百分比(32) · 376 内存条(12) · 392 用量/swap(14) · 412 “5 分钟” · 428 内存曲线(46)
 *   484 网络标题(20) · 508 速率(24) · 538 “5 分钟” · 554 网络曲线(46)
 *   610 磁盘标题(20) · 634 磁盘条(12) · 652 容器/SSD 脚注(14)
 * ⚠️ 改这里的任何一条都要同时改这段注释 —— 它是我量像素时的对照表。
 */
#define L_H_NAME_Y      58.0
#define L_H_ROLE_Y      82.0
#define L_H_CPU_Y      104.0
#define L_H_BIG_Y      126.0
#define L_H_CPU_BAR    166.0
#define L_H_CORES_Y    184.0
#define L_H_CORES_H     50.0
#define L_H_CPU_SPARK  262.0
#define L_H_MEM_Y      318.0
#define L_H_MEM_SPARK  428.0
#define L_H_NET_Y      484.0
#define L_H_NET_SPARK  554.0
#define L_H_DISK_Y     610.0
#define L_H_DISK_BAR   634.0
#define L_H_SPARK_H     46.0  /* 曲线高度 */
#define L_H_LBL_GAP     16.0  /* “5 分钟”标签在曲线顶部上方多少 */

/* ---- 进程榜列（两段：本机 + 生产）---- */
#define L_PROC_ROWS   12     /* 每段行数（本机 12 + 生产 12 = 24 条） */
#define L_PROC_COLS   1      /* 进程榜只占一列（用户口径） */
#define L_PROC_ROW_H  24.0   /* 行高 */
#define L_PROC_SEC_H  316.0  /* 每段高度 = 标题 28 + 12 行 × 24；两段 + gap 必须 ≤ 653 */
#define L_PROC_SEC_GAP 16.0  /* 两段之间的空隙（正好放“生产”段的顶部分隔线） */

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
