/* scene_trending.c —— 幕「GitHub 趋势」
 *
 * 数据由 systemd timer 每 3 小时抓一次（tools/fetch-trending.py），面板**只读缓存文件**：
 * 网络坏了就继续显示上一份，渲染永远不被网络阻塞（和 remote 采集同一个原则）。
 *
 * 为什么抓网页而不是 API：GitHub 没有 trending 的 API（详见 fetch-trending.py 顶部）。
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "theme.h"
#include "scene.h"

#define TR_MAX 16
#define TR_SHOW 10  /* 两列 × 5 行：描述占两行，行距才不挤 */

typedef struct {
    char name[80];
    char lang[32];
    char today[16];
    char total[16];
    char desc[256];
} tr_row_t;

static tr_row_t rows[TR_MAX];
static int nrows;
static char cache_path[256] = "/var/cache/hphelm-panel/trending.tsv";
static struct stat lst;
static int loaded;

static PangoFontDescription *FD_REPO, *FD_TODAY, *FD_DESC, *FD_RANK, *FD_HEAD;

static void init_fonts(void) {
    if (FD_REPO) return;
    FD_REPO = v_fd_proc();   /* 仓库名：比描述小一号，别喧宾夺主 */
    FD_TODAY = v_fd_label();
    FD_DESC = v_fd_desc();   /* 描述 13pt：两列后仍有足够空间 */
    FD_RANK = v_fd_small();
    FD_HEAD = v_fd_title();
}

void scene_trending_file(const char *p) {
    if (!p || !*p) return;
    snprintf(cache_path, sizeof cache_path, "%s", p);
}

/* 按真实像素宽度截断（不要用 v_truncate_mono：它按「字符数」算，CJK 会溢出） */
static void fit(char *out, size_t outn, PangoFontDescription *fd, const char *s, double maxw) {
    if (v_text_w(fd, s) <= maxw) {
        snprintf(out, outn, "%s", s);
        return;
    }
    int n = (int)strlen(s);
    int lo = 0, hi = n;
    double budget = maxw - v_text_w(fd, "…");
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        while (mid > lo && ((unsigned char)s[mid] & 0xC0) == 0x80) mid--;
        if (mid == lo) break; /* 多字节字符：再缩就退不回去了，到此为止 */
        char tmp[512];
        snprintf(tmp, sizeof tmp, "%.*s", mid, s);
        if (v_text_w(fd, tmp) <= budget) lo = mid; else hi = mid - 1;
    }
    while (lo > 0 && ((unsigned char)s[lo] & 0xC0) == 0x80) lo--;
    snprintf(out, outn, "%.*s…", lo, s);
}

static void load(void) {
    struct stat st;
    if (stat(cache_path, &st) != 0) {
        nrows = 0;
        loaded = 1;
        return;
    }
    if (loaded && st.st_mtime == lst.st_mtime && st.st_size == lst.st_size) return;
    lst = st;
    loaded = 1;
    nrows = 0;

    FILE *f = fopen(cache_path, "r");
    if (!f) return;
    char line[1024];
    while (fgets(line, sizeof line, f) && nrows < TR_MAX) {
        if (line[0] == '#') continue;
        char *p = line;
        char *col[5];
        int nc = 0;
        col[nc++] = p;
        for (; *p && nc < 5; p++)
            if (*p == '\t') {
                *p = 0;
                col[nc++] = p + 1;
            }
        if (nc < 5) continue;
        line[strcspn(line, "\r\n")] = 0;
        tr_row_t *r = &rows[nrows];
        snprintf(r->name, sizeof r->name, "%.79s", col[0]);
        snprintf(r->lang, sizeof r->lang, "%.15s", col[1]);
        snprintf(r->today, sizeof r->today, "%s", col[2]);
        snprintf(r->total, sizeof r->total, "%s", col[3]);
        snprintf(r->desc, sizeof r->desc, "%s", col[4]);
        nrows++;
    }
    fclose(f);
}

/* "313,652" → "313.7k"（远端榜单给的是带千分位的整数） */
static void fmt_total(const char *s, char *out, size_t n) {
    long v = 0;
    for (; *s; s++)  /* 榜单给的是 "313,652"，atol 会在逗号处停下 */
        if (*s >= '0' && *s <= '9') v = v * 10 + (*s - '0');
    if (v >= 1000000) snprintf(out, n, "%.1fM", v / 1000000.0);
    else if (v >= 1000) snprintf(out, n, "%.1fk", v / 1000.0);
    else snprintf(out, n, "%ld", v);
}

