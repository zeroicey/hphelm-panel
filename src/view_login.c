/* view_login.c —— 登录页版面
 *
 * 与面板同一套「无卡片」风格：整屏渐变底 + 居中的时钟/用户/输入框，
 * **没有外层卡片**（原来那个 C_MANTLE 圆角框已去掉）。
 * 唯一保留的「框」是密码输入框本身 —— 它是输入控件，不是装饰性卡片。
 */
#include <math.h>
#include <string.h>
#include <time.h>

#include "theme.h"
#include "view.h"

void view_login_draw(cairo_t *cr, const view_ctx_t *v, const metrics_t *m, const char *user,
                     int pwlen, const char *err, int busy, int cursor_on, int wait_s) {
    const double cx = v->lw / 2.0;
    const double cy = v->lh / 2.0;
    char b1[64], b2[64];

    v_bg(cr, v->lw, v->lh);

    /* ---- 时钟 ---- */
    strftime(b1, sizeof b1, "%H:%M", localtime(&m->now));
    strftime(b2, sizeof b2, "%Y-%m-%d %A", localtime(&m->now));
    v_text_center(cr, cx, cy - 216, v_fd_huge(), C_LAVENDER, "%s", b1);
    v_text_center(cr, cx, cy - 152, v_fd_label(), C_OVERLAY1, "%s", b2);
    v_fmt_uptime(m->uptime_s, b1, sizeof b1);
    v_text_center(cr, cx, cy - 128, v_fd_small(), C_OVERLAY0, "%s · %s · up %s", m->host,
                  m->net_up ? m->ip : "no-net", b1);

    /* ---- 细线（分区，不是卡片边框）---- */
    v_rule(cr, cx - 160, cy - 96, 320);

    /* ---- 用户 ---- */
    v_text_center(cr, cx, cy - 72, v_fd_value(), C_TEXT, "%s", user);
    v_text_center(cr, cx, cy - 44, v_fd_small(), C_OVERLAY0, "输入密码解锁");

    /* ---- 密码框（唯一的输入控件）---- */
    const double ew = 340, eh = 48;
    const double ex = cx - ew / 2, ey = cy - 22;
    v_round_rect(cr, ex, ey, ew, eh, 10);
    v_set_rgb(cr, C_SURFACE0, 0.8);
    cairo_fill_preserve(cr);
    v_set_rgb(cr, busy ? C_YELLOW : C_MAUVE, 0.95);
    cairo_set_line_width(cr, 2.0);
    cairo_stroke(cr);

    /* 圆点 + 光标 */
    double dx = ex + 20, dy = ey + eh / 2.0;
    int shown = pwlen > 12 ? 12 : pwlen;
    for (int i = 0; i < shown; i++) {
        cairo_arc(cr, dx, dy, 4.5, 0, 2 * M_PI);
        v_set_rgb(cr, C_TEXT, 1.0);
        cairo_fill(cr);
        dx += 15;
    }
    if (cursor_on && !busy) {
        cairo_rectangle(cr, dx - 1, dy - 11, 2, 22);
        v_set_rgb(cr, C_MAUVE, 1.0);
        cairo_fill(cr);
    }
    if (pwlen > 12) v_text(cr, dx + 8, dy - 9, v_fd_small(), C_OVERLAY0, "+%d", pwlen - 12);
    if (pwlen == 0 && cursor_on)
        v_text(cr, ex + 20, dy - 12, v_fd_label(), C_OVERLAY0, "按任意键开始输入");

    /* ---- 状态行 ---- */
    double sy = ey + eh + 24;
    if (busy) {
        /* 认证中的进度反馈：不确定态进度条 + 文案（原来只有一行字） */
        v_text_center(cr, cx, sy, v_fd_label(), C_YELLOW, "正在验证…");
        double pw = 200;
        v_round_rect(cr, cx - pw / 2, sy + 24, pw, 4, 2);
        v_set_rgb(cr, C_SURFACE0, 0.9);
        cairo_fill(cr);
        double phase = fmod((double)m->now, 1.0);
        if (phase < 0) phase = 0;
        double barw = pw * 0.35;
        double bx = cx - pw / 2 + (pw - barw) * phase;
        v_round_rect(cr, bx, sy + 24, barw, 4, 2);
        v_set_rgb(cr, C_YELLOW, 1.0);
        cairo_fill(cr);
    } else if (err && err[0]) {
        v_text_center(cr, cx, sy, v_fd_label(), C_RED, "%s", err);
        if (wait_s > 0)
            v_text_center(cr, cx, sy + 22, v_fd_small(), C_OVERLAY0, "%d 秒后可重试", wait_s);
    } else {
        v_text_center(cr, cx, sy, v_fd_small(), C_OVERLAY0,
                      "Enter 解锁 · Esc 清除 · 空闲后自动回面板");
    }

    /* ---- 底部 ---- */
    v_text_center(cr, cx, v->lh - 46, v_fd_small(), C_OVERLAY0,
                  "hphelm-panel · 自研锁屏（PAM）· 无 DM greeter");
}
