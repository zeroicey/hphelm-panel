/* view_panel.c —— 运维面板版面（三列：本机 │ 生产服务器 │ 进程榜）
 *
 * 2026-10-21 第四版（用户口径）：
 *   ① 「进程排行榜只占一列」⇒ 进程榜固定在**最右一列**，纵向占满整屏
 *   ② 「一块屏幕上同时显示本机跟生产服务器的资源监控」⇒ 左两列是两台主机，**对称**
 *   ③ 「不要轮播，两台常驻同屏」⇒ 不做相位/动画，内容签名照旧（不闪的判据不变）
 *
 *   ┌ 顶栏（一行）：hphelm · 生产 hpcore ● online │ 2026-10-21 16:44:02 ● 24×7 ┐
 *   ├─────────────┬─────────────┬──────────────────────────────────────────────┤
 *   │ hphelm ●    │ hpcore ●    │ 进程榜  本机 共 249                          │
 *   │ up 3d02h 8核│ up 1d15h 16核│ 1 codex  430M   pid                       │
 *   │ CPU  58.2°C │ CPU  42.9°C │ … 12 行                                      │
 *   │ 13% sched…  │ 31% powersv │ ── 生产 hpcore ─────────────                 │
 *   │ [CPU 条]    │ [CPU 条]    │ 1 magpie  211M                              │
 *   │ 每核柱      │ 每核柱      │ … 12 行                                      │
 *   │ [CPU 5min]  │ [CPU 5min]  │                                              │
 *   │ 内存 21%    │ 内存 10%    │                                              │
 *   │ …           │ …           │                                              │
 *   └─────────────┴─────────────┴──────────────────────────────────────────────┘
 *
 * **没有卡片**：分区只靠「1px 细线 + 字号层级 + 留白」，没有圆角框/底色块。
 * **一个 draw_host() 画两台**：版面、口径、字号全部对称 —— 不会出现「一台多一行」。
 */
#include <math.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>

#include "theme.h"
#include "view.h"

/* ============================ 进程榜（一列，两段） ============================ */
static void draw_proc_section(cairo_t *cr, const metrics_t *m, const char *tag, uint32_t tag_col,
                              double x, double y, double w, int rows) {
    char b[48];
    /* 段标题：主机名 + 进程总数（同一行，靠右是排序口径） */
    double tw = v_text(cr, x, y, v_fd_title(), tag_col, "%s", tag);
    v_text(cr, x + tw + 14, y + 1, v_fd_label(), C_OVERLAY1, "共 %d 个", m->nproc);
    v_text_right(cr, x + w, y + 2, v_fd_small(), C_OVERLAY0, "按内存 (RSS)");
    y += 28;

    int n = m->ntop < rows ? m->ntop : rows;
    if (n == 0) {
        v_text(cr, x, y, v_fd_small(), C_OVERLAY0, "（还没有数据）");
        return;
    }
    /* 列定位：名次 | 名称 | pid | 内存（右对齐） —— 四段位置按最宽行预算，避免逐行抖动 */
    const double x_rank = x + 18;      /* 名次右边界 */
    const double x_name = x + 26;      /* 名称左边界 */
    const double x_mem = x + w;        /* 内存右边界 */
    for (int i = 0; i < n; i++) {
        double ry = y + i * L_PROC_ROW_H;
        if (i == 0) { /* 表头式的极淡基线，方便眼睛对齐（不是卡片框） */
            cairo_rectangle(cr, x, ry - 4, w, 1.0);
            v_set_rgb(cr, C_SURFACE1, 0.25);
            cairo_fill(cr);
        }
        v_text_right(cr, x_rank, ry + 4, v_fd_small(), C_OVERLAY0, "%d", i + 1);
        v_fmt_mb(m->top[i].rss_mb, b, sizeof b);
        /* 内存：先右对齐画掉（返回宽度），省掉一次 v_text_w */
        double memw = v_text_right_w(cr, x_mem, ry, v_fd_proc(), C_OVERLAY1, "%s", b);
        /* 名称预算 = 列宽 - 内存 - pid 预算。字体等宽 ⇒ 按字符数估算，不做逐字符测量
         * （逐字符测是每帧几百次 pango 整形，实测让单帧绘制时间翻倍）。 */
        double pidw = v_char_w(v_fd_small()) * 8 + 8;
        double name_max = x_mem - memw - 12 - (x_name + pidw);
        if (name_max < 60) name_max = 60;
        char nm[48];
        v_truncate_mono(nm, sizeof nm, m->top[i].name, v_fd_proc(), name_max);
        double nw = v_text(cr, x_name, ry, v_fd_proc(), C_TEXT, "%s", nm);
        /* pid：跟名称实际宽度走，别留死空位 */
        if (x_name + nw + pidw < x_mem - memw - 8)
            v_text(cr, x_name + nw + 10, ry + 4, v_fd_small(), C_OVERLAY0, "%d", m->top[i].pid);
        v_text_right(cr, x_mem, ry, v_fd_proc(), C_OVERLAY1, "%s", b);
    }
}

