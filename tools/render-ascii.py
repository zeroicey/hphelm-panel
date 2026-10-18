#!/usr/bin/env python3
"""render-ascii.py —— 把整屏截图变成 ASCII，用于「看不见图也能核对布局」

用法：
    python3 tools/render-ascii.py /tmp/shot.png [每格宽] [每格高]

字符含义（亮度 10 档：" .:-=+*#%@"）：
    ' '=最暗  '@'=最亮      'C'=该格 >25% 像素是「彩色」（用于找头像/强调色块）

为什么需要它：本项目的开发环境看不到图（模型不支持图像输入），
布局核对只能靠像素统计。这个脚本能把 1920x1080 压成 ~78 行文本，
足以判断「某块内容在不在预期位置」。

配套：tools/render-region.py（把某个区域放大细看）
"""
import subprocess
import sys

RAMP = " .:-=+*#%@"


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    png = sys.argv[1]
    cw = int(sys.argv[2]) if len(sys.argv) > 2 else 12
    ch = int(sys.argv[3]) if len(sys.argv) > 3 else 14

    try:
        ident = subprocess.check_output(
            ["magick", "identify", "-format", "%w %h", png], stderr=subprocess.DEVNULL)
        w, h = (int(x) for x in ident.split())
    except Exception as exc:
        print("读不到图片尺寸（magick identify 失败）：%r" % (exc,), file=sys.stderr)
        return 1

    data = subprocess.check_output(
        ["magick", png, "-depth", "8", "rgb:-"], stderr=subprocess.DEVNULL)
    if len(data) < w * h * 3:
        print("像素数据不完整：%d < %d" % (len(data), w * h * 3), file=sys.stderr)
        return 1

    cols, rows = w // cw, h // ch
    print("   " + "".join(str((i // 10) % 10) if i % 10 == 0 else " " for i in range(cols)))
    for ry in range(rows):
        line = ""
        for rx in range(cols):
            tot = n = col = 0
            for y in range(ry * ch, min(h, (ry + 1) * ch)):
                base = y * w
                for x in range(rx * cw, min(w, (rx + 1) * cw)):
                    i = (base + x) * 3
                    r, g, b = data[i], data[i + 1], data[i + 2]
                    tot += (r * 299 + g * 587 + b * 114) // 1000
                    n += 1
                    if max(r, g, b) - min(r, g, b) > 45 and max(r, g, b) > 70:
                        col += 1
            lum = tot // max(n, 1)
            if col > n * 0.25:
                line += "C"
            else:
                line += RAMP[min(len(RAMP) - 1, lum * len(RAMP) // 256)]
        if line.strip():
            print("%4d %s" % (ry * ch, line))
    return 0


if __name__ == "__main__":
    sys.exit(main())
