/* scene_hero.c —— 「远看版」幕：两行主机 × 四个巨大数字
 *
 * 与 panel 幕的分工：panel 是「坐下来看」的密排版（曲线/进程榜/每核柱），
 * hero 是「路过瞄一眼」的远看版 —— 3 米外只要知道两台机器活着、CPU/内存没爆。
 * 没有进度条、没有曲线、没有进程榜：大字就是全部信息。
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "scene.h"
#include "theme.h"

static PangoFontDescription *FD_NUM, *FD_HOST, *FD_LAB, *FD_UNIT;

static void fonts(void) {
    if (FD_NUM) return;
    FD_NUM = pango_font_description_from_string("JetBrainsMono Nerd Font 80");
    FD_HOST = pango_font_description_from_string("JetBrainsMono Nerd Font 26");
    FD_LAB = pango_font_description_from_string("JetBrainsMono Nerd Font 11");
    FD_UNIT = pango_font_description_from_string("JetBrainsMono Nerd Font 26");
}

static unsigned long long sig(const scene_env_t *e) {
    unsigned long long h = 1469598103934665603ULL;
#define MIX(v) do { h ^= (unsigned long long)(v); h *= 1099511628211ULL; } while (0)
    MIX((long)(e->m->cpu_pct * 10));
    MIX((long)(e->m->mem_pct * 10));
    MIX((long)(e->m->load[0] * 100));
    MIX((long)(e->m->temp_c * 10));
    if (e->r && e->r->enabled) {
        MIX((long)(e->r->m.cpu_pct * 10));
        MIX((long)(e->r->m.mem_pct * 10));
        MIX((long)(e->r->m.load[0] * 100));
        MIX((long)(e->r->m.temp_c * 10));
        MIX(e->r->m.host[0]);
        MIX(e->feed ? e->feed->online : 0);
    }
    MIX(e->m->uptime_s / 60);
#undef MIX
    return h;
}

/* 一行主机：hostname + 在线圆点 + 四个大数字 */
static void row(cairo_t *cr, const view_ctx_t *v, const metrics_t *m, const view_feed_t *f,
                const char *role, uint32_t accent, double y, int remote) {
    const double mx = L_MARGIN, mw = v->lw - 2 * L_MARGIN;
    const double gx = mx + 128;              /* 数字区起点 */
    const double cw = (mw - 128) / 4;        /* 四等分 */
    int online = remote ? (f && f->online) : 1;

    /* 主机名 */
    double tw = v_text(cr, mx, y, FD_HOST, accent, "%s", m->host[0] ? m->host : role);
    v_dot_at_text(cr, mx + tw + 16, y + v_text_mid(FD_HOST), 4.0,
                  online ? (remote ? C_GREEN : C_MAUVE) : C_RED);
    v_text(cr, mx + tw + 28, y + 3, FD_LAB, online ? C_OVERLAY1 : C_RED, "%s",
           online ? "在线" : (f && f->age_s >= 0 ? "离线" : "未连接"));

    /* 一行小字：口径（核数/频率/运行时长） */
    char meta[96];
    snprintf(meta, sizeof meta, "%d 核 · %.1fGHz · up %dd%02dh", m->ncore,
             m->freq_mhz / 1000.0, (int)(m->uptime_s / 86400), (int)(m->uptime_s / 3600) % 24);
    v_text_right(cr, mx + mw, y + 8, FD_LAB, C_OVERLAY0, "%s%s", meta,
                 remote ? " · 生产" : "");

    double ny = y + 46;
    struct {
        const char *lab;
        char val[16];
        const char *unit;
        uint32_t col;
    } cell[4];

    snprintf(cell[0].val, sizeof cell[0].val, "%.0f%%", m->cpu_pct);
    cell[0].lab = "CPU";
    cell[0].unit = NULL;
    cell[0].col = C_MAUVE;

    snprintf(cell[1].val, sizeof cell[1].val, "%.0f%%", m->mem_pct);
    cell[1].lab = "MEM";
    cell[1].unit = NULL;
    cell[1].col = C_BLUE;

    snprintf(cell[2].val, sizeof cell[2].val, "%.2f", m->load[0]);
    cell[2].lab = "LOAD 1m";
    cell[2].unit = NULL;
    cell[2].col = C_YELLOW;

    if (m->temp_c > 0) {
        snprintf(cell[3].val, sizeof cell[3].val, "%.0f", m->temp_c);
        cell[3].unit = "°C";
        cell[3].col = v_temp_color(m->temp_c);
    } else {
        snprintf(cell[3].val, sizeof cell[3].val, "—");
        cell[3].unit = NULL;
        cell[3].col = C_OVERLAY0;
    }
    cell[3].lab = "TEMP";

    double nh = v_text_h(FD_NUM, "0");       /* 80pt 实测行高，不要拿 80 × 1.3 赌 */
    for (int i = 0; i < 4; i++) {
        double x = gx + cw * i;
        double w = v_text(cr, x, ny, FD_NUM, cell[i].col, "%s", cell[i].val);
        if (cell[i].unit)
            v_text(cr, x + w + 4, ny + nh - v_text_h(FD_UNIT, "°C") - 6, FD_UNIT, cell[i].col,
                   "%s", cell[i].unit);
        v_text(cr, x, ny + nh + 6, FD_LAB, C_OVERLAY0, "%s", cell[i].lab);
    }
}

static void draw(cairo_t *cr, const view_ctx_t *v, const scene_env_t *e) {
    fonts();
    const double mx = L_MARGIN, mw = v->lw - 2 * L_MARGIN;

    v_text(cr, mx, L_TOP_Y, v_fd_title(), C_OVERLAY1, "远看版  ·  两台主机");
    v_text_right(cr, mx + mw, L_TOP_Y, v_fd_title(), C_OVERLAY0, "大字 · 1 Hz · 24×7");
    v_rule(cr, mx, L_RULE1_Y, mw);

    row(cr, v, e->m, NULL, "本机", e->r2 && e->r2->enabled ? C_MAUVE : C_MAUVE, 78, 0);
    if (e->r && e->r->enabled)
        row(cr, v, &e->r->m, e->feed, e->r->target, C_LAVENDER, 356, 1);
    else {
        /* 没配生产服务器：留白 + 实话，不拿本机数据顶替 */
        v_text(cr, mx, 356, FD_HOST, C_OVERLAY0, "未配置生产服务器");
        v_text(cr, mx, 400, FD_LAB, C_OVERLAY0, "用 --remote <ssh 别名> 或 /etc/hphelm-panel.conf 的 remote= 打开");
    }

    v_rule(cr, mx, v->lh - 60, mw);
    v_text(cr, mx, v->lh - 44, FD_LAB, C_OVERLAY0, "密排/曲线/进程榜见 panel 幕");
    double left = scenes_dwell_left(e->now);
    if (left >= 0)
        v_text_right(cr, mx + mw, v->lh - 44, FD_LAB, C_OVERLAY0, "下一幕 %.0fs", left);
}
const scene_t SCENE_HERO = {"hero", "远看版", draw, sig};
