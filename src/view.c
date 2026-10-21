/* view.c —— 绘制基元（cairo + pango，无 GUI 工具包）
 *
 * 只放「与具体版面无关」的东西：文字、背景渐变、细线、进度条、每核柱、sparkline、格式化。
 * 版面在 view_panel.c / view_login.c 里。
 */
#include "view.h"

#include <math.h>
#include <stdarg.h>
#include <string.h>

#include "theme.h"

static PangoLayout *g_lay;
static PangoFontDescription *FD_TITLE, *FD_LABEL, *FD_SMALL, *FD_PROC, *FD_VALUE, *FD_BIG,
    *FD_HUGE;

static PangoFontDescription *mk(const char *s) { return pango_font_description_from_string(s); }

void view_init(PangoLayout *lay) {
    g_lay = lay;
    if (FD_TITLE) return; /* 重建 cairo 上下文时会再调一次：字体描述符只建一次 */
    FD_TITLE = mk(F_TITLE);
    FD_LABEL = mk(F_LABEL);
    FD_SMALL = mk(F_SMALL);
    FD_PROC = mk(F_PROC);
    FD_VALUE = mk(F_VALUE);
    FD_BIG = mk(F_BIG);
    FD_HUGE = mk(F_HUGE);
}

PangoFontDescription *v_fd_title(void) { return FD_TITLE; }
PangoFontDescription *v_fd_label(void) { return FD_LABEL; }
PangoFontDescription *v_fd_small(void) { return FD_SMALL; }
PangoFontDescription *v_fd_proc(void) { return FD_PROC; }
PangoFontDescription *v_fd_value(void) { return FD_VALUE; }
PangoFontDescription *v_fd_big(void) { return FD_BIG; }
PangoFontDescription *v_fd_huge(void) { return FD_HUGE; }

/* ---------------- 文字 ---------------- */
void v_set_rgb(cairo_t *cr, uint32_t rgb, double a) {
    cairo_set_source_rgba(cr, ((rgb >> 16) & 0xff) / 255.0, ((rgb >> 8) & 0xff) / 255.0,
                          (rgb & 0xff) / 255.0, a);
}

double v_text_w(PangoFontDescription *fd, const char *s) {
    pango_layout_set_font_description(g_lay, fd);
    pango_layout_set_text(g_lay, s, -1);
    PangoRectangle r;
    pango_layout_get_extents(g_lay, NULL, &r);
    return (double)r.width / PANGO_SCALE;
}

/* 排版一次 = 一次 pango 整形。实测一帧要整形 100+ 次（两台主机 + 24 行进程），
 * 每帧 5~10ms ⇒ 1 Hz 重绘就是 0.5~1% of one core。
 * ⚠️ 所以「右对齐/居中」**必须复用这一次排版**去取宽度：
 *    不要先 v_text_w() 量一遍、再 v_text() 排一遍（两倍整形，实测绘制时间 +40%）。 */
static double layout_prepare(PangoFontDescription *fd, const char *buf) {
    pango_layout_set_font_description(g_lay, fd);
    pango_layout_set_text(g_lay, buf, -1);
    PangoRectangle r;
    pango_layout_get_extents(g_lay, NULL, &r);
    return (double)r.width / PANGO_SCALE;
}

static void layout_show(cairo_t *cr, double x, double y, uint32_t rgb) {
    v_set_rgb(cr, rgb, 1.0);
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, g_lay);
}

double v_text(cairo_t *cr, double x, double y, PangoFontDescription *fd, uint32_t rgb,
              const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    double w = layout_prepare(fd, buf);
    layout_show(cr, x, y, rgb);
    return w;
}

void v_text_right(cairo_t *cr, double xr, double y, PangoFontDescription *fd, uint32_t rgb,
                  const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    double w = layout_prepare(fd, buf);
    layout_show(cr, xr - w, y, rgb);
}

double v_text_right_w(cairo_t *cr, double xr, double y, PangoFontDescription *fd, uint32_t rgb,
                      const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    double w = layout_prepare(fd, buf);
    layout_show(cr, xr - w, y, rgb);
    return w;
}

/* 等宽字体：量 10 个 M 再除 10（比逐字符测量便宜一个数量级） */
double v_char_w(PangoFontDescription *fd) {
    static struct { PangoFontDescription *fd; double w; } cache[8];
    static int n;
    for (int i = 0; i < n; i++)
        if (cache[i].fd == fd) return cache[i].w;
    double w = v_text_w(fd, "MMMMMMMMMM") / 10.0;
    if (n < 8) {
        cache[n].fd = fd;
        cache[n].w = w;
        n++;
    }
    return w;
}

double v_text_mid(PangoFontDescription *fd) {
    /* 用「有上伸部 + 下伸部的拉丁字母」量视觉中心：中文/数字的墨迹大致落在同一条带上 */
    layout_prepare(fd, "Ag|0");
    PangoRectangle ink;
    pango_layout_get_pixel_extents(g_lay, &ink, NULL);
    return (double)ink.y + (double)ink.height / 2.0;
}

