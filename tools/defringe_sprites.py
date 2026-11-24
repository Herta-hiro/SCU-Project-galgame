#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""给立绘去白边 / 去亮边（defringe v2）。

AI 出的透明底立绘常见问题：人物轮廓上有一圈浅色描边（白底混色留下的），
缩放到屏幕尺寸、再经过双线性/三线性过滤，就会变成人物周围一圈白雾。

处理步骤：
  1. 半透明像素做“白底反预乘”，把混进去的白去掉；
  2. 轮廓内沿裁掉 1 像素（最外那圈最容易被白边污染）；
  3. 整张图的透明区 RGB 全部填成“轮廓平均色”，这样 mipmap 采样时
     边缘平均到的是人物色，而不是白/黑，缩放后不会有亮边或黑边。

用法：
    python tools/defringe_sprites.py                 # data/sprites/*.png
    python tools/defringe_sprites.py a.png b.png
"""

import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

SPRITE_DIR = Path(__file__).resolve().parent.parent / "data" / "sprites"


def unpremultiply_white(rgb, alpha, min_a=0.05, light=150.0):
    out = rgb.copy()
    lum = 0.299 * rgb[..., 0] + 0.587 * rgb[..., 1] + 0.114 * rgb[..., 2]
    mask = (alpha > min_a) & (alpha < 0.98) & (lum > light)
    if mask.any():
        f = (rgb[mask] - (1.0 - alpha[mask][:, None]) * 255.0) / alpha[mask][:, None]
        out[mask] = np.clip(f, 0, 255)
    return out


def suppress_halo(rgb, alpha, passes=2, ratio=1.35, floor=170.0):
    """沿轮廓把明显偏亮的“描边像素”换成内侧颜色。

    AI 立绘有时会在头发/肩线外画一圈白描边（不是 alpha 问题，是画上去的），
    光反预乘去不掉。这里只在轮廓上、且亮度显著高于内侧邻域时才替换。
    """
    out = rgb.copy()
    op = alpha > 0.6
    if not op.any():
        return out
    ero = np.array(Image.fromarray((op * 255).astype(np.uint8))
                   .filter(ImageFilter.MinFilter(3))) > 0
    ring = op & ~ero

    for _ in range(passes):
        lum = 0.299 * out[..., 0] + 0.587 * out[..., 1] + 0.114 * out[..., 2]
        inner = op & ~ring
        if not inner.any():
            break
        # 内侧邻域均值
        acc = np.zeros_like(out); cnt = np.zeros(op.shape, dtype=np.float32)
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                k = np.roll(np.roll(inner, dy, axis=0), dx, axis=1)
                v = np.roll(np.roll(out, dy, axis=0), dx, axis=1)
                acc += v * k[..., None]; cnt += k.astype(np.float32)
        ref = acc / np.maximum(cnt[..., None], 1)
        ref_lum = 0.299 * ref[..., 0] + 0.587 * ref[..., 1] + 0.114 * ref[..., 2]
        fix = ring & (cnt > 0) & (lum > floor) & (lum > ref_lum * ratio)
        out[fix] = ref[fix]
    return out


def boundary_color(rgb, alpha):
    """不透明轮廓内沿的平均颜色，用来填透明区。"""
    op = alpha > 0.6
    if not op.any():
        return np.array([24.0, 24.0, 24.0], dtype=np.float32)
    ero = np.array(Image.fromarray((op * 255).astype(np.uint8))
                   .filter(ImageFilter.MinFilter(3))) > 0
    ring = op & ~ero
    if not ring.any():
        ring = op
    return rgb[ring].mean(axis=0)


def erode_alpha(alpha, px=1):
    """把 alpha 收缩 px 像素，切掉最外那圈。"""
    if px <= 0:
        return alpha
    a = (alpha * 255).astype(np.uint8)
    for _ in range(px):
        a = np.array(Image.fromarray(a).filter(ImageFilter.MinFilter(3)))
    return a.astype(np.float32) / 255.0


def process(path: Path) -> None:
    im = Image.open(path).convert("RGBA")
    arr = np.array(im).astype(np.float32)
    rgb, alpha = arr[..., :3], arr[..., 3] / 255.0

    rgb = unpremultiply_white(rgb, alpha)
    alpha = erode_alpha(alpha, 1)

    # Replace near-white pixels on the opaque contour with adjacent interior colors.
    op = alpha > 0.6
    eroded = np.array(Image.fromarray((op * 255).astype(np.uint8))
                      .filter(ImageFilter.MinFilter(3))) > 0
    ring = op & ~eroded
    inner = op & ~ring
    acc = np.zeros_like(rgb)
    cnt = np.zeros(op.shape, dtype=np.float32)
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            k = np.roll(np.roll(inner, dy, axis=0), dx, axis=1)
            v = np.roll(np.roll(rgb, dy, axis=0), dx, axis=1)
            acc += v * k[..., None]
            cnt += k.astype(np.float32)
    ref = acc / np.maximum(cnt[..., None], 1)
    near_white = (rgb[..., 0] > 235) & (rgb[..., 1] > 235) & (rgb[..., 2] > 235)
    fix = ring & near_white & (cnt > 0)
    rgb[fix] = ref[fix]

    edge = boundary_color(rgb, alpha)
    trans = alpha <= 0.01
    if trans.any():
        rgb[trans] = edge

    out = np.dstack([np.clip(rgb, 0, 255), np.clip(alpha, 0, 1) * 255.0]).astype(np.uint8)
    Image.fromarray(out, "RGBA").save(path, optimize=True)

    # 报告：内沿亮度/白度
    a2 = np.array(Image.fromarray(out, "RGBA")).astype(float)
    al = a2[..., 3]
    op = al > 200
    ero = np.array(Image.fromarray((op * 255).astype(np.uint8)).filter(ImageFilter.MinFilter(3))) > 0
    ring = op & ~ero
    px = a2[..., :3][ring]
    lum = (0.299 * px[:, 0] + 0.587 * px[:, 1] + 0.114 * px[:, 2])
    white = ((px[:, 0] > 225) & (px[:, 1] > 225) & (px[:, 2] > 225)).mean() * 100
    print(f"{path.name:24} 内沿亮度 {lum.mean():5.1f}  白 {white:4.1f}%  "
          f"{path.stat().st_size / 1024:6.1f} KB")


def main(argv):
    files = [Path(p) for p in argv[1:]] or sorted(SPRITE_DIR.glob("shenzhiji_*.png"))
    for f in files:
        process(f)
    print("完成。原图备份在 _backup/sprites-pre-defringe/")


if __name__ == "__main__":
    main(sys.argv)
