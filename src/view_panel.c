/* view_panel.c —— 运维面板版面（单机：一台主机双子列 + 进程榜整列）
 *
 * 2026-11-06 第六版（用户口径）：hpcmd 下线，全场 all-in-one 到 hpcore
 *   ① 「全显示 hpcore 相关东西」⇒ 不再分「本机 / 生产服务器」两列，只画**一台**主机；
 *      配了 --remote 就画那台（kiosk 用 hpcore），没配就画本机（开发时离屏看效果）。
 *   ② 「把空间占满」⇒ 主机区切成左右两个子列，各自「文字行 + 2 张曲线」；
 *      曲线把剩下的高度全部吃掉（653 - 187 固定 - 12 间距）÷ 2 ≈ 224 逻辑像素，
 *      并且用 v_chart_scale() 画刻度底 —— 低负载时曲线贴底也读作「一张图」而不是「空白」。
 *   ③ 「进程那一列可以全部给他了」⇒ 进程列整列归这台机：
 *      上段按内存（RSS）12 行、下段按 CPU 12 行，每行都带 pid 与 CPU%。
 *
 *   ┌ 顶栏：hpcore  生产服务器 ● │ 2026-11-06 15:44:02  ● 24×7 ─────────────────┐
 *   ├──────────────────────────────────┬──────────────────────────────────────┤
 *   │ hpcore                  ● 在线   │ hpcore 进程  共 496 个  按内存 (RSS)  │
 *   │ 生产 192.168.5.102 up 11d1h 16核 │ 1 pi       2669543  533M      19%    │
 *   │ ──────── 细线 ────────           │ …12 行                               │
 *   │ CPU 3% 68°C 1589MHz               │ ────────── 细线 ──────────            │
 *   │      [右] load 1.64 / 1.19 / 1.04│ hpcore 进程              按 CPU      │
 *   │ [CPU 条]                         │ 1 magpie   1818752  449M     200%    │
 *   │ [每核柱 ×16]                     │ …12 行                               │
 *   │ [CPU 曲线 ─ 刻度 ─      100%]    │                                      │
 *   │ 内存 40% 11.4G/28.2G 缓存20.6G   │                                      │
 *   │ [内存条]                         │                                      │
 *   │ [内存曲线 ─ 刻度 ─      100%]    │                                      │
 *   │ 近 5 分钟 · 1 Hz 采样            │                                      │
 *   └──────────────────────────────────┴──────────────────────────────────────┘
 *   右子列（上面那段是左子列）：网络 + 双曲线 → 磁盘 I/O + 双曲线 → 细线 →
 *   磁盘用量 + 条 → 细线 → 容器/线程/进程 → 累计流量 → 细线 → PSI 压力 + 盘累计读写
 *
 * **没有卡片**：分区只靠「1px 细线 + 字号层级 + 留白」，没有圆角框/底色块。
 * **两列固定高度必须相等**（各 187）——否则左右两列的曲线基线会错开，看着像画歪了。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "theme.h"
#include "view.h"



/* ============================ 进程榜（一列，两段） ============================ */
/* rows/nrows = 要显示的那份榜单（内存榜或 CPU 榜），sort_lbl = 排序口径（靠右小字）
 * total < 0 时不画「共 N 个」（同一台机的两段榜里不重复一遍进程总数） */
