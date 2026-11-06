#!/usr/bin/env python3
"""pngcrop.py —— 纯标准库的 PNG 读/裁/写（不依赖 ImageMagick / PIL）

为什么存在：`render-ascii.py` / `measure.py` 都靠 `magick`，而运行机 hpopi5b 没装
ImageMagick ⇒ 那条「量像素」的链路在真机链路上是断的。这个文件只做三件事：
读 PNG → 裁一块 → 写成 PNG 或打成 ASCII 亮度图，够用来核对「某个像素/某一行/某一列在哪」。

用法：
    python3 tools/pngcrop.py in.png out.png --crop X,Y,W,H [--zoom N]
    python3 tools/pngcrop.py in.png --crop X,Y,W,H --ascii [--zoom N] [--threshold]
    python3 tools/pngcrop.py in.png --col COLOR --row Y     # 某行里某颜色的横向游程
    python3 tools/pngcrop.py in.png --runs Y                # 某行的颜色分段（量柱宽/间距用）

只用 zlib/struct，别的什么都不 import。
"""

import sys
import zlib
import struct


# ---------------------------------------------------------------- 读 PNG
def load_png(path):
    data = open(path, "rb").read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "不是 PNG"
    pos = 8
    idat = b""
    w = h = depth = ctype = None
    while pos < len(data):
        (ln,) = struct.unpack(">I", data[pos : pos + 4])
        typ = data[pos + 4 : pos + 8]
        body = data[pos + 8 : pos + 8 + ln]
        pos += 12 + ln
        if typ == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", body[:10])
            if depth != 8 or ctype not in (2, 6):
                raise SystemExit("只支持 8bit RGB/RGBA（这张是 depth=%d ctype=%d）" % (depth, ctype))
        elif typ == b"IDAT":
            idat += body
        elif typ == b"IEND":
            break
    nch = 4 if ctype == 6 else 3
    raw = zlib.decompress(idat)
    stride = w * nch
    out = bytearray(h * stride)
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        ft = raw[p]
        p += 1
        line = bytearray(raw[p : p + stride])
        p += stride
        if ft == 1:  # Sub
            for i in range(nch, stride):
                line[i] = (line[i] + line[i - nch]) & 0xFF
        elif ft == 2:  # Up
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif ft == 3:  # Average
            for i in range(stride):
                a = line[i - nch] if i >= nch else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 0xFF
        elif ft == 4:  # Paeth
            for i in range(stride):
                a = line[i - nch] if i >= nch else 0
                b = prev[i]
                c = prev[i - nch] if i >= nch else 0
                pp = a + b - c
                pa, pb, pc = abs(pp - a), abs(pp - b), abs(pp - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        elif ft != 0:
            raise SystemExit("坏行过滤类型 %d" % ft)
        out[y * stride : (y + 1) * stride] = line
        prev = line
    return w, h, nch, out


# ---------------------------------------------------------------- 写 PNG
def save_png(path, w, h, nch, buf):
    ctype = 6 if nch == 4 else 2
    raw = bytearray()
    stride = w * nch
    for y in range(h):
        raw.append(0)
        raw += buf[y * stride : (y + 1) * stride]

    def chunk(typ, body):
        return (
            struct.pack(">I", len(body))
            + typ
            + body
            + struct.pack(">I", zlib.crc32(typ + body) & 0xFFFFFFFF)
        )

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, ctype, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    open(path, "wb").write(png)


def crop(w, h, nch, buf, x, y, cw, ch):
    x = max(0, min(x, w))
    y = max(0, min(y, h))
    cw = min(cw, w - x)
    ch = min(ch, h - y)
    out = bytearray(cw * ch * nch)
    for j in range(ch):
        s = ((y + j) * w + x) * nch
        out[j * cw * nch : (j + 1) * cw * nch] = buf[s : s + cw * nch]
    return cw, ch, out


def zoom(w, h, nch, buf, n):
    if n <= 1:
        return w, h, buf
    ow, oh = w * n, h * n
    out = bytearray(ow * oh * nch)
    for j in range(oh):
        sy = j // n
        for i in range(ow):
            sx = i // n
            s = (sy * w + sx) * nch
            d = (j * ow + i) * nch
            out[d : d + nch] = buf[s : s + nch]
    return ow, oh, out


# ---------------------------------------------------------------- ASCII
def ascii_art(w, h, nch, buf, threshold=False):
    ramp = " .:-=+*#%@"
    for j in range(h):
        row = []
        for i in range(w):
            s = (j * w + i) * nch
            r, g, b = buf[s], buf[s + 1], buf[s + 2]
            lum = (r * 299 + g * 587 + b * 114) // 1000
            if threshold:
                row.append(" " if lum > 160 else "#")
            else:
                row.append(ramp[min(len(ramp) - 1, (255 - lum) * len(ramp) // 256)])
        print("%4d|%s" % (j, "".join(row)))


# ---------------------------------------------------------------- 游程
def runs_at(w, h, nch, buf, y):
    """返回某一行的颜色游程：[(x, count, '#rrggbb'), ...]，相邻同色合并"""
    out = []
    prev = None
    for i in range(w):
        s = (y * w + i) * nch
        c = (buf[s], buf[s + 1], buf[s + 2])
        if c != prev:
            out.append([i, 1, c])
            prev = c
        else:
            out[-1][1] += 1
    return out


def main(argv):
    if len(argv) < 2:
        raise SystemExit(__doc__)
    path = argv[1]
    w, h, nch, buf = load_png(path)
    args = argv[2:]
    out = None
    cx = cy = cw = ch = None
    z = 1
    mode = "save"
    y_row = None
    minrun = 1
    i = 0
    if args and not args[0].startswith("-"):
        out = args[0]
        i = 1
    while i < len(args):
        a = args[i]
        if a == "--crop":
            cx, cy, cw, ch = [int(v) for v in args[i + 1].split(",")]
            i += 2
        elif a == "--zoom":
            z = int(args[i + 1])
            i += 2
        elif a == "--ascii":
            mode = "ascii"
            i += 1
        elif a == "--threshold":
            mode = "ascii_thr"
            i += 1
        elif a == "--runs":
            mode, y_row = "runs", int(args[i + 1])
            i += 2
        elif a == "--minrun":
            minrun = int(args[i + 1])
            i += 2
        elif a == "--info":
            print("%dx%d nch=%d" % (w, h, nch))
            return
        else:
            raise SystemExit("未知参数 %s" % a)
    if y_row is not None:
        print("row %d:" % y_row)
        for x, n, c in runs_at(w, h, nch, buf, y_row):
            if n >= minrun:
                print("  x=%4d len=%4d #%02x%02x%02x" % (x, n, c[0], c[1], c[2]))
        return
    if cx is not None:
        w, h, buf = crop(w, h, nch, buf, cx, cy, cw, ch)
    if mode.startswith("ascii"):
        ascii_art(w, h, nch, buf, threshold=(mode == "ascii_thr"))
        return
    if out is None:
        out = "out.png"
    w, h, buf = zoom(w, h, nch, buf, z)
    save_png(out, w, h, nch, buf)
    print("%s  %dx%d" % (out, w, h))


if __name__ == "__main__":
    main(sys.argv)
