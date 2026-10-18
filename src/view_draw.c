/* view_draw.c —— 两个界面：运维面板 / 登录页 */
#include <math.h>
#include <string.h>
#include <time.h>

#include "theme.h"
#include "view.h"

/* ============================ 面板 ============================ */
void view_panel_draw(cairo_t *cr, const view_ctx_t *v, const metrics_t *m) {
    const double mx = L_MARGIN, mw = v->lw - 2 * mx;
    const double cpu_w = (mw - L_GAP) * L_CPU_W_FRAC;
    const double mem_x = mx + cpu_w + L_GAP;
    const double mem_w = mw - cpu_w - L_GAP;
    char b1[64], b2[64];
    double px, py, x;

    v_set_rgb(cr, C_BASE, 1.0);
    cairo_paint(cr);

    /* ---- 顶栏 ---- */
    v_panel_box(cr, mx, mx, mw, L_HEADER_H);
    double hy = mx + 13;
    v_text(cr, mx + 16, hy, v_fd_title(), C_MAUVE, "%s", m->host);
    x = mx + 16 + v_text_w(v_fd_title(), m->host) + 14;
    x += v_text(cr, x, hy + 1, v_fd_label(), m->net_up ? C_GREEN : C_RED, "%s",
                m->net_up ? m->ip : "no-net") +
         14;
    v_fmt_uptime(m->uptime_s, b1, sizeof b1);
    x += v_text(cr, x, hy + 1, v_fd_label(), C_OVERLAY1, "up %s", b1) + 14;
    x += v_text(cr, x, hy + 1, v_fd_label(), C_OVERLAY1, "%d 核", m->ncore);
    strftime(b2, sizeof b2, "%Y-%m-%d %H:%M:%S", localtime(&m->now));
    v_text_right(cr, mx + mw - 16, hy + 1, v_fd_label(), C_SUBTEXT1, "%s", b2);
    v_round_rect(cr, mx + mw - 16 - v_text_w(v_fd_label(), "24×7") - 8, hy + 5, 6, 6, 3);
    v_set_rgb(cr, C_GREEN, 1.0);
    cairo_fill(cr);
    v_text_right(cr, mx + mw - 16 - 12, hy + 1, v_fd_label(), C_GREEN, "24×7");

    /* ---- CPU ---- */
    v_panel_box(cr, mx, L_ROW1_Y, cpu_w, L_ROW1_H);
    px = mx + 18;
    py = L_ROW1_Y + 14;
    v_text(cr, px, py, v_fd_title(), C_MAUVE, "CPU");
    v_text_right(cr, mx + cpu_w - 18, py, v_fd_title(), v_temp_color(m->temp_c), "%.1f°C",
                 m->temp_c);
    py += 26;
    double bigw = v_text(cr, px, py, v_fd_big(), C_TEXT, "%.0f%%", m->cpu_pct);
    v_text(cr, px + bigw + 14, py + 12, v_fd_label(), C_OVERLAY1, "%s · %ldMHz", m->governor,
           m->freq_mhz);
    py += 42;
    v_bar(cr, px, py, cpu_w - 36, 10, m->cpu_pct / 100.0, C_MAUVE);
    py += 20;
    if (m->ncore > 0) {
        double bw = 6, gap = 3, bx = px;
        for (int i = 0; i < m->ncore && i < 16; i++) {
            double hh = 26 * (m->core_pct[i] / 100.0);
            v_round_rect(cr, bx, py + (26 - hh), bw, hh < 1 ? 1 : hh, 1.5);
            v_set_rgb(cr, m->core_pct[i] > 80 ? C_RED : C_BLUE, 0.95);
            cairo_fill(cr);
            bx += bw + gap;
        }
        v_text(cr, px + m->ncore * (bw + gap) + 10, py + 8, v_fd_small(), C_OVERLAY1,
               "load %.2f %.2f %.2f", m->load[0], m->load[1], m->load[2]);
        py += 34;
    }
    v_spark(cr, px, py, cpu_w - 36, L_ROW1_Y + L_ROW1_H - 24 - py, &m->cpu_hist, C_MAUVE);
    v_text(cr, px, L_ROW1_Y + L_ROW1_H - 20, v_fd_small(), C_OVERLAY0, "CPU 5 分钟");

    /* ---- 内存 ---- */
    v_panel_box(cr, mem_x, L_ROW1_Y, mem_w, L_ROW1_H);
    px = mem_x + 18;
    py = L_ROW1_Y + 14;
    v_text(cr, px, py, v_fd_title(), C_BLUE, "内存");
    v_fmt_mb(m->mem_used_mb, b1, sizeof b1);
    v_fmt_mb(m->mem_total_mb, b2, sizeof b2);
    v_text_right(cr, mem_x + mem_w - 18, py, v_fd_title(), C_TEXT, "%s / %s", b1, b2);
    py += 26;
    v_text(cr, px, py, v_fd_big(), C_TEXT, "%.0f%%", m->mem_pct);
    py += 42;
    v_bar(cr, px, py, mem_w - 36, 10, m->mem_pct / 100.0, C_BLUE);
    py += 22;
    v_fmt_mb(m->swap_used_mb, b1, sizeof b1);
    v_fmt_mb(m->swap_total_mb, b2, sizeof b2);
    v_text(cr, px, py, v_fd_label(), C_OVERLAY1, "swap %s / %s (zram)", b1, b2);
    py += 20;
    if (m->have_ssd)
        v_text(cr, px, py, v_fd_label(), C_OVERLAY1, "SSD 读 %.1fG 写 %.1fG", m->ssd_read_gb,
               m->ssd_written_gb);
    py += 22;
    v_fmt_mb(m->disk_total_gb * 1024, b1, sizeof b1);
    v_text(cr, px, py, v_fd_label(), C_SUBTEXT0, "磁盘 /  %s", b1);
    v_text_right(cr, mem_x + mem_w - 18, py, v_fd_label(),
                 m->disk_used_pct > 90 ? C_RED : C_SUBTEXT0, "%.0f%%", m->disk_used_pct);
    py += 20;
    v_bar(cr, px, py, mem_w - 36, 8, m->disk_used_pct / 100.0,
          m->disk_used_pct > 90 ? C_RED : C_TEAL);

    /* ---- 网络 ---- */
    v_panel_box(cr, mx, L_ROW2_Y, mw, L_ROW2_H);
    px = mx + 18;
    py = L_ROW2_Y + 12;
    v_text(cr, px, py, v_fd_title(), C_GREEN, "网络");
    double nx = px + v_text_w(v_fd_title(), "网络") + 16;
    nx += v_text(cr, nx, py + 1, v_fd_label(), C_TEXT, "%s", m->iface) + 12;
    nx += v_text(cr, nx, py + 1, v_fd_label(), m->net_up ? C_GREEN : C_RED, "%s",
                 m->net_up ? m->ip : "down") +
          20;
    v_fmt_rate(m->tx_bps, b1, sizeof b1);
    v_fmt_rate(m->rx_bps, b2, sizeof b2);
    nx += v_text(cr, nx, py + 1, v_fd_label(), C_PEACH, "↑%s/s", b1) + 12;
    v_text(cr, nx, py + 1, v_fd_label(), C_TEAL, "↓%s/s", b2);
    v_text_right(cr, mx + mw - 18, py, v_fd_label(), m->ts_up ? C_GREEN : C_OVERLAY0,
                 "tailscale %s", m->ts_up ? m->ts_ip : "off");
    v_spark(cr, px, py + 24, mw - 36, L_ROW2_H - 40, &m->rx_hist, C_TEAL);

    /* ---- 进程榜 ---- */
    v_panel_box(cr, mx, L_ROW3_Y, mw, L_ROW3_H);
    px = mx + 18;
    py = L_ROW3_Y + 12;
    v_text(cr, px, py, v_fd_title(), C_YELLOW, "进程榜");
    v_text_right(cr, mx + mw - 18, py, v_fd_label(), C_OVERLAY1, "共 %d 个进程 · 按内存排序",
                 m->nproc);
    py += 28;
    for (int i = 0; i < m->ntop; i++) {
        double col = px + (i % 3) * ((mw - 36) / 3);
        double row = py + (i / 3) * 46;
        v_fmt_mb(m->top[i].rss_mb, b1, sizeof b1);
        v_text(cr, col, row, v_fd_value(), C_TEXT, "%s", m->top[i].name);
        v_text(cr, col, row + 22, v_fd_label(), C_OVERLAY1, "%s  ·  pid %d", b1, m->top[i].pid);
    }

    /* ---- 底栏 ---- */
    v_panel_box(cr, mx, L_FOOT_Y, mw, L_FOOT_H);
    py = L_FOOT_Y + 24;
    px = mx + 18;
    px += v_text(cr, px, py, v_fd_label(), C_SUBTEXT0, "磁盘 / %.0f%%", m->disk_used_pct) + 26;
    px += v_text(cr, px, py, v_fd_label(), C_SUBTEXT0, "负载 %.2f", m->load[0]) + 26;
    px += v_text(cr, px, py, v_fd_label(), v_temp_color(m->temp_c), "温度 %.1f°C", m->temp_c) +
          26;
    px += v_text(cr, px, py, v_fd_label(), C_SUBTEXT0, "内存 %.0f%%", m->mem_pct) + 26;
    v_text(cr, px, py, v_fd_label(), C_SUBTEXT0, "进程 %d", m->nproc);
    v_text_right(cr, mx + mw - 18, py, v_fd_label(), C_OVERLAY1, "hphelm-panel · cairo+pango · 1Hz");
}

