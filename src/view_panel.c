/* view_panel.c —— 运维面板版面
 *
 * 版式（2026-10-18 第三轮，用户口径「不要卡片 / 更紧凑」）：
 *
 *   ┌ 顶栏（一行，无边框）──────────────────────────────────────────────┐
 *   │ hphelm 192.168.5.6 up 8h06m 8 核 · eno1        2026-10-18 22:45 ● 24×7
 *   ├────────────────────────────── 细线 ──────────────────────────────┤
 *   │ CPU              55.2°C │ 内存      5.1G/15.4G │ 网络   tailscale │
 *   │ 12%  perf·3.2GHz  load  │ 32%                  │ ↓8.4K/s ↑1.2K/s  │
 *   │ [███████░░░░░░░░░░░░░░] │ [█████░░░░░░░░░░░░░] │                  │
 *   │ ▁▂▃▅▂▁▂▃（每核，带轨道）│ swap/SSD/磁盘         │ [↓↑ 双曲线 5min] │
 *   │ [CPU 5min 曲线]         │ [内存 5min 曲线]      │                  │
 *   ├────────────────────────────── 细线 ──────────────────────────────┤
 *   │ 进程榜  共 217 个              按内存 (RSS) · 每 3 秒刷新          │
 *   │ 1 code    1.2G   11 chrome 980M   21 ...   31 ...   ← 4 列 × 10 行 │
 *   └──────────────────────────────────────────────────────────────────┘
 *
 * **没有卡片**：分区只靠「一条 1px 细线 + 字号层级 + 留白」，没有任何圆角框/底色块。
 */
#include <math.h>
#include <string.h>
#include <time.h>

#include "theme.h"
#include "view.h"

/* 进程榜排序口径（与 read_procs 一致）*/
static void draw_procs(cairo_t *cr, const metrics_t *m, double x, double y, double w, double h) {
    const int cols = L_PROC_COLS, rows = L_PROC_ROWS;
    const double cgap = 30.0;
    const double cw = (w - cgap * (cols - 1)) / cols;
    const double rh = h / rows;
    for (int i = 0; i < m->ntop && i < cols * rows; i++) {
        /* 逐列向下填：第 1 列放前 10 名，第 2 列放 11~20 名 …… 按内存从高到低 */
        int c = i / rows, r = i % rows;
        double cx = x + c * (cw + cgap);
        double ry = y + r * rh + (rh - 19.0) / 2.0;
        char mb[32];
        v_fmt_mb(m->top[i].rss_mb, mb, sizeof mb);
        /* 名次：暗、小，只做阅读锚点 */
        v_text_right(cr, cx + 20, ry + 3, v_fd_small(), C_OVERLAY0, "%d", i + 1);
        /* 进程名 */
        double nw = v_text(cr, cx + 28, ry, v_fd_proc(), C_TEXT, "%s", m->top[i].name);
        /* pid：暗、小（要 kill 的时候用得上） */
        v_text(cr, cx + 28 + nw + 8, ry + 4, v_fd_small(), C_OVERLAY0, "%d", m->top[i].pid);
        /* 内存：右对齐到本列右边界 */
        v_text_right(cr, cx + cw, ry, v_fd_proc(), C_OVERLAY1, "%s", mb);
    }
}