void v_text_right_ex(cairo_t *cr, double xr, double y, PangoFontDescription *fd, uint32_t rgb,
                     double *w_out, double *mid_out, const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    double w = layout_prepare(fd, buf);
    if (mid_out) {
        /* 用**这次真实的排版结果**取墨迹 extents，不做任何估算 */
        PangoRectangle ink;
        pango_layout_get_extents(g_lay, &ink, NULL);
        *mid_out = (double)ink.y / PANGO_SCALE + (double)ink.height / PANGO_SCALE / 2.0;
    }
    if (w_out) *w_out = w;
    layout_show(cr, xr - w, y, rgb);
}

void v_dot_at_text(cairo_t *cr, double cx, double cy, double r, uint32_t rgb) {
    cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
    v_set_rgb(cr, rgb, 1.0);
    cairo_fill(cr);
}

void v_truncate_mono(char *out, size_t outn, const char *name, PangoFontDescription *fd,
                     double maxw) {
    double chw = v_char_w(fd);
    if (chw <= 0.1) chw = 8.0;
    int maxch = (int)(maxw / chw);
    if (maxch < 4) maxch = 4;
    int n = (int)strlen(name);
    if (n <= maxch) {
        snprintf(out, outn, "%s", name);
        return;
    }
    int cut = maxch - 1;
    if (cut < 1) cut = 1;
    /* 退到 UTF-8 字符边界（0x80..0xBF 是续字节，不能当结尾） */
    while (cut > 0 && ((unsigned char)name[cut] & 0xC0) == 0x80) cut--;
    snprintf(out, outn, "%.*s…", cut, name);
}

void v_text_center(cairo_t *cr, double cx, double y, PangoFontDescription *fd, uint32_t rgb,
                   const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    double w = layout_prepare(fd, buf);
    layout_show(cr, cx - w / 2.0, y, rgb);
}

/* ---------------- 版式基元 ---------------- */
void v_round_rect(cairo_t *cr, double x, double y, double w, double h, double r) {
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r < 0) r = 0;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
}

/* 整屏渐变底：面板与登录页共用（原来面板是平涂、登录页是渐变，观感不统一） */
void v_bg(cairo_t *cr, double w, double h) {
    cairo_pattern_t *g = cairo_pattern_create_linear(0, 0, 0, h);
    cairo_pattern_add_color_stop_rgb(g, 0.0, 0.043, 0.071, 0.125);
    cairo_pattern_add_color_stop_rgb(g, 1.0, 0.008, 0.012, 0.024);
    cairo_set_source(cr, g);
    cairo_rectangle(cr, 0, 0, w, h);
    cairo_fill(cr);
    cairo_pattern_destroy(g);
}

/* 细线：只切分带，不画边框 —— 这是「无卡片」版式的分区手段 */
void v_rule(cairo_t *cr, double x, double y, double w) {
    cairo_rectangle(cr, x, y, w, 1.0);
    v_set_rgb(cr, C_SURFACE1, 0.45);
    cairo_fill(cr);
}

void v_bar(cairo_t *cr, double x, double y, double w, double h, double frac, uint32_t col) {
    v_round_rect(cr, x, y, w, h, h / 2);
    v_set_rgb(cr, C_SURFACE0, 0.85);
    cairo_fill(cr);
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    double fw = w * frac;
    if (fw >= h) {
        v_round_rect(cr, x, y, fw, h, h / 2);
        v_set_rgb(cr, col, 1.0);
        cairo_fill(cr);
    } else if (fw > 0.5) {
        v_round_rect(cr, x, y, h, h, h / 2);
        v_set_rgb(cr, col, 1.0);
        cairo_fill(cr);
    }
}

/* 每核柱：**带轨道**（原来只画亮条 ⇒ 空闲时柱高 1px，根本看不清），
   并且给一个最低 2px 的亮条 ⇒ 空闲核也能看出「这里有核、现在是 0%」。
   >16 核自动分两行。 */
void v_core_bars(cairo_t *cr, double x, double y, double w, double h, const double *pct, int n) {
    if (n <= 0) return;
    if (n > MAX_CORE) n = MAX_CORE;
    int rows = (n > 16) ? 2 : 1;
    int per = (n + rows - 1) / rows;
    double gap = L_CORE_BAR_GAP;
    double bw = (w - gap * (per - 1)) / per;
    if (bw < 3) bw = 3;
    double bh = (h - (rows - 1) * 6.0) / rows;
    if (bh < 4) bh = 4;
    for (int i = 0; i < n; i++) {
        int r = i / per, c = i % per;
        double bx = x + c * (bw + gap);
        double by = y + r * (bh + 6.0);
        v_round_rect(cr, bx, by, bw, bh, 2.5);
        v_set_rgb(cr, C_SURFACE0, 0.7);
        cairo_fill(cr);
        double v = pct[i] / 100.0;
        if (v < 0) v = 0;
        if (v > 1) v = 1;
        double fh = bh * v;
        if (fh < 2.0) fh = 2.0; /* 空闲也留一条可见的亮线 */
        v_round_rect(cr, bx, by + bh - fh, bw, fh, 2.5);
        v_set_rgb(cr, pct[i] > 80 ? C_RED : (pct[i] > 50 ? C_PEACH : C_BLUE), 0.95);
        cairo_fill(cr);
    }
}

