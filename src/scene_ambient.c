/* scene_ambient.c —— 「有人路过」幕：几乎全暗，只留一个极慢的字
 *
 * 意图：这块屏 24×7 亮着，夜里/没人时不该有满屏文字。ambient 用接近背景的对比度，
 * 让它在余光里像一张「有质感的墙」，走近才看得出是 hphelm 在值班。
 * 省电也省眼：单帧重绘 1 次/分钟（只有右下角时钟会变）。
 */
#include <stdio.h>
#include <string.h>

#include "scene.h"
#include "theme.h"

static PangoFontDescription *FD_MARK, *FD_SUB;

static void fonts(void) {
    if (FD_MARK) return;
    FD_MARK = pango_font_description_from_string("JetBrainsMono Nerd Font 200");
    FD_SUB = pango_font_description_from_string("JetBrainsMono Nerd Font 14");
}

static unsigned long long sig(const scene_env_t *e) {
    unsigned long long h = (unsigned long long)(e->wall / 60);
    h = h * 1099511628211ULL ^ (unsigned long long)(e->feed && e->feed->online);
    if (e->r2 && e->r2->enabled) h ^= 0x5bf03635ULL;
    return h;
}

static void draw(cairo_t *cr, const view_ctx_t *v, const scene_env_t *e) {
    fonts();

    /* 先把预置渐变盖掉，铺平到底色：ambient 不需要亮部 */
    v_set_rgb(cr, C_CRUST, 1.0);
    cairo_paint(cr);

    /* 中央标记：只比背景亮一档（SURFACE1），不抢注意力。
       200pt 的行高约 260 逻辑像素 —— 副行位置按量出来的高度放，别写死偏移。 */
    double my = v->lh * 0.34;
    v_text_center(cr, v->lw / 2, my, FD_MARK, C_SURFACE1, "hphelm");

    /* 一行小字：两台机器还在不在 */
    char line[128];
    int n = 1, on = 1;
    if (e->r && e->r->enabled) {
        n++;
        if (e->feed && e->feed->online) on++;
    }
    snprintf(line, sizeof line, "%d/%d 在线  ·  %s", on, n, e->m->host[0] ? e->m->host : "hpcmd");
    v_text_center(cr, v->lw / 2, my + v_text_h(FD_MARK, "hphelm") + 18, FD_SUB, C_OVERLAY0, "%s",
                  line);

    /* 右下角极小的时钟：一分钟变一次，用来判断「它没死」 */
    struct tm tm;
    localtime_r(&e->wall, &tm);
    v_text_right(cr, v->lw - L_MARGIN, v->lh - L_MARGIN - 10, v_fd_small(), C_OVERLAY0, "%02d:%02d",
                 tm.tm_hour, tm.tm_min);
}

const scene_t SCENE_AMBIENT = {"ambient", "环境", draw, sig};