/* panel.c —— hphelm 面板 + 自研锁屏（状态机）
 *
 * 用户要的四条行为：
 *   ① 长时间未操作 → 进入自己画的资源面板
 *   ② 晃鼠标 / 点一下 → 进入登录页
 *   ③ 登录成功 → 回到桌面正常操作
 *   ④ 再次长时间未操作 → 重新进入面板（循环）
 *
 * 技术选型（实测，详见 README 与 runbooks/hphelm.md）：
 *   X11 + cairo + pango，**不套 GUI 工具包**；最小程序 PSS 7.4 MiB / 2 线程。
 *   认证走自建 setuid 助手 hphelm-auth（PAM `login` 栈），**不再用 lightdm 的 greeter**。
 *
 * 状态机：
 *   DESKTOP ──idle≥panel_idle──▶ PANEL ──input(≥input_grace)──▶ LOGIN
 *      ▲                                                          │
 *      └──────────────认证成功──────────────────────────────────┘
 *   LOGIN ──idle≥login_back──▶ PANEL（密码框留空、清错误）
 *   PANEL ──idle≥sleep_off──▶ 关屏（DPMS），任意输入即唤醒
 *
 * 用法： hphelm-panel [--scale N] [--panel-idle S] [--login-back S]
 *                     [--input-grace S] [--sleep-off S] [--dump] [--once]
 *                     [--state desktop|panel|login]   （测试用：强制初始状态）
 */
#define _GNU_SOURCE
#include <X11/XKBlib.h>
#include <X11/extensions/scrnsaver.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <cairo/cairo-xlib.h>
#include <cairo/cairo.h>
#include <errno.h>
#include <math.h>
#include <pango/pangocairo.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "auth.h"
#include "metrics.h"
#include "theme.h"
#include "view.h"

enum { ST_DESKTOP, ST_PANEL, ST_LOGIN };

static const char *state_name(int s) {
    switch (s) {
    case ST_DESKTOP: return "DESKTOP";
    case ST_PANEL: return "PANEL";
    case ST_LOGIN: return "LOGIN";
    default: return "?";
    }
}

/* ---------------- 配置 ---------------- */
static double SCALE = 1.48;
static double CFG_PANEL_IDLE = 180;   /* 桌面空闲多久进面板 */
static double CFG_LOGIN_BACK = 120;   /* 登录页空闲多久回面板 */
static double CFG_INPUT_GRACE = 1.2;  /* 进面板后多久才接受“唤醒”（防误触） */
static double CFG_SLEEP_OFF = 3600;   /* 面板空闲多久关屏 */
static int FORCE_STATE = -1;

/* ---------------- X 状态 ---------------- */
static Display *dpy;
static Window root, win;
static int SW = 1920, SH = 1080;
static cairo_surface_t *surf;
static cairo_t *cr;
static PangoLayout *lay;
static metrics_t M;
static int scr;

/* ---------------- 运行状态 ---------------- */
static int state = ST_DESKTOP;
static double state_enter = 0;
static char g_user[64];
static char pw[256];
static int pwlen = 0;
static char g_err[128];
static int busy = 0;
static int retry_at = 0;      /* 失败冷却结束的时刻（monotonic） */
static int screen_off = 0;
static int cursor_on = 1;

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec / 1e9;
}

static double x_idle(void) {
    XScreenSaverInfo *info = XScreenSaverAllocInfo();
    if (!info) return 0;
    double idle = 0;
    if (XScreenSaverQueryInfo(dpy, root, info)) idle = info->idle / 1000.0;
    XFree(info);
    return idle;
}

static void logf_(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char t[32];
    time_t tt = time(NULL);
    strftime(t, sizeof t, "%H:%M:%S", localtime(&tt));
    printf("[%s] ", t);
    vprintf(fmt, ap);
    printf("\n");
    fflush(stdout);
    va_end(ap);
}

/* ---------------- 窗口显隐 ---------------- */
/* ⚠️ 键盘 grab 必须「显示时抓、隐藏时放」：
 *    只在启动时抓一次 ⇒ 回到 DESKTOP 后 grab 仍生效 ⇒ 桌面收不到任何按键（实测会踩）。
 *    隐藏时还要把焦点还给 root，否则焦点留在一个未映射窗口上。 */
