"""把 RGBA 图片合成到指定底色上，用于检查抠图边缘质量。

用法：python compose_preview.py <in.png> <out.png> [r] [g] [b]
"""

import sys

sys.path.insert(0, __file__.rsplit("\\", 1)[0])
from png_inspect import read_png, write_png


def main():
    src, dst = sys.argv[1], sys.argv[2]
    bg = tuple(int(v) for v in (sys.argv[3:6] or ["58", "58", "62"]))
    width, height, channels, px = read_png(src)
    if channels != 4:
        raise SystemExit("需要 RGBA 图片")
    out = bytearray(width * height * 4)
    for i in range(0, len(px), 4):
        a = px[i + 3] / 255.0
        for c in range(3):
            out[i + c] = int(round(px[i + c] * a + bg[c] * (1 - a)))
        out[i + 3] = 255
    write_png(dst, width, height, out)
    print("已写出:", dst)


if __name__ == "__main__":
    main()