/* theme.c —— 白天 / 夜间两套配色 + 固定时刻判定
 *
 * 只有这里知道具体色值；其余文件一律用 theme.h 的语义宏（C_MAUVE/C_TEXT/…），
 * 所以版面和内容绘制对主题完全无感，加幕也不用写两遍。 */
#include <time.h>

#include "theme.h"

palette_t TP;   /* 当前主题（进程内唯一可变量） */

/* 夜间 = Catppuccin Mocha（沿用原来的观感） */
const palette_t THEME_NIGHT = {
    .base = 0x1e1e2e, .mantle = 0x181825, .crust = 0x11111b,
    .surface0 = 0x313244, .surface1 = 0x45475a, .surface2 = 0x585b70,
    .text = 0xcdd6f4, .subtext1 = 0xbac2de, .subtext0 = 0xa6adc8,
    .overlay1 = 0x7f849c, .overlay0 = 0x6c7086,
    .mauve = 0xcba6f7, .blue = 0x89b4fa, .green = 0xa6e3a1,
    .yellow = 0xf9e2af, .peach = 0xfab387, .red = 0xf38ba8,
    .teal = 0x94e2d5, .lavender = 0xb4befe,
    .bg0 = 0x0b1220, .bg1 = 0x020306,
};

/* 白天 = Catppuccin Latte（语义同色相，亮度整体抬起来；适合白天对着窗看） */
const palette_t THEME_DAY = {
    .base = 0xeff1f5, .mantle = 0xe6e9ef, .crust = 0xdce0e8,
    .surface0 = 0xccd0da, .surface1 = 0xbcc0cc, .surface2 = 0xacb0be,
    .text = 0x4c4f69, .subtext1 = 0x5c5f77, .subtext0 = 0x6c6f85,
    .overlay1 = 0x8c8fa1, .overlay0 = 0x7c7f93,
    .mauve = 0x8839ef, .blue = 0x1e66f5, .green = 0x40a02b,
    .yellow = 0xdf8e1d, .peach = 0xfe640b, .red = 0xd20f39,
    .teal = 0x179299, .lavender = 0x7287fd,
    .bg0 = 0xfbfcfd, .bg1 = 0xe7eaf1,
};

static int is_night;

void theme_set(int night) {
    is_night = night ? 1 : 0;
    TP = is_night ? THEME_NIGHT : THEME_DAY;
}

int theme_is_night(void) { return is_night; }

int theme_night_at(time_t t, int day_min, int night_min) {
    struct tm tm;
    localtime_r(&t, &tm);
    int m = tm.tm_hour * 60 + tm.tm_min;
    if (!night_min) night_min = 19 * 60;
    if (!day_min) day_min = 7 * 60;
    if (day_min == night_min) return 0;
    /* 跨零点：night_min > day_min 时，[night, 24) ∪ [0, day) 是夜间 */
    if (day_min < night_min) return m >= night_min || m < day_min;
    return m >= night_min && m < day_min;
}
