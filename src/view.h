/* view.h —— 绘制层公共接口（面板与登录页共用同一套文字/图形基元）
 *
 * 坐标一律是「逻辑像素」，调用方先 cairo_scale(scale) 再调用；
 * scale = 物理像素 / 逻辑像素（本机屏 141.8 DPI ⇒ ≈1.48）。
 *
 * 三个文件分工（用户要求 UI 拆开写）：
 *   view.c        —— 基元：背景渐变 / 细线 / 进度条 / 每核柱 / sparkline / 文字
 *   view_panel.c  —— 运维面板版面（无卡片、三栏 + 进程榜）
 *   view_login.c  —— 登录页版面（无卡片、居中）
 */
#ifndef VIEW_H
#define VIEW_H

#include <cairo/cairo.h>
#include <pango/pangocairo.h>
#include <stdint.h>
#include <stdio.h>

#include "metrics.h"

typedef struct {
    double scale;
    double lw, lh; /* 逻辑尺寸 */
    int bg_done;   /* 背景已经由主循环预填进后备缓冲（省掉每帧重算渐变） */
} view_ctx_t;

/* 生产服务器采集状态（面板需要把「离线多少秒」画出来，而不是静静显示旧数字） */
typedef struct {
    int enabled;          /* 配了生产服务器（--remote） */
    int online;           /* 采集健康：在跑且帧不老于 5 秒 */
    double age_s;         /* 距上一帧的秒数；-1 = 从未连上 */
    char target[64];      /* ssh 目标名（还没收到第一帧时用它当列标题） */
} view_feed_t;

/* 初始化（字体描述符 + 绑定 PangoLayout；可重复调用，字体描述符只建一次） */
void view_init(PangoLayout *lay);

PangoFontDescription *v_fd_title(void);
PangoFontDescription *v_fd_label(void);
PangoFontDescription *v_fd_small(void);
PangoFontDescription *v_fd_proc(void);
PangoFontDescription *v_fd_value(void);
PangoFontDescription *v_fd_big(void);
PangoFontDescription *v_fd_huge(void);

/* ---- 文字 ---- */
void v_set_rgb(cairo_t *cr, uint32_t rgb, double a);
double v_text_w(PangoFontDescription *fd, const char *s);
double v_text(cairo_t *cr, double x, double y, PangoFontDescription *fd, uint32_t rgb,
              const char *fmt, ...);
void v_text_right(cairo_t *cr, double xr, double y, PangoFontDescription *fd, uint32_t rgb,
                  const char *fmt, ...);
/* 右对齐并返回宽度（一次整形同时完成「量」与「画」，省掉一次 v_text_w） */
double v_text_right_w(cairo_t *cr, double xr, double y, PangoFontDescription *fd, uint32_t rgb,
                      const char *fmt, ...);
/* 该字体下单个等宽字符的宽度（字体是等宽的 ⇒ 一次测量可反复用） */
double v_char_w(PangoFontDescription *fd);
/* 按宽度把名字截断成「前缀…」，**不会把 UTF-8 多字节字符切一半**
 * （切一半 pango 会画出豆腐块或直接不画 —— 静默错误，肉眼以外很难发现）。
 * 用等宽字体按字符宽估算，不做逐字符测量（那会让单帧绘制时间翻倍）。 */
void v_truncate_mono(char *out, size_t outn, const char *name, PangoFontDescription *fd,
                     double maxw);
void v_text_center(cairo_t *cr, double cx, double y, PangoFontDescription *fd, uint32_t rgb,
                   const char *fmt, ...);

/* ---- 版式基元 ---- */
void v_round_rect(cairo_t *cr, double x, double y, double w, double h, double r);
/* 整屏渐变底（面板与登录页共用 ⇒ 两个界面风格统一） */
void v_bg(cairo_t *cr, double w, double h);
/* 1px 分隔线（**不是卡片**：只用来切分带，不画边框/填充） */
void v_rule(cairo_t *cr, double x, double y, double w);
/* 进度条（自带轨道） */
void v_bar(cairo_t *cr, double x, double y, double w, double h, double frac, uint32_t col);
/* 每核占用柱（自带轨道，空闲核也看得见；>16 核自动分两行） */
void v_core_bars(cairo_t *cr, double x, double y, double w, double h, const double *pct, int n);
/* 单曲线 sparkline。vmax<=0 ⇒ 自动取量化量程（见 v_nice_ceil） */
void v_spark(cairo_t *cr, double x, double y, double w, double h, const ring_t *r, uint32_t col,
             double vmax);
/* 双曲线 sparkline（共用同一量程；用于网络上下行） */
void v_spark2(cairo_t *cr, double x, double y, double w, double h, const ring_t *r1, uint32_t c1,
              const ring_t *r2, uint32_t c2, double vmax);
/* 把量程量化到 1/2/5×10ⁿ：**避免每来一个新高整条曲线就整体跳**（实测观感问题） */
double v_nice_ceil(double v);
uint32_t v_temp_color(double t);

/* ---- 格式化 ---- */
void v_fmt_mb(double mb, char *out, size_t n);
void v_fmt_rate(double bps, char *out, size_t n);
void v_fmt_uptime(double s, char *out, size_t n);

/* ---- 两个视图 ---- */
/* local = 本机；remote = 生产服务器（未配置时传 NULL）；feed = 远程采集状态 */
void view_panel_draw(cairo_t *cr, const view_ctx_t *v, const metrics_t *local,
                     const metrics_t *remote, const view_feed_t *feed);
void view_login_draw(cairo_t *cr, const view_ctx_t *v, const metrics_t *m, const char *user,
                     int pwlen, const char *err, int busy, int cursor_on, int wait_s);

#endif
