#!/usr/bin/env python3
"""从 Android NDK zip 中按需下载头文件 (不下载整个 600MB NDK)

通过 HTTP Range 请求读取 zip 中央目录, 只提取需要的文件:
  - toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/android/*.h
  - sources/android/native_app_glue/*

用法: python3 tools/fetch_ndk_headers.py <输出目录> [ndk版本]
"""
import os
import struct
import sys
import urllib.request
import zlib

NDK_BASE = "https://dl.google.com/android/repository/"
DEFAULT_NDK = "android-ndk-r27d-linux.zip"

SYSROOT_PREFIX = "toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/"
GLUE_PREFIX = "sources/android/native_app_glue/"


def http_range(url, start, end):
    req = urllib.request.Request(url, headers={"Range": "bytes=%d-%d" % (start, end)})
    with urllib.request.urlopen(req, timeout=120) as r:
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
    # EOCD: sig(4) disk(2) cd_disk(2) entries_disk(2) entries(2) cd_size(4) cd_offset(4)
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
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "ndk-headers"
    ndk = sys.argv[2] if len(sys.argv) > 2 else DEFAULT_NDK
    url = NDK_BASE + ndk
    print("reading central directory of", url)

    tail, total = fetch_tail(url, 1024 * 1024)
    print("zip size: %.1f MB" % (total / 1048576))
    cd_offset, cd_size = find_eocd(tail)
    if cd_offset + cd_size > total - (total - len(tail)):
        pass  # tail 已覆盖
    cd = http_range(url, cd_offset, cd_offset + cd_size - 1)
    print("central directory: %d bytes" % len(cd))

    wanted = []
    count = 0
    for entry in parse_central_directory(cd):
        count += 1
        name = entry[0]
        # zip 内所有条目都带一个根目录 (如 android-ndk-r27d/), 剥掉再匹配
        rel = name.split("/", 1)[1] if "/" in name else name
        if rel.startswith(SYSROOT_PREFIX + "android/") or rel.startswith(GLUE_PREFIX):
            if not rel.endswith("/"):
                wanted.append((rel,) + entry[1:])
    print("scanned %d entries, matched %d files" % (count, len(wanted)))

    for name, off, csize, method in wanted:
        if name.startswith(SYSROOT_PREFIX):
            dest = os.path.join(out_dir, "sysroot", name[len(SYSROOT_PREFIX):])
        else:
            dest = os.path.join(out_dir, "glue", os.path.basename(name))
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        data = extract_file(url, off, csize, method)
        with open(dest, "wb") as f:
            f.write(data)

    print("done -> %s (%d files)" % (out_dir, len(wanted)))


if __name__ == "__main__":
    main()