static void win_show(void) {
    XMapRaised(dpy, win);
    XSync(dpy, False);
    XSetInputFocus(dpy, win, RevertToPointerRoot, CurrentTime);
    XGrabKeyboard(dpy, win, True, GrabModeAsync, GrabModeAsync, CurrentTime);
    XFlush(dpy);
}

static void win_hide(void) {
    XUngrabKeyboard(dpy, CurrentTime);
    XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
    XUnmapWindow(dpy, win);
    XFlush(dpy);
}

static void set_state(int s) {
    if (s == state) return;
    logf_("状态 %s → %s（idle=%.1fs）", state_name(state), state_name(s), x_idle());
    state = s;
    state_enter = now_s();
    g_err[0] = 0;
    if (s == ST_DESKTOP) {
        win_hide();
        pwlen = 0;
        pw[0] = 0;
    } else {
        win_show();
    }
    if (s == ST_LOGIN) {
        pwlen = 0;
        pw[0] = 0;
        retry_at = 0;
    }
}

/* 关屏 / 唤醒 */
static void dpms(int on) {
    /* on=1 开屏；on=0 关屏 */
    Display *d = dpy;
    char cmd[128];
    snprintf(cmd, sizeof cmd, "xset dpms force %s", on ? "on" : "off");
    if (system(cmd) != 0) { /* 忽略 */ }
    screen_off = !on;
    (void)d;
}

/* ---------------- 绘制 ---------------- */
static void redraw(void) {
    if (state == ST_DESKTOP) return;
    view_ctx_t v = {.scale = SCALE, .lw = SW / SCALE, .lh = SH / SCALE};
    cairo_save(cr);
    cairo_scale(cr, SCALE, SCALE);
    if (state == ST_PANEL) {
        view_panel_draw(cr, &v, &M);
    } else {
        int wait = (retry_at > 0) ? (int)(retry_at - now_s()) : 0;
        if (wait < 0) wait = 0;
        view_login_draw(cr, &v, &M, g_user, pwlen, g_err, busy, cursor_on, wait);
    }
    cairo_restore(cr);
    cairo_surface_flush(surf);
    XFlush(dpy);
}

/* ---------------- 认证 ---------------- */
static void try_auth(void) {
    if (busy) return;
    if (retry_at > 0 && now_s() < retry_at) return;
    if (pwlen == 0) return;
    busy = 1;
    g_err[0] = 0;
    redraw();
    int r = auth_check(g_user, pw);
    busy = 0;
    if (r == 1) {
        logf_("认证成功 ⇒ 回桌面");
        pwlen = 0;
        pw[0] = 0;
        set_state(ST_DESKTOP);
        /* 把 idle 归零：刚解锁不该马上又进面板 */
        XResetScreenSaver(dpy);
        XFlush(dpy);
    } else {
        pwlen = 0;
        pw[0] = 0;
        retry_at = (int)now_s() + 2;
        snprintf(g_err, sizeof g_err, "%s", r < 0 ? "无法启动认证助手" : "密码错误");
        logf_("认证失败（r=%d）", r);
        redraw();
    }
}

/* ---------------- 输入处理 ---------------- */
static void on_key(XKeyEvent *ke) {
    /* 面板态：任意键都算“唤醒” */
    if (state == ST_PANEL) {
        if (now_s() - state_enter >= CFG_INPUT_GRACE) {
            if (screen_off) dpms(1);
            set_state(ST_LOGIN);
        }
        return;
    }
    if (state != ST_LOGIN) return;
    if (screen_off) {
        dpms(1);
        return;
    }
    KeySym ks = XkbKeycodeToKeysym(dpy, (KeyCode)ke->keycode, 0, 0);
    if (ks == XK_Return || ks == XK_KP_Enter) {
        try_auth();
        return;
    }
    if (ks == XK_Escape) {
        pwlen = 0;
        pw[0] = 0;
        g_err[0] = 0;
        redraw();
        return;
    }
    if (ks == XK_BackSpace) {
        if (pwlen > 0) pw[--pwlen] = 0;
        g_err[0] = 0;
        redraw();
        return;
    }
    /* 可打印字符 */
    char buf[8] = {0};
    int n = XLookupString(ke, buf, sizeof buf - 1, &ks, NULL);
    if (n > 0 && pwlen + n < (int)sizeof pw) {
        memcpy(pw + pwlen, buf, (size_t)n);
        pwlen += n;
        pw[pwlen] = 0;
        g_err[0] = 0;
        redraw();
    }
}