static void draw_proc_section(cairo_t *cr, const proc_row_t *rows, int nrows,
                              const char *tag, uint32_t tag_col, const char *sort_lbl, double x,
                              double y, double w, int maxrows, int total) {
    char b[48];
    /* 段标题：主机名 + 进程总数（同一行，靠右是排序口径） */
    double tw = v_text(cr, x, y, v_fd_title(), tag_col, "%s", tag);
    if (total >= 0)
        v_text(cr, x + tw + 14, y + 1, v_fd_label(), C_OVERLAY1, "共 %d 个", total);
    v_text_right(cr, x + w, y + 2, v_fd_small(), C_OVERLAY0, "%s", sort_lbl);
    y += 28;

    int n = nrows < maxrows ? nrows : maxrows;
    if (n <= 0) {
        v_text(cr, x, y, v_fd_small(), C_OVERLAY0, "（还没有数据）");
        return;
    }
    /* 列定位：名次 | 名称 | pid | 内存（右） | CPU%（右） —— 四段位置按最宽行预算，避免逐行抖动 */
    const double x_rank = x + 18;      /* 名次右边界 */
    const double x_name = x + 26;      /* 名称左边界 */
    const double x_cpu = x + w;        /* CPU% 右边界 */
    const double x_mem = x + w - 66;   /* 内存右边界 */
    for (int i = 0; i < n; i++) {
        double ry = y + i * L_PROC_ROW_H;
        if (i == 0) { /* 表头式的极淡基线，方便眼睛对齐（不是卡片框） */
            cairo_rectangle(cr, x, ry - 4, w, 1.0);
            v_set_rgb(cr, C_SURFACE1, 0.25);
            cairo_fill(cr);
        }
        v_text_right(cr, x_rank, ry + 4, v_fd_small(), C_OVERLAY0, "%d", i + 1);
        v_fmt_mb(rows[i].rss_mb, b, sizeof b);
        /* 内存：先右对齐画掉（返回宽度），省掉一次 v_text_w */
        double memw = v_text_right_w(cr, x_mem, ry, v_fd_proc(), C_OVERLAY1, "%s", b);
        /* CPU%：>100 是常态（多线程）。<10% 保留一位小数（否则 0.3% 全被舍成 0%）；
           真的没有这一列（老探针）用「-」占位，不显示假 0。 */
        /* cpu_pct < 0 ⇒ 探针是老格式（没有这一列）；0.0% 是**真值**，不要画成 "-" */
        if (rows[i].cpu_pct >= 0.0) {
            uint32_t cc = rows[i].cpu_pct > 80 ? C_PEACH : C_OVERLAY1;
            if (rows[i].cpu_pct < 10.0)
                v_text_right(cr, x_cpu, ry, v_fd_proc(), cc, "%.1f%%", rows[i].cpu_pct);
            else v_text_right(cr, x_cpu, ry, v_fd_proc(), cc, "%.0f%%", rows[i].cpu_pct);
        } else v_text_right(cr, x_cpu, ry, v_fd_proc(), C_SURFACE1, "-");
        /* 名称预算 = 列宽 - 内存 - CPU% - pid 预算。字体等宽 ⇒ 按字符数估算，不做逐字符测量
         * （逐字符测是每帧几百次 pango 整形，实测让单帧绘制时间翻倍）。 */
        double pidw = v_char_w(v_fd_small()) * 8 + 8;
        double name_max = x_mem - memw - 12 - (x_name + pidw);
        if (name_max < 60) name_max = 60;
        char nm[48];
        v_truncate_mono(nm, sizeof nm, rows[i].name, v_fd_proc(), name_max);
        double nw = v_text(cr, x_name, ry, v_fd_proc(), C_TEXT, "%s", nm);
        /* pid：跟名称实际宽度走，别留死空位 */
        if (x_name + nw + pidw < x_mem - memw - 8)
            v_text(cr, x_name + nw + 10, ry + 4, v_fd_small(), C_OVERLAY0, "%d", rows[i].pid);
    }
}

