#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成女主立绘（800×1200 透明 PNG），输出到 data/sprites/。

风格：扁平插画 + 边缘光，低马尾、深灰卫衣、手里拿着贴索引贴的笔记本。
和背景一样是程序画的，没有版权问题；想换成手绘/AI 立绘，把同名 png 丢进
data/sprites/ 覆盖即可（透明底、半身、底部对齐）。

用法：
    python tools/gen_sprites.py
"""

import math
import random
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

W, H = 800, 1200
OUT = Path(__file__).resolve().parent.parent / "data" / "sprites"

SKIN = (232, 202, 178)
SKIN_SHADE = (208, 176, 152)
HAIR = (28, 31, 40)
HAIR_HI = (58, 64, 78)
HOODIE = (58, 64, 72)
HOODIE_DARK = (44, 49, 57)
HOODIE_HI = (78, 86, 96)
PAPER = (226, 216, 196)
INK = (40, 38, 44)
TAB = [(120, 150, 170), (190, 120, 110), (150, 170, 130)]
RIM_COOL = (150, 180, 210)
RIM_WARM = (226, 186, 118)


def rim_light(layer, color, offset=(4, 0), blur=2, strength=170):
    """沿 alpha 边缘做一道轮廓光。"""
    a = layer.split()[-1]
    edge = a.filter(ImageFilter.MaxFilter(3)).point(lambda v: 255 if v > 40 else 0)
    shift = Image.new("L", layer.size, 0)
    shift.paste(edge, offset)

    ring = (np.array(shift, dtype=np.int16) - np.array(edge, dtype=np.int16))
    ring = Image.fromarray(ring.clip(0, 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(blur))

    out = Image.new("RGBA", layer.size, color + (0,))
    out.putalpha(ring.point(lambda v: min(255, int(v * strength / 255))))
    return Image.alpha_composite(layer, out)


def head(expr="normal", tilt=0.0):
    """头部图层：后发 + 脸 + 五官 + 前发 + 低马尾。"""
    size = (520, 760)
    layer = Image.new("RGBA", size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    cx, cy = 260, 300

    # 后发轮廓
    d.ellipse([cx - 175, cy - 190, cx + 175, cy + 150], fill=HAIR)
    # 低马尾（右侧）
    d.ellipse([cx + 110, cy + 40, cx + 190, cy + 300], fill=HAIR)
    d.ellipse([cx + 120, cy + 210, cx + 178, cy + 330], fill=(22, 25, 32))

    # 脖子
    d.rectangle([cx - 42, cy + 100, cx + 42, cy + 190], fill=SKIN_SHADE)

    # 脸
    d.ellipse([cx - 118, cy - 140, cx + 118, cy + 130], fill=SKIN)
    # 下颌收一点
    d.polygon([(cx - 100, cy + 40), (cx + 100, cy + 40), (cx + 52, cy + 140), (cx - 52, cy + 140)],
              fill=SKIN)

    # 耳朵
    d.ellipse([cx - 132, cy - 30, cx - 104, cy + 30], fill=SKIN_SHADE)
    d.ellipse([cx + 104, cy - 30, cx + 132, cy + 30], fill=SKIN_SHADE)

    # 眼睛（按表情）
    eye_y = cy + 6
    for sx in (-58, 58):
        ex = cx + sx

        if expr == "down":
            d.arc([ex - 24, eye_y - 12, ex + 24, eye_y + 16], 200, 340, fill=INK, width=5)
            d.line([(ex - 22, eye_y + 2), (ex + 22, eye_y + 2)], fill=INK, width=4)
        elif expr in ("soft", "smile", "shy"):
            d.arc([ex - 22, eye_y - 6, ex + 22, eye_y + 20], 190, 350, fill=INK, width=6)
            d.line([(ex - 22, eye_y + 4), (ex + 22, eye_y + 4)], fill=INK, width=3)
        elif expr == "look":
            d.ellipse([ex - 16, eye_y - 12, ex + 16, eye_y + 14], fill=(250, 248, 244))
            d.ellipse([ex - 8, eye_y - 8, ex + 9, eye_y + 11], fill=INK)
            d.ellipse([ex - 3, eye_y - 5, ex + 3, eye_y + 1], fill=(255, 255, 255))
        else:
            d.ellipse([ex - 17, eye_y - 11, ex + 17, eye_y + 13], fill=(250, 248, 244))
            d.ellipse([ex - 9, eye_y - 9, ex + 9, eye_y + 11], fill=INK)
            d.ellipse([ex - 3, eye_y - 6, ex + 3, eye_y], fill=(255, 255, 255))

        # 眉毛：平直偏淡，符合“清冷”
        d.line([(ex - 24, eye_y - 30), (ex + 22, eye_y - 34)], fill=(52, 44, 42), width=5)

    # 鼻、嘴
    d.line([(cx, cy + 34), (cx + 6, cy + 52)], fill=SKIN_SHADE, width=3)
    if expr in ("soft", "smile"):
        d.arc([cx - 16, cy + 62, cx + 16, cy + 90], 20, 160, fill=(150, 92, 92), width=4)
    elif expr == "normal":
        d.line([(cx - 12, cy + 78), (cx + 12, cy + 78)], fill=(150, 92, 92), width=4)
    else:
        d.line([(cx - 8, cy + 78), (cx + 10, cy + 76)], fill=(150, 92, 92), width=4)

    # 腮红（只有软下来的时候有一点）
    if expr == "soft":
        blush = Image.new("RGBA", size, (0, 0, 0, 0))
        bd = ImageDraw.Draw(blush)
        bd.ellipse([cx - 96, cy + 30, cx - 40, cy + 62], fill=(214, 140, 132, 70))
        bd.ellipse([cx + 40, cy + 30, cx + 96, cy + 62], fill=(214, 140, 132, 70))
        layer = Image.alpha_composite(layer, blush.filter(ImageFilter.GaussianBlur(6)))
        d = ImageDraw.Draw(layer)

    # 前发：齐刘海 + 两侧碎发
    d.polygon([(cx - 128, cy - 70), (cx - 110, cy - 150), (cx + 110, cy - 150), (cx + 128, cy - 70),
               (cx + 96, cy - 30), (cx + 60, cy - 70), (cx + 20, cy - 24), (cx - 16, cy - 72),
               (cx - 60, cy - 28), (cx - 96, cy - 66)], fill=HAIR)
    d.polygon([(cx - 132, cy - 120), (cx - 150, cy + 60), (cx - 120, cy + 70), (cx - 104, cy - 60)],
              fill=HAIR)
    d.polygon([(cx + 132, cy - 120), (cx + 150, cy + 60), (cx + 120, cy + 70), (cx + 104, cy - 60)],
              fill=HAIR)

    # 头发高光
    d.arc([cx - 120, cy - 148, cx + 120, cy - 40], 200, 340, fill=HAIR_HI, width=6)

    if tilt:
        layer = layer.rotate(tilt, resample=Image.BICUBIC, center=(cx, cy + 60))
    return layer


def body():
    """半身：卫衣、领口、抽绳、抱着的笔记本。"""
    layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    cx = W // 2

    # 肩线
    d.polygon([(cx - 40, 520), (cx + 40, 520), (cx + 210, 620), (cx + 300, 1200),
               (cx - 300, 1200), (cx - 210, 620)], fill=HOODIE)
    # 肩头圆角
    d.ellipse([cx - 300, 560, cx - 150, 760], fill=HOODIE)
    d.ellipse([cx + 150, 560, cx + 300, 760], fill=HOODIE)

    # 领口
    d.polygon([(cx - 96, 512), (cx + 96, 512), (cx + 70, 600), (cx, 640), (cx - 70, 600)],
              fill=HOODIE_DARK)
    d.line([(cx - 96, 516), (cx - 70, 600)], fill=HOODIE_HI, width=4)
    d.line([(cx + 96, 516), (cx + 70, 600)], fill=HOODIE_HI, width=4)

    # 抽绳
    d.line([(cx - 40, 600), (cx - 48, 760)], fill=(160, 160, 158), width=6)
    d.line([(cx + 34, 604), (cx + 44, 752)], fill=(160, 160, 158), width=6)
    d.ellipse([cx - 54, 754, cx - 40, 772], fill=(180, 180, 176))
    d.ellipse([cx + 38, 746, cx + 52, 764], fill=(180, 180, 176))

    # 抱着的地方志 / 笔记本
    bx0, by0, bx1, by1 = cx - 250, 900, cx + 250, 1200
    d.rounded_rectangle([bx0 + 6, by0 + 10, bx1 - 6, by1 - 30], radius=10, fill=(196, 186, 168))
    d.rounded_rectangle([bx0, by0, bx1, by1 - 44], radius=10, fill=PAPER)
    d.line([(bx0 + 8, by0 + 8), (bx1 - 8, by0 + 8)], fill=(206, 198, 180), width=3)
    # 索引贴
    rnd = random.Random(5)
    for i in range(5):
        tx = bx1 - 26
        ty = by0 + 60 + i * 56
        d.rounded_rectangle([tx, ty, tx + 22, ty + 40], radius=4,
                            fill=TAB[i % len(TAB)] + (235,))
    # 书脊阴影
    d.rounded_rectangle([bx0 + 10, by0, bx1 - 10, by0 + 26], radius=8,
                        fill=(0, 0, 0, 40))

    # 手（简化的两只，压在书上）
    for hx in (cx - 168, cx + 168):
        d.ellipse([hx - 34, 918, hx + 34, 1002], fill=SKIN)
        d.arc([hx - 34, 906, hx + 34, 1010], 200, 340, fill=SKIN_SHADE, width=4)


    # 卫衣褶皱
    for yy in (700, 820, 980):
        d.arc([cx - 250, yy - 30, cx - 40, yy + 60], 210, 330,
              fill=HOODIE_DARK, width=5)
        d.arc([cx + 40, yy - 30, cx + 250, yy + 60], 210, 330,
              fill=HOODIE_DARK, width=5)

    return layer


def compose(expr="normal", tilt=0.0):
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    bd = body()
    hd = head(expr, tilt)

    img = Image.alpha_composite(img, bd)
    img.alpha_composite(hd, (W // 2 - 260, 20))

    # 轮廓光：左冷（窗光）、右暖（桌灯）
    img = rim_light(img, RIM_COOL, offset=(5, 0), blur=3, strength=150)
    img = rim_light(img, RIM_WARM, offset=(-5, 0), blur=4, strength=90)
    return img


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    variants = {
        "shenzhiji_normal": ("normal", 0.0),
        "shenzhiji_soft": ("soft", 0.0),
        "shenzhiji_down": ("down", -4.0),
        "shenzhiji_look": ("look", 3.0),
    }
    for name, (expr, tilt) in variants.items():
        img = compose(expr, tilt)
        path = OUT / f"{name}.png"
        img.save(path, optimize=True)
        print(f"{path.name:26} {img.size[0]}x{img.size[1]}  {path.stat().st_size / 1024:6.1f} KB")


if __name__ == "__main__":
    main()
