# 离屏 harness 渲染出来**全黑**：忘了 `theme_set()`

**症状**：为了在没有 Xvfb / 没有 make 的 hpcmd 上验版式，写了个 harness 直接调
`view_panel_draw()` 画到 `cairo_image_surface` 再导 PPM。程序 exit 0、日志正常、
`wrote panel.ppm (1920x1080)` 也打印了，但**每一个像素都是 (0,0,0)**；
分析器（ASCII 图 / 行带统计）只吐出一片空白，看起来像「面板什么都没画」。

**根因**：`src/theme.h` 里所有语义色宏都是 `extern palette_t TP;` 的成员
（`C_MAUVE` → `TP.mauve`、`C_BASE` → `TP.base` …），而 `TP` 是**运行期**由
`theme_set(int night)`（`src/theme.c`）整体赋值的全局变量。harness 只调了
`view_init(lay)`（字体/布局）就开画 ⇒ `TP` 全 0 ⇒ 黑底画黑字、alpha 也全 0。

**修复**：画之前先 `theme_set(1)`（Mocha）/ `theme_set(0)`（Latte）。

**判据 / 下次怎么一眼认出**：

- 先做**独立对照**：同一进程里画一个已知颜色（红色矩形）再读回像素。
  如果红矩形能读出来、面板却是黑的 ⇒ 导出链路没问题，是调色板/绘制参数没初始化。
  如果连红矩形都是黑的 ⇒ 才是导出/stride/format 的锅（BGRA 别忘了 `row[x*4+2]` 是 R）。
- 完整可用的编译命令（hpcmd 上没有 pkg-config，头文件要手拼，harfbuzz 也要 `-I`）：

```sh
gcc -O2 -Wall -Wextra -Isrc -I/usr/include/cairo -I/usr/include/pango-1.0 \
    -I/usr/include/glib-2.0 -I/usr/lib/glib-2.0/include -I/usr/include/harfbuzz \
    -o /tmp/hpver/harness harness.c src/view_panel.c src/view.c src/theme.c src/metrics.c \
    -lcairo -lpangocairo-1.0 -lpango-1.0 -lgobject-2.0 -lglib-2.0 -lm
```

（`src/panel.c` 在 hpcmd 上编不了：缺 `/usr/include/X11/extensions/scrnsaver.h`；
所以只能验绘制层，CPU/RSS 判据必须在 hpopi5b 上用 `tools/deploy.sh` 跑。）