static void on_motion(void) {
    if (state == ST_PANEL) {
        if (now_s() - state_enter >= CFG_INPUT_GRACE) {
            if (screen_off) dpms(1);
            set_state(ST_LOGIN);
        }
        return;
    }
    if (state == ST_LOGIN && screen_off) dpms(1);
}

static void on_button(void) {
    if (state == ST_PANEL) {
        if (now_s() - state_enter >= CFG_INPUT_GRACE) {
            if (screen_off) dpms(1);
            set_state(ST_LOGIN);
        }
        return;
    }
    if (state == ST_LOGIN && screen_off) dpms(1);
}

/* ---------------- 主循环 ---------------- */
int main(int argc, char **argv) {
    int once = 0, dump = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--once")) once = 1;
        else if (!strcmp(argv[i], "--dump")) dump = 1;
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) SCALE = atof(argv[++i]);
        else if (!strcmp(argv[i], "--panel-idle") && i + 1 < argc) CFG_PANEL_IDLE = atof(argv[++i]);
        else if (!strcmp(argv[i], "--login-back") && i + 1 < argc) CFG_LOGIN_BACK = atof(argv[++i]);
        else if (!strcmp(argv[i], "--input-grace") && i + 1 < argc) CFG_INPUT_GRACE = atof(argv[++i]);
        else if (!strcmp(argv[i], "--sleep-off") && i + 1 < argc) CFG_SLEEP_OFF = atof(argv[++i]);
        else if (!strcmp(argv[i], "--state") && i + 1 < argc) {
            if (!strcmp(argv[i + 1], "desktop")) FORCE_STATE = ST_DESKTOP;
            else if (!strcmp(argv[i + 1], "panel")) FORCE_STATE = ST_PANEL;
            else if (!strcmp(argv[i + 1], "login")) FORCE_STATE = ST_LOGIN;
            i++;
        }
    }

    metrics_init(&M);
    auth_default_user(g_user, sizeof g_user);

    if (dump) {
        for (int i = 0; i < 4; i++) {
            metrics_sample(&M, i == 3);
            if (i < 3) usleep(1000000);
        }
        printf("host=%s iface=%s ip=%s net_up=%d ts=%s(%d)\n", M.host, M.iface, M.ip, M.net_up,
               M.ts_ip, M.ts_up);
        printf("cpu=%.1f%% ncore=%d load=%.2f %.2f %.2f temp=%.1fC gov=%s %ldMHz\n", M.cpu_pct,
               M.ncore, M.load[0], M.load[1], M.load[2], M.temp_c, M.governor, M.freq_mhz);
        printf("mem=%.0fM/%.0fM (%.0f%%) swap=%.0fM/%.0fM disk=%.0f%%\n", M.mem_used_mb,
               M.mem_total_mb, M.mem_pct, M.swap_used_mb, M.swap_total_mb, M.disk_used_pct);
        printf("nproc=%d top:", M.nproc);
        for (int i = 0; i < M.ntop; i++) printf(" %s(%.0fM)", M.top[i].name, M.top[i].rss_mb);
        printf("\n");
        printf("panel_idle=%.0fs login_back=%.0fs grace=%.1fs sleep_off=%.0fs user=%s\n",
               CFG_PANEL_IDLE, CFG_LOGIN_BACK, CFG_INPUT_GRACE, CFG_SLEEP_OFF, g_user);
        return 0;
    }

    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "hphelm-panel: 无法打开 X display（DISPLAY=%s）\n",
                getenv("DISPLAY") ? getenv("DISPLAY") : "(null)");
        return 1;
    }
    scr = DefaultScreen(dpy);
    SW = DisplayWidth(dpy, scr);
    SH = DisplayHeight(dpy, scr);
    root = RootWindow(dpy, scr);

    /* 屏幕尺寸变化（换接口）时重建窗口 */
    XSetWindowAttributes attrs;
    attrs.override_redirect = True;   /* 自己管位置：面板要盖住整个屏幕（含 polybar） */
    attrs.background_pixel = BlackPixel(dpy, scr);
    win = XCreateWindow(dpy, root, 0, 0, (unsigned)SW, (unsigned)SH, 0,
                        CopyFromParent, InputOutput, CopyFromParent,
                        CWOverrideRedirect | CWBackPixel, &attrs);
    XStoreName(dpy, win, "hphelm-panel");
    XSelectInput(dpy, win,
                 ExposureMask | KeyPressMask | ButtonPressMask | PointerMotionMask |
                     StructureNotifyMask | VisibilityChangeMask);
    /* 窗口先不映射：映射/抓键盘/设焦点统一由 win_show() 做
       （对未映射窗口调 SetInputFocus 会 BadMatch 直接退出，实测 Major opcode 42） */

    surf = cairo_xlib_surface_create(dpy, win, DefaultVisual(dpy, scr), SW, SH);
    cr = cairo_create(surf);
    lay = pango_cairo_create_layout(cr);
    view_init(lay);

    /* 初始状态 */
    state = (FORCE_STATE >= 0) ? FORCE_STATE : ST_DESKTOP;
    state_enter = now_s();
    logf_("启动：初始 %s · panel_idle=%.0fs · login_back=%.0fs · grace=%.1fs · "
          "sleep_off=%.0fs · scale=%.2f · %dx%d",
          state_name(state), CFG_PANEL_IDLE, CFG_LOGIN_BACK, CFG_INPUT_GRACE, CFG_SLEEP_OFF,
          SCALE, SW, SH);
    if (state != ST_DESKTOP) win_show();

    /* 预填一点曲线数据 */
    for (int i = 0; i < 3; i++) {
        metrics_sample(&M, 0);
        usleep(120000);
    }

    int tick = 0;
    for (;;) {
        /* --- 事件 --- */
        while (XPending(dpy)) {
            XEvent ev;
            XNextEvent(dpy, &ev);
            switch (ev.type) {
            case KeyPress: on_key(&ev.xkey); break;
            case MotionNotify: on_motion(); break;
            case ButtonPress: on_button(); break;
            case Expose:
                if (state != ST_DESKTOP) redraw();
                break;
            case ConfigureNotify:
                if (ev.xconfigure.width != SW || ev.xconfigure.height != SH) {
                    SW = ev.xconfigure.width;
                    SH = ev.xconfigure.height;
                    cairo_xlib_surface_set_size(surf, SW, SH);
                    if (state != ST_DESKTOP) redraw();
                }
                break;
            default: break;
            }
        }

        /* --- 采样 --- */
        metrics_sample(&M, (tick % 5) == 0);

        /* --- 状态机 --- */
        double idle = x_idle();
        double in_state = now_s() - state_enter;

        if (state == ST_DESKTOP) {
            if (idle >= CFG_PANEL_IDLE) set_state(ST_PANEL);
        } else if (state == ST_PANEL) {
            if (idle >= CFG_SLEEP_OFF && !screen_off) {
                logf_("面板空闲 %.0fs ⇒ 关屏（DPMS）", idle);
                dpms(0);
            }
            /* 从面板「被叫醒」的情况在上面的输入事件里处理；
               这里兜底：如果 idle 突然变小（输入事件被我们漏掉），也切登录页 */
            if (idle < 0.5 && in_state > CFG_INPUT_GRACE) set_state(ST_LOGIN);
        } else if (state == ST_LOGIN) {
            if (in_state > 1.5 && idle >= CFG_LOGIN_BACK) {
                logf_("登录页空闲 %.0fs ⇒ 回面板", idle);
                set_state(ST_PANEL);
            }
        }

        /* --- 重绘（面板 1Hz；登录页只闪光标） --- */
        if (state == ST_PANEL) {
            redraw();
        } else if (state == ST_LOGIN) {
            int c = ((int)(now_s() * 2)) % 2;
            if (c != cursor_on) {
                cursor_on = c;
                redraw();
            } else if ((tick % 10) == 0) {
                redraw(); /* 时钟/等待秒数 */
            }
        }

        if (once) {
            redraw();
            break;
        }
        tick++;
        struct pollfd p = {ConnectionNumber(dpy), POLLIN, 0};
        poll(&p, 1, state == ST_DESKTOP ? 1000 : 500);
    }

    cairo_destroy(cr);
    cairo_surface_destroy(surf);
    XCloseDisplay(dpy);
    return 0;
}
