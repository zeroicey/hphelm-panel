/* view.c —— 文字与图形基元（cairo + pango，无 GUI 工具包） */
#include "view.h"

#include <math.h>
#include <stdarg.h>
#include <string.h>

#include "theme.h"

static PangoLayout *g_lay;
static PangoFontDescription *FD_TITLE, *FD_LABEL, *FD_VALUE, *FD_BIG, *FD_SMALL, *FD_HUGE;

static PangoFontDescription *mk(const char *s) { return pango_font_description_from_string(s); }

void view_init(PangoLayout *lay) {
    g_lay = lay;
    FD_TITLE = mk(F_TITLE);
    FD_LABEL = mk(F_LABEL);
    FD_VALUE = mk(F_VALUE);
    FD_BIG = mk(F_BIG);
    FD_SMALL = mk(F_SMALL);
    FD_HUGE = mk(F_HUGE);
}

PangoFontDescription *v_fd_title(void) { return FD_TITLE; }
PangoFontDescription *v_fd_label(void) { return FD_LABEL; }
PangoFontDescription *v_fd_value(void) { return FD_VALUE; }
PangoFontDescription *v_fd_big(void) { return FD_BIG; }
PangoFontDescription *v_fd_small(void) { return FD_SMALL; }
PangoFontDescription *v_fd_huge(void) { return FD_HUGE; }

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

double v_text(cairo_t *cr, double x, double y, PangoFontDescription *fd, uint32_t rgb,
              const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    pango_layout_set_font_description(g_lay, fd);
    pango_layout_set_text(g_lay, buf, -1);
    PangoRectangle r;
    pango_layout_get_extents(g_lay, NULL, &r);
    v_set_rgb(cr, rgb, 1.0);
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, g_lay);
    return (double)r.width / PANGO_SCALE;
}

void v_text_right(cairo_t *cr, double xr, double y, PangoFontDescription *fd, uint32_t rgb,
                  const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    v_text(cr, xr - v_text_w(fd, buf), y, fd, rgb, "%s", buf);
}

void v_text_center(cairo_t *cr, double cx, double y, PangoFontDescription *fd, uint32_t rgb,
                   const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    v_text(cr, cx - v_text_w(fd, buf) / 2.0, y, fd, rgb, "%s", buf);
}

void v_round_rect(cairo_t *cr, double x, double y, double w, double h, double r) {
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
}

void v_panel_box(cairo_t *cr, double x, double y, double w, double h) {
    v_round_rect(cr, x, y, w, h, 10);
    v_set_rgb(cr, C_MANTLE, 0.62);
    cairo_fill_preserve(cr);
    v_set_rgb(cr, C_SURFACE1, 0.55);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
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

void v_spark(cairo_t *cr, double x, double y, double w, double h, const ring_t *r,
             uint32_t col) {
    if (r->len < 2) return;
    double mx = ring_max(r, 1.0);
    double step = w / (double)(RING_CAP - 1);
    double x0 = x + w - step * (r->len - 1);
    cairo_new_path(cr);
    cairo_move_to(cr, x0, y + h);
    for (int i = 0; i < r->len; i++) {
        double v = ring_at(r, i) / mx;
        if (v > 1) v = 1;
        cairo_line_to(cr, x0 + step * i, y + h - v * h);
    }
    cairo_line_to(cr, x0 + step * (r->len - 1), y + h);
    cairo_close_path(cr);
    v_set_rgb(cr, col, 0.22);
    cairo_fill(cr);

    cairo_new_path(cr);
    for (int i = 0; i < r->len; i++) {
        double v = ring_at(r, i) / mx;
        if (v > 1) v = 1;
        if (i == 0) cairo_move_to(cr, x0 + step * i, y + h - v * h);
        else cairo_line_to(cr, x0 + step * i, y + h - v * h);
    }
    v_set_rgb(cr, col, 1.0);
    cairo_set_line_width(cr, 1.6);
    cairo_stroke(cr);
}

uint32_t v_temp_color(double t) {
    if (t < 0) return C_OVERLAY0;
    if (t < 70) return C_TEAL;
    if (t < 85) return C_PEACH;
    return C_RED;
}

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
