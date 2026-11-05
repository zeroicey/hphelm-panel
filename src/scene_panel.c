/* scene_panel.c —— 「运维面板」幕：把原来的三列版面包进 scene 接口
 *
 * 这里只做两件事：把数据环境转给 view_panel_draw()；给出内容签名。
 * 版面本身仍归 src/view_panel.c（改面板只动那里，别在这里加坐标）。
 */
#include <stddef.h>

#include "scene.h"

static unsigned long long mix_bytes(unsigned long long h, const void *p, size_t n) {
    const unsigned char *b = p;
    for (size_t i = 0; i < n; i++) {
        h ^= b[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static unsigned long long sig(const scene_env_t *e) {
    unsigned long long h = 1469598103934665603ULL;
    h = mix_bytes(h, e->m, sizeof *e->m);
    if (e->r && e->r->enabled) h = mix_bytes(h, &e->r->m, sizeof e->r->m);
    if (e->r2 && e->r2->enabled) h = mix_bytes(h, &e->r2->m, sizeof e->r2->m);
#define MIX(v) do { h ^= (unsigned long long)(v); h *= 1099511628211ULL; } while (0)
    if (e->feed) {
        MIX(e->feed->online);
        MIX(e->feed->age_s >= 0 ? (long)e->feed->age_s : -1L);
    }
    if (e->feed2) {
        MIX(e->feed2->online);
        MIX(e->feed2->age_s >= 0 ? (long)e->feed2->age_s : -1L);
    }
#undef MIX
    return h;
}

static void draw(cairo_t *cr, const view_ctx_t *v, const scene_env_t *e) {
    /* 配了 --remote2 ⇒ 左列改为第二台远程机（纯显示器场景），否则左列是本机 */
    int lremote = e->r2 && e->r2->enabled;
    view_feed_t none = {0};
    view_panel_draw(cr, v, lremote ? &e->r2->m : e->m, lremote ? e->feed2 : &none,
                    lremote ? e->r2->target : "本机", e->r && e->r->enabled ? &e->r->m : NULL,
                    e->r && e->r->enabled ? e->feed : NULL,
                    e->r && e->r->enabled ? e->r->target : NULL);
}

const scene_t SCENE_PANEL = {"panel", "运维面板", draw, sig};