/* ============================ 顶栏 ============================ */
static void draw_topbar(cairo_t *cr, const view_ctx_t *v, const metrics_t *m,
                        const view_feed_t *feed, const char *target) {
    const double mx = L_MARGIN;
    const double mw = v->lw - 2 * mx;
    char b[64];
    double x = mx;
    const int remote = target != NULL;

    x += v_text(cr, x, L_TOP_Y, v_fd_title(), C_MAUVE, "%s", m->host) + 10;
    uint32_t st_c = !remote ? C_OVERLAY1 : (feed->online ? C_GREEN : C_RED);
    x += v_text(cr, x, L_TOP_Y + 1, v_fd_label(), C_OVERLAY1, "%s", remote ? "生产服务器" : "本机") +
         8;
    v_text(cr, x, L_TOP_Y + 1, v_fd_label(), st_c, "%s", remote ? (feed->online ? "●" : "○") : "");

    strftime(b, sizeof b, "%Y-%m-%d %H:%M:%S", localtime(&m->now));
    v_text_right_w(cr, mx + mw, L_TOP_Y + 1, v_fd_label(), C_SUBTEXT1, "%s", b);
    double tw2 = v_text_w(v_fd_label(), b);
    double bx = mx + mw - tw2 - 20, w247 = 0, mid247 = 0;
    v_text_right_ex(cr, bx, L_TOP_Y + 1, v_fd_label(), C_GREEN, &w247, &mid247, "24×7");
    v_dot_at_text(cr, bx - w247 - 8, L_TOP_Y + 1 + mid247, 3.0, C_GREEN);
    v_rule(cr, mx, L_RULE1_Y, mw);
}

/* ============================ 主机区（左右两个子列） ============================ */
/* 左列固定高度：名称 + 身份 + 细线 + CPU(行/条/每核柱) + 内存(行/条) */
#define FIX_A (L_H_NAME_H + L_H_IDENT_H + L_H_RULE_H + \
               (L_H_LINE_H + L_H_BAR_H + L_H_BAR_GAP + L_H_CORES_H + L_H_BAR_GAP) + \
               (L_H_LINE_H + L_H_BAR_H + L_H_BAR_GAP))
/* 右列固定高度：网络行 + 磁盘I/O 行 + 细线 + 磁盘(行/条) + 细线 + 三行小字
 * ⚠️ 必须与 FIX_A 相等（差 1px 都会让两列的曲线基线错开）。改一边，记得改另一边。 */
#define L_H_SYS_H 24.0
#define FIX_B (2 * L_H_LINE_H + L_H_RULE_H + (L_H_LINE_H + L_H_BAR_H + L_H_BAR_GAP) + \
               L_H_RULE_H + 2 * L_H_SYS_H + L_H_RULE_H + L_H_SYS_H)

static double chart_h(double avail, double fixed_h, int ncharts) {
    double ch = (avail - fixed_h - ncharts * L_H_CHART_GAP) / (double)ncharts;
    if (ch < L_H_CHART_MIN) ch = L_H_CHART_MIN;
    if (ch > L_H_CHART_MAX) ch = L_H_CHART_MAX;
    return ch;
}