static void draw(cairo_t *cr, const view_ctx_t *v, const scene_env_t *e) {
    init_fonts();
    load();
    (void)e;

    const double mx = L_MARGIN, mw = v->lw - mx * 2;

    v_text(cr, mx, L_TOP_Y + 13, FD_HEAD, C_MAUVE, "GitHub 趋势 · 每日");
    v_rule(cr, mx, L_RULE1_Y, mw);

    if (nrows == 0) {
        v_text_center(cr, v->lw / 2, v->lh / 2 - 24, v_fd_big(), C_OVERLAY0, "GitHub 趋势还没同步");
        v_text_center(cr, v->lw / 2, v->lh / 2 + 12, v_fd_label(), C_OVERLAY0,
                      "systemctl start hphelm-trending.service");
        v_text_center(cr, v->lw / 2, v->lh / 2 + 34, v_fd_small(), C_OVERLAY0, "%s", cache_path);
        return;
    }

    int n = nrows < TR_SHOW ? nrows : TR_SHOW;
    const double gap = L_COL_GAP;
    const double cw = (mw - gap) / 2;
    const double y0 = L_BODY_Y + 10;
    const int percol = 5;
    const double rh = (v->lh - 26 - y0) / percol;

    char head[64];
    long age = (long)(time(NULL) - lst.st_mtime);
    if (age < 90) snprintf(head, sizeof head, "刚刚同步");
    else if (age < 3600) snprintf(head, sizeof head, "%ld 分钟前同步", age / 60);
    else snprintf(head, sizeof head, "%ld 小时前同步", age / 3600);
    v_text_right(cr, v->lw - mx, L_TOP_Y + 13, v_fd_label(), C_OVERLAY0, "%s · 共 %d", head, nrows);

    for (int i = 0; i < n; i++) {
        tr_row_t *r = &rows[i];
        int col = i / percol, row = i % percol;
        double cx = mx + col * (cw + gap);
        double y = y0 + row * rh;
        double base = y + 30;

        v_text(cr, cx, base, FD_RANK, C_OVERLAY0, "%02d", i + 1);

        char tot[24];
        fmt_total(r->total, tot, sizeof tot);
        char today[40];
        snprintf(today, sizeof today, "%s/日", r->today);

        /* 第一行：仓库名（截断到「语言 + 涨星」之前） + 语言 + 今日涨星 */
        double today_w = v_text_w(FD_TODAY, today) + 12;
        char langtxt[64];
        if (r->lang[0]) snprintf(langtxt, sizeof langtxt, "%s · %s★", r->lang, tot);
        else snprintf(langtxt, sizeof langtxt, "%s★", tot);
        double lang_w = v_text_w(FD_DESC, langtxt) + 14;
        double name_max = cw - 30 - today_w - lang_w;
        char name[96];
        fit(name, sizeof name, FD_REPO, r->name, name_max);
        v_text(cr, cx + 30, base, FD_REPO, C_TEXT, "%s", name);
        v_text_right(cr, cx + cw - today_w, base, FD_DESC, C_SUBTEXT1, "%s", langtxt);
        v_text_right(cr, cx + cw, base, FD_TODAY, C_YELLOW, "%s", today);

        /* 第二行：描述最多两行（超出省略）；总星挪到标题行，描述可用整列宽 */
        v_text_wrap(cr, cx + 30, base + 24, FD_DESC, C_SUBTEXT0, cw - 30, 2, "%s", r->desc);

        if (row + 1 < percol && i + 1 < n) v_rule(cr, cx, y + rh - 4, cw);
    }
}

/* 缓存文件变了 / 每分钟一次的相对时间需要重画 */
static unsigned long long sig(const scene_env_t *e) {
    load();
    unsigned long long h = 1469598103934665603ULL;
    h = (h ^ (unsigned long long)lst.st_mtime) * 1099511628211ULL;
    h = (h ^ (unsigned long long)nrows) * 1099511628211ULL;
    h = (h ^ (unsigned long long)((long)e->wall / 60)) * 1099511628211ULL;
    return h;
}

const scene_t SCENE_TRENDING = {"trending", "GitHub 趋势", draw, sig};