/* ============================ 登录页 ============================ */
void view_login_draw(cairo_t *cr, const view_ctx_t *v, const metrics_t *m, const char *user,
                     int pwlen, const char *err, int busy, int cursor_on, int wait_s) {
    const double cx = v->lw / 2.0;
    const double cy = v->lh / 2.0;
    const double box_w = 460, box_h = 300;
    char b1[64], b2[64];

    /* 背景：整屏渐变（比纯色更有「锁屏」质感） */
    cairo_pattern_t *g = cairo_pattern_create_linear(0, 0, 0, v->lh);
    cairo_pattern_add_color_stop_rgb(g, 0.0, 0.043, 0.071, 0.125);
    cairo_pattern_add_color_stop_rgb(g, 1.0, 0.008, 0.012, 0.024);
    cairo_set_source(cr, g);
    cairo_paint(cr);
    cairo_pattern_destroy(g);

    /* 顶部：主机 + 时间（像锁屏） */
    strftime(b1, sizeof b1, "%H:%M", localtime(&m->now));
    strftime(b2, sizeof b2, "%Y-%m-%d %A", localtime(&m->now));
    v_text_center(cr, cx, cy - 190, v_fd_huge(), C_LAVENDER, "%s", b1);
    v_text_center(cr, cx, cy - 128, v_fd_label(), C_OVERLAY1, "%s", b2);
    v_text_center(cr, cx, cy - 104, v_fd_label(), C_OVERLAY0, "%s · %s · up %s", m->host,
                  m->net_up ? m->ip : "no-net",
                  (v_fmt_uptime(m->uptime_s, b2, sizeof b2), b2));

    /* 卡片 */
    v_round_rect(cr, cx - box_w / 2, cy - box_h / 2 + 40, box_w, box_h, 14);
    v_set_rgb(cr, C_MANTLE, 0.96);
    cairo_fill_preserve(cr);
    v_set_rgb(cr, C_MAUVE, 0.55);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    double y = cy - box_h / 2 + 40 + 28;
    v_text_center(cr, cx, y, v_fd_title(), C_MAUVE, "hphelm");
    y += 30;
    v_text_center(cr, cx, y, v_fd_label(), C_OVERLAY1, "输入密码解锁");
    y += 46;

    /* 用户 */
    v_text_center(cr, cx, y, v_fd_value(), C_TEXT, "%s", user);
    y += 40;

    /* 密码框 */
    double ew = 300, eh = 46;
    double ex = cx - ew / 2, ey = y;
    v_round_rect(cr, ex, ey, ew, eh, 10);
    v_set_rgb(cr, C_SURFACE1, 1.0);
    cairo_fill_preserve(cr);
    v_set_rgb(cr, busy ? C_YELLOW : C_MAUVE, 0.95);
    cairo_set_line_width(cr, 2.0);
    cairo_stroke(cr);

    /* 圆点 + 光标 */
    double dx = ex + 18, dy = ey + eh / 2.0;
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
    if (pwlen > 12)
        v_text(cr, dx + 8, dy - 9, v_fd_small(), C_OVERLAY0, "+%d", pwlen - 12);
    if (pwlen == 0 && cursor_on)
        v_text(cr, ex + 18, dy - 12, v_fd_label(), C_OVERLAY0, "按任意键开始输入");

    y += eh + 22;

    /* 状态行 */
    if (busy) {
        v_text_center(cr, cx, y, v_fd_label(), C_YELLOW, "正在验证…");
    } else if (err && err[0]) {
        v_text_center(cr, cx, y, v_fd_label(), C_RED, "%s", err);
        if (wait_s > 0)
            v_text_center(cr, cx, y + 22, v_fd_small(), C_OVERLAY0, "%d 秒后可重试", wait_s);
    } else {
        v_text_center(cr, cx, y, v_fd_small(), C_OVERLAY0, "Enter 解锁 · Esc 清除 · 空闲后自动回面板");
    }

    /* 底部提示 */
    v_text_center(cr, cx, v->lh - 60, v_fd_small(), C_OVERLAY0,
                  "hphelm-panel · 自研锁屏（PAM）· 无 DM greeter");
}