void view_panel_draw(cairo_t *cr, const view_ctx_t *v, const metrics_t *m) {
    const double mx = L_MARGIN;
    const double mw = v->lw - 2 * mx;
    char b1[64], b2[64];
    double x, py;

    v_bg(cr, v->lw, v->lh);

    /* ==================== 顶栏（一行，无边框） ==================== */
    x = mx;
    x += v_text(cr, x, L_TOP_Y, v_fd_title(), C_MAUVE, "%s", m->host) + 16;
    x += v_text(cr, x, L_TOP_Y + 1, v_fd_label(), m->net_up ? C_GREEN : C_RED, "%s",
                m->net_up ? m->ip : "no-net") + 16;
    v_fmt_uptime(m->uptime_s, b1, sizeof b1);
    x += v_text(cr, x, L_TOP_Y + 1, v_fd_label(), C_OVERLAY1, "up %s", b1) + 16;
    v_text(cr, x, L_TOP_Y + 1, v_fd_label(), C_OVERLAY1, "%d 核 · %s", m->ncore, m->iface);

    strftime(b2, sizeof b2, "%Y-%m-%d %H:%M:%S", localtime(&m->now));
    v_text_right(cr, mx + mw, L_TOP_Y + 1, v_fd_label(), C_SUBTEXT1, "%s", b2);
    /* 24×7 标记：放在时间左边 */
    double w247 = v_text_w(v_fd_label(), "24×7");
    double bx = mx + mw - v_text_w(v_fd_label(), b2) - 20;
    v_text_right(cr, bx, L_TOP_Y + 1, v_fd_label(), C_GREEN, "24×7");
    cairo_arc(cr, bx - w247 - 11, L_TOP_Y + 8, 3.0, 0, 2 * M_PI);
    v_set_rgb(cr, C_GREEN, 1.0);
    cairo_fill(cr);

    v_rule(cr, mx, L_RULE1_Y, mw);

    /* ==================== 系统三栏 ==================== */
    const double avail = mw - 2 * L_COL_GAP;
    const double w0 = avail * L_CPU_FRAC, w1 = avail * L_MEM_FRAC;
    const double cw2[3] = {w0, w1, avail - w0 - w1};   /* ⚠️ 第三栏必须算出来，别写 0 */
    const double cx[3] = {mx, mx + w0 + L_COL_GAP, mx + w0 + w1 + 2 * L_COL_GAP};

    /* ---------- CPU ---------- */
    {
        const double w = cw2[0];
        py = L_SYS_Y;
        v_text(cr, cx[0], py, v_fd_title(), C_MAUVE, "CPU");
        v_text_right(cr, cx[0] + w, py, v_fd_title(), v_temp_color(m->temp_c), "%.1f°C",
                     m->temp_c);
        py += 22;
        double bw = v_text(cr, cx[0], py, v_fd_big(), C_TEXT, "%.0f%%", m->cpu_pct);
        v_text(cr, cx[0] + bw + 14, py + 13, v_fd_label(), C_OVERLAY1, "%s · %ldMHz",
               m->governor, m->freq_mhz);
        v_text_right(cr, cx[0] + w, py + 15, v_fd_small(), C_OVERLAY0, "load %.2f %.2f %.2f",
                     m->load[0], m->load[1], m->load[2]);
        py += 38;
        v_bar(cr, cx[0], py, w, 12, m->cpu_pct / 100.0, C_MAUVE);
        py += 22;
        v_core_bars(cr, cx[0], py, w, 54, m->core_pct, m->ncore);
        py += 62;
        v_text_right(cr, cx[0] + w, py, v_fd_small(), C_OVERLAY0, "5 分钟");
        v_spark(cr, cx[0], py + 14, w, L_SYS_Y + L_SYS_H - (py + 14), &m->cpu_hist, C_MAUVE,
                100.0);
    }

    /* ---------- 内存 ---------- */
    {
        const double w = cw2[1];
        py = L_SYS_Y;
        v_text(cr, cx[1], py, v_fd_title(), C_BLUE, "内存");
        v_fmt_mb(m->mem_used_mb, b1, sizeof b1);
        v_fmt_mb(m->mem_total_mb, b2, sizeof b2);
        v_text_right(cr, cx[1] + w, py, v_fd_title(), C_TEXT, "%s / %s", b1, b2);
        py += 22;
        v_text(cr, cx[1], py, v_fd_big(), C_TEXT, "%.0f%%", m->mem_pct);
        py += 38;
        v_bar(cr, cx[1], py, w, 12, m->mem_pct / 100.0, C_BLUE);
        py += 20;
        v_fmt_mb(m->swap_used_mb, b1, sizeof b1);
        v_fmt_mb(m->swap_total_mb, b2, sizeof b2);
        v_text(cr, cx[1], py, v_fd_label(), C_OVERLAY1, "swap %s / %s (zram)", b1, b2);
        py += 21;
        if (m->have_ssd)
            v_text(cr, cx[1], py, v_fd_label(), C_OVERLAY1, "SSD 读 %.1fG · 写 %.1fG",
                   m->ssd_read_gb, m->ssd_written_gb);
        py += 21;
        v_text(cr, cx[1], py, v_fd_label(), C_SUBTEXT0, "磁盘 /");
        v_fmt_mb(m->disk_total_gb * 1024, b1, sizeof b1);
        v_text_right(cr, cx[1] + w, py, v_fd_label(),
                     m->disk_used_pct > 90 ? C_RED : C_SUBTEXT0, "%.0f%% · %s", m->disk_used_pct,
                     b1);
        py += 20;
        v_bar(cr, cx[1], py, w, 10, m->disk_used_pct / 100.0,
              m->disk_used_pct > 90 ? C_RED : C_TEAL);
        py += 18;
        v_text_right(cr, cx[1] + w, py, v_fd_small(), C_OVERLAY0, "5 分钟");
        v_spark(cr, cx[1], py + 12, w, L_SYS_Y + L_SYS_H - (py + 12), &m->mem_hist, C_BLUE,
                100.0);
    }

    /* ---------- 网络 ---------- */
    {
        const double w = cw2[2];
        py = L_SYS_Y;
        v_text(cr, cx[2], py, v_fd_title(), C_GREEN, "网络");
        v_text_right(cr, cx[2] + w, py + 1, v_fd_label(), m->ts_up ? C_GREEN : C_OVERLAY0,
                     "tailscale %s", m->ts_up ? m->ts_ip : "off");
        py += 22;
        v_fmt_rate(m->rx_bps, b1, sizeof b1);
        v_fmt_rate(m->tx_bps, b2, sizeof b2);
        double rw = v_text(cr, cx[2], py, v_fd_value(), C_TEAL, "↓ %s/s", b1);
        v_text(cr, cx[2] + rw + 22, py, v_fd_value(), C_PEACH, "↑ %s/s", b2);
        py += 40;
        v_text_right(cr, cx[2] + w, py, v_fd_small(), C_OVERLAY0, "5 分钟");
        v_spark2(cr, cx[2], py + 14, w, L_SYS_Y + L_SYS_H - (py + 14), &m->rx_hist, C_TEAL,
                 &m->tx_hist, C_PEACH, 0);
    }

    /* ==================== 进程榜 ==================== */
    v_rule(cr, mx, L_RULE2_Y, mw);
    py = L_PROC_HDR_Y;
    x = mx;
    x += v_text(cr, x, py, v_fd_title(), C_YELLOW, "进程榜") + 16;
    v_text(cr, x, py + 1, v_fd_label(), C_OVERLAY1, "共 %d 个", m->nproc);
    v_text_right(cr, mx + mw, py + 2, v_fd_small(), C_OVERLAY0, "按内存 (RSS) · 每 %d 秒刷新",
                 3);

    double list_h = v->lh - L_MARGIN - L_PROC_LIST_Y;
    if (list_h > 40) draw_procs(cr, m, mx, L_PROC_LIST_Y, mw, list_h);
}