/* ============================ 一台主机的一整列 ============================
 *
 * 2026-10-21 第五版（用户口径）：
 *   ① 「每一个资源的标题能不能去掉，尽量让文字部分只占一行」⇒ 标题嵌在数值行里，没有独立标题行
 *   ② 「进度条/图表压缩一点，不要留废话空间」⇒ 行高 24、条高 10、曲线吃掉剩余高度
 *   ③ 「竖着看下来还是挺空的，放一些别的资源」⇒ 新增：缓存/swap、丢包错包、PSI 压力、
 *      线程数、累计流量、温度曲线
 *
 * 纵向是**流式**的：`y` 一行一行往下走，四张曲线最后均分剩下的高度。
 * ⚠️ 不要改回写死绝对坐标 —— 上一版加了内容就把底部顶出屏幕（且编译零告警）。
 */
static void draw_host(cairo_t *cr, const view_ctx_t *v, const metrics_t *m, double x, double w,
                      uint32_t accent, const char *role, int is_remote, int online, double age_s,
                      const char *fallback_name) {
    char b1[64], b2[64], b3[64];
    double y = L_H_NAME_Y;

    /* ---------- 主机名 + 连接状态（圆点精确对齐文字中线）---------- */
    const char *hname = m->host[0] ? m->host : (fallback_name ? fallback_name : "?");
    v_text(cr, x, y, v_fd_value(), accent, "%s", hname);
    {
        uint32_t stc = C_GREEN;
        const char *sts = "本地";
        if (is_remote) {
            sts = "在线";
            if (!online) {
                if (age_s < 0) {
                    stc = C_RED;
                    sts = "连接中";
                } else if (age_s >= 5) {
                    stc = age_s > 60 ? C_RED : C_YELLOW;
                    snprintf(b1, sizeof b1, "离线 %.0fs", age_s);
                    sts = b1;
                }
            }
        }
        double tw = 0, mid = 0;
        const double ty = y + 5;
        v_text_right_ex(cr, x + w, ty, v_fd_label(), stc, &tw, &mid, "%s", sts);
        v_dot_at_text(cr, x + w - tw - 8, ty + mid, 3.2, stc);
    }
    y += L_H_NAME_H;

    /* ---------- 身份行：角色 · IP · up · 核数 · 网卡（一行）---------- */
    {
        double ax = x;
        ax += v_text(cr, ax, y, v_fd_small(), C_OVERLAY0, "%s", role) + 10;
        ax += v_text(cr, ax, y, v_fd_small(), m->net_up ? C_GREEN : C_RED, "%s",
                     m->net_up ? m->ip : "no-net") + 10;
        v_fmt_uptime(m->uptime_s, b1, sizeof b1);
        ax += v_text(cr, ax, y, v_fd_small(), C_OVERLAY1, "up %s", b1) + 10;
        ax += v_text(cr, ax, y, v_fd_small(), C_OVERLAY1, "%d 核", m->ncore) + 10;
        v_text(cr, ax, y, v_fd_small(), C_OVERLAY0, "%s", m->iface);
        y += L_H_IDENT_H;
    }

    /* ---------- 远端还没收到第一帧：说清楚在等什么，不要拿 0 冒充「很闲」---------- */
    if (is_remote && age_s < 0) {
        y += L_H_RULE_H;
        v_text(cr, x, y, v_fd_label(), C_OVERLAY1, "正在建立 ssh 连接…");
        v_text(cr, x, y + 24, v_fd_small(), C_OVERLAY0,
               "探针只读 /proc 与 /sys，不在对方机器上装任何东西");
        return;
    }

    /* ---------- 四张曲线的高度：均分剩余，clamp 到 [MIN, MAX] ---------- */
    const double bottom = v->lh - L_MARGIN;
    /* 固定高度 = 下面 draw 顺序里**除曲线以外**的每一段之和。
     * ⚠️ 改排版顺序时这张表必须同步改，否则剩下的空间会算错（曲线要么撑破底部、要么底部空一片）。
     * 逐段对应：① 细线 ② CPU 行+条+每核柱 ③ 内存 行+条 ④ 网络行 ⑤ 磁盘 细线+行+条 ⑥ 脚注 */
    double fixed = L_H_RULE_H                                            /* ① 细线 */
                   + L_H_LINE_H + L_H_BAR_H + L_H_BAR_GAP + L_H_CORES_H + L_H_BAR_GAP /* ② CPU */
                   + L_H_LINE_H + L_H_BAR_H + L_H_BAR_GAP               /* ③ 内存 */
                   + L_H_LINE_H                                        /* ④ 网络 */
                   + L_H_RULE_H + L_H_LINE_H + L_H_BAR_H + L_H_BAR_GAP /* ⑤ 磁盘 */
                   + L_H_FOOT_H;                                       /* ⑥ 脚注 */
    const int nchart = 4; /* CPU / 内存 / 网络 / 温度 */
    double spare = (bottom - y) - fixed - nchart * L_H_CHART_GAP;
    double ch = spare / nchart;
    if (ch < L_H_CHART_MIN) ch = L_H_CHART_MIN;
    if (ch > L_H_CHART_MAX) ch = L_H_CHART_MAX;

    /* ================= CPU ================= */
    y += L_H_RULE_H;
    {
        double ax = x;
        ax += v_text(cr, ax, y, v_fd_title(), C_MAUVE, "CPU") + 12;
        /* 百分比与标题同排 ⇒ 省掉一整行标题 */
        ax += v_text(cr, ax, y - 4, v_fd_value(), C_TEXT, "%.0f%%", m->cpu_pct) + 12;
        if (m->temp_c >= 0)
            ax += v_text(cr, ax, y + 3, v_fd_small(), v_temp_color(m->temp_c), "%.0f°C",
                         m->temp_c) + 10;
        ax += v_text(cr, ax, y + 3, v_fd_small(), C_OVERLAY1, "%ldMHz · %s", m->freq_mhz,
                     m->governor);
        v_text_right(cr, x + w, y + 3, v_fd_small(), C_OVERLAY0, "load %.2f", m->load[0]);
        y += L_H_LINE_H;
        v_bar(cr, x, y, w, L_H_BAR_H, m->cpu_pct / 100.0, C_MAUVE);
        y += L_H_BAR_H + L_H_BAR_GAP;
        v_core_bars(cr, x, y, w, L_H_CORES_H, m->core_pct, m->ncore);
        y += L_H_CORES_H + L_H_BAR_GAP;
        v_spark(cr, x, y, w, ch, &m->cpu_hist, C_MAUVE, 100.0);
        y += ch + L_H_CHART_GAP;
    }

    /* ================= 内存 ================= */
    {
        double ax = x;
        ax += v_text(cr, ax, y, v_fd_title(), C_BLUE, "内存") + 12;
        ax += v_text(cr, ax, y - 4, v_fd_value(), C_TEXT, "%.0f%%", m->mem_pct) + 12;
        v_fmt_mb(m->mem_used_mb, b1, sizeof b1);
        v_fmt_mb(m->mem_total_mb, b2, sizeof b2);
        ax += v_text(cr, ax, y + 3, v_fd_small(), C_SUBTEXT0, "%s / %s", b1, b2) + 10;
        /* 右对齐：缓存（可回收）+ swap —— 两个真指标，占地不多但很能说明问题 */
        v_fmt_mb(m->mem_cached_mb, b3, sizeof b3);
        v_text_right(cr, x + w, y + 3, v_fd_small(), C_OVERLAY0, "缓存 %s · swap %.0fM", b3,
                     m->swap_used_mb);
        y += L_H_LINE_H;
        v_bar(cr, x, y, w, L_H_BAR_H, m->mem_pct / 100.0, C_BLUE);
        y += L_H_BAR_H + L_H_BAR_GAP;
        v_spark(cr, x, y, w, ch, &m->mem_hist, C_BLUE, 100.0);
        y += ch + L_H_CHART_GAP;
    }

    /* ================= 网络 ================= */
    {
        double ax = x;
        ax += v_text(cr, ax, y, v_fd_title(), C_GREEN, "网络") + 12;
        v_fmt_rate(m->rx_bps, b1, sizeof b1);
        v_fmt_rate(m->tx_bps, b2, sizeof b2);
        ax += v_text(cr, ax, y - 4, v_fd_value(), C_TEAL, "↓%s", b1) + 8;
        ax += v_text(cr, ax, y - 4, v_fd_value(), C_PEACH, "↑%s", b2) + 12;
        ax += v_text(cr, ax, y + 3, v_fd_small(), C_OVERLAY0, "累计 %.1fG / %.1fG",
                     m->rx_total_mb / 1024.0, m->tx_total_mb / 1024.0);
        v_text_right(cr, x + w, y + 3, v_fd_small(),
                     (m->net_err + m->net_drop) ? C_RED : C_OVERLAY0, "丢%d 错%ld", m->net_drop,
                     m->net_err);
        y += L_H_LINE_H;
        v_spark2(cr, x, y, w, ch, &m->rx_hist, C_TEAL, &m->tx_hist, C_PEACH, 0);
        y += ch + L_H_CHART_GAP;
    }

    /* ================= 温度曲线 ================= */
    if (m->temp_c >= 0 && m->temp_hist.len >= 2) {
        v_spark(cr, x, y, w, ch, &m->temp_hist, v_temp_color(m->temp_c), 0);
        y += ch + L_H_CHART_GAP;
    } else {
        y += ch + L_H_CHART_GAP;
    }

    /* ================= 磁盘 ================= */
    {
        y += L_H_RULE_H;
        double ax = x;
        ax += v_text(cr, ax, y, v_fd_title(), C_TEAL, "磁盘") + 12;
        ax += v_text(cr, ax, y - 4, v_fd_value(), C_TEXT, "%.0f%%", m->disk_used_pct) + 12;
        char dtot[32], dfree[32];
        if (m->disk_total_gb >= 1024) snprintf(dtot, sizeof dtot, "%.1fT", m->disk_total_gb / 1024);
        else snprintf(dtot, sizeof dtot, "%.0fG", m->disk_total_gb);
        snprintf(dfree, sizeof dfree, "%.0fG", m->disk_free_gb);
        v_text(cr, ax, y + 3, v_fd_small(), C_SUBTEXT0, "剩 %s / %s", dfree, dtot);
        /* SSD 累计读写：本机有、远端不采 ⇒ 放在**同一行右对齐**，
           这样两列底部仍然是同样的「块数」（单独占一行会让两列错位）。 */
        if (m->have_ssd)
            v_text_right(cr, x + w, y + 3, v_fd_small(), C_OVERLAY0, "SSD 读 %.0fG 写 %.0fG",
                         m->ssd_read_gb, m->ssd_written_gb);
        y += L_H_LINE_H;
        v_bar(cr, x, y, w, L_H_BAR_H, m->disk_used_pct / 100.0,
              m->disk_used_pct > 90 ? C_RED : C_TEAL);
        y += L_H_BAR_H + L_H_BAR_GAP;
    }

    /* ================= 脚注：容器 · 线程 · PSI 压力 · SSD ================= */
    {
        double ax = x;
        if (m->ndocker >= 0)
            ax += v_text(cr, ax, y, v_fd_small(), m->ndocker > 0 ? C_GREEN : C_OVERLAY0,
                         "容器 %d", m->ndocker) + 12;
        if (m->nthr > 0)
            ax += v_text(cr, ax, y, v_fd_small(), C_OVERLAY1, "线程 %d", m->nthr) + 12;
        if (getenv("LAYOUT_DEBUG"))
            fprintf(stderr, "HOST %.0f: ch=%.1f 脚注y=%.1f 底线=%.1f 余=%.1f\n", x, ch, y, bottom,
                    bottom - (y + L_H_FOOT_H));
        /* PSI：>10% 才算「开始被卡住」，用黄/红提醒（这是最容易被忽视的指标） */
        uint32_t pc = m->psi_io > 20 ? C_RED : (m->psi_io > 5 ? C_YELLOW : C_OVERLAY1);
        v_text_right(cr, x + w, y, v_fd_small(), pc, "压力 cpu %.0f · io %.0f", m->psi_cpu,
                     m->psi_io);

    }
}

