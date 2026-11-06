/* xshot.c —— 把 X 上某个窗口截成 PNG（零新依赖：只用 x11 + cairo，本来就是构建依赖）
 *
 * 为什么自带这个：
 *   以前截图靠 ImageMagick 的 `import`，寄生在 **hpcmd** 上；hpcmd 2026-11-06 下线后，
 *   机队里 `import` / `magick` / `xwd` 都不存在了。而 cairo 本来就在依赖里 ⇒ 自己截。
 *   `docs/verify.md` 的「看不到图怎么验」依赖它（先像素取证，再谈好看）。
 *
 * 构建（在带屏幕的机器上，本仓库构建依赖已装）：
 *   cc -O2 -Wall -Wextra -o tools/xshot tools/xshot.c $(pkg-config --cflags --libs x11 cairo)
 *
 * 用法：
 *   DISPLAY=:0 tools/xshot out.png                 # 找标题含 "hpopi5b-panel" 的窗口（缺省找最上层全屏窗口）
 *   DISPLAY=:0 tools/xshot out.png 窗口标题子串     # 指定标题子串
 *   DISPLAY=:0 tools/xshot -l                      # 只列出候选窗口（调试用）
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <cairo/cairo.h>

#define MAXW 64

typedef struct {
    Window w;
    int depth;
    char name[256];
} cand_t;

static void win_name(Display *d, Window w, char *out, size_t n) {
    out[0] = 0;
    /* 优先 _NET_WM_NAME（UTF-8） */
    Atom a = XInternAtom(d, "_NET_WM_NAME", True);
    Atom type;
    int fmt;
    unsigned long cnt, after;
    unsigned char *prop = NULL;
    if (a != None && XGetWindowProperty(d, w, a, 0, 256, False, XInternAtom(d, "UTF8_STRING", False),
                                        &type, &fmt, &cnt, &after, &prop) == Success &&
        prop && cnt > 0) {
        snprintf(out, n, "%s", (const char *)prop);
        XFree(prop);
        return;
    }
    char *nm = NULL;
    if (XFetchName(d, w, &nm) && nm) {
        snprintf(out, n, "%s", nm);
        XFree(nm);
    }
}

/* 全屏覆盖层没有父窗口管理器，直接递归找；命中多个时取面积最大的（面板是全屏） */
static void collect(Display *d, Window w, cand_t *c, int *n, int max) {
    if (*n >= max) return;
    XWindowAttributes wa;
    if (!XGetWindowAttributes(d, w, &wa)) return;
    if (wa.map_state == IsViewable && wa.width > 200 && wa.height > 200) {
        c[*n].w = w;
        c[*n].depth = wa.depth;
        win_name(d, w, c[*n].name, sizeof c[*n].name);
        (*n)++;
    }
    Window root, parent, *kids = NULL;
    unsigned int nk = 0;
    if (!XQueryTree(d, w, &root, &parent, &kids, &nk)) return;
    for (unsigned int i = 0; i < nk && *n < max; i++) collect(d, kids[i], c, n, max);
    if (kids) XFree(kids);
}

static unsigned long chan(unsigned long px, unsigned long mask) {
    if (!mask) return 0;
    int shift = 0;
    unsigned long m = mask;
    while (!(m & 1)) { m >>= 1; shift++; }
    unsigned long v = (px & mask) >> shift;
    unsigned long max = m;
    /* 归一到 8 bit（掩码可能不是 0xff） */
    return (v * 255 + max / 2) / max;
}