/* 量程量化：1/2/5×10ⁿ。曲线不再随「历史最大值」连续缩放 ⇒ 不会整体跳。 */
double v_nice_ceil(double v) {
    if (!(v > 0)) return 1.0;
    double e = pow(10.0, floor(log10(v)));
    double m = v / e;
    double n = (m <= 1.0) ? 1.0 : (m <= 2.0) ? 2.0 : (m <= 5.0) ? 5.0 : 10.0;
    return n * e;
}

static void spark_path(cairo_t *cr, double x, double y, double w, double h, const ring_t *r,
                       double vmax) {
    double step = w / (double)(RING_CAP - 1);
    double x0 = x + w - step * (r->len - 1);
    cairo_new_path(cr);
    for (int i = 0; i < r->len; i++) {
        double v = ring_at(r, i) / vmax;
        if (v < 0) v = 0;
        if (v > 1) v = 1;
        if (i == 0) cairo_move_to(cr, x0 + step * i, y + h - v * h);
        else cairo_line_to(cr, x0 + step * i, y + h - v * h);
    }
}

static void spark_draw(cairo_t *cr, double x, double y, double w, double h, const ring_t *r,
                       uint32_t col, double vmax, double fill_a) {
    if (!r || r->len < 2) return;
    if (vmax <= 0) vmax = v_nice_ceil(ring_max(r, 1.0));
    double step = w / (double)(RING_CAP - 1);
    double x0 = x + w - step * (r->len - 1);
    /* 面积填充 */
    spark_path(cr, x, y, w, h, r, vmax);
    cairo_line_to(cr, x0 + step * (r->len - 1), y + h);
    cairo_line_to(cr, x0, y + h);
    cairo_close_path(cr);
    v_set_rgb(cr, col, fill_a);
    cairo_fill(cr);
    /* 折线 */
    spark_path(cr, x, y, w, h, r, vmax);
    v_set_rgb(cr, col, 1.0);
    cairo_set_line_width(cr, 1.6);
    cairo_stroke(cr);
}

/* 零基线：让「还没攒够数据」的空曲线区看起来是「0 值」而不是「坏了」 */
static void spark_base(cairo_t *cr, double x, double y, double w, double h) {
    cairo_rectangle(cr, x, y + h - 1.0, w, 1.0);
    v_set_rgb(cr, C_SURFACE1, 0.30);
    cairo_fill(cr);
}

void v_spark(cairo_t *cr, double x, double y, double w, double h, const ring_t *r, uint32_t col,
             double vmax) {
    spark_base(cr, x, y, w, h);
    spark_draw(cr, x, y, w, h, r, col, vmax, 0.16);
}

void v_spark2(cairo_t *cr, double x, double y, double w, double h, const ring_t *r1, uint32_t c1,
              const ring_t *r2, uint32_t c2, double vmax) {
    if (vmax <= 0) {
        double m = 1.0;
        if (r1) m = ring_max(r1, m);
        if (r2) m = ring_max(r2, m);
        vmax = v_nice_ceil(m);
    }
    spark_base(cr, x, y, w, h);
    spark_draw(cr, x, y, w, h, r1, c1, vmax, 0.14);
    spark_draw(cr, x, y, w, h, r2, c2, vmax, 0.14);
}

uint32_t v_temp_color(double t) {
    if (t < 0) return C_OVERLAY0;
    if (t < 70) return C_TEAL;
    if (t < 85) return C_PEACH;
    return C_RED;
}

/* ---------------- 格式化 ---------------- */
void v_fmt_mb(double mb, char *out, size_t n) {
    if (mb >= 1024) snprintf(out, n, "%.1fG", mb / 1024.0);
    else snprintf(out, n, "%.0fM", mb);
}

void v_fmt_rate(double bps, char *out, size_t n) {
    if (bps < 1024) snprintf(out, n, "%.0fB", bps);
    else if (bps < 1048576) snprintf(out, n, "%.1fK", bps / 1024.0);
    else snprintf(out, n, "%.1fM", bps / 1048576.0);
}

void v_fmt_uptime(double s, char *out, size_t n) {
    int d = (int)(s / 86400), h = (int)(s / 3600) % 24, mi = (int)(s / 60) % 60;
    if (d > 0) snprintf(out, n, "%dd%dh%dm", d, h, mi);
    else snprintf(out, n, "%dh%dm", h, mi);
}
