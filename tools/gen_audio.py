#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成音频素材：雨声环境音 + 两个界面音效。

用法（在 gal 目录下）：
    python tools/gen_audio.py

产出 data/audio/：
    bgm_rain.ogg   60 秒无缝循环的雨声（省图三楼窗外在下雨；无 ffmpeg 时生成 wav）
    se_click.wav   推进对话的轻响
    se_page.wav    翻页 / 开菜单

只依赖 numpy。雨声是滤波后的噪声，不是采样素材，所以可以随便重跑、改参数。
"""

import math
import shutil
import subprocess
import os
import wave

import numpy as np

SR = 22050                 # 采样率；雨声不需要高频，22k 足够，文件也小一半
OUT_DIR = os.path.join("data", "audio")

FFMPEG_FALLBACK = r"C:\Users\赵一霖\WorkBuddy\2026-11-04-14-03-05\tools\ffmpeg.exe"


def find_ffmpeg():
    """PATH 里没有就用已知的本地路径——PowerShell 会话常常没把工具目录加进 PATH。"""
    found = shutil.which("ffmpeg") or shutil.which("ffmpeg.exe")
    if found:
        return found
    if os.path.exists(FFMPEG_FALLBACK):
        return FFMPEG_FALLBACK
    return None


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


def spectral_lowpass(x, cutoff, order=4, sr=SR):
    """频域整形：把 cutoff 以上按 order 阶陡降。

    为什么不用 onepole 级联：一阶低通的滚降只有 -6dB/oct，串两层也才 -12dB/oct，
    8kHz 处还剩不少能量——听起来就是"嘶嘶"的数码噪声，不像雨。频域整形的
    滚降可以做到 -24dB/oct 以上，把高频彻底压掉。
    """
    n = len(x)
    spec = np.fft.rfft(x)
    freq = np.fft.rfftfreq(n, 1.0 / sr)

    # 1 / (1 + (f/fc)^(2*order)) 的幅度响应，平滑无振铃
    resp = 1.0 / (1.0 + (freq / cutoff) ** (2 * order))
    return np.fft.irfft(spec * resp, n=n)


def rain_grains(total, rate, dur_range, gain, sr=SR, rng=None, lo=800.0, hi=4000.0):
    """雨滴颗粒：泊松分布的离散短脉冲。

    连续噪声只能做出"沙沙"的雨幕，缺的是**颗粒感**——真实雨声由大量
    离散小撞击叠成。这里按每秒 rate 颗的密度撒脉冲，每颗是一个带随机
    中心频率的短衰减包络，叠起来才是"雨"而不是"杂音"。
    """
    rng = rng or np.random.default_rng()
    out = np.zeros(total)

    count = int(rate * total / sr)
    starts = rng.integers(0, total - int(sr * 0.02), count)
    durs = rng.integers(int(sr * dur_range[0]), int(sr * dur_range[1]), count)
    freqs = rng.uniform(lo, hi, count)
    amps = rng.exponential(gain, count)          # 指数分布：大部分很轻，偶尔一颗响

    for s, d, f, a in zip(starts, durs, freqs, amps):
        tt = np.arange(d) / sr
        env = np.exp(-tt / (d / sr / 2.5))       # 快速衰减
        out[s:s + d] += np.sin(2 * np.pi * f * tt) * env * a

    return out


def make_rain(seconds=60.0, fade=1.2):
    """雨声：雨幕底噪 + 大量雨滴颗粒 + 阵风起伏 + 零星打窗。

    旧版是「两层低通噪声 + 6kHz 白噪 detail」，听感是一条连续的"嘶——"，
    用户直接听成了底噪。这版把能量重新分配：

        雨幕（低频嗡）  陡峭低通 1.2kHz，低音量，负责"下雨了"的氛围
        雨滴颗粒        400 颗/秒的短脉冲，1~4kHz，负责"沙沙"的质感
        打窗点          每秒 3~8 颗更响的，2~7kHz，负责层次
        阵风            0.07Hz 慢起伏，避免一条直线听久了发木
    """
    rng = np.random.default_rng(20261123)

    n = int(SR * seconds)
    f = int(SR * fade)
    total = n + f

    # 1) 雨幕：白噪 → 陡峭低通，压到 1.2kHz 以下
    bed = spectral_lowpass(rng.standard_normal(total), 1200.0, order=4)

    # 2) 雨滴颗粒：主体是 1~4kHz 的密集小撞击。
    #    bed 的能量比颗粒大得多，所以这里 bed 只给 0.22 垫底，
    #    不然颗粒会被雨幕淹掉，听起来又是一条连续的"沙——"。
    grains = rain_grains(total, 550, (0.004, 0.016), 0.040, rng=rng, lo=1000, hi=4000)

    # 3) 打窗：零星、更响、稍高频，给层次
    taps = rain_grains(total, 6, (0.010, 0.040), 0.070, rng=rng, lo=2000, hi=7000)

    sig = 0.22 * bed + 1.0 * grains + 1.0 * taps

    # 4) 阵风：0.07Hz 的慢起伏，幅度 ±12%
    t = np.arange(total) / SR
    gust = 1.0 + 0.12 * np.sin(2 * np.pi * 0.07 * t) \
                + 0.05 * np.sin(2 * np.pi * 0.023 * t + 1.1)
    sig *= gust

    # 5) 滴水：每 4~9 秒一颗，比打窗更"实"，是屋檐上的水
    pos = 0
    while True:
        pos += int(SR * rng.uniform(4.0, 9.0))
        if pos >= total - int(SR * 0.2):
            break
        amp = rng.uniform(0.02, 0.05)
        freq = rng.uniform(900.0, 1900.0)
        dur = int(SR * 0.09)
        tt = np.arange(dur) / SR
        drip = np.sin(2 * np.pi * freq * tt) * np.exp(-tt / 0.018) * amp
        sig[pos:pos + dur] += drip

    sig = normalize(sig, 0.16)                  # 环境音要压得很低，别盖过台词

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


def main(argv=None):
    import sys
    argv = argv if argv is not None else sys.argv[1:]
    want_rain = "--with-synth-rain" in argv

    print("生成音频：")

    # 雨声默认**不**生成——现在的 data/audio/bgm_rain.ogg 是 tools/import_rain.py
    # 从 CC0 真实录音处理出来的，跑这个脚本会把它覆盖成一版合成的，等于白改。
    # 真要合成版（离线兜底、或者想对比），加 --with-synth-rain。
    if want_rain:
        wav = os.path.join(OUT_DIR, "bgm_rain.wav")
        write_wav(wav, make_rain())

        # 仓库里只存压缩后的 bgm_rain.ogg。有 ffmpeg 就顺手转，
        # 没有就留着 wav，引擎两个都会找（audio.c 先 ogg 后 wav）。
        ogg = os.path.join(OUT_DIR, "bgm_rain.ogg")
        ff = find_ffmpeg()
        if ff:
            r = subprocess.run([ff, "-y", "-loglevel", "error", "-i", wav,
                                "-c:a", "libvorbis", "-q:a", "4", ogg])
            if r.returncode == 0:
                os.remove(wav)
                print(f"  bgm_rain 已用合成版覆盖（{ogg}）")
            else:
                print("  ffmpeg 转 ogg 失败，保留 wav")
        else:
            print("  没找到 ffmpeg，保留 wav")
    else:
        rain = os.path.join(OUT_DIR, "bgm_rain.ogg")
        if os.path.exists(rain):
            print(f"  跳过 bgm_rain（现有的是真实录音；要合成版加 --with-synth-rain）")
        else:
            print("  没有 bgm_rain，也没要求合成：先跑 tools/import_rain.py 下载真实录音")

    write_wav(os.path.join(OUT_DIR, "se_click.wav"), make_click())
    write_wav(os.path.join(OUT_DIR, "se_page.wav"), make_page())
    print("完成。音效改参数重跑即可覆盖。")


if __name__ == "__main__":
    main()
