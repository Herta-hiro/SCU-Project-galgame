#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成背景图（1920×1080 PNG），输出到 data/backgrounds/。

风格：秋雨、黄昏、暖光台灯、书架剪影、玻璃雨痕——和游戏的深色 UI 同一套配色。
全部程序生成，没有版权问题。想换成实拍照片：把同名 png/jpg 放进 data/backgrounds/
覆盖即可，引擎优先读 png，找不到再读 jpg，都没有就退回原来的渐变底。

用法：
    python tools/gen_backgrounds.py
"""

import random
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

W, H = 1920, 1080
OUT = Path(__file__).resolve().parent.parent / "data" / "backgrounds"

NIGHT_TOP = (14, 18, 26)
NIGHT_BOT = (28, 34, 44)
LAMP = (232, 196, 132)
WINDOW_SKY = (96, 114, 138)
RAIN = (206, 220, 236)


def vgrad(size, top, bot):
    w, h = size
    t = np.linspace(0.0, 1.0, h).reshape(h, 1, 1)
    arr = np.array(top, dtype=np.float32) * (1 - t) + np.array(bot, dtype=np.float32) * t
    arr = np.repeat(arr, w, axis=1)
    return Image.fromarray(arr.astype(np.uint8), "RGB")


def radial(size, cx, cy, rx, ry, color, strength=1.0):
    w, h = size
    yy, xx = np.mgrid[0:h, 0:w]
    d = np.sqrt(((xx - cx) / rx) ** 2 + ((yy - cy) / ry) ** 2)
    a = np.clip(1.0 - d, 0.0, 1.0) ** 2 * 255 * strength
    layer = np.zeros((h, w, 4), dtype=np.uint8)
    layer[..., 0] = color[0]
    layer[..., 1] = color[1]
    layer[..., 2] = color[2]
    layer[..., 3] = a.astype(np.uint8)
    return Image.fromarray(layer, "RGBA")


def glow(img, cx, cy, rx, ry, color, strength=1.0):
    return Image.alpha_composite(
        img.convert("RGBA"), radial(img.size, cx, cy, rx, ry, color, strength)).convert("RGB")


def grain(img, amount=5):
    arr = np.array(img.convert("RGB")).astype(np.int16)
    noise = np.random.normal(0, amount, arr.shape[:2]).astype(np.int16)[..., None]
    return Image.fromarray(np.clip(arr + noise, 0, 255).astype(np.uint8), "RGB")


def vignette(img, strength=0.55):
    w, h = img.size
    yy, xx = np.mgrid[0:h, 0:w]
    d = np.sqrt(((xx - w / 2) / (w / 2)) ** 2 + ((yy - h / 2) / (h / 2)) ** 2)
    k = np.clip((d - 0.55) / 1.05, 0, 1) ** 1.6 * 255 * strength
    dark = np.zeros((h, w, 4), dtype=np.uint8)
    dark[..., 3] = k.astype(np.uint8)
    return Image.alpha_composite(img.convert("RGBA"), Image.fromarray(dark, "RGBA")).convert("RGB")


def rain_streaks(img, count=260, seed=1, slant=-0.35, alpha=(20, 70), length=(40, 190)):
    rnd = random.Random(seed)
    layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    for _ in range(count):
        x = rnd.randrange(-100, W + 100)
        y = rnd.randrange(-100, H)
        ln = rnd.randint(*length)
        a = rnd.randint(*alpha)
        d.line([(x, y), (x + int(ln * slant), y + ln)], fill=RAIN + (a,),
               width=1 if rnd.random() < 0.8 else 2)
    return Image.alpha_composite(img.convert("RGBA"), layer).convert("RGB")


def bokeh(img, points, blur=8):
    layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    for (x, y, r, c, a) in points:
        d.ellipse([x - r, y - r, x + r, y + r], fill=c + (a,))
    return Image.alpha_composite(img.convert("RGBA"),
                                 layer.filter(ImageFilter.GaussianBlur(blur))).convert("RGB")


def shelves(img, x0, x1, y0, y1, rows=5, seed=3, dark=0.55):
    """书架剪影：隔板 + 书脊，整体压暗，只留轮廓和一点暖色书脊。"""
    rnd = random.Random(seed)
    layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    d.rectangle([x0, y0, x1, y1], fill=(12, 15, 20, int(255 * dark)))

    row_h = (y1 - y0) / rows
    for r in range(rows):
        ty = y0 + r * row_h
        d.line([(x0, ty), (x1, ty)], fill=(8, 10, 14, 255), width=6)
        bx = x0 + 8
        while bx < x1 - 8:
            bw = rnd.randint(10, 26)
            bh = row_h * rnd.uniform(0.55, 0.86)
            c = rnd.randint(26, 66)
            tint = (c + 14, c + 4, c - 8) if rnd.random() < 0.45 else (c, c - 4, c - 10)
            d.rectangle([bx, ty + row_h - bh, bx + bw, ty + row_h - 5], fill=tint + (255,))
            bx += bw + rnd.randint(2, 6)
    return Image.alpha_composite(img.convert("RGBA"), layer).convert("RGB")


def window(img, box, mullions=(3, 2), sky=WINDOW_SKY, seed=7):
    """一扇窗：冷色天空 + 窗外树影 + 玻璃雨痕 + 窗框。"""
    x0, y0, x1, y1 = box
    w, h = x1 - x0, y1 - y0
    rnd = random.Random(seed)

    glass = vgrad((w, h), sky, (30, 38, 50))
    gl = ImageDraw.Draw(glass)
    for _ in range(26):
        bx = rnd.randrange(-20, w + 20)
        by = rnd.randrange(int(h * 0.35), h)
        gl.line([(bx, by), (bx + rnd.randint(-40, 40), by - rnd.randint(60, 220))],
                fill=(20, 26, 30), width=rnd.randint(4, 12))
    glass = glass.filter(ImageFilter.GaussianBlur(7))

    layer = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    for _ in range(150):
        rx = rnd.randrange(0, w)
        ry = rnd.randrange(0, h)
        ln = rnd.randint(20, 90)
        d.line([(rx, ry), (rx - int(ln * 0.25), ry + ln)],
               fill=RAIN + (rnd.randint(18, 48),), width=1)
    glass = Image.alpha_composite(glass.convert("RGBA"), layer).convert("RGB")

    img.paste(glass, (x0, y0))

    d = ImageDraw.Draw(img)
    frame = (34, 40, 48)
    d.rectangle([x0 - 14, y0 - 14, x1 + 14, y1 + 14], outline=frame, width=14)
    mx, my = mullions
    for i in range(1, mx):
        px = x0 + w * i / mx
        d.rectangle([px - 5, y0, px + 5, y1], fill=frame)
    for j in range(1, my):
        py = y0 + h * j / my
        d.rectangle([x0, py - 5, x1, py + 5], fill=frame)

    return glow(img, x0 + w / 2, y0 + h / 2, w * 0.8, h * 0.85, (150, 180, 210), 0.16)


def table(img, y, color=(40, 30, 22), height=150):
    layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    d.rectangle([0, y, W, y + height], fill=color + (255,))
    d.line([(0, y), (W, y)], fill=(126, 96, 62, 130), width=3)
    return Image.alpha_composite(img.convert("RGBA"), layer).convert("RGB")


def base_scene():
    img = vgrad((W, H), NIGHT_TOP, NIGHT_BOT)
    return img


# ---------------- 场景 ----------------

def bg_lib_window():
    """省图三楼靠窗长桌，雨天下午。"""
    img = base_scene()
    img = window(img, (150, 90, 980, 700), mullions=(3, 2), seed=11)
    img = shelves(img, 1080, 1880, 40, 760, rows=5, seed=5)

    img = table(img, 760, height=320)
    img = glow(img, 1330, 748, 380, 280, LAMP, 0.55)
    img = glow(img, 1330, 700, 120, 90, (255, 224, 168), 0.75)
    img = glow(img, 560, 920, 540, 220, (120, 150, 180), 0.20)

    img = rain_streaks(img, count=90, seed=21, alpha=(8, 26), length=(60, 240))
    return grain(vignette(img, 0.6), 5)


def bg_rain_window():
    """雨窗特写：玻璃水痕、窗外街灯化成光斑。"""
    rnd = random.Random(12)
    img = vgrad((W, H), (24, 30, 40), (12, 16, 22))
    img = bokeh(img, [(rnd.randrange(0, W), rnd.randrange(120, 620), rnd.randint(24, 70),
                       rnd.choice([LAMP, (150, 180, 210), (120, 150, 170)]), rnd.randint(50, 120))
                      for _ in range(22)], blur=18)
    img = rain_streaks(img, count=520, seed=33, alpha=(14, 60), length=(90, 300))

    layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    for _ in range(180):
        x, y = rnd.randrange(W), rnd.randrange(H)
        r = rnd.randint(2, 7)
        d.ellipse([x - r, y - r * 1.2, x + r, y + r * 1.2],
                  fill=(220, 232, 244, rnd.randint(24, 70)))
    img = Image.alpha_composite(img.convert("RGBA"),
                                layer.filter(ImageFilter.GaussianBlur(1.2))).convert("RGB")
    return grain(vignette(img, 0.55), 4)


def bg_lib_shelf():
    """书架走廊：两侧书架向纵深收，尽头一盏暖灯。"""
    img = vgrad((W, H), (16, 20, 27), (8, 10, 15))

    def wall(x0, x1, top, bot, shade, seed):
        layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
        d = ImageDraw.Draw(layer)
        d.polygon([(x0, top), (x1, top + 40), (x1, bot - 30), (x0, bot)],
                  fill=(shade, shade - 3, shade - 8, 255))
        rnd = random.Random(seed)
        row_h = (bot - top) / 7
        for row in range(7):
            ty = top + row_h * row
            d.line([(x0, ty), (x1, ty + 20)], fill=(6, 8, 12, 255), width=5)
            bx = x0 + 10
            while bx < x1 - 10:
                bw = rnd.randint(8, 22)
                bh = rnd.uniform(0.5, 0.85) * row_h
                c = rnd.randint(26, 74)
                d.rectangle([bx, ty + row_h - bh, bx + bw, ty + row_h - 3],
                            fill=(c + 10, c, c - 10, 255))
                bx += bw + rnd.randint(2, 5)
        return Image.alpha_composite(img.convert("RGBA"), layer).convert("RGB")

    img = wall(-260, 620, 40, 1000, 30, 1)
    img = wall(1300, 2180, 80, 1040, 24, 2)

    img = glow(img, 960, 470, 320, 320, LAMP, 0.50)
    img = glow(img, 960, 470, 90, 90, (255, 230, 180), 0.80)
    img = glow(img, 960, 1000, 640, 150, (120, 92, 60), 0.25)

    return grain(vignette(img, 0.7), 5)


def bg_lib_night():
    """闭馆：灯一排排灭掉，只剩窗边一点月光。"""
    img = vgrad((W, H), (10, 13, 19), (16, 20, 28))
    img = window(img, (120, 80, 760, 640), mullions=(2, 2), sky=(44, 56, 74), seed=41)
    img = shelves(img, 900, 1900, 20, 820, rows=5, seed=9, dark=0.7)
    img = table(img, 700, color=(26, 22, 18), height=380)

    img = glow(img, 1700, 140, 160, 120, (120, 40, 36), 0.35)   # 应急灯
    img = glow(img, 420, 700, 460, 260, (130, 160, 190), 0.10)  # 月光

    img = rain_streaks(img, count=70, seed=55, alpha=(6, 20))
    return grain(vignette(img, 0.78), 5)


def bg_street_night():
    """闭馆后的台阶和路灯，湿地面反光。"""
    img = vgrad((W, H), (12, 16, 24), (6, 8, 12))

    layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    rnd = random.Random(77)
    x = -50
    while x < W + 50:
        bw = rnd.randint(120, 300)
        bh = rnd.randint(180, 420)
        d.rectangle([x, H * 0.42 - bh, x + bw, H * 0.62], fill=(14, 18, 26, 255))
        for wy in range(int(H * 0.42 - bh) + 40, int(H * 0.62), 70):
            if rnd.random() < 0.35:
                d.rectangle([x + 20, wy, x + 46, wy + 30], fill=(226, 186, 118, 90))
        x += bw + rnd.randint(30, 90)
    img = Image.alpha_composite(img.convert("RGBA"),
                                layer.filter(ImageFilter.GaussianBlur(9))).convert("RGB")

    img = table(img, int(H * 0.62), color=(12, 15, 21), height=int(H * 0.38))

    for lx in (520, 1380):
        img = glow(img, lx, 420, 260, 300, LAMP, 0.42)
        img = glow(img, lx, 420, 42, 42, (255, 228, 176), 0.85)

        layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
        d = ImageDraw.Draw(layer)
        d.rectangle([lx - 4, 420, lx + 4, H * 0.62], fill=(10, 12, 18, 255))
        d.polygon([(lx - 26, H * 0.62), (lx + 26, H * 0.62), (lx + 70, H), (lx - 70, H)],
                  fill=(150, 120, 70, 26))
        img = Image.alpha_composite(img.convert("RGBA"), layer).convert("RGB")

    img = rain_streaks(img, count=420, seed=66, alpha=(12, 46), length=(60, 220))
    return grain(vignette(img, 0.65), 5)


def bg_title():
    """标题：书架 + 雨窗叠在一起，大幅模糊。"""
    rnd = random.Random(4242)
    img = base_scene()
    img = shelves(img, -100, 900, -60, 900, rows=6, seed=13, dark=0.5)
    img = window(img, (1050, 60, 1820, 720), mullions=(2, 2), seed=17)
    img = img.filter(ImageFilter.GaussianBlur(9))
    img = table(img, 800, color=(30, 24, 18), height=280)
    img = bokeh(img, [(rnd.randrange(0, W), rnd.randrange(500, 900), rnd.randint(30, 80),
                       LAMP, rnd.randint(40, 90)) for _ in range(14)], blur=22)
    img = rain_streaks(img, count=120, seed=88, alpha=(6, 18))
    return grain(vignette(img, 0.62), 4)


def lift(img, gain=1.32, bias=8.0):
    """整体提亮一点。游戏里还会盖一层压暗蒙版，底图太暗会糊成一片。"""
    arr = np.array(img.convert("RGB")).astype(np.float32)
    arr = np.clip(arr * gain + bias, 0, 255)
    return Image.fromarray(arr.astype(np.uint8), "RGB")


SCENES = {
    "bg_title": bg_title,
    "bg_lib_window": bg_lib_window,
    "bg_lib_shelf": bg_lib_shelf,
    "bg_lib_night": bg_lib_night,
    "bg_rain_window": bg_rain_window,
    "bg_street_night": bg_street_night,
}


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for name, fn in SCENES.items():
        img = lift(fn().convert("RGB")).convert("RGB")
        path = OUT / f"{name}.png"
        img.save(path, optimize=True)
        arr = np.array(img)
        print(f"{path.name:22} {img.size[0]}x{img.size[1]}  "
              f"mean={arr.mean():5.1f}  max={arr.max():3d}  "
              f"{path.stat().st_size / 1024:6.1f} KB")


if __name__ == "__main__":
    main()