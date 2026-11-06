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
#include <time.h>

/* ---- 主题：白天 / 夜间两套配色 ----
 *
 * 语义名保持不变（C_MAUVE 永远是「CPU/主色」），只是背后的色值随主题变。
 * 所有绘制代码只读 C_* 宏 ⇒ 换主题不需要动任何版面文件。
 * TP 由 theme_set() 在运行期整体赋值；bg0/bg1 是整屏渐变的两端。
 */
typedef struct {
    uint32_t base, mantle, crust, surface0, surface1, surface2;
    uint32_t text, subtext1, subtext0, overlay1, overlay0;
    uint32_t mauve, blue, green, yellow, peach, red, teal, lavender;
    uint32_t bg0, bg1;          /* 整屏渐变：bg0 在顶部，bg1 在底部 */
} palette_t;

extern palette_t TP;            /* 当前生效的主题 */
extern const palette_t THEME_DAY, THEME_NIGHT;

#define C_BASE      TP.base
#define C_MANTLE    TP.mantle
#define C_CRUST     TP.crust
#define C_SURFACE0  TP.surface0
#define C_SURFACE1  TP.surface1
#define C_SURFACE2  TP.surface2
#define C_TEXT      TP.text
#define C_SUBTEXT1  TP.subtext1
#define C_SUBTEXT0  TP.subtext0
#define C_OVERLAY1  TP.overlay1
#define C_OVERLAY0  TP.overlay0
#define C_MAUVE     TP.mauve
#define C_BLUE      TP.blue
#define C_GREEN     TP.green
#define C_YELLOW    TP.yellow
#define C_PEACH     TP.peach
#define C_RED       TP.red
#define C_TEAL      TP.teal
#define C_LAVENDER  TP.lavender
#define C_BG0       TP.bg0
#define C_BG1       TP.bg1

void theme_set(int night);                       /* 立即生效（调用方负责重画背景） */
/* 固定时刻规则：day_min 之前、night_min 之后算夜间；两个参数都是「0 点起的分钟数」 */
int theme_night_at(time_t t, int day_min, int night_min);
int theme_is_night(void);

/* ---- 版式（逻辑像素；屏 141.8dpi ⇒ SCALE≈1.48，逻辑屏 ≈1297×729） ---- */
#define L_MARGIN      18.0
#define L_COL_GAP     26.0   /* 列之间的水平间距 */

#define L_TOP_Y       12.0   /* 顶栏文字顶部 */
#define L_RULE1_Y     46.0   /* 顶栏下分隔线 */
#define L_BODY_Y      58.0   /* 主体顶部 */

/* ---- 宽度分配（2026-11-06 单机化：hpcore all-in-one 之后只剩一台机）----
 * 原来是「本机 │ 生产服务器 │ 进程榜」三列；现在 = 主机区（内含左右两个子列）+ 进程榜一列。
 * ⚠️ 占比必须 < 1；进程榜宽度用「剩余量」算 ⇒ 不会出现「宽度 0」那种静默错版。
 */
#define L_HOST_FRAC   0.560  /* 主机区占内容宽度的比例（≈706 逻辑像素 ⇒ 两个子列各 ≈336） */
#define L_SUB_GAP     34.0   /* 主机区内部左右两个子列的间距 */

/* ---- 主机区内部（**自上而下的流式排版**）----
 * 2026-10-21 第五版（用户口径：标题行不单独占行、图表压缩、别留废话空间）：
 *   · 每个资源块 = **一行文字**（标题嵌在数值行里）+ 一条细进度条，没有单独的标题行
 *   · 纵向不再写死绝对坐标，而是「流」：一行一行往下走，剩下的高度**全部分给曲线**
 *     ⇒ 以后加/减监控项不会再把版面顶出屏幕（上一版就是因为绝对坐标+加内容踩过）
 * 2026-11-06 单机版：两个子列各自「2 张曲线 + 其余流式内容」，两列的固定高度**对齐**
 *   （187 + 187），所以两张曲线共用同一个高度、左右两列的横向基线自然对齐。
 *
 * 纵向预算（逻辑屏高 729.7、页边 18 ⇒ 内容区 58..711，共 653）：
 *   左列固定 187（名称 36 + 身份 20 + 细线 8 + CPU 行 24 + 条 10+9 + 每核柱 28+9
 *                   + 内存行 24 + 条 10+9）
 *   右列固定 187（网络行 24 + 磁盘 I/O 行 24 + 细线 8 + 磁盘行 24 + 条 10+9
 *                   + 细线 8 + 三行小字 24×3）
 *   ⇒ 653 - 187 - 2×6(曲线间距) = 448 ⇒ 每张曲线 224（占满，不靠假数据填）
 */
#define L_H_NAME_Y      58.0   /* 列内第一行（主机名）的顶部 */
#define L_H_NAME_H      36.0   /* 主机名行高（17pt；用户要求与下面的身份行拉开） */
#define L_H_IDENT_H     20.0   /* 身份行（角色/IP/up/核/网卡） */
#define L_H_RULE_H       8.0   /* 细线上下的留白合计 */
#define L_H_LINE_H      24.0   /* 一个资源行的行高（标题 + 数值同排） */
#define L_H_BAR_H       10.0   /* 进度条高度 */
#define L_H_BAR_GAP      9.0   /* 进度条与下一行的间距 */
#define L_H_CORES_H     28.0   /* 每核柱高度 */
#define L_H_CHART_MIN   30.0   /* 曲线最小高度 */
#define L_H_CHART_MAX  260.0   /* 曲线最大高度（单机化后剩余空间都给它；超过 260 才会留白） */
#define L_H_CAP_H      16.0   /* 曲线时间轴说明行（「近 5 分钟 · 1 Hz」），两个子列各一条 */
#define L_H_CHART_GAP    6.0   /* 曲线与其下内容的间距 */
#define L_H_FOOT_H      18.0   /* 脚注行（容器/SSD/累计流量） */

/* ---- 进程榜列（两段：按内存 + 按 CPU；整列都归这一台机）---- */
#define L_PROC_ROWS   12     /* 每段行数（内存 12 + CPU 12 = 24 条） */
#define L_PROC_COLS   1      /* 进程榜只占一列（用户口径） */
#define L_PROC_ROW_H  24.0   /* 行高 */
#define L_PROC_SEC_H  316.0  /* 每段高度 = 标题 28 + 12 行 × 24；两段 + gap 必须 ≤ 653 */
#define L_PROC_SEC_GAP 16.0  /* 两段之间的空隙（正好放“生产”段的顶部分隔线） */

#define L_CORE_BAR_GAP 4.0   /* 每核柱间距 */

/* ---- 字号 ---- */
#define F_TITLE  "JetBrainsMono Nerd Font 11"
#define F_LABEL  "JetBrainsMono Nerd Font 10"
#define F_SMALL  "JetBrainsMono Nerd Font 9"
#define F_DESC   "JetBrainsMono Nerd Font 13"  /* 榜单描述：要比 label 大、又不能抢过 repo */
#define F_PROC   "JetBrainsMono Nerd Font 14"
#define F_VALUE  "JetBrainsMono Nerd Font 17"
#define F_BIG    "JetBrainsMono Nerd Font 26"
#define F_HUGE   "JetBrainsMono Nerd Font 44"

#endif
