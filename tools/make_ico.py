"""把多尺寸 PNG 打包成 Windows .ico（PNG 压缩格式，Vista 及以上通用）。

用法：
  python make_ico.py <输出.ico> <输入目录>       # 读取目录里的 icon_*.png
  python make_ico.py <输出.ico> a.png b.png ...  # 显式列出文件

图标尺寸取自 PNG 自带的 IHDR，所以先生成好 16/32/48/64/128/256 的 PNG 再喂进来。
"""

import glob
import os
import struct
import sys


def png_size(path):
    with open(path, "rb") as handle:
        head = handle.read(24)
    if head[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path} 不是 PNG")
    return struct.unpack(">II", head[16:24])


def build_ico(out_path, png_paths):
    images = []
    for path in sorted(png_paths, key=lambda p: png_size(p)[0]):
        width, height = png_size(path)
        with open(path, "rb") as handle:
            images.append((width, height, handle.read()))

    if not images:
        raise SystemExit("没有可用的 PNG")

    header = struct.pack("<HHH", 0, 1, len(images))
    directory = b""
    offset = 6 + 16 * len(images)
    for width, height, data in images:
        # 256 及以上在 ICO 目录里用 0 表示
        directory += struct.pack("<BBBBHHII",
                                 width % 256, height % 256, 0, 0,
                                 1, 32, len(data), offset)
        offset += len(data)

    with open(out_path, "wb") as handle:
        handle.write(header)
        handle.write(directory)
        for _, _, data in images:
            handle.write(data)

    print(f"已写出 {out_path}，包含 {len(images)} 个尺寸：" +
          ", ".join(f"{w}x{h}" for w, h, _ in images))


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    out_path = sys.argv[1]
    target = sys.argv[2]
    if os.path.isdir(target):
        paths = glob.glob(os.path.join(target, "icon_*.png"))
    else:
        paths = sys.argv[2:]
    build_ico(out_path, paths)


if __name__ == "__main__":
    main()