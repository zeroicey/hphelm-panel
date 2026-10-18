#!/usr/bin/env python3
"""render-region.py —— 放大某个矩形区域（可指定每格像素），用于细看某个控件

用法：
    python3 tools/render-region.py /tmp/shot.png X0 Y0 X1 Y1 [每格像素]

例（看登录页卡片区，4px 一格）：
    python3 tools/render-region.py /tmp/login.png 700 560 1230 780 4

每格像素越小越细（1 = 逐像素，输出会很长）。配合 render-ascii.py：
先整屏找位置，再局部放大看形状。

⚠️ 默认滤掉「颜色直方图」输出（那部分会淹没地图）。需要颜色统计时自己加
   `magick <png> -crop ... -format %c histogram:info:- | sort -rn | head`。
"""
import subprocess
import sys

RAMP = " .:-=+*#%@"


def main() -> int:
    if len(sys.argv) < 6:
        print(__doc__)
        return 2
    png = sys.argv[1]
    x0, y0, x1, y1 = (int(v) for v in sys.argv[2:6])
    cell = int(sys.argv[6]) if len(sys.argv) > 6 else 4
    if cell < 1:
        cell = 1

    try:
        ident = subprocess.check_output(
            ["magick", "identify", "-format", "%w %h", png], stderr=subprocess.DEVNULL)
        w, h = (int(x) for x in ident.split())
    except Exception as exc:
        print("读不到图片尺寸：%r" % (exc,), file=sys.stderr)
        return 1

    x1 = min(x1, w)
    y1 = min(y1, h)
    data = subprocess.check_output(
        ["magick", png, "-depth", "8", "rgb:-"], stderr=subprocess.DEVNULL)

    print("区域 x%d..%d y%d..%d 每格 %dpx（图 %dx%d）" % (x0, x1, y0, y1, cell, w, h))
    hdr = "     "
    for x in range(x0, x1, cell):
        hdr += str((x // 10) % 10) if x % 10 < cell else " "
    print(hdr)

    for y in range(y0, y1, cell):
        line = ""
        for x in range(x0, x1, cell):
            tot = n = 0
            for yy in range(y, min(y1, y + cell)):
                base = yy * w
                for xx in range(x, min(x1, x + cell)):
                    i = (base + xx) * 3
                    tot += (data[i] * 299 + data[i + 1] * 587 + data[i + 2] * 114) // 1000
                    n += 1
            lum = tot // max(n, 1)
            line += RAMP[min(len(RAMP) - 1, lum * len(RAMP) // 256)]
        print("%4d %s" % (y, line))
    return 0


if __name__ == "__main__":
    sys.exit(main())