static void draw_host(cairo_t *cr, const metrics_t *m, const view_feed_t *feed,
                      const char *target, double x, double w, double bottom) {
    const int remote = target != NULL;
    const double wa = (w - L_SUB_GAP) / 2.0;
    const double xa = x, xb = x + wa + L_SUB_GAP, wb = wa;
    const double ch = chart_h(bottom - L_BODY_Y - L_H_CAP_H, FIX_A, 2);
    char b1[32], b2[32], b3[96];
    double y = L_BODY_Y;

    /* ---------------- 左子列 ---------------- */
    /* 主机名 + 状态（同一行：状态靠右，颜色即语义） */
    v_text(cr, xa, y, v_fd_value(), C_MAUVE, "%s", m->host);
    if (remote) {
        if (feed->online) {
            v_text_right(cr, xa + wa, y + 3, v_fd_small(), C_GREEN, "● 在线");
        } else if (feed->age_s >= 0) {
            v_text_right(cr, xa + wa, y + 3, v_fd_small(), C_RED, "○ 离线 %.0fs", feed->age_s);
        } else {
            v_text_right(cr, xa + wa, y + 3, v_fd_small(), C_YELLOW, "○ 连接中…");
        }
    }
    y += L_H_NAME_H;

    /* 身份行：角色 · IP · up · 核数 · 网卡 */
    {
        char up[32];
        v_fmt_uptime(m->uptime_s, up, sizeof up);
        double ax = xa;
        ax += v_text(cr, ax, y, v_fd_small(), C_SUBTEXT0, "%s", remote ? "生产" : "本地") + 10;
        if (m->ip[0]) ax += v_text(cr, ax, y, v_fd_small(), C_OVERLAY1, "%s", m->ip) + 10;
        ax += v_text(cr, ax, y, v_fd_small(), C_OVERLAY1, "up %s", up) + 10;
        ax += v_text(cr, ax, y, v_fd_small(), C_OVERLAY1, "%d 核", m->ncore) + 10;
        if (m->iface[0]) v_text(cr, ax, y, v_fd_small(), C_OVERLAY0, "%s", m->iface);
        y += L_H_IDENT_H;
    }
    v_rule(cr, xa, y + 3, wa);
    y += L_H_RULE_H;

    /* CPU 行：CPU 3%  58°C  1967MHz · powersave      [右] load 1.53
     * 预算：先量出右侧 load 的宽度，剩下的才算左边能用的；放不下就在
     * 「MHz · governor → 只 MHz → 全省」之间降级。
     * ⚠️ 单机化后 wa 只有 ~336 逻辑像素，不这么做 governor 会直接压在 load 上。 */
    {
        char ldbuf[64];
        snprintf(ldbuf, sizeof ldbuf, "load %.2f / %.2f / %.2f", m->load[0], m->load[1],
                 m->load[2]);
        const double budget = xa + wa - v_text_w(v_fd_small(), ldbuf) - 12;
        double ax = xa;
        ax += v_text(cr, ax, y + 3, v_fd_label(), C_OVERLAY1, "CPU") + 10;
        ax += v_text(cr, ax, y, v_fd_title(), C_MAUVE, "%.0f%%", m->cpu_pct) + 10;
        if (m->temp_c >= 0)
            ax += v_text(cr, ax, y + 3, v_fd_small(), v_temp_color(m->temp_c), "%.0f°C",
                         m->temp_c) +
                  10;
        if (m->freq_mhz > 0) {
            char f1[64], f2[32];
            if (m->governor[0]) snprintf(f1, sizeof f1, "%ldMHz · %s", m->freq_mhz, m->governor);
            else snprintf(f1, sizeof f1, "%ldMHz", m->freq_mhz);
            snprintf(f2, sizeof f2, "%ldMHz", m->freq_mhz);
            const char *fb = f1;
            if (ax + v_text_w(v_fd_small(), f1) > budget) fb = NULL;
            if (ax + v_text_w(v_fd_small(), f2) <= budget) fb = f2;
            if (fb) v_text(cr, ax, y + 3, v_fd_small(), C_OVERLAY0, "%s", fb);
        }
        v_text_right(cr, xa + wa, y + 3, v_fd_small(), C_OVERLAY1, "%s", ldbuf);
        y += L_H_LINE_H;
        v_bar(cr, xa, y, wa, L_H_BAR_H, m->cpu_pct / 100.0, C_MAUVE);
        y += L_H_BAR_H + L_H_BAR_GAP;
        v_core_bars(cr, xa, y, wa, L_H_CORES_H, m->core_pct, m->ncore);
        y += L_H_CORES_H + L_H_BAR_GAP;
        double vmax = v_chart_scale(cr, xa, y, wa, ch, 100.0, 1);
        v_spark(cr, xa, y, wa, ch, &m->cpu_hist, C_MAUVE, vmax);
        y += ch + L_H_CHART_GAP;
    }

    /* 内存行：内存 38%  10.8G / 28.2G        [右] 缓存 21.0G · swap 802M */
    {
        char u[32], t[32], sw[32];
        v_fmt_mb(m->mem_used_mb, u, sizeof u);
        v_fmt_mb(m->mem_total_mb, t, sizeof t);
        double ax = xa;
        ax += v_text(cr, ax, y + 3, v_fd_label(), C_OVERLAY1, "内存") + 10;
        ax += v_text(cr, ax, y, v_fd_title(), C_BLUE, "%.0f%%", m->mem_pct) + 10;
        v_text(cr, ax, y + 3, v_fd_small(), C_SUBTEXT0, "%s / %s", u, t);
        if (m->swap_total_mb > 0) {
            v_fmt_mb(m->swap_used_mb, sw, sizeof sw);
            snprintf(b3, sizeof b3, "缓存 %.1fG · swap %s", m->mem_cached_mb / 1024.0, sw);
        } else {
            snprintf(b3, sizeof b3, "缓存 %.1fG", m->mem_cached_mb / 1024.0);
        }
        v_text_right(cr, xa + wa, y + 3, v_fd_small(), C_OVERLAY0, "%s", b3);
        y += L_H_LINE_H;
        v_bar(cr, xa, y, wa, L_H_BAR_H, m->mem_pct / 100.0, C_BLUE);
        y += L_H_BAR_H + L_H_BAR_GAP;
        double vmax = v_chart_scale(cr, xa, y, wa, ch, 100.0, 1);
        v_spark(cr, xa, y, wa, ch, &m->mem_hist, C_BLUE, vmax);
    }

    /* ---------------- 右子列 ---------------- */
    y = L_BODY_Y;

    /* 网络行：网络 ↓20.4K ↑12.0K            [右] 丢 3 错 0 */
    {
        v_fmt_rate(m->rx_bps, b1, sizeof b1);
        v_fmt_rate(m->tx_bps, b2, sizeof b2);
        double ax = xb;
        ax += v_text(cr, ax, y + 3, v_fd_label(), C_OVERLAY1, "网络") + 10;
        ax += v_text(cr, ax, y, v_fd_title(), C_TEAL, "↓%s", b1) + 10;
        v_text(cr, ax, y, v_fd_title(), C_PEACH, "↑%s", b2);
        if (m->net_err || m->net_drop)
            v_text_right(cr, xb + wb, y + 3, v_fd_small(),
                         (m->net_drop || m->net_err) ? C_RED : C_OVERLAY1, "丢 %ld 错 %ld",
                         m->net_drop, m->net_err);
        else v_text_right(cr, xb + wb, y + 3, v_fd_small(), C_OVERLAY0, "丢 0 错 0");
        y += L_H_LINE_H;
        double mx_ring = ring_max(&m->rx_hist, 1.0), mt = ring_max(&m->tx_hist, 1.0);
        double vmax = v_nice_ceil(mx_ring > mt ? mx_ring : mt);
        vmax = v_chart_scale(cr, xb, y, wb, ch, vmax, 0);
        v_spark2(cr, xb, y, wb, ch, &m->rx_hist, C_TEAL, &m->tx_hist, C_PEACH, vmax);
        y += ch + L_H_CHART_GAP;
    }

    /* 磁盘 I/O 行：磁盘 I/O 读 0B 写 1.4M   （速率就是下面那条曲线的当前值，颜色即图例） */
    {
        v_fmt_rate(m->io_rd_bps, b1, sizeof b1);
        v_fmt_rate(m->io_wr_bps, b2, sizeof b2);
        double ax = xb;
        ax += v_text(cr, ax, y + 3, v_fd_label(), C_OVERLAY1, "磁盘 I/O") + 10;
        ax += v_text(cr, ax, y + 3, v_fd_small(), C_TEAL, "读 %s", b1) + 10;
        v_text(cr, ax, y + 3, v_fd_small(), C_PEACH, "写 %s", b2);
        y += L_H_LINE_H;
        double mx_ring = ring_max(&m->io_rd_hist, 1.0), mt = ring_max(&m->io_wr_hist, 1.0);
        double vmax = v_nice_ceil(mx_ring > mt ? mx_ring : mt);
        vmax = v_chart_scale(cr, xb, y, wb, ch, vmax, 0);
        v_spark2(cr, xb, y, wb, ch, &m->io_rd_hist, C_TEAL, &m->io_wr_hist, C_PEACH, vmax);
        y += ch + L_H_CHART_GAP;
    }
    v_rule(cr, xb, y + 1, wb);
    y += L_H_RULE_H;

    /* 磁盘用量行：磁盘 16%  剩 185G / 221G
     * 判据用 disk_total_gb（远端探针也发这个），不要用 have_disk ——
     * have_disk 只有本机版（有累计 I/O）才置位，用它会让远端显示「没找到整盘设备」。 */
    if (m->disk_total_gb > 0) {
        char dtot[16], dfree[16];
        if (m->disk_total_gb >= 1024) snprintf(dtot, sizeof dtot, "%.1fT", m->disk_total_gb / 1024);
        else snprintf(dtot, sizeof dtot, "%.0fG", m->disk_total_gb);
        if (m->disk_free_gb >= 1024)
            snprintf(dfree, sizeof dfree, "%.1fT", m->disk_free_gb / 1024);
        else snprintf(dfree, sizeof dfree, "%.0fG", m->disk_free_gb);
        double ax = xb;
        ax += v_text(cr, ax, y + 3, v_fd_label(), C_OVERLAY1, "磁盘") + 10;
        ax += v_text(cr, ax, y, v_fd_title(), C_TEAL, "%.0f%%", m->disk_used_pct) + 10;
        v_text(cr, ax, y + 3, v_fd_small(), C_SUBTEXT0, "剩 %s / %s", dfree, dtot);
        y += L_H_LINE_H;
        v_bar(cr, xb, y, wb, L_H_BAR_H, m->disk_used_pct / 100.0,
              m->disk_used_pct > 90 ? C_RED : C_TEAL);
        y += L_H_BAR_H + L_H_BAR_GAP;
    } else {
        v_text(cr, xb, y + 3, v_fd_small(), C_OVERLAY0, "磁盘（没找到整盘设备）");
        y += L_H_LINE_H + L_H_BAR_H + L_H_BAR_GAP;
    }
    v_rule(cr, xb, y + 1, wb);
    y += L_H_RULE_H;

    /* 系统小字行（慢变值，不需要曲线也不需要条） */
    {
        double ax = xb;
        if (m->ndocker >= 0)
            ax += v_text(cr, ax, y, v_fd_small(), m->ndocker > 0 ? C_GREEN : C_OVERLAY0, "容器 %d",
                         m->ndocker) +
                  12;
        if (m->nthr > 0)
            ax += v_text(cr, ax, y, v_fd_small(), C_OVERLAY1, "线程 %d", m->nthr) + 12;
        v_text(cr, ax, y, v_fd_small(), C_OVERLAY1, "进程 %d", m->nproc);
        y += L_H_SYS_H;

        v_fmt_mb(m->rx_total_mb, b1, sizeof b1);
        v_fmt_mb(m->tx_total_mb, b2, sizeof b2);
        v_text_right(cr, xb + wb, y, v_fd_small(), C_OVERLAY0, "累计 ↓%s ↑%s", b1, b2);
        y += L_H_SYS_H;
        v_rule(cr, xb, y + 3, wb);
        y += L_H_RULE_H;

        /* PSI：>10% 才算「开始被卡住」，用黄/红提醒（这是最容易被忽视的指标） */
        uint32_t pc = m->psi_io > 20 ? C_RED : (m->psi_io > 5 ? C_YELLOW : C_OVERLAY1);
        double bx = xb;
        bx += v_text(cr, bx, y, v_fd_small(), C_OVERLAY0, "压力") + 10;
        bx += v_text(cr, bx, y, v_fd_small(), pc, "io %.0f", m->psi_io) + 12;
        bx += v_text(cr, bx, y, v_fd_small(), m->psi_mem > 10 ? C_YELLOW : C_OVERLAY1,
                     "mem %.0f", m->psi_mem) + 12;
        v_text(cr, bx, y, v_fd_small(), C_OVERLAY1, "cpu %.0f", m->psi_cpu);
        if (m->have_disk)
            v_text_right(cr, xb + wb, y, v_fd_small(), C_OVERLAY0, "盘 读%.0fG 写%.0fG",
                         m->disk_read_gb, m->disk_written_gb);
    }

    /* 曲线时间轴说明：占 L_H_CAP_H。左列说时间跨度，右列说累计值口径
     * （只写一遍「近 5 分钟」，避免两列重复同一句话）。 */
    v_text(cr, xa, bottom - L_H_CAP_H, v_fd_small(), C_OVERLAY0, "近 5 分钟 · 1 Hz 采样");
    v_text_right(cr, xb + wb, bottom - L_H_CAP_H, v_fd_small(), C_OVERLAY0,
                 "丢/错与累计流量 = 开机以来");

    if (getenv("LAYOUT_DEBUG"))
        fprintf(stderr, "HOST %.0f: wa=%.1f ch=%.1f FIX_A=%.0f FIX_B=%.0f 余=%.1f\n", x, wa, ch,
                FIX_A, FIX_B,
                (bottom - L_BODY_Y - L_H_CAP_H) - (FIX_A + 2 * (ch + L_H_CHART_GAP)));
}

