# -*- coding: utf-8 -*-
# 解码 PNG 并检测顶部/底部系统栏区域是否有非应用内容
import struct
import sys
import zlib

data = open(sys.argv[1], "rb").read()
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

def rowcolor(y, x):
    i = y * stride + x * bpp
    if bpp >= 3:
        return (out[i], out[i + 1], out[i + 2])
    return (out[i], out[i], out[i])

print("size %dx%d" % (w, h))
# 应用背景色 = (24,20,16); 统计指定行的"非背景"像素比例
def scan(name, y0, y1):
    total = 0
    bgmatch = 0
    for y in range(y0, y1):
        for x in range(0, w, 4):
            r, g, b = rowcolor(y, x)
            total += 1
            if abs(r - 24) + abs(g - 20) + abs(b - 16) < 40:
                bgmatch += 1
    pct = 100.0 * bgmatch / total if total else 0
    print("%s (y %d-%d): background %.1f%%" % (name, y0, y1, pct))

scan("top strip   ", 0, 80)
scan("bottom strip", h - 140, h)
# 中间采样一行确认应用在渲染
scan("middle      ", h // 2, h // 2 + 40)
