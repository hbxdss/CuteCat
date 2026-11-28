"""PNG 检查工具（纯标准库实现，本机无 Pillow/numpy 时使用）。

用途：
  1. 读取 PNG 头信息（尺寸、颜色类型、位深）。
  2. 解码像素，统计 alpha 通道分布，输出不透明内容的包围盒。
  3. 可选：把带 alpha 的 PNG 裁剪到内容边界并缩放，另存为新 PNG。

用法：
  python png_inspect.py <input.png>
  python png_inspect.py <input.png> --trim <out.png> --size 512
"""

import argparse
import struct
import sys
import zlib


def read_png(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("不是 PNG 文件")
    pos = 8
    width = height = bit_depth = color_type = None
    idat = bytearray()
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        ctype = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if ctype == b"IHDR":
            width, height, bit_depth, color_type = struct.unpack(">IIBB", chunk[:10])
        elif ctype == b"IDAT":
            idat += chunk
        elif ctype == b"IEND":
            break
    if bit_depth != 8 or color_type not in (2, 6):
        raise ValueError(f"仅支持 8 位 RGB/RGBA PNG：bit_depth={bit_depth} color_type={color_type}")
    channels = 4 if color_type == 6 else 3
    raw = zlib.decompress(bytes(idat))
    stride = width * channels
    out = bytearray(width * height * channels)
    prev = bytearray(stride)
    p = 0
    for y in range(height):
        ft = raw[p]
        p += 1
        line = bytearray(raw[p:p + stride])
        p += stride
        if ft == 1:
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 0xFF
        elif ft == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif ft == 3:
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 0xFF
        elif ft == 4:
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                b = prev[i]
                c = prev[i - channels] if i >= channels else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return width, height, channels, out


def alpha_report(path):
    width, height, channels, px = read_png(path)
    print(f"尺寸: {width}x{height}, 通道: {channels}, 颜色类型: {'RGBA' if channels == 4 else 'RGB'}")
    if channels == 3:
        print("无 alpha 通道（RGB 图），背景为实色内容")
        return None
    hist = [0] * 256
    opaque = 0
    for i in range(3, len(px), 4):
        hist[px[i]] += 1
    total = width * height
    transparent = sum(hist[0:8])
    partial = sum(hist[8:248])
    opaque = sum(hist[248:])
    print(f"像素总数: {total}")
    print(f"全透明(a<8): {transparent} ({transparent * 100.0 / total:.2f}%)")
    print(f"半透明: {partial} ({partial * 100.0 / total:.2f}%)")
    print(f"不透明(a>=248): {opaque} ({opaque * 100.0 / total:.2f}%)")
    # 不透明包围盒
    minx, miny, maxx, maxy = width, height, -1, -1
    for y in range(height):
        base = y * width * 4
        for x in range(width):
            if px[base + x * 4 + 3] >= 16:
                if x < minx:
                    minx = x
                if x > maxx:
                    maxx = x
                if y < miny:
                    miny = y
                if y > maxy:
                    maxy = y
    if maxx < 0:
        print("内容包围盒: 空（全透明）")
        return None
    print(f"内容包围盒: x={minx} y={miny} w={maxx - minx + 1} h={maxy - miny + 1}")
    return width, height, px, (minx, miny, maxx, maxy)


def write_png(path, width, height, px):
    raw = bytearray()
    stride = width * 4
    for y in range(height):
        raw.append(0)
        raw += px[y * stride:(y + 1) * stride]

    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload
                + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", ihdr))
        f.write(chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        f.write(chunk(b"IEND", b""))


def trim_and_scale(src, dst, size, pad_ratio=0.02):
    width, height, channels, px = read_png(src)
    if channels != 4:
        raise ValueError("裁剪需要 RGBA 图片")
    minx, miny, maxx, maxy = width, height, -1, -1
    for y in range(height):
        base = y * width * 4
        for x in range(width):
            if px[base + x * 4 + 3] >= 16:
                if x < minx:
                    minx = x
                if x > maxx:
                    maxx = x
                if y < miny:
                    miny = y
                if y > maxy:
                    maxy = y
    cw, ch = maxx - minx + 1, maxy - miny + 1
    side = int(max(cw, ch) * (1 + pad_ratio * 2))
    out = bytearray(side * side * 4)
    ox = (side - cw) // 2
    oy = (side - ch) // 2
    for y in range(ch):
        srow = (miny + y) * width * 4 + minx * 4
        drow = (oy + y) * side * 4 + ox * 4
        out[drow:drow + cw * 4] = px[srow:srow + cw * 4]
    # 等比缩放（双线性）
    scale = size / side
    if abs(scale - 1.0) > 1e-6:
        newpx = bytearray(size * size * 4)
        for y in range(size):
            sy = min(max((y + 0.5) / scale - 0.5, 0), side - 1)
            y0 = int(sy)
            y1 = min(y0 + 1, side - 1)
            fy = sy - y0
            for x in range(size):
                sx = min(max((x + 0.5) / scale - 0.5, 0), side - 1)
                x0 = int(sx)
                x1 = min(x0 + 1, side - 1)
                fx = sx - x0
                for c in range(4):
                    p00 = out[(y0 * side + x0) * 4 + c]
                    p10 = out[(y0 * side + x1) * 4 + c]
                    p01 = out[(y1 * side + x0) * 4 + c]
                    p11 = out[(y1 * side + x1) * 4 + c]
                    top = p00 + (p10 - p00) * fx
                    bottom = p01 + (p11 - p01) * fx
                    newpx[(y * size + x) * 4 + c] = int(round(top + (bottom - top) * fy))
        out = newpx
        side = size
    write_png(dst, side, side, out)
    print(f"已写出: {dst} ({side}x{side})，原内容 {cw}x{ch}，裁剪自 ({minx},{miny})")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("input")
    ap.add_argument("--trim")
    ap.add_argument("--size", type=int, default=512)
    args = ap.parse_args()
    info = alpha_report(args.input)
    if args.trim:
        trim_and_scale(args.input, args.trim, args.size)


if __name__ == "__main__":
    sys.exit(main())