#!/usr/bin/env python3
"""生成示例 WASM 模块 -> app/src/main/assets/sample.wasm

模块设计（与 Native 层的约定）：

  线性内存: 5 页 (320KB)
    偏移 0x1000 起: 虚拟显存 256x256, 每像素 4 字节 (0xAABBGGRR, 小端)
  导入 (import module "env"):
    host_present(offset: i32, width: i32, height: i32) -> ()
        wasm 画完一帧后调用, 把"显存偏移 + 宽高"传给宿主
    host_log(offset: i32, len: i32) -> ()
        让宿主打印线性内存里的一段字符串 (调试用)
  导出:
    memory              线性内存
    init() -> i32       返回显存偏移
    render(frame: i32) -> i32
        在显存中绘制一帧动画, 调用 host_present, 返回显存偏移

用法: python3 tools/gen_sample_wasm.py [输出路径]
"""
import os
import struct
import sys

# ---------------------------------------------------------------- 常量
W, H = 256, 256                 # 虚拟显存分辨率
FB_OFFSET = 0x1000              # 显存在线性内存中的偏移
FB_BYTES = W * H * 4
PAGES = (FB_OFFSET + FB_BYTES + 0xFFFF) // 0x10000  # 需要的内存页数 (64KB/页)

# ---------------------------------------------------------------- 编码工具
I32 = b"\x7f"


def u8(n):
    return bytes([n & 0xFF])


def uleb(n):
    out = b""
    while True:
        b = n & 0x7F
        n >>= 7
        if n:
            out += bytes([b | 0x80])
        else:
            out += bytes([b])
            return out


def sleb(n):
    """有符号 LEB128"""
    out = b""
    while True:
        b = n & 0x7F
        n >>= 7
        if (n == 0 and not (b & 0x40)) or (n == -1 and (b & 0x40)):
            out += bytes([b])
            return out
        out += bytes([b | 0x80])


def vec(items):
    return uleb(len(items)) + b"".join(items)


def section(sid, payload):
    return u8(sid) + uleb(len(payload)) + payload


def name(s):
    b = s.encode()
    return uleb(len(b)) + b


# ---------------------------------------------------------------- 指令
def i32c(n):
    """i32.const, 数值归一化为有符号 32 位"""
    n &= 0xFFFFFFFF
    if n >= 0x80000000:
        n -= 0x100000000
    return b"\x41" + sleb(n)


def local_get(i):
    return b"\x20" + uleb(i)


def local_set(i):
    return b"\x21" + uleb(i)


ADD, SUB, MUL = b"\x6a", b"\x6b", b"\x6c"
DIV_U, REM_U = b"\x6e", b"\x6f"
AND, OR, XOR = b"\x71", b"\x72", b"\x73"
SHL = b"\x74"
LT_U = b"\x49"
STORE = b"\x36"
END = b"\x0b"
LOOP = b"\x03"
BR_IF = b"\x0d"
CALL = b"\x10"


def i32_store(align_log2=2, offset=0):
    return STORE + uleb(align_log2) + uleb(offset)


# ---------------------------------------------------------------- 各 section
# 1) 类型
types = vec([
    b"\x60" + vec([I32] * 3) + vec([]),      # 0: (i32,i32,i32)->()  host_present
    b"\x60" + vec([I32] * 2) + vec([]),      # 1: (i32,i32)->()      host_log
    b"\x60" + vec([]) + vec([I32]),          # 2: ()->i32            init
    b"\x60" + vec([I32]) + vec([I32]),       # 3: (i32)->i32         render
])

# 2) 导入: env.host_present, env.host_log
imports = vec([
    name("env") + name("host_present") + u8(0x00) + uleb(0),
    name("env") + name("host_log") + u8(0x00) + uleb(1),
])

# 3) 函数: init -> 类型2, render -> 类型3
funcs = vec([uleb(2), uleb(3)])

# 5) 内存: 最小 PAGES 页
memory = vec([b"\x00" + uleb(PAGES)])

# 7) 导出: memory(内存0), init(函数2), render(函数3)
#    函数索引空间: 0,1 是两个导入函数, 定义的函数从 2 开始
exports = vec([
    name("memory") + u8(0x02) + uleb(0),
    name("init") + u8(0x00) + uleb(2),
    name("render") + u8(0x00) + uleb(3),
])


# 10) 代码
def body(local_groups, code):
    """local_groups: [(个数, 值类型)], code: 指令序列 (不含 end)"""
    b = vec([uleb(c) + t for c, t in local_groups]) + code + END
    return uleb(len(b)) + b


# init(): 直接返回显存偏移
init_body = body([], i32c(FB_OFFSET))

# render(frame): 填充显存 -> host_present -> 返回显存偏移
#   局部变量: 0=frame(参数), 1=i, 2=x, 3=y, 4=r, 5=g, 6=b, 7=col
c = b""
c += i32c(0) + local_set(1)                       # i = 0
c += LOOP + b"\x40"                               # loop
c += local_get(1) + i32c(W) + REM_U + local_set(2)    # x = i % W
c += local_get(1) + i32c(W) + DIV_U + local_set(3)    # y = i / W
# r = (x*3 + frame*5) & 255
c += local_get(2) + i32c(3) + MUL + local_get(0) + i32c(5) + MUL + ADD + i32c(255) + AND + local_set(4)
# g = (y*3 + frame*7) & 255
c += local_get(3) + i32c(3) + MUL + local_get(0) + i32c(7) + MUL + ADD + i32c(255) + AND + local_set(5)
# b = (x ^ y ^ frame) & 255
c += local_get(2) + local_get(3) + XOR + local_get(0) + XOR + i32c(255) + AND + local_set(6)
# col = r | (g<<8) | (b<<16) | 0xFF000000
c += local_get(4) + local_get(5) + i32c(8) + SHL + OR + local_get(6) + i32c(16) + SHL + OR + i32c(0xFF000000) + OR + local_set(7)
# [FB_OFFSET + i*4] = col
c += i32c(FB_OFFSET) + local_get(1) + i32c(4) + MUL + ADD + local_get(7) + i32_store()
# i++; if (i < W*H) continue
c += local_get(1) + i32c(1) + ADD + local_set(1)
c += local_get(1) + i32c(W * H) + LT_U + BR_IF + uleb(0)
c += END                                          # end loop
# host_present(FB_OFFSET, W, H)  (导入函数索引 0)
c += i32c(FB_OFFSET) + i32c(W) + i32c(H) + CALL + uleb(0)
# return FB_OFFSET
c += i32c(FB_OFFSET)

render_body = body([(7, I32)], c)
code = vec([init_body, render_body])

# ---------------------------------------------------------------- 组装
module = b"\x00asm" + struct.pack("<I", 1)
module += section(1, types)
module += section(2, imports)
module += section(3, funcs)
module += section(5, memory)
module += section(7, exports)
module += section(10, code)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        here, "..", "app", "src", "main", "assets", "sample.wasm")
    out = os.path.normpath(out)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "wb") as f:
        f.write(module)
    print("written %s (%d bytes)" % (out, len(module)))


if __name__ == "__main__":
    main()
