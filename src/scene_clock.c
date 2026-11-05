/* scene_clock.c —— 钟幕：在大屏上「远看就知道几点」+ 一句实话
 *
 * 设计意图（区别于密排面板）：
 *   · 只放一个巨大的 HH:MM（240pt），3 米外可读；
 *   · 下方一行日期/星期；一条 1px 线随秒生长（1 Hz，很便宜，但让人看出它还活着）；
 *   · 右上角不写「第几屏」——幕指示由主循环统一画在屏幕最下沿。
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "scene.h"
#include "theme.h"

static PangoFontDescription *FD_TIME, *FD_DATE, *FD_META;

static void fonts(void) {
    if (FD_TIME) return;
    FD_TIME = pango_font_description_from_string("JetBrainsMono Nerd Font 200");
    FD_DATE = pango_font_description_from_string("JetBrainsMono Nerd Font 26");
    FD_META = pango_font_description_from_string("JetBrainsMono Nerd Font 11");
}

static unsigned long long sig(const scene_env_t *e) { return (unsigned long long)e->wall; }

static void draw(cairo_t *cr, const view_ctx_t *v, const scene_env_t *e) {
    fonts();
    const double mw = v->lw - 2 * L_MARGIN;
    const double cx = v->lw / 2;

    struct tm tm;
    localtime_r(&e->wall, &tm);
    char hhmm[8], date[64], meta[160];
    snprintf(hhmm, sizeof hhmm, "%02d:%02d", tm.tm_hour, tm.tm_min);
    static const char *WD[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
    snprintf(date, sizeof date, "%04d-%02d-%02d  %s", tm.tm_year + 1900, tm.tm_mon + 1,
             tm.tm_mday, WD[tm.tm_wday % 7]);

    /* 顶部一行很小的字：这台屏是谁、为什么亮着 */
    v_text(cr, L_MARGIN, L_TOP_Y, v_fd_title(), C_OVERLAY1, "hphelm  ·  常亮看板");
    v_text_right(cr, L_MARGIN + mw, L_TOP_Y, v_fd_title(), C_OVERLAY0, "%s · 24×7",
                 e->m->host[0] ? e->m->host : "hpcmd");
    v_rule(cr, L_MARGIN, L_RULE1_Y, mw);

    /* 主时间：整块（时间 + 日期 + 秒线）垂直居中。
       ⚠️ 不要用「字号 × 系数」猜下一行 —— 200pt 的行高约 270 逻辑像素，
       猜 250 会让日期压在钟上（实测）。全部按 v_text_h 量。 */
    double th = v_text_h(FD_TIME, "00:00");
    double dh = v_text_h(FD_DATE, date);
    double block = th + 24 + dh + 34;
    double ty = (v->lh - block) / 2 - 20;
    if (ty < 80) ty = 80;
    v_text_center(cr, cx, ty, FD_TIME, C_TEXT, "%s", hhmm);

    /* 重心线：日期 */
    v_text_center(cr, cx, ty + th + 24, FD_DATE, C_SUBTEXT1, "%s", date);
    /* 不用「冒号闪动」：太吵。改成日期行下方的秒进度线（宽度 = 本分钟进度） */
    double ly = ty + th + 24 + dh + 16;
    v_rule(cr, cx - mw * 0.22, ly, mw * 0.44);
    double prog = (tm.tm_sec + 1) / 60.0;
    cairo_rectangle(cr, cx - mw * 0.22, ly, mw * 0.44 * prog, 1.0);
    v_set_rgb(cr, C_MAUVE, 0.85);
    cairo_fill(cr);

    /* 底部一句实话：两台机器在不在、下一幕还有几秒 */
    double left = scenes_dwell_left(e->now);
    if (left >= 0) {
        snprintf(meta, sizeof meta, "下一幕 %s · %.0fs", scene_at((scenes_current_index() + 1) %
                                                               scenes_count())
                                                         ->label,
                 left);
    } else {
        snprintf(meta, sizeof meta, "本幕常驻");
    }
    v_text_center(cr, cx, v->lh - 70, FD_META, C_OVERLAY0, "%s", meta);
}
const scene_t SCENE_CLOCK = {"clock", "钟与日期", draw, sig};
