/* view.h —— 绘制层公共接口（面板与登录页共用同一套文字/图形基元）
 *
 * 坐标一律是「逻辑像素」，调用方先 cairo_scale(scale) 再调用；
 * scale = 物理像素 / 逻辑像素（本机屏 141.8 DPI ⇒ ≈1.48）。
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
} view_ctx_t;

/* 初始化（字体描述符 + 绑定 PangoLayout） */
void view_init(PangoLayout *lay);

PangoFontDescription *v_fd_title(void);
PangoFontDescription *v_fd_label(void);
PangoFontDescription *v_fd_value(void);
PangoFontDescription *v_fd_big(void);
PangoFontDescription *v_fd_small(void);
PangoFontDescription *v_fd_huge(void);

void v_set_rgb(cairo_t *cr, uint32_t rgb, double a);
double v_text_w(PangoFontDescription *fd, const char *s);
double v_text(cairo_t *cr, double x, double y, PangoFontDescription *fd, uint32_t rgb,
              const char *fmt, ...);
void v_text_right(cairo_t *cr, double xr, double y, PangoFontDescription *fd, uint32_t rgb,
                  const char *fmt, ...);
void v_text_center(cairo_t *cr, double cx, double y, PangoFontDescription *fd, uint32_t rgb,
                   const char *fmt, ...);
void v_round_rect(cairo_t *cr, double x, double y, double w, double h, double r);
void v_panel_box(cairo_t *cr, double x, double y, double w, double h);
void v_bar(cairo_t *cr, double x, double y, double w, double h, double frac, uint32_t col);
void v_spark(cairo_t *cr, double x, double y, double w, double h, const ring_t *r, uint32_t col);
uint32_t v_temp_color(double t);
void v_fmt_mb(double mb, char *out, size_t n);
void v_fmt_rate(double bps, char *out, size_t n);
void v_fmt_uptime(double s, char *out, size_t n);

/* 两个视图 */
void view_panel_draw(cairo_t *cr, const view_ctx_t *v, const metrics_t *m);
void view_login_draw(cairo_t *cr, const view_ctx_t *v, const metrics_t *m, const char *user,
                     int pwlen, const char *err, int busy, int cursor_on, int wait_s);

#endif