/* ============================ 整屏 ============================ */
void view_panel_draw(cairo_t *cr, const view_ctx_t *v, const metrics_t *m,
                     const view_feed_t *feed, const char *target) {
    view_feed_t zero = {0};
    if (!feed) feed = &zero;
    const double mx = L_MARGIN;
    const double mw = v->lw - 2 * mx;
    const double bottom = v->lh - L_MARGIN;

    if (!v->bg_done) v_bg(cr, v->lw, v->lh);
    draw_topbar(cr, v, m, feed, target);

    /* 宽分配：主机区（两个子列）+ 进程榜吃掉剩余量（⚠️ 不要写死进程列宽度） */
    const double host_w = mw * L_HOST_FRAC;
    const double x_host = mx;
    const double x_proc = mx + host_w + L_COL_GAP;
    const double proc_w = mx + mw - x_proc;

    /* 一根竖细线（分区用，不是卡片边框） */
    cairo_rectangle(cr, x_proc - L_COL_GAP / 2, L_BODY_Y - 4, 1.0, bottom - (L_BODY_Y - 4));
    v_set_rgb(cr, C_SURFACE1, 0.35);
    cairo_fill(cr);

    draw_host(cr, m, feed, target, x_host, host_w, bottom);

    /* ---------- 进程榜（整列归这一台机：上段按内存，下段按 CPU） ---------- */
    char lbl[80];
    snprintf(lbl, sizeof lbl, "%s 进程", m->host);
    double py = L_BODY_Y;
    draw_proc_section(cr, m->top, m->ntop, lbl, C_YELLOW, "按内存 (RSS)", x_proc, py, proc_w,
                      L_PROC_ROWS, m->nproc);
    py += L_PROC_SEC_H;
    /* 两段之间一条细线（不是卡片） */
    cairo_rectangle(cr, x_proc, py + L_PROC_SEC_GAP / 2, proc_w, 1.0);
    v_set_rgb(cr, C_SURFACE1, 0.35);
    cairo_fill(cr);
    py += L_PROC_SEC_GAP;
    draw_proc_section(cr, m->top_cpu, m->ntop_cpu, lbl, C_YELLOW, "按 CPU（近 1 秒）", x_proc,
                      py, proc_w, L_PROC_ROWS, -1);
}

/* 段标题里的「共 N 个」不再单独占位，改由 sort_lbl 带出（避免两段重复同一句） */
