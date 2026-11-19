#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""单独补一张早期页面用的图书馆背景：data/backgrounds/bg_lib_window.png

只写这一个文件，不动 data/backgrounds/ 里其他图（那些是 AI 生成的成品）。

场景：四川省图书馆三楼社科区，秋雨天下午。
左侧整面窗（冷光 + 雨痕），右后方书架，前景一张长桌（台灯、书堆、笔记本、杯子），
地面有一点反光。构图里右侧会给立绘留位置，避免人被桌子/灯挡住。

用法：
    python tools/gen_lib_window.py
"""

import random
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

S = 2                      # 先按 2 倍画，最后缩一半，边缘更干净
W, H = 1920 * S, 1080 * S
OUT = Path(__file__).resolve().parent.parent / "data" / "backgrounds" / "bg_lib_window.png"

# 冷灰蓝 + 一点木头暖色，和 AI 生成那几张的色调对齐（低饱和、暗部偏蓝）
C_WALL_TOP = (58, 66, 74)
C_WALL_BOT = (26, 31, 37)
C_WINDOW_TOP = (128, 146, 160)
C_WINDOW_BOT = (54, 68, 80)
C_FRAME = (34, 38, 44)
C_SHELF = (30, 33, 37)
C_WOOD = (58, 46, 34)
C_LAMP = (216, 180, 132)


def vgrad(size, top, bot):
    w, h = size
    t = np.linspace(0.0, 1.0, h).reshape(h, 1, 1)
    arr = np.array(top, np.float32) * (1 - t) + np.array(bot, np.float32) * t
    return Image.fromarray(np.repeat(arr, w, axis=1).astype(np.uint8), "RGB").convert("RGBA")


def glow(size, cx, cy, rx, ry, color, strength):
    w, h = size
    yy, xx = np.mgrid[0:h, 0:w]
    d = np.sqrt(((xx - cx) / rx) ** 2 + ((yy - cy) / ry) ** 2)
    a = np.clip(1 - d, 0, 1) ** 2 * 255 * strength
    layer = np.zeros((h, w, 4), np.uint8)
    layer[..., 0], layer[..., 1], layer[..., 2] = color
    layer[..., 3] = a
    return Image.fromarray(layer, "RGBA")


def add(img, layer):
    return Image.alpha_composite(img, layer)


def soft_mask(size, pts, blur):
    m = Image.new("L", size, 0)
    ImageDraw.Draw(m).polygon(pts, fill=255)
    return m.filter(ImageFilter.GaussianBlur(blur))


def window_scene(img, rng):
    """左侧整面窗：外面是雨天的树影和楼，玻璃上有雨痕。"""
    x0, y0, x1, y1 = int(0.04 * W), int(0.05 * H), int(0.60 * W), int(0.62 * H)

    glass = vgrad((x1 - x0, y1 - y0), C_WINDOW_TOP, C_WINDOW_BOT)
    gd = ImageDraw.Draw(glass)

    # 窗外：远处楼影
    for _ in range(6):
        bx = rng.randint(-40, x1 - x0)
        bw = rng.randint(int(0.08 * (x1 - x0)), int(0.22 * (x1 - x0)))
        bh = rng.randint(int(0.15 * (y1 - y0)), int(0.35 * (y1 - y0)))
        gd.rectangle([bx, (y1 - y0) - bh, bx + bw, (y1 - y0)], fill=(58, 70, 80))

    # 窗外：树影（模糊成一团）
    trees = Image.new("RGBA", glass.size, (0, 0, 0, 0))
    td = ImageDraw.Draw(trees)
    for _ in range(30):
        cx = rng.randint(-30, x1 - x0)
        cy = rng.randint(int(0.45 * (y1 - y0)), (y1 - y0))
        r = rng.randint(30, 90)
        td.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(38, 48, 52, 190))
    glass = Image.alpha_composite(glass, trees.filter(ImageFilter.GaussianBlur(18 * S)))

    # 雨痕
    rain = Image.new("RGBA", glass.size, (0, 0, 0, 0))
    rd = ImageDraw.Draw(rain)
    for _ in range(420):
        rx, ry = rng.randrange(0, glass.size[0]), rng.randrange(0, glass.size[1])
        ln = rng.randint(40 * S, 220 * S)
        rd.line([(rx, ry), (rx - int(ln * 0.22), ry + ln)],
                fill=(200, 214, 226, rng.randint(14, 46)), width=rng.choice([1, 1, 2]))
    glass = Image.alpha_composite(glass, rain.filter(ImageFilter.GaussianBlur(0.6 * S)))

    # 玻璃反光：一道斜向的高光
    sheen = Image.new("RGBA", glass.size, (0, 0, 0, 0))
    ImageDraw.Draw(sheen).polygon(
        [(0, int(0.62 * glass.size[1])), (int(0.55 * glass.size[0]), 0),
         (int(0.78 * glass.size[0]), 0), (int(0.2 * glass.size[0]), glass.size[1])],
        fill=(190, 210, 225, 18))
    glass = Image.alpha_composite(glass, sheen.filter(ImageFilter.GaussianBlur(24 * S)))

    img.paste(glass, (x0, y0))

    # 窗框
    d = ImageDraw.Draw(img)
    fw = int(9 * S)
    d.rectangle([x0 - fw, y0 - fw, x1 + fw, y1 + fw], outline=C_FRAME + (255,), width=fw)
    for i in range(1, 3):
        px = x0 + (x1 - x0) * i / 3
        d.rectangle([px - fw // 2, y0, px + fw // 2, y1], fill=C_FRAME + (255,))
    py = y0 + (y1 - y0) * 0.52
    d.rectangle([x0, py - fw // 2, x1, py + fw // 2], fill=C_FRAME + (255,))

    return img


def light_shaft(img):
    """窗光斜打进室内，靠右下变淡。"""
    shaft = Image.new("RGBA", img.size, (0, 0, 0, 0))
    ImageDraw.Draw(shaft).polygon(
        [(int(0.06 * W), int(0.58 * H)), (int(0.58 * W), int(0.18 * H)),
         (int(1.05 * W), int(0.78 * H)), (int(0.52 * W), int(1.0 * H))],
        fill=(176, 196, 214, 16))
    return add(img, shaft.filter(ImageFilter.GaussianBlur(60 * S)))


def shelves(img, rng):
    """右后方书架：压暗、稍微失焦，看得出是书脊就行。"""
    x0, x1 = int(0.63 * W), int(1.02 * W)
    y0, y1 = int(0.06 * H), int(0.66 * H)

    layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    d.rectangle([x0, y0, x1, y1], fill=C_SHELF + (255,))

    rows = 5
    rh = (y1 - y0) / rows
    for r in range(rows):
        ty = y0 + r * rh
        d.line([(x0, ty), (x1, ty)], fill=(16, 18, 22, 255), width=int(7 * S))
        bx = x0 + int(6 * S)
        while bx < x1:
            bw = rng.randint(int(9 * S), int(24 * S))
            bh = rh * rng.uniform(0.55, 0.84)
            c = rng.randint(26, 62)
            tint = (c + 6, c, c - 6) if rng.random() < 0.4 else (c, c, c - 4)
            d.rectangle([bx, ty + rh - bh, bx + bw, ty + rh - 6 * S], fill=tint + (255,))
            bx += bw + rng.randint(2 * S, 5 * S)

    return add(img, layer.filter(ImageFilter.GaussianBlur(2.2 * S)))


def table_scene(img, rng):
    """前景长桌：桌沿、木纹、台灯、书堆、笔记本、杯子。"""
    top = int(0.63 * H)

    table = Image.new("RGBA", img.size, (0, 0, 0, 0))
    td = ImageDraw.Draw(table)
    td.polygon([(0, top), (W, top - int(0.05 * H)), (W, H), (0, H)], fill=C_WOOD + (255,))

    # 木纹
    for _ in range(70):
        y = rng.randint(top, H)
        td.line([(0, y), (W, y - rng.randint(-30, 30))],
                fill=(70, 56, 42, rng.randint(40, 110)), width=rng.choice([1, 2]))
    table = table.filter(ImageFilter.GaussianBlur(1.2 * S))
    img = add(img, table)

    # 桌沿高光
    edge = Image.new("RGBA", img.size, (0, 0, 0, 0))
    ImageDraw.Draw(edge).line([(0, top), (W, top - int(0.05 * H))],
                              fill=(150, 128, 96, 70), width=int(3 * S))
    img = add(img, edge.filter(ImageFilter.GaussianBlur(2 * S)))

    # 台灯（右中，暖光）
    lx, ly = int(0.80 * W), int(0.52 * H)
    img = add(img, glow(img.size, lx, ly + int(0.04 * H), int(0.30 * W), int(0.24 * H), C_LAMP, 0.35))
    img = add(img, glow(img.size, lx, ly, int(0.035 * W), int(0.05 * H), (255, 232, 186), 0.75))

    lamp = Image.new("RGBA", img.size, (0, 0, 0, 0))
    ld = ImageDraw.Draw(lamp)
    ld.polygon([(lx - int(0.045 * W), ly), (lx + int(0.045 * W), ly),
                (lx + int(0.030 * W), ly - int(0.075 * H)),
                (lx - int(0.030 * W), ly - int(0.075 * H))], fill=(28, 30, 34, 255))
    ld.rectangle([lx - int(0.004 * W), ly, lx + int(0.004 * W), ly + int(0.10 * H)],
                 fill=(24, 26, 30, 255))
    ld.ellipse([lx - int(0.035 * W), ly + int(0.095 * H),
                lx + int(0.035 * W), ly + int(0.115 * H)], fill=(24, 26, 30, 255))
    img = add(img, lamp)

    # 书堆（左下，靠近桌子中心偏左）
    bx, by = int(0.30 * W), int(0.66 * H)
    books = Image.new("RGBA", img.size, (0, 0, 0, 0))
    bd = ImageDraw.Draw(books)
    for i in range(5):
        w_ = int((0.10 + rng.random() * 0.03) * W)
        h_ = int(0.016 * H)
        off = int(i * 0.004 * H)
        shade = rng.randint(46, 78)
        bd.rounded_rectangle([bx, by - off, bx + w_, by - off + h_], radius=int(3 * S),
                             fill=(shade, shade - 8, shade - 16, 255))
        bd.line([(bx + 3 * S, by - off + h_ - 2 * S), (bx + w_ - 3 * S, by - off + h_ - 2 * S)],
                fill=(210, 200, 178, 60), width=int(1 * S))
    # 笔记本
    bd.rounded_rectangle([bx + int(0.11 * W), by - int(0.012 * H),
                          bx + int(0.21 * W), by + int(0.03 * H)],
                         radius=int(4 * S), fill=(206, 198, 182, 255))
    bd.line([(bx + int(0.165 * W), by - int(0.012 * H)),
             (bx + int(0.165 * W), by + int(0.03 * H))],
            fill=(150, 142, 128, 160), width=int(2 * S))
    # 杯子
    cx, cy = int(0.55 * W), int(0.70 * H)
    bd.rounded_rectangle([cx, cy, cx + int(0.032 * W), cy + int(0.06 * H)],
                         radius=int(6 * S), fill=(196, 190, 178, 255))
    bd.ellipse([cx, cy - int(0.006 * H), cx + int(0.032 * W), cy + int(0.010 * H)],
               fill=(60, 62, 64, 255))
    img = add(img, books)

    # 前景椅背（左下角，虚焦）
    chair = Image.new("RGBA", img.size, (0, 0, 0, 0))
    cd = ImageDraw.Draw(chair)
    cd.rounded_rectangle([-int(0.06 * W), int(0.72 * H),
                          int(0.22 * W), H + int(0.05 * H)],
                         radius=int(30 * S), fill=(20, 22, 26, 255))
    img = add(img, chair.filter(ImageFilter.GaussianBlur(10 * S)))

    return img


def grade(img):
    """压一点对比、暗部偏蓝、整体降饱和——贴近 AI 那几张的色调。"""
    arr = np.array(img.convert("RGB")).astype(np.float32) / 255.0

    # 降饱和
    grey = arr.mean(axis=2, keepdims=True)
    arr = grey + (arr - grey) * 0.88

    # S 曲线：暗部压一点、高光提一点
    arr = np.clip(arr, 0, 1)
    arr = arr * arr * (3 - 2 * arr) * 0.35 + arr * 0.65

    # 暗部加蓝、亮部加一点暖
    shadow = (1 - arr.mean(axis=2)) ** 2
    arr[..., 2] += shadow * 0.05
    arr[..., 0] += (1 - shadow) * 0.02

    arr = np.clip(arr * 255, 0, 255).astype(np.uint8)
    out = Image.fromarray(arr, "RGB")

    # 暗角 + 颗粒
    w, h = out.size
    yy, xx = np.mgrid[0:h, 0:w]
    d = np.sqrt(((xx - w / 2) / (w / 2)) ** 2 + ((yy - h / 2) / (h / 2)) ** 2)
    vig = np.clip((d - 0.6) / 1.0, 0, 1) ** 1.5 * 90
    vimg = np.zeros((h, w, 4), np.uint8)
    vimg[..., 3] = vig.astype(np.uint8)
    out = Image.alpha_composite(out.convert("RGBA"), Image.fromarray(vimg, "RGBA")).convert("RGB")

    a = np.array(out).astype(np.int16)
    a = np.clip(a + np.random.normal(0, 4, a.shape[:2]).astype(np.int16)[..., None], 0, 255)
    return Image.fromarray(a.astype(np.uint8), "RGB")


def main():
    rng = random.Random(20261109)
    img = vgrad((W, H), C_WALL_TOP, C_WALL_BOT)

    img = window_scene(img, rng)
    img = light_shaft(img)
    img = shelves(img, rng)
    img = table_scene(img, rng)

    # 窗边的浮尘
    dust = Image.new("RGBA", img.size, (0, 0, 0, 0))
    dd = ImageDraw.Draw(dust)
    for _ in range(140):
        x, y = rng.randrange(0, int(0.75 * W)), rng.randrange(0, int(0.7 * H))
        r = rng.randint(1, 3) * S
        dd.ellipse([x - r, y - r, x + r, y + r], fill=(230, 236, 242, rng.randint(10, 34)))
    img = add(img, dust.filter(ImageFilter.GaussianBlur(1.5 * S)))

    img = grade(img)
    img = img.resize((1920, 1080), Image.LANCZOS)

    OUT.parent.mkdir(parents=True, exist_ok=True)
    img.save(OUT, optimize=True)
    a = np.array(img).astype(float)
    print(f"{OUT.name}  {img.size[0]}x{img.size[1]}  mean={a.mean():.1f}  "
          f"std={a.std():.1f}  {OUT.stat().st_size / 1024:.1f} KB")


if __name__ == "__main__":
    main()