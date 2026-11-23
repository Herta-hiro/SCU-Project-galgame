#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成音频素材：雨声环境音 + 两个界面音效。

用法（在 gal 目录下）：
    python tools/gen_audio.py

产出 data/audio/：
    bgm_rain.wav   60 秒无缝循环的雨声（省图三楼窗外在下雨）
    se_click.wav   推进对话的轻响
    se_page.wav    翻页 / 开菜单

只依赖 numpy。雨声是滤波后的噪声，不是采样素材，所以可以随便重跑、改参数。
"""

import math
import os
import wave

import numpy as np

SR = 22050                 # 采样率；雨声不需要高频，22k 足够，文件也小一半
OUT_DIR = os.path.join("data", "audio")


def write_wav(path, samples, sr=SR):
    """16 位单声道 PCM。samples 是 [-1, 1] 的 float。"""
    x = np.clip(samples, -1.0, 1.0)
    pcm = (x * 32767.0).astype("<i2")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(pcm.tobytes())
    print(f"  {path}  {os.path.getsize(path) / 1e6:.2f} MB  "
          f"{len(x) / sr:.1f}s")


def onepole_lowpass(x, cutoff, sr=SR):
    a = math.exp(-2.0 * math.pi * cutoff / sr)
    y = np.empty_like(x)
    prev = 0.0
    for i, v in enumerate(x):
        prev = (1.0 - a) * v + a * prev
        y[i] = prev
    return y


def highpass(x, cutoff, sr=SR):
    return x - onepole_lowpass(x, cutoff, sr)


def normalize(x, peak):
    m = np.max(np.abs(x))
    return x if m < 1e-9 else x * (peak / m)


def make_rain(seconds=60.0, fade=1.2):
    """雨声：低通噪声打底 + 一点高频细节 + 极慢的阵风起伏 + 零星滴水。"""
    rng = np.random.default_rng(20261123)

    n = int(SR * seconds)
    f = int(SR * fade)
    total = n + f

    noise = rng.standard_normal(total)

    # 底噪：大部分能量在 2kHz 以下，听起来像"沙沙"而不是"嘶"
    low = onepole_lowpass(noise, 1800.0)
    low = onepole_lowpass(low, 1800.0)          # 二阶级联，滚降更陡

    # 细节：6k 以上的细碎声，很轻，只用来撑"雨点打在玻璃上"
    detail = highpass(noise, 6000.0)

    sig = 1.0 * low + 0.10 * detail

    # 阵风：0.07Hz 的慢起伏，幅度 ±12%，避免一条直线听久了发木
    t = np.arange(total) / SR
    gust = 1.0 + 0.12 * np.sin(2 * np.pi * 0.07 * t) \
                + 0.05 * np.sin(2 * np.pi * 0.023 * t + 1.1)
    sig *= gust

    # 滴水：每 4~9 秒一颗，短促的高频衰减音
    pos = 0
    while True:
        pos += int(SR * rng.uniform(4.0, 9.0))
        if pos >= total - int(SR * 0.2):
            break
        amp = rng.uniform(0.012, 0.035)
        freq = rng.uniform(900.0, 1900.0)
        dur = int(SR * 0.09)
        tt = np.arange(dur) / SR
        drip = np.sin(2 * np.pi * freq * tt) * np.exp(-tt / 0.018) * amp
        sig[pos:pos + dur] += drip

    sig = normalize(sig, 0.18)                  # 环境音要压得很低，别盖过文字

    # 无缝循环：把尾巴 fade 秒交叉到开头，然后切掉，循环点就听不出接缝
    w = np.linspace(0.0, 1.0, f)
    out = sig[:n].copy()
    out[:f] = sig[:f] * w + sig[n:n + f] * (1.0 - w)
    return out


def make_click(seconds=0.05):
    """推进对话的轻响：一点正弦 + 一点噪声，衰减很快。"""
    rng = np.random.default_rng(7)
    t = np.arange(int(SR * seconds)) / SR
    env = np.exp(-t / 0.005)
    sig = np.sin(2 * np.pi * 1500.0 * t) * env * 0.35
    sig += rng.standard_normal(len(t)) * env * 0.25
    return normalize(sig, 0.35)


def make_page(seconds=0.16):
    """翻页：一段带包络的带通噪声，起音软、收得快。"""
    rng = np.random.default_rng(11)
    n = int(SR * seconds)
    t = np.arange(n) / SR

    noise = rng.standard_normal(n)
    band = onepole_lowpass(noise, 3200.0) - onepole_lowpass(noise, 700.0)

    attack = np.minimum(t / 0.012, 1.0)
    release = np.exp(-t / 0.045)
    sig = band * attack * release
    return normalize(sig, 0.30)


def main():
    print("生成音频：")
    write_wav(os.path.join(OUT_DIR, "bgm_rain.wav"), make_rain())
    write_wav(os.path.join(OUT_DIR, "se_click.wav"), make_click())
    write_wav(os.path.join(OUT_DIR, "se_page.wav"), make_page())
    print("完成。改参数重跑即可覆盖。")


if __name__ == "__main__":
    main()