/* ============================ 顶栏 ============================ */
static void draw_topbar(cairo_t *cr, const view_ctx_t *v, const metrics_t *local,
                        const metrics_t *remote, const view_feed_t *feed) {
    const double mx = L_MARGIN;
    const double mw = v->lw - 2 * mx;
    char b[64];
    double x = mx;

    x += v_text(cr, x, L_TOP_Y, v_fd_title(), C_MAUVE, "%s", local->host) + 10;
    x += v_text(cr, x, L_TOP_Y + 1, v_fd_label(), C_OVERLAY1, "本地") + 24;
    if (remote) {
        x += v_text(cr, x, L_TOP_Y, v_fd_title(), C_LAVENDER, "%s", remote->host) + 10;
        uint32_t c = feed->online ? C_GREEN : (feed->age_s < 0 ? C_RED : C_YELLOW);
        x += v_text(cr, x, L_TOP_Y + 1, v_fd_label(), c, "生产 %s",
                    feed->online ? "●" : "○") +
             24;
    }
    strftime(b, sizeof b, "%Y-%m-%d %H:%M:%S", localtime(&local->now));
    double tw2 = 0;
    v_text_right_w(cr, mx + mw, L_TOP_Y + 1, v_fd_label(), C_SUBTEXT1, "%s", b);
    tw2 = v_text_w(v_fd_label(), b);
    double bx = mx + mw - tw2 - 20, w247 = 0, mid247 = 0;
    v_text_right_ex(cr, bx, L_TOP_Y + 1, v_fd_label(), C_GREEN, &w247, &mid247, "24×7");
    v_dot_at_text(cr, bx - w247 - 8, L_TOP_Y + 1 + mid247, 3.0, C_GREEN);

    v_rule(cr, mx, L_RULE1_Y, mw);
}

