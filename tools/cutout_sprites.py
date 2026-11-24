#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""flood-fill 抠图：只删与边框相连的白色背景，人物皮肤高光不被误删。

适配 gal1 运行目录。白底源图 -> 透明底 PNG，bbox 裁边、缩放到高 1600、底部渐隐。
参数沿用立绘工作流：WHITE_DIST=60，EDGE_FEATHER=0 硬切（避免半透明白边），FADE=160。

用法：
    python tools/cutout_sprites.py            # 按下面 JOBS 批量处理
    python tools/cutout_sprites.py 图.jpg --as shenzhiji_xxx
"""
import sys
from pathlib import Path

import numpy as np
from PIL import Image
import scipy.ndimage as ndi

ROOT = Path(__file__).resolve().parent.parent
SRC_DIR = Path(r"C:/Users/赵一霖/AppData/Local/Temp/szj_new")
OUT = ROOT / "data" / "sprites"

H, FADE = 1600, 160
WHITE_DIST = 60
EDGE_FEATHER = 0

JOBS = {
    "surprised.jpg": "shenzhiji_surprised",
    "shy.jpg":       "shenzhiji_shy",
    "smile.jpg":     "shenzhiji_smile",
    "cold.jpg":      "shenzhiji_cold",
    "umbrella.jpg":  "shenzhiji_umbrella",
}


def cutout(im: Image.Image) -> Image.Image:
    arr = np.array(im.convert("RGB")).astype(np.float32)
    dist = np.sqrt(((arr - 255.0) ** 2).sum(axis=-1))

    white = (dist < WHITE_DIST).astype(np.uint8)
    labeled, _ = ndi.label(white)

    h, w = dist.shape
    corner_lbls = set()
    for pt in [(0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1),
               (w // 2, 0), (w // 2, h - 1), (0, h // 2), (w - 1, h // 2)]:
        v = labeled[pt[1], pt[0]]
        if v > 0:
            corner_lbls.add(v)

    bg_mask = np.isin(labeled, list(corner_lbls))
    alpha = np.where(bg_mask, 0.0, 255.0).astype(np.float32)

    if EDGE_FEATHER > 0:
        from scipy.ndimage import distance_transform_edt
        fg_dist = distance_transform_edt(~bg_mask)
        feather = np.clip(fg_dist / EDGE_FEATHER, 0, 1)
        alpha = feather * 255.0

    rgba = np.dstack([arr, alpha])
    out_im = Image.fromarray(rgba.astype(np.uint8), "RGBA")

    bbox = out_im.split()[-1].getbbox()
    if bbox:
        out_im = out_im.crop(bbox)

    w, h = out_im.size
    scale = H / h
    out_im = out_im.resize((int(w * scale), H), Image.LANCZOS)

    a = np.array(out_im).astype(np.float32)
    fade = np.ones(H, dtype=np.float32)
    for y in range(H - FADE, H):
        fade[y] = (H - y) / FADE
    a[..., 3] = a[..., 3] * fade[:, None]
    return Image.fromarray(a.astype(np.uint8), "RGBA")


def main(argv):
    OUT.mkdir(parents=True, exist_ok=True)
    jobs = JOBS
    if len(argv) >= 2:
        src = Path(argv[1])
        name = argv[3] if len(argv) >= 4 and argv[2] == "--as" else src.stem
        jobs = {src.name: name}
        src_dir = src.parent
    else:
        src_dir = SRC_DIR

    for src_name, out_name in jobs.items():
        src = src_dir / src_name
        if not src.exists():
            print(f"缺源图：{src}")
            continue
        out_im = cutout(Image.open(src))
        dst = OUT / f"{out_name}.png"
        out_im.save(dst, optimize=True)
        print(f"{dst.name:26} {out_im.size[0]}x{out_im.size[1]}  "
              f"{dst.stat().st_size // 1024} KB")


if __name__ == "__main__":
    main(sys.argv)
