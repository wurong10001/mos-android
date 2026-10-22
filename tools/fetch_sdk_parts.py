#!/usr/bin/env python3
"""从大 zip 中按需提取文件 (HTTP Range, 不整包下载)

通用版 tools/fetch_ndk_headers.py: 传入 zip URL 和需要的前缀/文件名列表。

用法:
  python3 tools/fetch_sdk_parts.py <zip_url> <输出目录> <路径前缀或精确文件名> [...]

例:
  python3 tools/fetch_sdk_parts.py \
    https://github.com/kaspersigi/android-sdk-linux-aarch64/releases/download/v1.0.12/android-sdk-linux.zip \
    ~/android-sdk \
    build-tools/36.0.0/aapt2 \
    platforms/android-36/android.jar
"""
import os
import struct
import sys
import urllib.request
import zlib


def http_range(url, start, end):
    req = urllib.request.Request(url, headers={"Range": "bytes=%d-%d" % (start, end)})
    with urllib.request.urlopen(req, timeout=300) as r:
        return r.read()


def fetch_tail(url, n):
    req = urllib.request.Request(url, method="HEAD")
    with urllib.request.urlopen(req, timeout=60) as r:
        size = int(r.headers["Content-Length"])
    return http_range(url, max(0, size - n), size - 1), size


def find_eocd(tail):
    idx = tail.rfind(b"PK" + bytes([5, 6]))
    if idx < 0:
        raise RuntimeError("EOCD not found")
    cd_size, cd_offset = struct.unpack("<II", tail[idx + 12:idx + 20])
    return cd_offset, cd_size


def parse_central_directory(data):
    pos = 0
    while pos + 46 <= len(data) and data[pos:pos + 4] == b"PK" + bytes([1, 2]):
        (ver, ver_need, flags, method, mtime, mdate, crc, comp_size, uncomp_size,
         name_len, extra_len, comment_len, disk_start, int_attr, ext_attr,
         local_off) = struct.unpack("<HHHHHHIIIHHHHHII", data[pos + 4:pos + 46])
        name = data[pos + 46:pos + 46 + name_len].decode("utf-8", "replace")
        yield name, local_off, comp_size, method
        pos += 46 + name_len + extra_len + comment_len


def extract_file(url, local_off, comp_size, method):
    head = http_range(url, local_off, local_off + 29)
    if head[:4] != b"PK" + bytes([3, 4]):
        raise RuntimeError("bad local header")
    name_len, extra_len = struct.unpack("<HH", head[26:30])
    data_start = local_off + 30 + name_len + extra_len
    raw = http_range(url, data_start, data_start + comp_size - 1)
    if method == 0:
        return raw
    return zlib.decompress(raw, -15)


def main():
    url = sys.argv[1]
    out_dir = sys.argv[2]
    patterns = sys.argv[3:]

    tail, total = fetch_tail(url, 1024 * 1024)
    print("zip size: %.1f MB" % (total / 1048576))
    cd_offset, cd_size = find_eocd(tail)
    cd = http_range(url, cd_offset, cd_offset + cd_size - 1)

    matched = 0
    total_bytes = 0
    for name, off, csize, method in parse_central_directory(cd):
        # zip 内可能带顶层目录 (如 sdk/), 同时尝试带/不带前缀匹配
        rel = name.split("/", 1)[1] if "/" in name else name
        if any(rel == p or rel.startswith(p.rstrip("/") + "/") or
               rel.startswith(p) for p in patterns):
            if name.endswith("/"):
                continue
            dest = os.path.join(out_dir, rel)
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            data = extract_file(url, off, csize, method)
            with open(dest, "wb") as f:
                f.write(data)
            matched += 1
            total_bytes += len(data)
            print("  %s (%.1f KB)" % (rel, len(data) / 1024))
    print("done: %d files, %.1f MB -> %s" % (matched, total_bytes / 1048576, out_dir))


if __name__ == "__main__":
    main()
