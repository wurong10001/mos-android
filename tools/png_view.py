#!/usr/bin/env python3
# 把 PNG 截图指定区域渲染成 ASCII art (无需显示器, 用于验证 UI 渲染)
# 用法: python3 tools/png_view.py <png> [x0 y0 x1 y1 [step]]
#       不带区域参数时显示整图缩略
import struct
import sys
import zlib


def decode_png(path):
    data = open(path, "rb").read()
    pos = 8
    idat = b""
    while pos < len(data):
        length, ctype = struct.unpack(">I4s", data[pos:pos + 8])
        chunk = data[pos + 8:pos + 8 + length]
        if ctype == b"IHDR":
            w, h, bd, ct = struct.unpack(">IIBB", chunk[:10])
        elif ctype == b"IDAT":
            idat += chunk
        pos += 12 + length
    raw = zlib.decompress(idat)
    bpp = {0: 1, 2: 3, 4: 2, 6: 4}[ct]
    stride = w * bpp
    out = bytearray(h * stride)
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        f = raw[p]; p += 1
        line = bytearray(raw[p:p + stride]); p += stride
        if f == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 0xFF
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif f == 3:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 0xFF
        elif f == 4:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                b = prev[i]; c = prev[i - bpp] if i >= bpp else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        out[y * stride:(y + 1) * stride] = line
        prev = line

    def px(x, y):
        i = y * stride + x * bpp
        if bpp >= 3:
            return (out[i], out[i + 1], out[i + 2])
        return (out[i],) * 3

    return w, h, px


def main():
    w, h, px = decode_png(sys.argv[1])
    if len(sys.argv) >= 6:
        x0, y0, x1, y1 = (int(v) for v in sys.argv[2:6])
        step = int(sys.argv[6]) if len(sys.argv) > 6 else 1
    else:
        x0, y0, x1, y1, step = 0, 0, w, h, max(1, w // 100)
    bg = px(2, 2)
    print("size %dx%d region=(%d,%d)-(%d,%d) step=%d bg=%s" % (w, h, x0, y0, x1, y1, step, bg))
    for y in range(y0, min(y1, h), step):
        row = []
        for x in range(x0, min(x1, w), step):
            r, g, b = px(x, y)
            d = abs(r - bg[0]) + abs(g - bg[1]) + abs(b - bg[2])
            row.append("#" if d > 60 else ".")
        print("".join(row))


if __name__ == "__main__":
    main()