int main(int argc, char **argv) {
    const char *out = NULL;
    const char *needle = "hpopi5b-panel";
    int list = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-l")) list = 1;
        else if (!out) out = argv[i];
        else needle = argv[i];
    }
    if (!out && !list) {
        fprintf(stderr, "usage: xshot OUT.png [title-substr] | xshot -l\n");
        return 2;
    }

    Display *d = XOpenDisplay(NULL);
    if (!d) {
        fprintf(stderr, "xshot: 打不开 DISPLAY=%s\n", getenv("DISPLAY") ? getenv("DISPLAY") : "(空)");
        return 1;
    }

    cand_t cand[MAXW];
    int nc = 0;
    collect(d, DefaultRootWindow(d), cand, &nc, MAXW);
    if (nc == 0) {
        fprintf(stderr, "xshot: 没有可截的窗口\n");
        return 1;
    }

    int pick = -1;
    if (needle && *needle) {
        for (int i = 0; i < nc; i++)
            if (strstr(cand[i].name, needle)) { pick = i; break; }
    }
    if (pick < 0) { /* 没指定/没命中 ⇒ 取面积最大的（面板 = 全屏覆盖层） */
        long best = -1;
        for (int i = 0; i < nc; i++) {
            XWindowAttributes wa;
            if (!XGetWindowAttributes(d, cand[i].w, &wa)) continue;
            long area = (long)wa.width * wa.height;
            if (area > best) { best = area; pick = i; }
        }
    }
    if (pick < 0) pick = 0;

    if (list) {
        for (int i = 0; i < nc; i++) {
            XWindowAttributes wa;
            XGetWindowAttributes(d, cand[i].w, &wa);
            printf("%s%lu  %dx%d  \"%s\"\n", i == pick ? "* " : "  ", cand[i].w, wa.width, wa.height,
                   cand[i].name);
        }
        return 0;
    }

    Window w = cand[pick].w;
    XWindowAttributes wa;
    if (!XGetWindowAttributes(d, w, &wa)) return 1;
    XImage *im = XGetImage(d, w, 0, 0, wa.width, wa.height, AllPlanes, ZPixmap);
    if (!im) {
        fprintf(stderr, "xshot: XGetImage 失败（窗口 %lu）\n", w);
        return 1;
    }

    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_RGB24, wa.width, wa.height);
    unsigned char *dst = cairo_image_surface_get_data(s);
    int stride = cairo_image_surface_get_stride(s);
    int fast = (im->depth == 24 && im->bits_per_pixel == 32 && im->red_mask == 0xff0000 &&
                im->green_mask == 0x00ff00 && im->blue_mask == 0x0000ff);
    for (int y = 0; y < wa.height; y++) {
        const unsigned char *src = (const unsigned char *)im->data + (size_t)y * im->bytes_per_line;
        unsigned char *row = dst + (size_t)y * stride;
        for (int x = 0; x < wa.width; x++) {
            unsigned long px;
            if (fast) {
                memcpy(&px, src + (size_t)x * 4, 4); /* BGRX，正好是 cairo RGB24 的字节序 */
                memcpy(row + (size_t)x * 4, &px, 4);
            } else if (im->bits_per_pixel == 32) {
                memcpy(&px, src + (size_t)x * 4, 4);
                unsigned long v = (chan(px, im->red_mask) << 16) | (chan(px, im->green_mask) << 8) |
                                  chan(px, im->blue_mask);
                memcpy(row + (size_t)x * 4, &v, 4);
            } else {
                row[x * 4 + 0] = (unsigned char)chan(XGetPixel(im, x, y), im->blue_mask);
                row[x * 4 + 1] = (unsigned char)chan(XGetPixel(im, x, y), im->green_mask);
                row[x * 4 + 2] = (unsigned char)chan(XGetPixel(im, x, y), im->red_mask);
                row[x * 4 + 3] = 0;
            }
        }
    }
    cairo_surface_mark_dirty(s);
    cairo_status_t st = cairo_surface_write_to_png(s, out);
    cairo_surface_destroy(s);
    XDestroyImage(im);
    XCloseDisplay(d);
    if (st != CAIRO_STATUS_SUCCESS) {
        fprintf(stderr, "xshot: 写 PNG 失败: %s\n", cairo_status_to_string(st));
        return 1;
    }
    printf("xshot: %dx%d window %lu \"%s\" -> %s\n", wa.width, wa.height, w, cand[pick].name, out);
    return 0;
}
