#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""导入现成立绘：裁成半身、缩放、底部做透明渐隐，输出到 data/sprites/。

用法：
    python tools/import_sprite.py 原图.png --as shenzhiji_normal
    python tools/import_sprite.py 原图.png --as codel_03 --crop 0.58 --height 1600

参数：
    --as      输出名（不带扩展名），生成 data/sprites/<名字>.png
    --crop    保留高度比例，从头往下裁，默认 0.58（立绘是全身图时裁成半身）
    --height  输出高度，默认 1600
    --fade    底部渐隐高度（像素），默认 140，避免半身被切断的硬边
"""

import argparse
from pathlib import Path

from PIL import Image

OUT = Path(__file__).resolve().parent.parent / "data" / "sprites"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("--as", dest="name", required=True)
    ap.add_argument("--crop", type=float, default=0.58)
    ap.add_argument("--height", type=int, default=1600)
    ap.add_argument("--fade", type=int, default=140)
    args = ap.parse_args()

    src = Path(args.src)
    img = Image.open(src).convert("RGBA")

    # 按内容 bbox 裁掉四周空白，保证不同表情对齐
    bbox = img.split()[-1].getbbox()
    if bbox:
        img = img.crop(bbox)

    if 0 < args.crop < 1:
        w, h = img.size
        img = img.crop((0, 0, w, int(h * args.crop)))

    w, h = img.size
    scale = args.height / h
    img = img.resize((max(1, int(w * scale)), args.height), Image.LANCZOS)

    # 底部渐隐
    if args.fade > 0:
        alpha = img.split()[-1]
        fade = Image.new("L", img.size, 255)
        px = fade.load()
        for y in range(img.size[1] - args.fade, img.size[1]):
            k = (img.size[1] - y) / args.fade
            for x in range(img.size[0]):
                px[x, y] = int(255 * k)
        from PIL import ImageChops
        img.putalpha(ImageChops.multiply(alpha, fade))

    OUT.mkdir(parents=True, exist_ok=True)
    dst = OUT / f"{args.name}.png"
    img.save(dst, optimize=True)
    print(f"{dst.name}  {img.size[0]}x{img.size[1]}  {dst.stat().st_size / 1024:.1f} KB")


if __name__ == "__main__":
    main()