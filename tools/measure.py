#!/usr/bin/env python3
"""measure.py —— 量一张截图的几何（**看不到图时的核心手段**）

三种模式（可组合）：

  --runs Y           取第 Y 行，把同色连续像素合并成「游程」⇒ 量柱宽/间距/对齐边界
  --bands            统计每行非背景像素数 ⇒ 量行数/行距（进程榜的行距对不对）
  --color R,G,B      统计某颜色的 bbox 与逐行分布（可配 --tol）⇒ 量某元素画在哪

用法：
    python3 tools/measure.py assets/panel.png --runs 240
    python3 tools/measure.py assets/panel.png --bands --bands-crop 27,460,1866,600
    python3 tools/measure.py assets/panel.png --color 250,179,135 --color-crop 1360,190,520,250

为什么需要它：维护这个项目的 AI 看不到图。设计值是算出来的（theme.h 的逻辑常量 × SCALE），
实测值必须逐位吻合；「看着差不多」不算过。
"""
import argparse
import subprocess
import sys


def load(png):
    try:
        ident = subprocess.check_output(
            ["magick", "identify", "-format", "%w %h", png], stderr=subprocess.DEVNULL)
        w, h = (int(v) for v in ident.split())
    except Exception as exc:  # noqa: BLE001
        sys.exit("读不到图片尺寸（magick identify 失败）：%r" % (exc,))
    raw = subprocess.check_output(
        ["magick", png, "-depth", "8", "rgb:-"], stderr=subprocess.DEVNULL)
    if len(raw) < w * h * 3:
        sys.exit("像素数据不完整：%d < %d" % (len(raw), w * h * 3))
    return w, h, raw


def px(raw, w, x, y):
    i = (y * w + x) * 3
    return (raw[i], raw[i + 1], raw[i + 2])


def mode_runs(w, h, raw, y, min_w):
    print("y=%d 行色块游程（只打印宽 >= %d 的块）：" % (y, min_w))
    runs, start, prev = [], 0, px(raw, w, 0, y)
    for x in range(1, w):
        cur = px(raw, w, x, y)
        if cur != prev:
            runs.append((start, x - 1, prev))
            start, prev = x, cur
    runs.append((start, w - 1, prev))
    for a, b, c in runs:
        if b - a + 1 >= min_w:
            print("  x%4d-%4d 宽%4d rgb%s" % (a, b, b - a + 1, c))


def mode_bands(w, h, raw, crop, ink_ratio, min_h):
    x0, y0, cw, ch = crop if crop else (0, 0, w, h)
    x0, y0 = max(0, x0), max(0, y0)
    cw, ch = min(cw, w - x0), min(ch, h - y0)
    # 背景参考：每行最右一个像素（渐变底在同一行内近似恒定）
    print("墨迹带（区域 x%d..%d y%d..%d，阈值 %.0f%% 像素非背景）："
          % (x0, x0 + cw, y0, y0 + ch, ink_ratio * 100))
    bands, start = [], None
    for y in range(y0, y0 + ch):
        base = px(raw, w, x0 + cw - 1, y)
        ink = 0
        for x in range(x0, x0 + cw, 2):  # 隔点采样即可
            p = px(raw, w, x, y)
            if max(abs(p[i] - base[i]) for i in range(3)) > 40:
                ink += 1
        is_ink = ink > (cw / 2.0) * ink_ratio
        if is_ink and start is None:
            start = y
        elif not is_ink and start is not None:
            if y - start >= min_h:
                bands.append((start, y - 1))
            start = None
    if start is not None:
        bands.append((start, y0 + ch - 1))
    prev = None
    for a, b in bands:
        gap = ("  间距 %d" % (a - prev)) if prev is not None else ""
        print("  y%4d-%4d 高%3d%s" % (a, b, b - a + 1, gap))
        prev = b
    print("  共 %d 条带" % len(bands))


def mode_color(w, h, raw, rgb, tol, crop):
    x0, y0, cw, ch = crop if crop else (0, 0, w, h)
    x0, y0 = max(0, x0), max(0, y0)
    cw, ch = min(cw, w - x0), min(ch, h - y0)
    bx0, by0, bx1, by1, n = 10 ** 9, 10 ** 9, -1, -1, 0
    rows = {}
    for y in range(y0, y0 + ch):
        for x in range(x0, x0 + cw):
            p = px(raw, w, x, y)
            if all(abs(p[i] - rgb[i]) <= tol for i in range(3)):
                n += 1
                bx0, by0 = min(bx0, x), min(by0, y)
                bx1, by1 = max(bx1, x), max(by1, y)
                rows.setdefault(y, [x, x])
                rows[y][0] = min(rows[y][0], x)
                rows[y][1] = max(rows[y][1], x)
    if not n:
        print("区域 x%d..%d y%d..%d 内没有 rgb%s（容差 %d）的像素"
              % (x0, x0 + cw, y0, y0 + ch, rgb, tol))
        return
    print("rgb%s 容差 %d：%d 像素  bbox x%d..%d y%d..%d"
          % (rgb, tol, n, bx0, bx1, by0, by1))
    ys = sorted(rows)
    step = max(1, len(ys) // 12)
    for y in ys[::step]:
        print("   y%4d  x %4d..%4d" % (y, rows[y][0], rows[y][1]))


def parse_crop(s):
    parts = [int(v) for v in s.split(",")]
    if len(parts) != 4:
        sys.exit("--*-crop 需要 X0,Y0,W,H")
    return tuple(parts)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("png")
    ap.add_argument("--runs", type=int, metavar="Y", help="量第 Y 行的色块游程")
    ap.add_argument("--runs-min", type=int, default=4, help="游程最小宽度（默认 4）")
    ap.add_argument("--bands", action="store_true", help="量墨迹带（行数/行距）")
    ap.add_argument("--bands-crop", metavar="X0,Y0,W,H")
    ap.add_argument("--bands-ink", type=float, default=0.004,
                    help="某行非背景像素比例超过它就算有墨（默认 0.004）")
    ap.add_argument("--bands-min", type=int, default=4, help="带的最小高度（默认 4）")
    ap.add_argument("--color", metavar="R,G,B", help="量某颜色的 bbox 与逐行分布")
    ap.add_argument("--color-crop", metavar="X0,Y0,W,H")
    ap.add_argument("--tol", type=int, default=30, help="颜色容差（默认 30）")
    a = ap.parse_args()

    w, h, raw = load(a.png)
    print("图 %dx%d" % (w, h))
    did = False
    if a.runs is not None:
        mode_runs(w, h, raw, a.runs, a.runs_min)
        did = True
    if a.bands:
        mode_bands(w, h, raw, parse_crop(a.bands_crop) if a.bands_crop else None,
                   a.bands_ink, a.bands_min)
        did = True
    if a.color:
        rgb = tuple(int(v) for v in a.color.split(","))
        mode_color(w, h, raw, rgb, a.tol,
                   parse_crop(a.color_crop) if a.color_crop else None)
        did = True
    if not did:
        ap.print_help()
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