void view_panel_draw(cairo_t *cr, const view_ctx_t *v, const metrics_t *local,
                     const metrics_t *remote, const view_feed_t *feed) {
    const double mx = L_MARGIN;
    const double mw = v->lw - 2 * mx;

    if (!v->bg_done) v_bg(cr, v->lw, v->lh);
    draw_topbar(cr, v, local, remote, feed);

    /* 列宽：两台主机等宽，进程榜吃掉剩余量（⚠️ 不要写死第三列的宽度） */
    const double host_w = mw * L_HOST_FRAC;
    const double x_local = mx;
    const double x_remote = mx + host_w + L_COL_GAP;
    const double x_proc = x_remote + host_w + L_COL_GAP;
    const double proc_w = mx + mw - x_proc;

    /* 两根竖细线（分区用，不是卡片边框） */
    const double sep_y0 = L_BODY_Y - 4, sep_y1 = v->lh - L_MARGIN;
    for (int i = 0; i < 2; i++) {
        double sx = (i == 0 ? x_remote : x_proc) - L_COL_GAP / 2;
        cairo_rectangle(cr, sx, sep_y0, 1.0, sep_y1 - sep_y0);
        v_set_rgb(cr, C_SURFACE1, 0.35);
        cairo_fill(cr);
    }

    draw_host(cr, v, local, x_local, host_w, C_MAUVE, "本机", 0, 1, -1, NULL);
    if (remote && feed && feed->enabled) {
        draw_host(cr, v, remote, x_remote, host_w, C_LAVENDER, "生产", 1, feed->online,
                  feed->age_s, feed->target);
    } else {
        /* 没配生产服务器：**留白 + 一句实话**，不要拿本机数据填满右列 */
        v_text(cr, x_remote, L_H_NAME_Y, v_fd_value(), C_OVERLAY0, "未配置");
        v_text(cr, x_remote, L_H_NAME_Y + 34, v_fd_small(), C_OVERLAY0,
               "用 --remote <ssh 目标> 启用（例：--remote hpcore）");
        v_text(cr, x_remote, L_H_NAME_Y + 56, v_fd_small(), C_OVERLAY0,
               "探针只读 /proc，不在被监控机装东西");
    }

    /* ---------- 进程榜（一列，两段） ---------- */
    double py = L_BODY_Y;
    draw_proc_section(cr, local, "本机进程", C_YELLOW, x_proc, py, proc_w, L_PROC_ROWS);
    py += L_PROC_SEC_H;
    if (remote && feed && feed->enabled) {
        cairo_rectangle(cr, x_proc, py + L_PROC_SEC_GAP / 2, proc_w, 1.0);
        v_set_rgb(cr, C_SURFACE1, 0.45);
        cairo_fill(cr);
        py += L_PROC_SEC_GAP;
        draw_proc_section(cr, remote, "生产进程", C_PEACH, x_proc, py, proc_w, L_PROC_ROWS);
    }
}
